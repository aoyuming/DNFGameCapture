import { createHash } from 'node:crypto';
import type Database from 'better-sqlite3';

import { matchSnapshotSchema, type MatchSnapshot } from './schemas.js';

const MAX_SNAPSHOT_BYTES = 65_536;

export type SaveSnapshotFailureCode =
  | 'invalid_snapshot'
  | 'snapshot_too_large'
  | 'no_membership'
  | 'room_mismatch'
  | 'stale_revision'
  | 'duplicate_snapshot';

export type SaveSnapshotResult =
  | { ok: true; acceptedRevision: number; contentHash: string }
  | { ok: false; code: SaveSnapshotFailureCode };

export interface SaveSnapshotInput {
  deviceId: string;
  roomId: string;
  snapshot: unknown;
  receivedAt: number;
}

export interface StoredSnapshot {
  deviceId: string;
  roomId: string;
  snapshot: MatchSnapshot;
  receivedAt: number;
}

export interface RoomSnapshotRow extends StoredSnapshot {
  broadcasterName: string;
  deviceSuffix: string;
  online?: boolean;
}

interface MembershipRow {
  room_id: string;
}

interface CurrentSnapshotRow {
  client_revision: number;
  content_hash: string;
}

interface StoredSnapshotRow {
  device_id: string;
  room_id: string;
  payload_json: string;
  received_at: number;
}

interface RoomSnapshotDatabaseRow extends StoredSnapshotRow {
  broadcaster_name: string;
}

function sortJsonValue(value: unknown): unknown {
  if (Array.isArray(value)) {
    return value.map(sortJsonValue);
  }
  if (value !== null && typeof value === 'object') {
    return Object.fromEntries(
      Object.entries(value)
        .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
        .map(([key, child]) => [key, sortJsonValue(child)]),
    );
  }
  return value;
}

function canonicalJson(value: unknown): string {
  return JSON.stringify(sortJsonValue(value));
}

function contentHash(snapshot: MatchSnapshot): string {
  const { clientRevision: _clientRevision, clientTime: _clientTime, ...content } = snapshot;
  return createHash('sha256').update(canonicalJson(content), 'utf8').digest('hex');
}

function safeClientRevision(snapshot: unknown): number | null {
  if (snapshot === null || typeof snapshot !== 'object' || Array.isArray(snapshot)) {
    return null;
  }
  const revision = (snapshot as { clientRevision?: unknown }).clientRevision;
  return typeof revision === 'number' && Number.isSafeInteger(revision) ? revision : null;
}

function serializeInput(snapshot: unknown): string | null {
  try {
    const serialized = JSON.stringify(snapshot);
    return typeof serialized === 'string' ? serialized : null;
  } catch {
    return null;
  }
}

function parseStoredSnapshot(row: StoredSnapshotRow): StoredSnapshot | null {
  try {
    const parsed = matchSnapshotSchema.safeParse(JSON.parse(row.payload_json));
    if (!parsed.success) {
      return null;
    }
    return {
      deviceId: row.device_id,
      roomId: row.room_id,
      snapshot: parsed.data,
      receivedAt: row.received_at,
    };
  } catch {
    return null;
  }
}

export function saveSnapshot(
  db: Database.Database,
  input: SaveSnapshotInput,
): SaveSnapshotResult {
  const serializedInput = serializeInput(input.snapshot);
  const revisionForAudit = safeClientRevision(input.snapshot);
  let rejectedBeforePersistence: SaveSnapshotFailureCode | null = null;
  let snapshot: MatchSnapshot | null = null;

  if (serializedInput === null) {
    rejectedBeforePersistence = 'invalid_snapshot';
  } else if (Buffer.byteLength(serializedInput, 'utf8') > MAX_SNAPSHOT_BYTES) {
    rejectedBeforePersistence = 'snapshot_too_large';
  } else {
    const parsed = matchSnapshotSchema.safeParse(input.snapshot);
    if (parsed.success) {
      snapshot = parsed.data;
    } else {
      rejectedBeforePersistence = 'invalid_snapshot';
    }
  }

  const save = db.transaction((): SaveSnapshotResult => {
    const audit = (
      accepted: boolean,
      reason: string,
      clientRevision: number | null,
    ): void => {
      if (clientRevision === null) {
        return;
      }
      db.prepare(
        `INSERT INTO snapshot_audit (
           device_id, room_id, client_revision, accepted, reason, received_at
         ) VALUES (?, ?, ?, ?, ?, ?)`,
      ).run(
        input.deviceId,
        input.roomId,
        clientRevision,
        accepted ? 1 : 0,
        reason,
        input.receivedAt,
      );
    };

    if (rejectedBeforePersistence !== null || snapshot === null) {
      const code = rejectedBeforePersistence ?? 'invalid_snapshot';
      audit(false, code, revisionForAudit);
      return { ok: false, code };
    }

    const membership = db
      .prepare('SELECT room_id FROM memberships WHERE device_id = ?')
      .get(input.deviceId) as MembershipRow | undefined;
    if (!membership) {
      audit(false, 'no_membership', snapshot.clientRevision);
      return { ok: false, code: 'no_membership' };
    }
    if (membership.room_id !== input.roomId) {
      audit(false, 'room_mismatch', snapshot.clientRevision);
      return { ok: false, code: 'room_mismatch' };
    }

    const current = db
      .prepare(
        `SELECT client_revision, content_hash
         FROM snapshots WHERE device_id = ?`,
      )
      .get(input.deviceId) as CurrentSnapshotRow | undefined;
    if (current && snapshot.clientRevision <= current.client_revision) {
      audit(false, 'stale_revision', snapshot.clientRevision);
      return { ok: false, code: 'stale_revision' };
    }

    const hash = contentHash(snapshot);
    if (current?.content_hash === hash) {
      audit(false, 'duplicate_snapshot', snapshot.clientRevision);
      return { ok: false, code: 'duplicate_snapshot' };
    }

    db.prepare(
      `INSERT INTO snapshots (
         device_id, room_id, client_revision, content_hash, change_source,
         synced_from_device_id, synced_from_revision, payload_json, received_at
       ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET
         room_id = excluded.room_id,
         client_revision = excluded.client_revision,
         content_hash = excluded.content_hash,
         change_source = excluded.change_source,
         synced_from_device_id = excluded.synced_from_device_id,
         synced_from_revision = excluded.synced_from_revision,
         payload_json = excluded.payload_json,
         received_at = excluded.received_at`,
    ).run(
      input.deviceId,
      input.roomId,
      snapshot.clientRevision,
      hash,
      snapshot.changeSource,
      snapshot.syncedFrom?.deviceId ?? null,
      snapshot.syncedFrom?.revision ?? null,
      canonicalJson(snapshot),
      input.receivedAt,
    );
    audit(true, 'accepted', snapshot.clientRevision);
    return {
      ok: true,
      acceptedRevision: snapshot.clientRevision,
      contentHash: hash,
    };
  });

  return save();
}

export function getSnapshot(
  db: Database.Database,
  deviceId: string,
): StoredSnapshot | null {
  const row = db
    .prepare(
      `SELECT device_id, room_id, payload_json, received_at
       FROM snapshots WHERE device_id = ?`,
    )
    .get(deviceId) as StoredSnapshotRow | undefined;
  return row ? parseStoredSnapshot(row) : null;
}

export function getRoomSnapshots(
  db: Database.Database,
  roomId: string,
): RoomSnapshotRow[] {
  const rows = db
    .prepare(
      `SELECT s.device_id, s.room_id, m.broadcaster_name,
              s.payload_json, s.received_at
       FROM snapshots AS s
       JOIN memberships AS m
         ON m.device_id = s.device_id AND m.room_id = s.room_id
       WHERE m.room_id = ?
       ORDER BY s.device_id`,
    )
    .all(roomId) as RoomSnapshotDatabaseRow[];

  return rows.flatMap((row) => {
    const stored = parseStoredSnapshot(row);
    return stored
      ? [
          {
            ...stored,
            broadcasterName: row.broadcaster_name,
            deviceSuffix: row.device_id.slice(-4),
          },
        ]
      : [];
  });
}
