import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import type Database from 'better-sqlite3';

export interface RegisteredDevice {
  deviceId: string;
  deviceToken: string;
}

interface StoredDeviceToken {
  token_hash: string;
}

function hashToken(token: string): Buffer {
  return createHash('sha256').update(token, 'utf8').digest();
}

export function registerDevice(
  db: Database.Database,
  deviceId: string,
  nowSec: number,
): RegisteredDevice {
  const deviceToken = randomBytes(32).toString('base64url');
  const tokenHash = hashToken(deviceToken).toString('hex');
  const register = db.transaction(() => {
    db.prepare(
      `INSERT INTO devices (id, token_hash, created_at, last_seen_at)
       VALUES (?, ?, ?, ?)
       ON CONFLICT(id) DO UPDATE SET
         token_hash = excluded.token_hash,
         last_seen_at = excluded.last_seen_at`,
    ).run(deviceId, tokenHash, nowSec, nowSec);
  });

  register();
  return { deviceId, deviceToken };
}

export function authenticateDevice(
  db: Database.Database,
  deviceId: string,
  deviceToken: string,
): boolean {
  const stored = db
    .prepare('SELECT token_hash FROM devices WHERE id = ?')
    .get(deviceId) as StoredDeviceToken | undefined;
  if (!stored || !/^[a-f0-9]{64}$/.test(stored.token_hash)) {
    return false;
  }

  const expectedHash = Buffer.from(stored.token_hash, 'hex');
  const suppliedHash = hashToken(deviceToken);
  return expectedHash.length === suppliedHash.length && timingSafeEqual(expectedHash, suppliedHash);
}

export function touchLastSeen(
  db: Database.Database,
  deviceId: string,
  nowSec: number,
): void {
  db.prepare('UPDATE devices SET last_seen_at = ? WHERE id = ?').run(nowSec, deviceId);
}
