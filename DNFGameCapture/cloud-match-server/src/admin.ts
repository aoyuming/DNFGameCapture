import { createHash, timingSafeEqual } from 'node:crypto';
import type Database from 'better-sqlite3';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
} from 'express';

import {
  buildAdminState,
  clearOfflineAndTemporaryBroadcasterData,
  deleteBroadcasterLobbyData,
  listAdminTemporaryBroadcasterIds,
  pruneExpiredAdminData,
} from './admin-data.js';
import {
  ADMIN_PAGE_CSS,
  ADMIN_PAGE_JS,
  buildAdminPage,
} from './admin-page.js';
import { deviceIdSchema } from './schemas.js';
import { generateLicenseKey, isNativeLicenseKey } from './auth.js';
import {
  approvePlayerLibrarySubmission,
  createLicense,
  getBroadcasterOcrDisabledUntil,
  listLicenses,
  listPendingPlayerLibrarySubmissions,
  listPlayerLibrary,
  setBroadcasterOcrDisabledUntil,
  setLicenseDisabled,
} from './v2-api.js';

export interface AdminSocketController {
  getActiveDeviceIds(): ReadonlySet<string>;
  disconnectDevice(deviceId: string): boolean;
  stopRealtimeViewer(viewerDeviceId: string): boolean;
  notifyDirectoryChanged(reason: string): void;
}

export interface CreateCloudMatchAdminAppOptions {
  db: Database.Database;
  now(): number;
  csrfToken: string;
  adminPassword: string;
  socketController: AdminSocketController;
}

function safeDeviceId(value: unknown): string | null {
  const parsed = deviceIdSchema.safeParse(value);
  return parsed.success ? parsed.data : null;
}

function securityHeaders(
  _request: Request,
  response: Response,
  next: NextFunction,
): void {
  response.setHeader('X-Frame-Options', 'DENY');
  response.setHeader('X-Content-Type-Options', 'nosniff');
  response.setHeader('Referrer-Policy', 'no-referrer');
  response.setHeader('Cache-Control', 'no-store, max-age=0');
  response.setHeader(
    'Content-Security-Policy',
    "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'",
  );
  next();
}

function constantTimeTextEqual(left: string, right: string): boolean {
  const leftDigest = createHash('sha256').update(left, 'utf8').digest();
  const rightDigest = createHash('sha256').update(right, 'utf8').digest();
  return timingSafeEqual(leftDigest, rightDigest);
}

function basicCredentials(value: string | undefined): {
  username: string;
  password: string;
} | null {
  if (!value?.startsWith('Basic ')) return null;
  try {
    const decoded = Buffer.from(value.slice(6), 'base64').toString('utf8');
    const separator = decoded.indexOf(':');
    if (separator < 0) return null;
    return {
      username: decoded.slice(0, separator),
      password: decoded.slice(separator + 1),
    };
  } catch {
    return null;
  }
}

function requireAdminAuthentication(adminPassword: string) {
  return (request: Request, response: Response, next: NextFunction): void => {
    const credentials = basicCredentials(request.get('authorization'));
    const usernameMatches = constantTimeTextEqual(
      credentials?.username ?? '',
      'admin',
    );
    const passwordMatches = constantTimeTextEqual(
      credentials?.password ?? '',
      adminPassword,
    );
    if (!usernameMatches || !passwordMatches) {
      response.setHeader(
        'WWW-Authenticate',
        'Basic realm="DNF Cloud Match Admin", charset="UTF-8"',
      );
      response.status(401).type('text').send('Authentication required');
      return;
    }
    next();
  };
}

export function createCloudMatchAdminApp(
  options: CreateCloudMatchAdminAppOptions,
): Express {
  const { db, now, csrfToken, adminPassword, socketController } = options;
  const app = express();
  app.disable('x-powered-by');
  app.use(securityHeaders);

  app.get('/admin/health', (_request, response) => {
    response.json({ ok: true });
  });
  app.use('/admin', requireAdminAuthentication(adminPassword));
  app.use(express.json({ limit: '4kb' }));

  app.get('/admin', (_request, response) => {
    response.type('html').send(buildAdminPage(csrfToken));
  });
  app.get('/admin/style.css', (_request, response) => {
    response.type('css').send(ADMIN_PAGE_CSS);
  });
  app.get('/admin/app.js', (_request, response) => {
    response.setHeader('Content-Type', 'application/javascript; charset=utf-8');
    response.send(ADMIN_PAGE_JS);
  });
  app.get('/admin/api/state', (request, response) => {
    const query = typeof request.query.q === 'string'
      ? request.query.q.slice(0, 64)
      : '';
    response.json({
      ok: true,
      ...buildAdminState(db, socketController.getActiveDeviceIds(), now(), query),
      licenses: listLicenses(db),
      playerLibrary: listPlayerLibrary(db),
      pendingLibrarySubmissions: listPendingPlayerLibrarySubmissions(db),
    });
  });

  app.use('/admin/api', (request, response, next) => {
    if (request.method === 'GET') {
      next();
      return;
    }
    if (request.get('x-dnf-admin-csrf') !== csrfToken) {
      response.status(403).json({ ok: false, code: 'invalid_csrf' });
      return;
    }
    next();
  });

  app.post('/admin/api/licenses', (request, response) => {
    const body = request.body as Record<string, unknown> | null;
    const label = typeof body?.label === 'string' ? body.label.trim().slice(0, 128) : '';
    const expiresAt = body?.expiresAt === null
      ? null
      : typeof body?.expiresAt === 'number' && Number.isSafeInteger(body.expiresAt)
        ? body.expiresAt
        : null;
    if (body?.expiresAt !== undefined && body.expiresAt !== null && expiresAt === null) {
      response.status(400).json({ ok: false, code: 'invalid_expiry' });
      return;
    }
    const suppliedKey = typeof body?.key === 'string' ? body.key.trim() : '';
    const generatedDuration = expiresAt === null
      ? 0xFFFFFFFF
      : Math.max(1, Math.min(0xFFFFFFFF, expiresAt - now()));
    const key = suppliedKey || generateLicenseKey(generatedDuration);
    if (suppliedKey && !isNativeLicenseKey(key)) {
      response.status(400).json({ ok: false, code: 'invalid_license_format' });
      return;
    }
    try {
      const created = createLicense(db, { key, label, expiresAt, nowSec: now() });
      response.status(201).json({
        ok: true,
        id: created.id,
        key,
        label,
        expiresAt: created.expiresAt,
      });
    } catch {
      response.status(409).json({ ok: false, code: 'license_already_exists' });
    }
  });

  app.get('/admin/api/licenses', (_request, response) => {
    response.json({ ok: true, licenses: listLicenses(db) });
  });

  app.post('/admin/api/licenses/:licenseId/disable', (request, response) => {
    const licenseId = Number.parseInt(request.params.licenseId, 10);
    const disabled = (request.body as Record<string, unknown> | null)?.disabled;
    if (!Number.isSafeInteger(licenseId) || typeof disabled !== 'boolean') {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    const changed = setLicenseDisabled(db, licenseId, disabled ? now() : null, now());
    if (!changed) {
      response.status(404).json({ ok: false, code: 'license_not_found' });
      return;
    }
    response.json({ ok: true, disabled });
  });

  app.get('/admin/api/player-library', (_request, response) => {
    response.json({ ok: true, ...listPlayerLibrary(db) });
  });

  app.get('/admin/api/player-library/submissions', (_request, response) => {
    response.json({ ok: true, submissions: listPendingPlayerLibrarySubmissions(db) });
  });

  app.post('/admin/api/player-library/submissions/:submissionId/approve', (request, response) => {
    const submissionId = Number.parseInt(request.params.submissionId, 10);
    if (!Number.isSafeInteger(submissionId)) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    const result = approvePlayerLibrarySubmission(db, submissionId, now());
    if (!result.ok) {
      response.status(result.code === 'submission_not_pending' ? 404 : 409).json(result);
      return;
    }
    response.json(result);
  });

  app.put('/admin/api/broadcasters/:deviceId/ocr-policy', (request, response) => {
    const deviceId = safeDeviceId(request.params.deviceId);
    const disabledUntil = (request.body as Record<string, unknown> | null)?.disabledUntil;
    if (!deviceId || (disabledUntil !== null &&
      (typeof disabledUntil !== 'number' || !Number.isSafeInteger(disabledUntil) || disabledUntil < 0))) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    setBroadcasterOcrDisabledUntil(db, deviceId, disabledUntil as number | null, now());
    response.json({
      ok: true,
      deviceId,
      disabledUntil: getBroadcasterOcrDisabledUntil(db, deviceId),
    });
  });

  app.post('/admin/api/broadcasters/:deviceId/disconnect', (request, response) => {
    const deviceId = safeDeviceId(request.params.deviceId);
    if (!deviceId) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    if (!socketController.disconnectDevice(deviceId)) {
      response.status(404).json({ ok: false, code: 'broadcaster_not_online' });
      return;
    }
    socketController.notifyDirectoryChanged('admin_disconnect');
    response.json({ ok: true });
  });

  app.post('/admin/api/realtime/:viewerDeviceId/stop', (request, response) => {
    const viewerDeviceId = safeDeviceId(request.params.viewerDeviceId);
    if (!viewerDeviceId) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    if (!socketController.stopRealtimeViewer(viewerDeviceId)) {
      response.status(404).json({ ok: false, code: 'relation_not_found' });
      return;
    }
    socketController.notifyDirectoryChanged('admin_realtime_stop');
    response.json({ ok: true });
  });

  app.delete('/admin/api/broadcasters/:deviceId/data', (request, response) => {
    const deviceId = safeDeviceId(request.params.deviceId);
    if (!deviceId) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    if (socketController.getActiveDeviceIds().has(deviceId)) {
      response.status(409).json({ ok: false, code: 'broadcaster_online' });
      return;
    }
    const deleted = deleteBroadcasterLobbyData(db, deviceId);
    if (deleted) socketController.notifyDirectoryChanged('admin_data_deleted');
    response.json({ ok: true, deleted });
  });

  app.post('/admin/api/cleanup/offline', (_request, response) => {
    const activeBefore = socketController.getActiveDeviceIds();
    for (const deviceId of listAdminTemporaryBroadcasterIds(db)) {
      if (activeBefore.has(deviceId)) socketController.disconnectDevice(deviceId);
    }
    const result = clearOfflineAndTemporaryBroadcasterData(
      db,
      socketController.getActiveDeviceIds(),
    );
    if (result.deletedCount > 0) {
      socketController.notifyDirectoryChanged('admin_offline_cleanup');
    }
    response.json({ ok: true, ...result });
  });

  app.post('/admin/api/cleanup/expired', (_request, response) => {
    const result = pruneExpiredAdminData(db, now());
    if (result.removedBroadcasters > 0 || result.removedSyncRecords > 0) {
      socketController.notifyDirectoryChanged('admin_expired_cleanup');
    }
    response.json({ ok: true, ...result });
  });

  app.use((_error: unknown, _request: Request, response: Response, _next: NextFunction) => {
    response.status(500).json({ ok: false, code: 'internal_error' });
  });
  return app;
}
