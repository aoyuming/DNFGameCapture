import type Database from 'better-sqlite3';

import { ALL_BROADCASTERS_ROOM_ID } from './db.js';
import { broadcasterNameSchema } from './schemas.js';

export const BROADCASTER_RETENTION_SECONDS = 24 * 60 * 60;

export interface UnifiedBroadcaster {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
  online: boolean;
  snapshotRevision: number | null;
  receivedAt: number | null;
  offlineExpiresAt: number | null;
}

interface UnifiedBroadcasterRow {
  device_id: string;
  broadcaster_name: string;
  last_seen_at: number;
  client_revision: number | null;
  received_at: number | null;
}

export function pruneExpiredBroadcasters(
  db: Database.Database,
  nowSec: number,
): number {
  const cutoff = nowSec - BROADCASTER_RETENTION_SECONDS;
  const remove = db.transaction(() => {
    const ids = db.prepare(
      `SELECT m.device_id
       FROM memberships AS m
       JOIN devices AS d ON d.id = m.device_id
       WHERE m.room_id = ? AND d.last_seen_at <= ?`,
    ).all(ALL_BROADCASTERS_ROOM_ID, cutoff) as Array<{ device_id: string }>;
    if (ids.length === 0) return 0;
    const deleteSnapshot = db.prepare('DELETE FROM snapshots WHERE device_id = ?');
    const deleteMembership = db.prepare('DELETE FROM memberships WHERE device_id = ?');
    for (const row of ids) {
      deleteSnapshot.run(row.device_id);
      deleteMembership.run(row.device_id);
    }
    return ids.length;
  });
  return remove();
}

export function isUnifiedBroadcasterNameTaken(
  db: Database.Database,
  broadcasterName: string,
  activeDeviceIds: ReadonlySet<string>,
  excludeDeviceId = '',
): boolean {
  const normalized = broadcasterName.normalize('NFC').trim();
  const rows = db.prepare(
    `SELECT device_id, broadcaster_name
     FROM memberships WHERE room_id = ?`,
  ).all(ALL_BROADCASTERS_ROOM_ID) as Array<{
    device_id: string;
    broadcaster_name: string;
  }>;
  return rows.some((row) => row.device_id !== excludeDeviceId &&
    activeDeviceIds.has(row.device_id) &&
    row.broadcaster_name.normalize('NFC').trim() === normalized);
}

export function joinUnifiedPool(
  db: Database.Database,
  deviceId: string,
  broadcasterName: string,
  nowSec: number,
): { deviceId: string; broadcasterName: string } | null {
  const parsed = broadcasterNameSchema.safeParse(broadcasterName);
  if (!parsed.success) return null;
  const normalized = parsed.data;
  const join = db.transaction(() => {
    db.prepare(
      'INSERT OR IGNORE INTO rooms (id, display_name) VALUES (?, ?)',
    ).run(ALL_BROADCASTERS_ROOM_ID, '统一在线主播池');
    const previous = db.prepare(
      'SELECT room_id FROM memberships WHERE device_id = ?',
    ).get(deviceId) as { room_id: string } | undefined;
    db.prepare(
      `INSERT INTO memberships (device_id, room_id, broadcaster_name, updated_at)
       VALUES (?, ?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET
         room_id = excluded.room_id,
         broadcaster_name = excluded.broadcaster_name,
         updated_at = excluded.updated_at`,
    ).run(deviceId, ALL_BROADCASTERS_ROOM_ID, normalized, nowSec);
    if (previous?.room_id && previous.room_id !== ALL_BROADCASTERS_ROOM_ID) {
      db.prepare(
        'UPDATE snapshots SET room_id = ? WHERE device_id = ?',
      ).run(ALL_BROADCASTERS_ROOM_ID, deviceId);
    }
    return { deviceId, broadcasterName: normalized };
  });
  return join();
}

export function renameUnifiedBroadcaster(
  db: Database.Database,
  deviceId: string,
  broadcasterName: string,
  nowSec: number,
): { deviceId: string; broadcasterName: string } | null {
  const parsed = broadcasterNameSchema.safeParse(broadcasterName);
  if (!parsed.success) return null;
  const result = db.prepare(
    `UPDATE memberships SET broadcaster_name = ?, updated_at = ?
     WHERE device_id = ? AND room_id = ?`,
  ).run(parsed.data, nowSec, deviceId, ALL_BROADCASTERS_ROOM_ID);
  return result.changes === 1 ? { deviceId, broadcasterName: parsed.data } : null;
}

export function listUnifiedBroadcasters(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
  nowSec: number,
): UnifiedBroadcaster[] {
  pruneExpiredBroadcasters(db, nowSec);
  const rows = db.prepare(
    `SELECT m.device_id, m.broadcaster_name, d.last_seen_at,
            s.client_revision, s.received_at
     FROM memberships AS m
     JOIN devices AS d ON d.id = m.device_id
     LEFT JOIN snapshots AS s ON s.device_id = m.device_id
     WHERE m.room_id = ?
     ORDER BY CASE WHEN d.last_seen_at >= ? THEN 0 ELSE 1 END,
              m.broadcaster_name COLLATE NOCASE, m.device_id`,
  ).all(ALL_BROADCASTERS_ROOM_ID, nowSec - BROADCASTER_RETENTION_SECONDS) as UnifiedBroadcasterRow[];
  return rows.map((row) => {
    const online = activeDeviceIds.has(row.device_id);
    return {
      deviceId: row.device_id,
      broadcasterName: row.broadcaster_name,
      deviceSuffix: row.device_id.slice(-4),
      online,
      snapshotRevision: row.client_revision,
      receivedAt: row.received_at,
      offlineExpiresAt: online ? null : row.last_seen_at + BROADCASTER_RETENTION_SECONDS,
    };
  });
}
