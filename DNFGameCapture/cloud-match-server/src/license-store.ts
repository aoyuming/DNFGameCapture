import { createHash } from 'node:crypto';
import type Database from 'better-sqlite3';
import { createSessionToken, generateLicenseKey, hashLicenseKey, hashSessionToken, isLegacyPermanentLicenseKey, isLicenseUsable, normalizeLicenseKey, type LicenseRecord } from './auth.js';
import { decryptLicenseKey, encryptLicenseKey, LicenseVaultError } from './license-vault.js';

export const LICENSE_PRESETS = [
  { id: 'hour', label: '1小时', durationSeconds: 3600 },
  { id: 'day', label: '1天', durationSeconds: 86400 },
  { id: 'week', label: '7天', durationSeconds: 604800 },
  { id: 'ten_days', label: '10天', durationSeconds: 864000 },
  { id: 'quarter', label: '3个月（90天）', durationSeconds: 7776000 },
  { id: 'half_year', label: '半年（180天）', durationSeconds: 15552000 },
  { id: 'year', label: '1年（365天）', durationSeconds: 31536000 },
  { id: 'permanent', label: '永久', durationSeconds: null },
] as const;
export const MAX_LICENSE_BATCH = 200;
const MAX_EXPIRY = 0xFFFFFFFE;
export class LicenseError extends Error {
  constructor(public status: number, public code: string) { super(code); }
}
export function presetSeconds(id: string): number | null {
  const found = LICENSE_PRESETS.find(p => p.id === id);
  if (!found) throw new LicenseError(400, 'invalid_preset');
  return found.durationSeconds;
}
function boundedTime(value: number): number {
  if (!Number.isSafeInteger(value) || value <= 0 || value > MAX_EXPIRY) throw new LicenseError(400, 'invalid_expiry');
  return value;
}
export interface ManagedLicense extends LicenseRecord {
  label: string; createdAt: number; updatedAt: number; revision: number;
  activationMode: 'fixed' | 'first_use'; durationSeconds: number | null; activatedAt: number | null; hasKey: boolean;
}
type StoredLicense = Omit<ManagedLicense, 'hasKey'> & { keyCiphertext: string | null };
const columns = `id,key_hash AS keyHash,label,expires_at AS expiresAt,disabled_at AS disabledAt,
  bound_device_id AS boundDeviceId,created_at AS createdAt,updated_at AS updatedAt,revision,
  activation_mode AS activationMode,duration_seconds AS durationSeconds,activated_at AS activatedAt,key_ciphertext AS keyCiphertext`;
function storedLicense(db: Database.Database, id: number): StoredLicense {
  const row = db.prepare(`SELECT ${columns} FROM licenses WHERE id=?`).get(id) as StoredLicense | undefined;
  if (!row) throw new LicenseError(404, 'license_not_found');
  return row;
}
function publicLicense(row: StoredLicense) {
  const { keyHash: _hash, keyCiphertext, ...publicFields } = row;
  return { ...publicFields, hasKey: keyCiphertext !== null };
}
export function getManagedLicense(db: Database.Database, id: number) { return publicLicense(storedLicense(db, id)); }
export function listLicenses(db: Database.Database) {
  return (db.prepare(`SELECT ${columns} FROM licenses ORDER BY id DESC`).all() as StoredLicense[]).map(publicLicense);
}
function audit(db: Database.Database, id: number, action: string, now: number, details: object) {
  db.prepare('INSERT INTO license_audit(license_id,action,created_at,details_json) VALUES(?,?,?,?)').run(id, action, now, JSON.stringify(details));
}
export function licenseAudit(db: Database.Database, id: number) {
  storedLicense(db, id);
  const rows = db.prepare('SELECT id,action,created_at AS createdAt,details_json AS details FROM license_audit WHERE license_id=? ORDER BY id DESC LIMIT 200').all(id) as
    { id: number; action: string; createdAt: number; details: string }[];
  return rows.map(row => ({ ...row, details: JSON.parse(row.details) as object }));
}
export function revealLicense(db: Database.Database, id: number): string {
  const row = storedLicense(db, id);
  if (!row.keyCiphertext) throw new LicenseError(404, 'key_unavailable');
  return decryptLicenseKey(db, row.keyCiphertext, row.keyHash);
}
function verifyRevision(row: StoredLicense, revision: number | undefined) {
  if (revision !== undefined && row.revision !== revision) throw new LicenseError(409, 'stale_license');
}
function operation<T>(db: Database.Database, requestId: string | undefined, payload: object, now: number, task: () => T): { value: T; replayed: boolean } {
  return db.transaction(() => {
    const fingerprint = createHash('sha256').update(JSON.stringify(payload)).digest('hex');
    if (requestId) {
      const old = db.prepare('SELECT fingerprint,result_json FROM license_admin_operations WHERE request_id=?').get(requestId) as { fingerprint: string; result_json: string } | undefined;
      if (old) {
        if (old.fingerprint !== fingerprint) throw new LicenseError(409, 'request_id_conflict');
        return { value: JSON.parse(old.result_json) as T, replayed: true };
      }
    }
    const value = task();
    if (requestId) db.prepare('INSERT INTO license_admin_operations VALUES(?,?,?,?)').run(requestId, fingerprint, JSON.stringify(value), now);
    return { value, replayed: false };
  }).immediate();
}
export function createLicense(db: Database.Database, input: { key: string; label?: string; expiresAt?: number | null; nowSec: number }) {
  return db.transaction(() => {
    const keyHash = hashLicenseKey(input.key), ciphertext = encryptLicenseKey(db, input.key);
    const expiresAt = input.expiresAt ?? null;
    if (expiresAt !== null) boundedTime(expiresAt);
    const result = db.prepare(`INSERT INTO licenses(key_hash,label,expires_at,created_at,updated_at,key_ciphertext) VALUES(?,?,?,?,?,?)`)
      .run(keyHash, input.label ?? '', expiresAt, input.nowSec, input.nowSec, ciphertext);
    const id = Number(result.lastInsertRowid); audit(db, id, 'generate', input.nowSec, { activationMode: 'fixed', expiresAt });
    return { id, keyHash, expiresAt };
  }).immediate();
}
export function generateLicenseBatch(db: Database.Database, input: { preset: string; count: number; label: string; key?: string; requestId: string }, now: number) {
  const duration = presetSeconds(input.preset);
  const result = operation(db, input.requestId, { action: 'generate', ...input }, now, () => {
    const ids: number[] = [];
    for (let i = 0; i < input.count; ++i) {
      const key = input.key || generateLicenseKey(duration ?? 0xFFFFFFFF);
      const hash = hashLicenseKey(key);
      if (db.prepare('SELECT 1 FROM licenses WHERE key_hash=?').get(hash)) throw new LicenseError(409, 'license_already_exists');
      const created = db.prepare(`INSERT INTO licenses(key_hash,label,expires_at,created_at,updated_at,activation_mode,duration_seconds,key_ciphertext)
        VALUES(?,?,NULL,?,?,'first_use',?,?)`).run(hash, input.label, now, now, duration, encryptLicenseKey(db, key));
      const id = Number(created.lastInsertRowid); ids.push(id);
      audit(db, id, 'generate', now, { activationMode: 'first_use', durationSeconds: duration, batchCount: input.count });
    }
    return ids;
  });
  return { licenses: result.value.map(id => ({ ...getManagedLicense(db, id), key: revealLicense(db, id) })), replayed: result.replayed };
}
export function extendLicense(db: Database.Database, id: number, preset: string, revision: number, requestId: string, now: number) {
  const added = presetSeconds(preset);
  const result = operation(db, requestId, { action: 'extend', id, preset, revision }, now, () => {
    const row = storedLicense(db, id); verifyRevision(row, revision);
    const pending = row.activationMode === 'first_use' && row.activatedAt === null;
    const expiresAt = pending || added === null || row.expiresAt === null ? null : boundedTime(Math.max(now, row.expiresAt) + added);
    const duration = added === null ? null : pending
      ? row.durationSeconds === null ? null : boundedTime(row.durationSeconds + added)
      : expiresAt === null ? null : row.durationSeconds;
    if (duration !== row.durationSeconds || expiresAt !== row.expiresAt) {
      db.prepare('UPDATE licenses SET expires_at=?,duration_seconds=?,updated_at=?,revision=revision+1 WHERE id=?').run(expiresAt, duration, now, id);
    }
    audit(db, id, 'extend', now, { preset, beforeExpiresAt: row.expiresAt, expiresAt, beforeDurationSeconds: row.durationSeconds, durationSeconds: duration });
    return id;
  });
  return { license: getManagedLicense(db, id), replayed: result.replayed };
}
export function listLicenseDevices(db: Database.Database) {
  // Authorization machine IDs and lobby device IDs belong to different namespaces.
  return db.prepare(`SELECT deviceId,'' AS name FROM (
    SELECT device_id AS deviceId FROM auth_sessions
    UNION SELECT bound_device_id FROM licenses WHERE bound_device_id IS NOT NULL
    UNION SELECT json_extract(details_json,'$.deviceId') FROM license_audit WHERE action='activate'
  ) WHERE deviceId IS NOT NULL ORDER BY deviceId`).all() as { deviceId: string; name: string }[];
}
export function rebindLicense(db: Database.Database, id: number, deviceId: string | null, revision: number, requestId: string, now: number) {
  const result = operation(db, requestId, { action: 'rebind', id, deviceId, revision }, now, () => {
    const row = storedLicense(db, id); verifyRevision(row, revision);
    if (deviceId !== null && !listLicenseDevices(db).some(d => d.deviceId === deviceId)) throw new LicenseError(400, 'unknown_device');
    db.prepare('DELETE FROM auth_sessions WHERE license_id=?').run(id);
    db.prepare('UPDATE licenses SET bound_device_id=?,updated_at=?,revision=revision+1 WHERE id=?').run(deviceId, now, id);
    audit(db, id, 'rebind', now, { beforeDeviceId: row.boundDeviceId, deviceId });
    return row.boundDeviceId;
  });
  return { license: getManagedLicense(db, id), replayed: result.replayed, oldDeviceId: result.value };
}
export function disableLicense(db: Database.Database, id: number, disabled: boolean, revision: number | undefined, requestId: string | undefined, now: number) {
  const result = operation(db, requestId, { action: 'disable', id, disabled, revision }, now, () => {
    const row = storedLicense(db, id); verifyRevision(row, revision);
    if (disabled) db.prepare('DELETE FROM auth_sessions WHERE license_id=?').run(id);
    if ((row.disabledAt !== null) !== disabled) {
      db.prepare('UPDATE licenses SET disabled_at=?,updated_at=?,revision=revision+1 WHERE id=?').run(disabled ? now : null, now, id);
      audit(db, id, disabled ? 'disable' : 'enable', now, {});
    }
    return row.boundDeviceId;
  });
  return { license: getManagedLicense(db, id), disabled, replayed: result.replayed, oldDeviceId: result.value };
}
export function recoverLicenseKey(db: Database.Database, id: number, key: string, revision: number, requestId: string, now: number) {
  const result = operation(db, requestId, { action: 'recover_key', id, keyHash: hashLicenseKey(key), revision }, now, () => {
    const row = storedLicense(db, id); verifyRevision(row, revision);
    if (hashLicenseKey(key) !== row.keyHash) throw new LicenseError(400, 'key_mismatch');
    db.prepare('UPDATE licenses SET key_ciphertext=?,updated_at=?,revision=revision+1 WHERE id=?').run(encryptLicenseKey(db, key), now, id);
    audit(db, id, 'recover_key', now, {}); return id;
  });
  return { license: getManagedLicense(db, id), replayed: result.replayed };
}

function hasEquivalentLegacyLicense(db: Database.Database, key: string): boolean {
  const rows = db.prepare('SELECT key_hash FROM licenses').all() as { key_hash: string }[];
  if (!rows.length) return false;
  const hashes = new Set(rows.map(row => row.key_hash));
  const [, duration, nonce, rawSignature] = normalizeLicenseKey(key).split('-');
  const signature = BigInt('0x' + rawSignature).toString(16).toUpperCase();
  // Earlier admin APIs accepted leading zeroes and kept only the input hash.
  // Check equivalent spellings within their 256-character limit before enrollment.
  // Existing exact-key authentication never takes this compatibility-only path.
  const paddingLimit = 256 - `CDK-${duration}-${nonce}-${signature}`.length;
  for (let durationPadding = 0; durationPadding <= paddingLimit; durationPadding++) {
    const prefix = `CDK-${'0'.repeat(durationPadding)}${duration}-${nonce}-`;
    for (let padding = 0; padding <= paddingLimit - durationPadding; padding++) {
      if (hashes.has(hashLicenseKey(prefix + '0'.repeat(padding) + signature))) return true;
    }
  }
  return false;
}

export function activateStoredLicense(db: Database.Database, key: string, deviceId: string, now: number, sessionTtl: number,
  allowLegacyPermanentKeys = false) {
  return db.transaction(() => {
    const keyHash = hashLicenseKey(key);
    let match = db.prepare('SELECT id FROM licenses WHERE key_hash=?').get(keyHash) as { id: number } | undefined;
    if (!match) {
      if (!allowLegacyPermanentKeys || !isLegacyPermanentLicenseKey(key)) throw new LicenseError(401, 'invalid_license');
      if (hasEquivalentLegacyLicense(db, key)) throw new LicenseError(409, 'license_already_exists');
      const ciphertext = encryptLicenseKey(db, key);
      const inserted = db.prepare(`INSERT INTO licenses(key_hash,label,expires_at,created_at,updated_at,key_ciphertext)
        VALUES(?,?,NULL,?,?,?)`).run(keyHash, '旧永久卡自动登记', now, now, ciphertext);
      match = { id: Number(inserted.lastInsertRowid) };
      audit(db, match.id, 'enroll_legacy', now, { activationMode: 'fixed', expiresAt: null });
    }
    const row = storedLicense(db, match.id);
    const usable = isLicenseUsable(row, now);
    if (!usable.ok) throw new LicenseError(403, 'license_' + usable.code);
    if (row.boundDeviceId && row.boundDeviceId !== deviceId) throw new LicenseError(409, 'license_bound_to_other_device');
    const first = row.activatedAt === null;
    const expiresAt = first && row.activationMode === 'first_use'
      ? row.durationSeconds === null ? null : boundedTime(now + row.durationSeconds) : row.expiresAt;
    let ciphertext = row.keyCiphertext;
    if (!ciphertext) {
      try { ciphertext = encryptLicenseKey(db, normalizeLicenseKey(key)); }
      catch (error) { if (!(error instanceof LicenseVaultError)) throw error; }
    }
    const changed = first || row.boundDeviceId !== deviceId || ciphertext !== row.keyCiphertext;
    db.prepare(`UPDATE licenses SET expires_at=?,activated_at=COALESCE(activated_at,?),bound_device_id=?,key_ciphertext=?,updated_at=?,revision=revision+? WHERE id=?`)
      .run(expiresAt, now, deviceId, ciphertext, now, changed ? 1 : 0, row.id);
    const token = createSessionToken();
    db.prepare('DELETE FROM auth_sessions WHERE device_id=?').run(deviceId);
    db.prepare('INSERT INTO auth_sessions(token_hash,license_id,device_id,created_at,last_seen_at,expires_at) VALUES(?,?,?,?,?,?)')
      .run(hashSessionToken(token), row.id, deviceId, now, now, Math.min(now + sessionTtl, expiresAt ?? Number.MAX_SAFE_INTEGER));
    if (first || row.boundDeviceId !== deviceId) audit(db, row.id, 'activate', now, { deviceId, expiresAt, firstActivation: first });
    return { token, license: { ...row, expiresAt, boundDeviceId: deviceId } };
  }).immediate();
}
