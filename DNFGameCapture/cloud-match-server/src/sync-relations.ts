import type Database from 'better-sqlite3';

export const SYNC_RETENTION_SECONDS = 24 * 60 * 60;
const MAX_HISTORY_ROWS = 200;

export type SyncType = 'once' | 'realtime';

export interface SyncHistoryInput {
  sourceDeviceId: string;
  sourceName: string;
  targetDeviceId: string;
  targetName: string;
  syncType: SyncType;
  snapshotRevision: number;
  merged: boolean;
  createdAt: number;
}

export interface SyncHistoryRecord extends SyncHistoryInput {
  id: number;
  expiresAt: number;
}

export interface RealtimeSyncInput {
  viewerDeviceId: string;
  viewerName: string;
  targetDeviceId: string;
  targetName: string;
  startedAt: number;
}

export interface RealtimeSyncRecord extends RealtimeSyncInput {
  lastHeartbeatAt: number;
}

export interface SyncHistoryView {
  incoming: SyncHistoryRecord[];
  outgoing: SyncHistoryRecord[];
}

export function initializeSyncRelationSchema(db: Database.Database): void {
  db.exec(`
    CREATE TABLE IF NOT EXISTS sync_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      source_device_id TEXT NOT NULL,
      source_name TEXT NOT NULL,
      target_device_id TEXT NOT NULL,
      target_name TEXT NOT NULL,
      sync_type TEXT NOT NULL CHECK (sync_type IN ('once', 'realtime')),
      snapshot_revision INTEGER NOT NULL,
      merged INTEGER NOT NULL CHECK (merged IN (0, 1)),
      created_at INTEGER NOT NULL,
      expires_at INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_sync_history_source_created
      ON sync_history (source_device_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_sync_history_target_created
      ON sync_history (target_device_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_sync_history_expires
      ON sync_history (expires_at);

    CREATE TABLE IF NOT EXISTS realtime_sync (
      viewer_device_id TEXT PRIMARY KEY,
      viewer_name TEXT NOT NULL,
      target_device_id TEXT NOT NULL,
      target_name TEXT NOT NULL,
      started_at INTEGER NOT NULL,
      last_heartbeat_at INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_realtime_sync_target
      ON realtime_sync (target_device_id);
  `);
}

export function pruneSyncRelationData(
  db: Database.Database,
  nowSec: number,
): void {
  db.prepare('DELETE FROM sync_history WHERE expires_at <= ?').run(nowSec);
}

function toHistoryRecord(row: {
  id: number;
  source_device_id: string;
  source_name: string;
  target_device_id: string;
  target_name: string;
  sync_type: SyncType;
  snapshot_revision: number;
  merged: number;
  created_at: number;
  expires_at: number;
}): SyncHistoryRecord {
  return {
    id: row.id,
    sourceDeviceId: row.source_device_id,
    sourceName: row.source_name,
    targetDeviceId: row.target_device_id,
    targetName: row.target_name,
    syncType: row.sync_type,
    snapshotRevision: row.snapshot_revision,
    merged: row.merged === 1,
    createdAt: row.created_at,
    expiresAt: row.expires_at,
  };
}

export function recordSuccessfulSync(
  db: Database.Database,
  input: SyncHistoryInput,
): SyncHistoryRecord {
  if (input.sourceDeviceId === input.targetDeviceId) {
    throw new Error('Cannot record a self sync');
  }
  if (!Number.isSafeInteger(input.snapshotRevision) || input.snapshotRevision < 1) {
    throw new RangeError('snapshotRevision must be a positive safe integer');
  }
  if (!Number.isSafeInteger(input.createdAt) || input.createdAt < 0) {
    throw new RangeError('createdAt must be a non-negative safe integer');
  }

  const expiresAt = input.createdAt + SYNC_RETENTION_SECONDS;
  const insert = db.prepare(
    `INSERT INTO sync_history (
       source_device_id, source_name, target_device_id, target_name,
       sync_type, snapshot_revision, merged, created_at, expires_at
     ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
  );
  const result = insert.run(
    input.sourceDeviceId,
    input.sourceName,
    input.targetDeviceId,
    input.targetName,
    input.syncType,
    input.snapshotRevision,
    input.merged ? 1 : 0,
    input.createdAt,
    expiresAt,
  );
  return {
    ...input,
    id: Number(result.lastInsertRowid),
    expiresAt,
  };
}

export function listSyncHistory(
  db: Database.Database,
  deviceId: string,
  nowSec: number,
): SyncHistoryView {
  pruneSyncRelationData(db, nowSec);
  const rows = db
    .prepare(
      `SELECT id, source_device_id, source_name, target_device_id, target_name,
              sync_type, snapshot_revision, merged, created_at, expires_at
       FROM sync_history
       WHERE source_device_id = ? OR target_device_id = ?
       ORDER BY created_at DESC, id DESC
       LIMIT ?`,
    )
    .all(deviceId, deviceId, MAX_HISTORY_ROWS) as Array<{
      id: number;
      source_device_id: string;
      source_name: string;
      target_device_id: string;
      target_name: string;
      sync_type: SyncType;
      snapshot_revision: number;
      merged: number;
      created_at: number;
      expires_at: number;
    }>;

  const history = rows.map(toHistoryRecord);
  return {
    incoming: history.filter((item) => item.targetDeviceId === deviceId),
    outgoing: history.filter((item) => item.sourceDeviceId === deviceId),
  };
}

export function listAllSyncHistory(
  db: Database.Database,
  nowSec: number,
): SyncHistoryRecord[] {
  pruneSyncRelationData(db, nowSec);
  const rows = db
    .prepare(
      `SELECT id, source_device_id, source_name, target_device_id, target_name,
              sync_type, snapshot_revision, merged, created_at, expires_at
       FROM sync_history
       ORDER BY created_at DESC, id DESC
       LIMIT ?`,
    )
    .all(MAX_HISTORY_ROWS) as Array<{
      id: number;
      source_device_id: string;
      source_name: string;
      target_device_id: string;
      target_name: string;
      sync_type: SyncType;
      snapshot_revision: number;
      merged: number;
      created_at: number;
      expires_at: number;
    }>;
  return rows.map(toHistoryRecord);
}

export function startRealtimeSync(
  db: Database.Database,
  input: RealtimeSyncInput,
): RealtimeSyncRecord {
  if (input.viewerDeviceId === input.targetDeviceId) {
    throw new Error('Cannot follow self');
  }
  const now = input.startedAt;
  const transaction = db.transaction(() => {
    db.prepare('DELETE FROM realtime_sync WHERE viewer_device_id = ?').run(
      input.viewerDeviceId,
    );
    db.prepare(
      `INSERT INTO realtime_sync (
         viewer_device_id, viewer_name, target_device_id, target_name,
         started_at, last_heartbeat_at
       ) VALUES (?, ?, ?, ?, ?, ?)`,
    ).run(
      input.viewerDeviceId,
      input.viewerName,
      input.targetDeviceId,
      input.targetName,
      input.startedAt,
      now,
    );
  });
  transaction();
  return { ...input, lastHeartbeatAt: now };
}

export function isReverseRealtimeSyncBlocked(
  db: Database.Database,
  viewerDeviceId: string,
  targetDeviceId: string,
  nowSec: number,
): boolean {
  pruneStaleRealtimeSync(db, nowSec);
  const row = db.prepare(
    `SELECT 1
     FROM realtime_sync
     WHERE viewer_device_id = ? AND target_device_id = ?
     LIMIT 1`,
  ).get(targetDeviceId, viewerDeviceId);
  return row !== undefined;
}

export function heartbeatRealtimeSync(
  db: Database.Database,
  viewerDeviceId: string,
  nowSec: number,
): boolean {
  return db
    .prepare(
      'UPDATE realtime_sync SET last_heartbeat_at = ? WHERE viewer_device_id = ?',
    )
    .run(nowSec, viewerDeviceId).changes === 1;
}

export function stopRealtimeSync(
  db: Database.Database,
  viewerDeviceId: string,
): boolean {
  return db
    .prepare('DELETE FROM realtime_sync WHERE viewer_device_id = ?')
    .run(viewerDeviceId).changes === 1;
}

export function stopRealtimeSyncForTarget(
  db: Database.Database,
  targetDeviceId: string,
): number {
  return db
    .prepare('DELETE FROM realtime_sync WHERE target_device_id = ?')
    .run(targetDeviceId).changes;
}

export function pruneStaleRealtimeSync(
  db: Database.Database,
  nowSec: number,
  timeoutSeconds = 5,
): number {
  if (!Number.isSafeInteger(timeoutSeconds) || timeoutSeconds < 1) {
    throw new RangeError('timeoutSeconds must be a positive safe integer');
  }
  return db.prepare(
    'DELETE FROM realtime_sync WHERE last_heartbeat_at <= ?',
  ).run(nowSec - timeoutSeconds).changes;
}

export function listRealtimeSyncForDevice(
  db: Database.Database,
  deviceId: string,
  nowSec: number,
): { incoming: RealtimeSyncRecord[]; outgoing: RealtimeSyncRecord[] } {
  pruneStaleRealtimeSync(db, nowSec);
  const rows = db
    .prepare(
      `SELECT viewer_device_id, viewer_name, target_device_id, target_name,
              started_at, last_heartbeat_at
       FROM realtime_sync
       WHERE viewer_device_id = ? OR target_device_id = ?
       ORDER BY started_at ASC, viewer_device_id ASC`,
    )
    .all(deviceId, deviceId) as Array<{
      viewer_device_id: string;
      viewer_name: string;
      target_device_id: string;
      target_name: string;
      started_at: number;
      last_heartbeat_at: number;
    }>;
  const records = rows.map((row) => ({
    viewerDeviceId: row.viewer_device_id,
    viewerName: row.viewer_name,
    targetDeviceId: row.target_device_id,
    targetName: row.target_name,
    startedAt: row.started_at,
    lastHeartbeatAt: row.last_heartbeat_at,
  }));
  return {
    incoming: records.filter((item) => item.targetDeviceId === deviceId),
    outgoing: records.filter((item) => item.viewerDeviceId === deviceId),
  };
}

export function listAllRealtimeSync(
  db: Database.Database,
  nowSec: number,
): RealtimeSyncRecord[] {
  pruneStaleRealtimeSync(db, nowSec);
  const rows = db
    .prepare(
      `SELECT viewer_device_id, viewer_name, target_device_id, target_name,
              started_at, last_heartbeat_at
       FROM realtime_sync
       ORDER BY started_at ASC, viewer_device_id ASC`,
    )
    .all() as Array<{
      viewer_device_id: string;
      viewer_name: string;
      target_device_id: string;
      target_name: string;
      started_at: number;
      last_heartbeat_at: number;
    }>;
  return rows.map((row) => ({
    viewerDeviceId: row.viewer_device_id,
    viewerName: row.viewer_name,
    targetDeviceId: row.target_device_id,
    targetName: row.target_name,
    startedAt: row.started_at,
    lastHeartbeatAt: row.last_heartbeat_at,
  }));
}
