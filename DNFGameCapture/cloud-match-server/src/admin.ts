import { createHash, timingSafeEqual } from 'node:crypto';
import type Database from 'better-sqlite3';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
} from 'express';
import { z } from 'zod';

import {
  buildAdminState,
  clearOfflineAndTemporaryBroadcasterData,
  deleteBroadcasterLobbyData,
  listAdminTemporaryBroadcasterIds,
  pruneExpiredAdminData,
} from './admin-data.js';
import {
  BroadcasterAttributionError,
  createBroadcasterAttributionService,
  type BroadcasterAttributionService,
} from './broadcaster-attribution.js';
import {
  ADMIN_PAGE_CSS,
  ADMIN_PAGE_JS,
  buildAdminPage,
} from './admin-page.js';
import { buildBroadcasterAdminPage, BROADCASTER_ADMIN_CSS, BROADCASTER_ADMIN_JS } from './broadcaster-admin-page.js';
import { buildLicenseAdminPage, LICENSE_ADMIN_CSS, LICENSE_ADMIN_JS } from './license-admin-page.js';
import { createLicenseAdminApi } from './license-admin.js';
import { deviceIdSchema } from './schemas.js';
import { createLibraryAdminApi } from './library-admin.js';
import { LibraryAdminError, libraryReviewGuardSchema, reviewAdminSubmissions } from './library-admin-data.js';
import { buildLibraryAdminPage, LIBRARY_ADMIN_CSS, LIBRARY_ADMIN_JS } from './library-admin-page.js';
import {
  getBroadcasterOcrDisabledUntil,
  getPlayerLibrarySubmission,
  listLicenses,
  listPendingPlayerLibrarySubmissions,
  listPlayerLibrary,
  setBroadcasterOcrDisabledUntil,
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
  attribution?: BroadcasterAttributionService;
}

const manualLicenseLinkSchema = z.object({
  licenseId: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
}).strict();

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
  const attribution = options.attribution ?? createBroadcasterAttributionService(db);
  const app = express();
  app.disable('x-powered-by');
  app.use(securityHeaders);

  app.get('/admin/health', (_request, response) => {
    response.json({ ok: true });
  });
  app.use('/admin', requireAdminAuthentication(adminPassword));
  app.use('/admin/api', (request, response, next) => {
    if (request.method !== 'GET' && request.get('x-dnf-admin-csrf') !== csrfToken) {
      response.status(403).json({ ok: false, code: 'invalid_csrf' });
      return;
    }
    next();
  });
  app.use('/admin/api/library', createLibraryAdminApi(db, now));
  app.get('/admin/library', (_request, response) => {
    response.type('html').send(buildLibraryAdminPage(csrfToken));
  });
  app.get('/admin/library/style.css', (_request, response) => {
    response.type('css').send(LIBRARY_ADMIN_CSS);
  });
  app.get('/admin/library/app.js', (_request, response) => {
    response.type('application/javascript').send(LIBRARY_ADMIN_JS);
  });
  app.use(express.json({ limit: '4kb' }));
  app.use('/admin/api/licenses', createLicenseAdminApi(db, now));

  app.get('/admin', (_request, response) => {
    response.type('html').send(buildAdminPage(csrfToken));
  });
  for (const [path, buildPage, css, js] of [
    ['/admin/licenses', buildLicenseAdminPage, LICENSE_ADMIN_CSS, LICENSE_ADMIN_JS],
    ['/admin/broadcasters', buildBroadcasterAdminPage, BROADCASTER_ADMIN_CSS, BROADCASTER_ADMIN_JS],
  ] as const) {
    app.get(path, (_request, response) => response.type('html').send(buildPage(csrfToken)));
    app.get(path + '/style.css', (_request, response) => response.type('css').send(css));
    app.get(path + '/app.js', (_request, response) => response.type('application/javascript').send(js));
  }
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
      ...buildAdminState(db, socketController.getActiveDeviceIds(), now(), attribution, query),
      licenses: listLicenses(db),
      playerLibrary: listPlayerLibrary(db),
      pendingLibrarySubmissions: listPendingPlayerLibrarySubmissions(db),
    });
  });

  app.get('/admin/api/broadcasters/state', (request, response) => {
    const query = typeof request.query.q === 'string' ? request.query.q.slice(0, 64) : '';
    response.json({ ok: true, ...buildAdminState(db, socketController.getActiveDeviceIds(), now(), attribution, query) });
  });

  app.get('/admin/api/player-library', (_request, response) => {
    response.json({ ok: true, ...listPlayerLibrary(db) });
  });

  app.get('/admin/api/player-library/submissions', (_request, response) => {
    response.json({ ok: true, submissions: listPendingPlayerLibrarySubmissions(db) });
  });

  app.get('/admin/api/player-library/submissions/:submissionId', (request, response) => {
    const submissionId = Number(request.params.submissionId);
    if (!Number.isSafeInteger(submissionId) || submissionId <= 0) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    const submission = getPlayerLibrarySubmission(db, submissionId);
    if (!submission) {
      response.status(404).json({ ok: false, code: 'submission_not_found' });
      return;
    }
    response.json({ ok: true, submission });
  });

  for (const action of ['approve', 'reject'] as const) {
    app.post(`/admin/api/player-library/submissions/:submissionId/${action}`, (request, response, next) => {
      const submissionId = Number(request.params.submissionId);
      const guard = libraryReviewGuardSchema.safeParse(request.body);
      if (!Number.isSafeInteger(submissionId) || submissionId <= 0 || !guard.success) {
        response.status(400).json({ ok: false, code: 'invalid_request' });
        return;
      }
      try {
        const result = reviewAdminSubmissions(db, guard.data.revision, now(), action,
          [{ id: submissionId, submissionRevision: guard.data.submissionRevision }]);
        response.json(action === 'approve' ? { ok: true, revision: result.revision } : { ok: true });
      } catch (error) {
        if (error instanceof LibraryAdminError) {
          response.status(error.status).json({ ok: false, code: error.code, conflicts: error.conflicts });
          return;
        }
        next(error);
      }
    });
  }

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

  app.put('/admin/api/broadcasters/:deviceId/license', (request, response, next) => {
    const deviceId = safeDeviceId(request.params.deviceId);
    const parsed = manualLicenseLinkSchema.safeParse(request.body);
    if (!deviceId || !parsed.success) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }
    try {
      response.json({
        ok: true,
        link: attribution.manualLink(deviceId, parsed.data.licenseId, now()),
      });
    } catch (error) {
      next(error);
    }
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
    if (_error instanceof BroadcasterAttributionError) {
      response.status(_error.status).json({ ok: false, code: _error.code });
      return;
    }
    response.status(500).json({ ok: false, code: 'internal_error' });
  });
  return app;
}
