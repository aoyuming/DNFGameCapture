import { randomBytes } from 'node:crypto';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
  type Router,
} from 'express';
import type Database from 'better-sqlite3';
import { z } from 'zod';

import {
  createSessionToken,
  generateLicenseKey,
  hashLicenseKey,
  hashSessionToken,
  isLicenseUsable,
  verifyLicenseKey,
  type LicenseRecord,
} from './auth.js';
import {
  canonicalizeIdentifiers,
  detectIdentifierConflicts,
  normalizeIdentifier,
  resolvePlayerIdentity,
  type PlayerEntity,
} from './player-library.js';
import { deviceIdSchema, playerNameSchema } from './schemas.js';

const MAX_LIBRARY_BYTES = 256 * 1024;
const DEFAULT_SESSION_TTL_SECONDS = 5 * 24 * 60 * 60;
const entityIdSchema = z.string().min(1).max(128).regex(/^[A-Za-z0-9_-]+$/);
const identifierSchema = z.string().min(1).max(128);
const playerEntityInputSchema = z.object({
  entityId: entityIdSchema.optional(),
  names: z.array(playerNameSchema).min(1).max(32),
  gameIds: z.array(identifierSchema).max(256),
  adventureGroupIds: z.array(identifierSchema).max(256),
}).strict();
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
  adventureGroupIds: z.array(identifierSchema).max(64).default([]),
}).strict();

export interface V2ApiOptions {
  db: Database.Database;
  now(): number;
  serverUrl: string;
  sessionTtlSeconds?: number;
}

export interface SubmittedPlayerEntity {
  entityId?: string;
  names: string[];
  gameIds: string[];
  adventureGroupIds: string[];
}

export interface PublicPlayerEntity extends PlayerEntity {}

interface StoredLicenseRow {
  id: number;
  key_hash: string;
  expires_at: number | null;
  disabled_at: number | null;
  bound_device_id: string | null;
}

interface SessionContext {
  deviceId: string;
  license: LicenseRecord;
}

interface LibrarySubmissionRow {
  id: number;
  device_id: string;
  payload_json: string;
  status: 'pending' | 'approved' | 'rejected';
  created_at: number;
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

function safeEntityId(value: string | undefined): string {
  return value ?? `player-${randomBytes(12).toString('hex')}`;
}

function normalizeSubmittedEntities(
  entities: readonly SubmittedPlayerEntity[],
): PlayerEntity[] {
  return entities.map((entity) => ({
    entityId: safeEntityId(entity.entityId),
    names: [...new Set(entity.names.map((name) => name.normalize('NFC').trim()))],
    gameIds: canonicalizeIdentifiers(entity.gameIds),
    adventureGroupIds: canonicalizeIdentifiers(entity.adventureGroupIds),
  }));
}

function readLibraryEntities(db: Database.Database): PublicPlayerEntity[] {
  const rows = db.prepare(
    'SELECT entity_id, created_at, updated_at FROM player_entities ORDER BY entity_id',
  ).all() as Array<{ entity_id: string; created_at: number; updated_at: number }>;
  const names = db.prepare(
    'SELECT entity_id, display_name FROM player_entity_names ORDER BY entity_id, name_norm',
  ).all() as Array<{ entity_id: string; display_name: string }>;
  const identifiers = db.prepare(
    `SELECT entity_id, kind, display_value
     FROM player_entity_identifiers
     ORDER BY entity_id, kind, identifier_norm`,
  ).all() as Array<{ entity_id: string; kind: 'game' | 'adventure'; display_value: string }>;
  const byId = new Map<string, PublicPlayerEntity>();
  for (const row of rows) {
    byId.set(row.entity_id, {
      entityId: row.entity_id,
      names: [],
      gameIds: [],
      adventureGroupIds: [],
    });
  }
  for (const row of names) byId.get(row.entity_id)?.names.push(row.display_name);
  for (const row of identifiers) {
    const entity = byId.get(row.entity_id);
    if (!entity) continue;
    (row.kind === 'game' ? entity.gameIds : entity.adventureGroupIds).push(row.display_value);
  }
  return [...byId.values()];
}

export function listPlayerLibrary(db: Database.Database): {
  revision: number;
  entities: PublicPlayerEntity[];
} {
  const revision = (db.prepare(
    'SELECT revision FROM player_library_meta WHERE id = 1',
  ).get() as { revision: number } | undefined)?.revision ?? 0;
  return { revision, entities: readLibraryEntities(db) };
}

function bumpLibraryRevision(db: Database.Database): number {
  db.prepare('UPDATE player_library_meta SET revision = revision + 1 WHERE id = 1').run();
  return (db.prepare(
    'SELECT revision FROM player_library_meta WHERE id = 1',
  ).get() as { revision: number }).revision;
}

function identifierConflictError(
  conflicts: ReturnType<typeof detectIdentifierConflicts>,
): V2RequestError | null {
  if (conflicts.gameIds.length > 0 || conflicts.adventureGroupIds.length > 0) {
    return new V2RequestError(409, 'identifier_conflict');
  }
  return null;
}

function mergePublicEntities(
  db: Database.Database,
  inputEntities: readonly PlayerEntity[],
  nowSec: number,
): { revision: number; entities: PublicPlayerEntity[] } {
  const payloadConflict = identifierConflictError(detectIdentifierConflicts(inputEntities));
  if (payloadConflict) throw payloadConflict;

  return db.transaction(() => {
    const existing = readLibraryEntities(db);
    const byEntityId = new Map(existing.map((item) => [item.entityId, item]));
    const byName = new Map<string, string>();
    for (const entity of existing) {
      for (const name of entity.names) byName.set(name.normalize('NFC').trim().toLocaleLowerCase(), entity.entityId);
    }

    let changed = false;
    for (const incoming of inputEntities) {
      const namedOwners = [...new Set(incoming.names
        .map((name) => byName.get(name.normalize('NFC').trim().toLocaleLowerCase()))
        .filter((value): value is string => Boolean(value)))];
      if (namedOwners.length > 1) throw new V2RequestError(409, 'name_conflict');
      const entityId = incoming.entityId && byEntityId.has(incoming.entityId)
        ? incoming.entityId
        : namedOwners[0] ?? incoming.entityId ?? safeEntityId(undefined);
      const current = byEntityId.get(entityId);
      if (!current) {
        db.prepare(
          'INSERT INTO player_entities (entity_id, created_at, updated_at) VALUES (?, ?, ?)',
        ).run(entityId, nowSec, nowSec);
        byEntityId.set(entityId, {
          entityId,
          names: [],
          gameIds: [],
          adventureGroupIds: [],
        });
        changed = true;
      }
      const target = byEntityId.get(entityId)!;
      const allNames = canonicalizeIdentifiers([...target.names, ...incoming.names]);
      for (const name of allNames) {
        const key = name.toLocaleLowerCase();
        const otherOwner = byName.get(key);
        if (otherOwner && otherOwner !== entityId) throw new V2RequestError(409, 'name_conflict');
        if (!target.names.some((item) => item.toLocaleLowerCase() === key)) {
          db.prepare(
            `INSERT INTO player_entity_names (entity_id, name_norm, display_name)
             VALUES (?, ?, ?)`,
          ).run(entityId, key, name);
          target.names.push(name);
          byName.set(key, entityId);
          changed = true;
        }
      }
      for (const [kind, values] of [
        ['game', incoming.gameIds] as const,
        ['adventure', incoming.adventureGroupIds] as const,
      ]) {
        const targetValues = kind === 'game' ? target.gameIds : target.adventureGroupIds;
        for (const value of canonicalizeIdentifiers(values)) {
          const identifierNorm = value.toLocaleLowerCase();
          const owner = db.prepare(
            `SELECT entity_id FROM player_entity_identifiers
             WHERE kind = ? AND identifier_norm = ?`,
          ).get(kind, identifierNorm) as { entity_id: string } | undefined;
          if (owner && owner.entity_id !== entityId) {
            throw new V2RequestError(409, 'identifier_conflict');
          }
          if (!targetValues.some((item) => item.toLocaleLowerCase() === identifierNorm)) {
            db.prepare(
              `INSERT INTO player_entity_identifiers
               (entity_id, kind, identifier_norm, display_value)
               VALUES (?, ?, ?, ?)`,
            ).run(entityId, kind, identifierNorm, value);
            targetValues.push(value);
            changed = true;
          }
        }
      }
      if (changed) {
        db.prepare('UPDATE player_entities SET updated_at = ? WHERE entity_id = ?').run(nowSec, entityId);
      }
    }
    const revision = changed ? bumpLibraryRevision(db) : listPlayerLibrary(db).revision;
    return { revision, entities: readLibraryEntities(db) };
  })();
}

export function approvePlayerLibrarySubmission(
  db: Database.Database,
  submissionId: number,
  nowSec: number,
): { ok: true; revision: number } | { ok: false; code: string } {
  const row = db.prepare(
    `SELECT id, device_id, payload_json, status, created_at
     FROM player_library_submissions WHERE id = ?`,
  ).get(submissionId) as LibrarySubmissionRow | undefined;
  if (!row || row.status !== 'pending') return { ok: false, code: 'submission_not_pending' };
  let payload: unknown;
  try {
    payload = JSON.parse(row.payload_json);
  } catch {
    db.prepare(
      `UPDATE player_library_submissions SET status = 'rejected', reviewed_at = ?, review_reason = ? WHERE id = ?`,
    ).run(nowSec, 'invalid_payload', submissionId);
    return { ok: false, code: 'invalid_payload' };
  }
  const parsed = playerLibraryPayloadSchema.safeParse(payload);
  if (!parsed.success) {
    db.prepare(
      `UPDATE player_library_submissions SET status = 'rejected', reviewed_at = ?, review_reason = ? WHERE id = ?`,
    ).run(nowSec, 'invalid_payload', submissionId);
    return { ok: false, code: 'invalid_payload' };
  }
  try {
    const result = mergePublicEntities(db, normalizeSubmittedEntities(parsed.data.entities), nowSec);
    db.prepare(
      `UPDATE player_library_submissions SET status = 'approved', reviewed_at = ?, review_reason = NULL WHERE id = ?`,
    ).run(nowSec, submissionId);
    return { ok: true, revision: result.revision };
  } catch (error) {
    const code = error instanceof V2RequestError ? error.code : 'internal_error';
    db.prepare(
      `UPDATE player_library_submissions SET status = 'rejected', reviewed_at = ?, review_reason = ? WHERE id = ?`,
    ).run(nowSec, code, submissionId);
    return { ok: false, code };
  }
}

export function createPlayerLibraryEntity(
  db: Database.Database,
  entity: SubmittedPlayerEntity,
  nowSec: number,
): { revision: number; entity: PublicPlayerEntity } {
  const result = mergePublicEntities(db, normalizeSubmittedEntities([entity]), nowSec);
  const normalizedId = entity.entityId ?? result.entities.at(-1)?.entityId;
  const found = result.entities.find((item) => item.entityId === normalizedId);
  if (!found) throw new V2RequestError(500, 'entity_not_found');
  return { revision: result.revision, entity: found };
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
  if (error instanceof V2RequestError) {
    response.status(error.status).json({ ok: false, code: error.code });
    return;
  }
  response.status(500).json({ ok: false, code: 'internal_error' });
}

export function createV2Api(options: V2ApiOptions): Router {
  const router = express.Router();
  const sessionTtlSeconds = options.sessionTtlSeconds ?? DEFAULT_SESSION_TTL_SECONDS;
  router.use(express.json({ limit: `${MAX_LIBRARY_BYTES + 32_768}b` }));

  router.post('/auth/activate', (request, response, next) => {
    try {
      const parsed = activateSchema.safeParse(request.body);
      if (!parsed.success) throw new V2RequestError(400, 'invalid_request');
      const { key, deviceId } = parsed.data;
      const row = options.db.prepare(
        `SELECT id, key_hash, expires_at, disabled_at, bound_device_id
         FROM licenses WHERE key_hash = ?`,
      ).get(hashLicenseKey(key)) as StoredLicenseRow | undefined;
      if (!row) throw new V2RequestError(401, 'invalid_license');
      const license: LicenseRecord = {
        id: row.id,
        keyHash: row.key_hash,
        expiresAt: row.expires_at,
        disabledAt: row.disabled_at,
        boundDeviceId: row.bound_device_id,
      };
      if (!verifyLicenseKey(license, key)) throw new V2RequestError(401, 'invalid_license');
      const usable = isLicenseUsable(license, options.now());
      if (!usable.ok) throw new V2RequestError(403, `license_${usable.code}`);
      if (license.boundDeviceId && license.boundDeviceId !== deviceId) {
        throw new V2RequestError(409, 'license_bound_to_other_device');
      }
      const token = createSessionToken();
      const tokenHash = hashSessionToken(token);
      const nowSec = options.now();
      const expiresAt = Math.min(
        nowSec + sessionTtlSeconds,
        license.expiresAt ?? Number.MAX_SAFE_INTEGER,
      );
      options.db.transaction(() => {
        options.db.prepare(
          `UPDATE licenses SET bound_device_id = COALESCE(bound_device_id, ?), updated_at = ? WHERE id = ?`,
        ).run(deviceId, nowSec, license.id);
        options.db.prepare('DELETE FROM auth_sessions WHERE device_id = ?').run(deviceId);
        options.db.prepare(
          `INSERT INTO auth_sessions (token_hash, license_id, device_id, created_at, last_seen_at, expires_at)
           VALUES (?, ?, ?, ?, ?, ?)`,
        ).run(tokenHash, license.id, deviceId, nowSec, nowSec, expiresAt);
      })();
      response.json(authResponse(options, deviceId, token, { ...license, boundDeviceId: deviceId }));
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
      response.json({ ok: true, ...resolvePlayerIdentity(library.entities, parsed.data.gameIds, parsed.data.adventureGroupIds) });
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
      const normalized = normalizeSubmittedEntities(parsed.data.entities);
      const conflict = identifierConflictError(detectIdentifierConflicts(normalized));
      if (conflict) throw conflict;
      const createdAt = options.now();
      const result = options.db.prepare(
        `INSERT INTO player_library_submissions (device_id, payload_json, status, created_at)
         VALUES (?, ?, 'pending', ?)`,
      ).run(session.deviceId, JSON.stringify({ entities: normalized } satisfies { entities: PlayerEntity[] }), createdAt);
      response.status(202).json({ ok: true, status: 'pending_review', submissionId: Number(result.lastInsertRowid) });
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

export function createLicense(
  db: Database.Database,
  input: { key: string; label?: string; expiresAt?: number | null; nowSec: number },
): { id: number; keyHash: string; expiresAt: number | null } {
  const keyHash = hashLicenseKey(input.key);
  const result = db.prepare(
    `INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
     VALUES (?, ?, ?, NULL, NULL, ?, ?)`,
  ).run(keyHash, input.label ?? '', input.expiresAt ?? null, input.nowSec, input.nowSec);
  return { id: Number(result.lastInsertRowid), keyHash, expiresAt: input.expiresAt ?? null };
}

export function listLicenses(db: Database.Database): Array<Record<string, unknown>> {
  return (db.prepare(
    `SELECT id, label, expires_at AS expiresAt, disabled_at AS disabledAt,
            bound_device_id AS boundDeviceId, created_at AS createdAt, updated_at AS updatedAt
     FROM licenses ORDER BY id DESC`,
  ).all() as Array<Record<string, unknown>>);
}

export function setLicenseDisabled(
  db: Database.Database,
  licenseId: number,
  disabledAt: number | null,
  nowSec: number,
): boolean {
  return db.prepare(
    'UPDATE licenses SET disabled_at = ?, updated_at = ? WHERE id = ?',
  ).run(disabledAt, nowSec, licenseId).changes === 1;
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
