import express, { type NextFunction, type Request, type Response } from 'express';
import type Database from 'better-sqlite3';
import { z } from 'zod';
import { generateLicenseKey, isNativeLicenseKey, normalizeLicenseKey } from './auth.js';
import { deviceIdSchema } from './schemas.js';
import { LicenseVaultError } from './license-vault.js';
import { createLicense, disableLicense, extendLicense, generateLicenseBatch, licenseAudit,
  LicenseError, LICENSE_PRESETS, listLicenseDevices, listLicenses, MAX_LICENSE_BATCH, rebindLicense, recoverLicenseKey, revealLicense } from './license-store.js';

const requestId = z.string().min(8).max(80).regex(/^[A-Za-z0-9_-]+$/);
const revision = z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER);
const guard = z.object({ revision, requestId });
const key = z.string().trim().max(256).default('');
const label = z.string().trim().max(128).default('');
const batch = z.object({ preset: z.string().max(32), count: z.number().int().min(1).max(MAX_LICENSE_BATCH).default(1), label, key, requestId }).strict();
const legacy = z.object({ key, label, expiresAt: z.number().int().positive().max(0xFFFFFFFE).nullable().optional() }).strict();
function parse<S extends z.ZodTypeAny>(schema: S, body: unknown): z.output<S> {
  const result = schema.safeParse(body);
  if (!result.success) throw new LicenseError(400, 'invalid_request');
  return result.data;
}
function idOf(value: unknown): number {
  if (typeof value !== 'string' || !/^[1-9][0-9]*$/.test(value) || !Number.isSafeInteger(Number(value))) throw new LicenseError(400, 'invalid_request');
  return Number(value);
}
function checkKey(value: string) { if (value && !isNativeLicenseKey(value)) throw new LicenseError(400, 'invalid_license_format'); }

export function createLicenseAdminApi(db: Database.Database, now: () => number) {
  const router = express.Router();
  router.get('/', (_request, response) => response.json({ ok: true, licenses: listLicenses(db), presets: LICENSE_PRESETS, maxBatch: MAX_LICENSE_BATCH, devices: listLicenseDevices(db) }));
  router.post('/', (request, response) => {
    const tick = now();
    if (request.body && 'preset' in request.body) {
      const body = parse(batch, request.body); checkKey(body.key);
      if (body.key && body.count !== 1) throw new LicenseError(400, 'invalid_request');
      response.status(201).json({ ok: true, ...generateLicenseBatch(db, body, tick) });
    } else {
      const body = parse(legacy, request.body); checkKey(body.key);
      const expiresAt = body.expiresAt ?? null;
      if (expiresAt !== null && expiresAt <= tick) throw new LicenseError(400, 'invalid_expiry');
      const value = normalizeLicenseKey(body.key || generateLicenseKey(expiresAt === null ? 0xFFFFFFFF : expiresAt - tick));
      const created = createLicense(db, { key: value, label: body.label, expiresAt, nowSec: tick });
      response.status(201).json({ ok: true, id: created.id, key: value, label: body.label, expiresAt });
    }
  });
  router.post('/:id/reveal', (request, response) => {
    response.json({ ok: true, key: revealLicense(db, idOf(request.params.id)) });
  });
  router.get('/:id/audit', (request, response) => response.json({ ok: true, events: licenseAudit(db, idOf(request.params.id)) }));
  router.post('/:id/extend', (request, response) => {
    const body = parse(guard.extend({ preset: z.string().max(32) }).strict(), request.body);
    response.json({ ok: true, ...extendLicense(db, idOf(request.params.id), body.preset, body.revision, body.requestId, now()) });
  });
  router.post('/:id/rebind', (request, response) => {
    const body = parse(guard.extend({ deviceId: deviceIdSchema.nullable() }).strict(), request.body);
    const { oldDeviceId, ...result } = rebindLicense(db, idOf(request.params.id), body.deviceId, body.revision, body.requestId, now());
    response.json({ ok: true, ...result });
  });
  router.post('/:id/key', (request, response) => {
    const body = parse(guard.extend({ key: z.string().trim().min(1).max(256) }).strict(), request.body); checkKey(body.key);
    response.json({ ok: true, ...recoverLicenseKey(db, idOf(request.params.id), body.key, body.revision, body.requestId, now()) });
  });
  router.post('/:id/disable', (request, response) => {
    const body = parse(z.object({ disabled: z.boolean(), revision: revision.optional(), requestId: requestId.optional() }).strict(), request.body);
    const { oldDeviceId, ...result } = disableLicense(db, idOf(request.params.id), body.disabled, body.revision, body.requestId, now());
    response.json(body.revision === undefined && body.requestId === undefined ? { ok: true, disabled: body.disabled } : { ok: true, ...result });
  });
  router.use((error: unknown, _request: Request, response: Response, _next: NextFunction) => {
    if (error instanceof LicenseError) response.status(error.status).json({ ok: false, code: error.code });
    else if (error instanceof LicenseVaultError) response.status(503).json({ ok: false, code: 'vault_unavailable' });
    else if ((error as { code?: string })?.code === 'SQLITE_CONSTRAINT_UNIQUE') response.status(409).json({ ok: false, code: 'license_already_exists' });
    else response.status(500).json({ ok: false, code: 'internal_error' });
  });
  return router;
}
