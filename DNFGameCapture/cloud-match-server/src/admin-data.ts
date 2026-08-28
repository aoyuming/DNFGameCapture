import type Database from 'better-sqlite3';

import type { MatchSnapshot } from './schemas.js';
import { getSnapshot } from './snapshots.js';
import {
  listAllRealtimeSync,
  listAllSyncHistory,
  pruneSyncRelationData,
} from './sync-relations.js';
import {
  listUnifiedBroadcasters,
  pruneExpiredBroadcasters,
} from './unified.js';

export interface AdminBroadcasterState {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
  online: boolean;
  snapshotRevision: number | null;
  receivedAt: number | null;
  offlineExpiresAt: number | null;
  snapshot: MatchSnapshot | null;
}

export interface AdminState {
  generatedAt: number;
  broadcasters: AdminBroadcasterState[];
  relations: ReturnType<typeof listAllRealtimeSync>;
  history: ReturnType<typeof listAllSyncHistory>;
}

export interface AdminCleanupResult {
  deletedCount: number;
  deletedDeviceIds: string[];
}

export function buildAdminState(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
  nowSec: number,
  query = '',
): AdminState {
  const normalizedQuery = query.normalize('NFC').trim().toLocaleLowerCase();
  const broadcasters = listUnifiedBroadcasters(db, activeDeviceIds, nowSec)
    .filter((item) => {
      if (!normalizedQuery) return true;
      return item.broadcasterName.toLocaleLowerCase().includes(normalizedQuery) ||
        item.deviceId.toLocaleLowerCase().includes(normalizedQuery) ||
        item.deviceSuffix.toLocaleLowerCase().includes(normalizedQuery);
    })
    .map((item) => ({
      ...item,
      snapshot: getSnapshot(db, item.deviceId)?.snapshot ?? null,
    }));
  return {
    generatedAt: nowSec,
    broadcasters,
    relations: listAllRealtimeSync(db, nowSec),
    history: listAllSyncHistory(db, nowSec),
  };
}

export function listAdminTemporaryBroadcasterIds(
  db: Database.Database,
): string[] {
  const rows = db.prepare(
    `SELECT device_id
     FROM memberships
     WHERE room_id = 'all-broadcasters' AND device_id LIKE 'dnf-tmp-%'
     ORDER BY device_id`,
  ).all() as Array<{ device_id: string }>;
  return rows.map((row) => row.device_id);
}

export function deleteBroadcasterLobbyData(
  db: Database.Database,
  deviceId: string,
): boolean {
  return db.transaction(() => {
    const membership = db.prepare(
      'SELECT room_id FROM memberships WHERE device_id = ?',
    ).get(deviceId) as { room_id: string } | undefined;
    if (!membership) return false;

    db.prepare(
      `DELETE FROM sync_history
       WHERE source_device_id = ? OR target_device_id = ?`,
    ).run(deviceId, deviceId);
    db.prepare(
      `DELETE FROM realtime_sync
       WHERE viewer_device_id = ? OR target_device_id = ?`,
    ).run(deviceId, deviceId);
    db.prepare('DELETE FROM snapshot_audit WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM snapshots WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM memberships WHERE device_id = ?').run(deviceId);
    db.prepare(
      `UPDATE rooms
       SET revision = revision + 1,
           presence_revision = presence_revision + 1
       WHERE id = ?`,
    ).run(membership.room_id);
    return true;
  })();
}

export function clearOfflineAndTemporaryBroadcasterData(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
): AdminCleanupResult {
  const rows = db.prepare(
    `SELECT device_id
     FROM memberships
     WHERE room_id = 'all-broadcasters'
     ORDER BY device_id`,
  ).all() as Array<{ device_id: string }>;
  const deletedDeviceIds = rows
    .map((row) => row.device_id)
    .filter((deviceId) =>
      deviceId.startsWith('dnf-tmp-') || !activeDeviceIds.has(deviceId))
    .filter((deviceId) => deleteBroadcasterLobbyData(db, deviceId));
  return { deletedCount: deletedDeviceIds.length, deletedDeviceIds };
}

export function pruneExpiredAdminData(
  db: Database.Database,
  nowSec: number,
): {
  removedBroadcasters: number;
  removedSyncRecords: number;
  removedSnapshotAudit: number;
} {
  const historyBefore = (db.prepare(
    'SELECT COUNT(*) AS count FROM sync_history',
  ).get() as { count: number }).count;
  const auditBefore = (db.prepare(
    'SELECT COUNT(*) AS count FROM snapshot_audit',
  ).get() as { count: number }).count;
  const removedBroadcasters = pruneExpiredBroadcasters(db, nowSec);
  pruneSyncRelationData(db, nowSec);
  db.prepare(
    `DELETE FROM snapshot_audit
     WHERE device_id NOT IN (SELECT device_id FROM memberships)`,
  ).run();
  const historyAfter = (db.prepare(
    'SELECT COUNT(*) AS count FROM sync_history',
  ).get() as { count: number }).count;
  const auditAfter = (db.prepare(
    'SELECT COUNT(*) AS count FROM snapshot_audit',
  ).get() as { count: number }).count;
  return {
    removedBroadcasters,
    removedSyncRecords: historyBefore - historyAfter,
    removedSnapshotAudit: auditBefore - auditAfter,
  };
}
