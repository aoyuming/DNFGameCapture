import { createHash, randomBytes } from 'node:crypto';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
  type Router,
} from 'express';
import type Database from 'better-sqlite3';
import { z } from 'zod';

import type { BroadcasterAttributionService } from './broadcaster-attribution.js';
import {
  hashSessionToken,
  isLicenseUsable,
  type LicenseRecord,
} from './auth.js';
import {
  canonicalizeIdentifiers,
  MAX_PLAYER_ENTITY_VALUES,
  detectIdentifierConflicts,
  resolvePlayerIdentity,
  type PlayerEntity,
} from './player-library.js';
import { deviceIdSchema, playerNameSchema } from './schemas.js';
import { activateStoredLicense, disableLicense, LicenseError } from './license-store.js';
export { createLicense, listLicenses } from './license-store.js';
import { listPlayerLibrary, readAutomaticIdentityEvidence } from './library-store.js';
import { reconcileLibrarySubmission } from './library-submission-reconcile.js';
import { identityKey, type AutomaticIdentityEvidence } from './library-identity-policy.js';
export { listPlayerLibrary } from './library-store.js';
import {
  LibraryAdminError, libraryReviewGuardSchema, mutatePublicLibrary, parseAdminEntities,
  readAdminSubmission, reviewAdminSubmissions, revisionSchema, projectedLibrary,
} from './library-admin-data.js';

const MAX_LIBRARY_BYTES = 256 * 1024;
const DEFAULT_SESSION_TTL_SECONDS = 5 * 24 * 60 * 60;
const entityIdSchema = z.string().min(1).max(128).regex(/^[A-Za-z0-9_-]+$/);
const identifierSchema = z.string().min(1).max(128);
const playerEntityInputSchema = z.object({
  entityId: entityIdSchema.optional(),
  names: z.array(playerNameSchema).min(1).max(MAX_PLAYER_ENTITY_VALUES),
  gameIds: z.array(identifierSchema).max(MAX_PLAYER_ENTITY_VALUES),
}).strip();
const playerLibraryPayloadSchema = z.object({
  entities: z.array(playerEntityInputSchema).max(10_000),
}).strict();
const activateSchema = z.object({
  key: z.string().min(1).max(256),
  deviceId: deviceIdSchema,
  clientVersion: z.string().max(64).optional(),
}).strict();
const validateSchema = z.object({
  sessionToken: z.string().min(32).max(256),
  deviceId: deviceIdSchema,
}).strict();
const resolveSchema = z.object({
  gameIds: z.array(identifierSchema).max(64).default([]),
  activeEntityIds: z.array(entityIdSchema).max(8).refine(ids => new Set(ids).size === ids.length).optional(),
}).strip();

export interface V2ApiOptions {
  db: Database.Database;
  now(): number;
  serverUrl: string;
  sessionTtlSeconds?: number;
  allowLegacyPermanentKeys?: boolean;
  resolveClientIp?(remoteAddress: string): string;
  attribution?: BroadcasterAttributionService;
}

export interface SubmittedPlayerEntity {
  entityId?: string;
  names: string[];
  gameIds: string[];
}

export interface PublicPlayerEntity extends PlayerEntity {}

interface SessionContext {
  deviceId: string;
  license: LicenseRecord;
}

class V2RequestError extends Error {
  constructor(
    public readonly status: number,
    public readonly code: string,
  ) {
    super(code);
  }
}

function jsonByteLength(value: unknown): number {
  return Buffer.byteLength(JSON.stringify(value), 'utf8');
}

function canonicalValues(values: readonly string[]): string[] {
  return [...new Set(values.map(identityKey))].sort();
}

function submissionSignature(entities: readonly PlayerEntity[], redirects: ReadonlyMap<string, string>): string {
  // Keep each source group and its identity; flattened public unions can hide new merge evidence.
  return JSON.stringify([...new Set(entities.map(entity => JSON.stringify([
    redirects.get(entity.entityId) ?? entity.entityId, canonicalValues(entity.names), canonicalValues(entity.gameIds),
  ])))].sort());
}

function normalizeSubmittedEntities(
  entities: readonly SubmittedPlayerEntity[],
  deviceId: string,
): PlayerEntity[] {
  const unique = new Map<string, PlayerEntity>();
  for (const source of entities) {
    const names = canonicalizeIdentifiers(source.names), gameIds = canonicalizeIdentifiers(source.gameIds);
    // Anonymous retries need stable source IDs, scoped to this device and this exact normalized group.
    const entityId = source.entityId ?? `player-${createHash('sha256')
      .update(JSON.stringify(['submission-v1', deviceId, canonicalValues(names), canonicalValues(gameIds)]))
      .digest('hex').slice(0, 24)}`;
    const entity = { entityId, names, gameIds };
    const signature = submissionSignature([entity], new Map());
    if (!unique.has(signature)) unique.set(signature, entity);
  }
  return [...unique.values()];
}

function createPendingSignature(current: ReturnType<typeof listPlayerLibrary>, evidence: AutomaticIdentityEvidence) {
  const redirects = new Map(current.entityRedirects.map(item => [item.fromEntityId, item.toEntityId]));
  const known = new Map(current.entities.map(entity => [entity.entityId, {
    names: new Set(canonicalValues(entity.names)), gameIds: new Set(canonicalValues(entity.gameIds)),
  }]));
  return (entities: PlayerEntity[], projection?: ReturnType<typeof projectedLibrary>): string => {
    if (new Set(entities.map(entity => entity.entityId)).size !== entities.length) return submissionSignature(entities, redirects);
    const changed = entities.filter(entity => {
      const previous = known.get(redirects.get(entity.entityId) ?? entity.entityId);
      return !previous || entity.names.some(value => !previous.names.has(identityKey(value))) ||
        entity.gameIds.some(value => !previous.gameIds.has(identityKey(value)));
    });
    if (changed.length !== entities.length) {
      projection ??= projectedLibrary(current.entities,
        reconcileLibrarySubmission(current.entities, entities, current.entityRedirects).entities, evidence);
      // Even a no-op row can trigger publication when every changed row is blocked in partial review.
      // Prune it only when the full projection cannot create redirects or new automatic evidence.
      if (!projection.automaticGroups.length) return submissionSignature(changed, redirects);
    }
    return submissionSignature(entities, redirects);
  };
}

function findPendingSubmission(db: Database.Database, deviceId: string, signature: string,
  signatureOf: ReturnType<typeof createPendingSignature>): number | undefined {
  const rows = db.prepare(`SELECT id,payload_json FROM player_library_submissions
    WHERE device_id=? AND status='pending' ORDER BY id`).iterate(deviceId) as Iterable<{ id: number; payload_json: string }>;
  for (const row of rows) {
    if (Buffer.byteLength(row.payload_json, 'utf8') > MAX_LIBRARY_BYTES) continue;
    let entities: PlayerEntity[];
    try {
      entities = parseAdminEntities(JSON.parse(row.payload_json), index => `submission-${row.id}-${index}`);
    } catch { continue; }
    if (signatureOf(entities) === signature) return row.id;
  }
  return undefined;
}

export interface PlayerLibraryReviewGuard { revision: number; submissionRevision: string }

export function approvePlayerLibrarySubmission(
  db: Database.Database, submissionId: number, nowSec: number, guard: PlayerLibraryReviewGuard,
): { ok: true; revision: number } | { ok: false; code: string } {
  try {
    const parsed = libraryReviewGuardSchema.safeParse(guard);
    if (!parsed.success) return { ok: false, code: 'invalid_request' };
    const result = reviewAdminSubmissions(db, parsed.data.revision, nowSec, 'approve',
      [{ id: submissionId, submissionRevision: parsed.data.submissionRevision }]);
    return { ok: true, revision: result.revision };
  } catch (error) {
    if (!(error instanceof LibraryAdminError)) return { ok: false, code: 'internal_error' };
    // Names retain unique ownership; identifier references can be shared.
    const code = error.code === 'ownership_conflict'
      ? error.conflicts.some(conflict => conflict.kind === 'names') ? 'name_conflict' : 'identifier_conflict'
      : error.code;
    return { ok: false, code };
  }
}

export function createPlayerLibraryEntity(
  db: Database.Database, entity: SubmittedPlayerEntity, nowSec: number, revision: number,
): { revision: number; entity: PublicPlayerEntity } {
  if (!revisionSchema.safeParse(revision).success) throw new LibraryAdminError(400, 'invalid_request');
  const result = mutatePublicLibrary(db, revision, nowSec, 'create', parseAdminEntities({ entities: [entity] }));
  return { revision: result.revision, entity: result.entity! };
}

function loadSession(
  db: Database.Database,
  token: string,
  deviceId: string,
  nowSec: number,
): SessionContext | null {
  const row = db.prepare(
    `SELECT s.device_id, s.expires_at, l.id, l.key_hash, l.expires_at AS license_expires_at,
            l.disabled_at, l.bound_device_id
     FROM auth_sessions AS s
     JOIN licenses AS l ON l.id = s.license_id
     WHERE s.token_hash = ? AND s.device_id = ?`,
  ).get(hashSessionToken(token), deviceId) as {
    device_id: string;
    expires_at: number;
    id: number;
    key_hash: string;
    license_expires_at: number | null;
    disabled_at: number | null;
    bound_device_id: string | null;
  } | undefined;
  if (!row || nowSec >= row.expires_at || row.bound_device_id !== deviceId) return null;
  const license: LicenseRecord = {
    id: row.id,
    keyHash: row.key_hash,
    expiresAt: row.license_expires_at,
    disabledAt: row.disabled_at,
    boundDeviceId: row.bound_device_id,
  };
  if (!isLicenseUsable(license, nowSec).ok) return null;
  db.prepare('UPDATE auth_sessions SET last_seen_at = ? WHERE token_hash = ?').run(nowSec, hashSessionToken(token));
  return { deviceId, license };
}

function bearerToken(request: Request): string | null {
  const value = request.get('authorization');
  return value?.startsWith('Bearer ') ? value.slice(7).trim() || null : null;
}

function observeLicenseRequest(
  options: V2ApiOptions,
  request: Request,
  deviceId: string,
  license: LicenseRecord,
): void {
  if (!options.attribution) return;
  try {
    const remoteAddress = request.socket.remoteAddress ?? 'unknown';
    options.attribution.observeLicense({
      licenseId: license.id,
      licenseDeviceId: deviceId,
      ipAddress: options.resolveClientIp?.(remoteAddress) ?? remoteAddress,
      observedAt: options.now(),
    });
  } catch {
    // Attribution is diagnostic and must not block a valid authorization request.
  }
}

function requireSession(options: V2ApiOptions) {
  return (request: Request, _response: Response, next: NextFunction): void => {
    const deviceId = typeof request.header('x-dnf-device-id') === 'string'
      ? request.header('x-dnf-device-id')!.trim()
      : '';
    const token = bearerToken(request);
    const parsedDevice = deviceIdSchema.safeParse(deviceId);
    const session = parsedDevice.success && token
      ? loadSession(options.db, token, parsedDevice.data, options.now())
      : null;
    if (!session) {
      next(new V2RequestError(401, 'unauthorized'));
      return;
    }
    (request as Request & { v2Session: SessionContext }).v2Session = session;
    observeLicenseRequest(options, request, session.deviceId, session.license);
    next();
  };
}

function currentPolicy(db: Database.Database, deviceId: string): number | null {
  return (db.prepare(
    'SELECT ocr_disabled_until FROM broadcaster_policies WHERE device_id = ?',
  ).get(deviceId) as { ocr_disabled_until: number | null } | undefined)?.ocr_disabled_until ?? null;
}

function authResponse(
  options: V2ApiOptions,
  deviceId: string,
  token: string,
  license: LicenseRecord,
): Record<string, unknown> {
  return {
    ok: true,
    sessionToken: token,
    cloudServerUrl: options.serverUrl,
    // Keep the wire value numeric so native clients can distinguish a
    // permanent license from an omitted/invalid expiry without relying on
    // JSON null conversion rules.
    licenseExpiresAt: license.expiresAt ?? 0xFFFFFFFF,
    ocrDisabledUntil: currentPolicy(options.db, deviceId),
    capabilities: [
      'server_auth_v2',
      'player_library_v2',
      'player_library_submit_review',
      'ocr_policy_v1',
    ],
  };
}

function handleError(
  error: unknown,
  _request: Request,
  response: Response,
  _next: NextFunction,
): void {
  if (error instanceof V2RequestError || error instanceof LicenseError) {
    response.status(error.status).json({ ok: false, code: error.code });
    return;
  }
  response.status(500).json({ ok: false, code: 'internal_error' });
}

export function createV2Api(options: V2ApiOptions): Router {
  const router = express.Router();
  const sessionTtlSeconds = options.sessionTtlSeconds ?? DEFAULT_SESSION_TTL_SECONDS;
  router.use(express.json({ limit: `${MAX_LIBRARY_BYTES + 32_768}b` }));

  // Read-only deployment probe: never activate a real card just to verify an upgrade.
  router.get('/health', (_request, response) => {
    response.json({ ok: true, protocolVersion: 2, cloudServerUrl: options.serverUrl,
      allowLegacyPermanentKeys: options.allowLegacyPermanentKeys === true });
  });

  router.post('/auth/activate', (request, response, next) => {
    try {
      const parsed = activateSchema.safeParse(request.body);
      if (!parsed.success) throw new V2RequestError(400, 'invalid_request');
      const { key, deviceId } = parsed.data;
      const { token, license } = activateStoredLicense(options.db, key, deviceId, options.now(), sessionTtlSeconds,
        options.allowLegacyPermanentKeys);
      observeLicenseRequest(options, request, deviceId, license);
      response.json(authResponse(options, deviceId, token, license));
    } catch (error) {
      next(error);
    }
  });

  router.post('/auth/validate', (request, response, next) => {
    try {
      const parsed = validateSchema.safeParse(request.body);
      if (!parsed.success) throw new V2RequestError(400, 'invalid_request');
      const session = loadSession(options.db, parsed.data.sessionToken, parsed.data.deviceId, options.now());
      if (!session) throw new V2RequestError(401, 'invalid_session');
      observeLicenseRequest(options, request, parsed.data.deviceId, session.license);
      response.json(authResponse(options, parsed.data.deviceId, parsed.data.sessionToken, session.license));
    } catch (error) {
      next(error);
    }
  });

  router.get('/player-library', requireSession(options), (request, response, next) => {
    try {
      const library = listPlayerLibrary(options.db);
      if (jsonByteLength(library) > MAX_LIBRARY_BYTES) throw new V2RequestError(413, 'library_too_large');
      response.json({ ok: true, ...library, aliasAppendSupported: true });
    } catch (error) {
      next(error);
    }
  });

  router.post('/player-library/resolve', requireSession(options), (request, response, next) => {
    try {
      const parsed = resolveSchema.safeParse(request.body);
      if (!parsed.success) throw new V2RequestError(400, 'invalid_request');
      const library = listPlayerLibrary(options.db);
      const redirects = new Map(library.entityRedirects.map(item => [item.fromEntityId, item.toEntityId]));
      const activeIds = parsed.data.activeEntityIds?.map(id => redirects.get(id) ?? id);
      if (activeIds && new Set(activeIds).size !== activeIds.length) throw new V2RequestError(400, 'ambiguous_active_roster');
      response.json({ ok: true, ...resolvePlayerIdentity(library.entities, parsed.data.gameIds, activeIds) });
    } catch (error) {
      next(error);
    }
  });

  router.post('/player-library/submit', requireSession(options), (request, response, next) => {
    try {
      const parsed = playerLibraryPayloadSchema.safeParse(request.body);
      if (!parsed.success || jsonByteLength(request.body) > MAX_LIBRARY_BYTES) {
        throw new V2RequestError(400, 'invalid_library');
      }
      const session = (request as Request & { v2Session: SessionContext }).v2Session;
      const normalized = normalizeSubmittedEntities(parsed.data.entities, session.deviceId);
      const result = options.db.transaction(() => {
        const current = listPlayerLibrary(options.db);
        if (!normalized.length) return { ok: true, status: 'no_changes', revision: current.revision,
          identifierConflictCount: 0, ownershipConflictCount: 0 };
        const analysis = reconcileLibrarySubmission(current.entities, normalized, current.entityRedirects);
        const evidence = readAutomaticIdentityEvidence(options.db);
        const projection = projectedLibrary(current.entities, analysis.entities, evidence);
        const counts = { revision: current.revision,
          identifierConflictCount: projection.conflicts.filter(conflict => conflict.kind !== 'names').length,
          ownershipConflictCount: projection.conflicts.length };
        if (new Set(normalized.map(entity => entity.entityId)).size === normalized.length &&
          !analysis.addedEntityCount && !analysis.updatedEntityCount &&
          !projection.conflicts.length && !projection.automaticGroups.length) {
          return { ok: true, status: 'no_changes', ...counts };
        }
        const signatureOf = createPendingSignature(current, evidence);
        const pendingId = findPendingSubmission(options.db, session.deviceId, signatureOf(normalized, projection), signatureOf);
        if (pendingId !== undefined) return { ok: true, status: 'already_pending', submissionId: pendingId, ...counts };
        // Check the current draft, never an original copy or a rejected/approved submission.
        const inserted = options.db.prepare(
          `INSERT INTO player_library_submissions (device_id, payload_json, status, created_at)
           VALUES (?, ?, 'pending', ?)`,
        ).run(session.deviceId, JSON.stringify({ entities: normalized } satisfies { entities: PlayerEntity[] }), options.now());
        return { ok: true, status: 'pending_review', submissionId: Number(inserted.lastInsertRowid), ...counts };
      }).immediate();
      response.status(result.status === 'pending_review' ? 202 : 200).json(result);
    } catch (error) {
      next(error);
    }
  });

  router.use(handleError);
  return router;
}

export function listPendingPlayerLibrarySubmissions(db: Database.Database): Array<{
  id: number;
  deviceId: string;
  createdAt: number;
  status: string;
}> {
  return (db.prepare(
    `SELECT id, device_id, created_at, status
     FROM player_library_submissions WHERE status = 'pending' ORDER BY created_at, id`,
  ).all() as Array<{ id: number; device_id: string; created_at: number; status: string }>).map((row) => ({
    id: row.id,
    deviceId: row.device_id,
    createdAt: row.created_at,
    status: row.status,
  }));
}

export function getPlayerLibrarySubmission(db: Database.Database, submissionId: number) {
  try {
    return db.transaction(() => {
      const library = listPlayerLibrary(db);
      const submission = readAdminSubmission(db, submissionId, library.entities);
      return { ...submission, revision: library.revision, ownershipConflicts: submission.conflicts,
        conflicts: { gameIds: [] },
        sharedIdentifiers: detectIdentifierConflicts(submission.entities) };
    })();
  } catch (error) {
    if (error instanceof LibraryAdminError && error.code === 'submission_not_found') return null;
    throw error;
  }
}

export function rejectPlayerLibrarySubmission(
  db: Database.Database, submissionId: number, nowSec: number, guard: PlayerLibraryReviewGuard,
): boolean {
  const parsed = libraryReviewGuardSchema.safeParse(guard);
  if (!parsed.success) throw new LibraryAdminError(400, 'invalid_request');
  reviewAdminSubmissions(db, parsed.data.revision, nowSec, 'reject',
    [{ id: submissionId, submissionRevision: parsed.data.submissionRevision }]);
  return true;
}

export function setLicenseDisabled(
  db: Database.Database,
  licenseId: number,
  disabledAt: number | null,
  nowSec: number,
): boolean {
  if (!db.prepare('SELECT 1 FROM licenses WHERE id=?').get(licenseId)) return false;
  disableLicense(db, licenseId, disabledAt !== null, undefined, undefined, nowSec);
  return true;
}

export function setBroadcasterOcrDisabledUntil(
  db: Database.Database,
  deviceId: string,
  disabledUntil: number | null,
  nowSec: number,
): void {
  db.prepare(
    `INSERT INTO broadcaster_policies (device_id, ocr_disabled_until, updated_at)
     VALUES (?, ?, ?)
     ON CONFLICT(device_id) DO UPDATE SET
       ocr_disabled_until = excluded.ocr_disabled_until,
       updated_at = excluded.updated_at`,
  ).run(deviceId, disabledUntil, nowSec);
}

export function getBroadcasterOcrDisabledUntil(
  db: Database.Database,
  deviceId: string,
): number | null {
  return currentPolicy(db, deviceId);
}
