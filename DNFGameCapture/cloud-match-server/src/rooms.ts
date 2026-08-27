import type Database from 'better-sqlite3';

export interface RoomDto {
  id: string;
  displayName: string;
}

export interface MembershipDto {
  room: RoomDto;
  broadcasterName: string;
  deviceSuffix: string;
}

interface RoomRow {
  id: string;
  display_name: string;
}

interface MembershipRow extends RoomRow {
  broadcaster_name: string;
  device_id: string;
}

interface ExistingMembershipRow {
  room_id: string;
}

function toRoomDto(row: RoomRow): RoomDto {
  return { id: row.id, displayName: row.display_name };
}

function toMembershipDto(row: MembershipRow): MembershipDto {
  return {
    room: toRoomDto(row),
    broadcasterName: row.broadcaster_name,
    deviceSuffix: row.device_id.slice(-4),
  };
}

export function listRooms(db: Database.Database): RoomDto[] {
  const rows = db
    .prepare('SELECT id, display_name FROM rooms ORDER BY rowid')
    .all() as RoomRow[];
  return rows.map(toRoomDto);
}

export function getMembership(
  db: Database.Database,
  deviceId: string,
): MembershipDto | null {
  const row = db
    .prepare(
      `SELECT m.device_id, m.broadcaster_name, r.id, r.display_name
       FROM memberships AS m
       JOIN rooms AS r ON r.id = m.room_id
       WHERE m.device_id = ?`,
    )
    .get(deviceId) as MembershipRow | undefined;
  return row ? toMembershipDto(row) : null;
}

export function joinRoom(
  db: Database.Database,
  deviceId: string,
  roomId: string,
  broadcasterName: string,
  nowSec: number,
): MembershipDto | null {
  const join = db.transaction(() => {
    const room = db
      .prepare('SELECT id, display_name FROM rooms WHERE id = ?')
      .get(roomId) as RoomRow | undefined;
    if (!room) {
      return null;
    }

    const existing = db
      .prepare('SELECT room_id FROM memberships WHERE device_id = ?')
      .get(deviceId) as ExistingMembershipRow | undefined;
    if (existing && existing.room_id !== roomId) {
      db.prepare('DELETE FROM snapshots WHERE device_id = ?').run(deviceId);
    }

    db.prepare(
      `INSERT INTO memberships (device_id, room_id, broadcaster_name, updated_at)
       VALUES (?, ?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET
         room_id = excluded.room_id,
         broadcaster_name = excluded.broadcaster_name,
         updated_at = excluded.updated_at`,
    ).run(deviceId, roomId, broadcasterName, nowSec);

    return getMembership(db, deviceId);
  });

  return join();
}

export function renameBroadcaster(
  db: Database.Database,
  deviceId: string,
  broadcasterName: string,
  nowSec: number,
): MembershipDto | null {
  const rename = db.transaction(() => {
    const result = db
      .prepare(
        `UPDATE memberships
         SET broadcaster_name = ?, updated_at = ?
         WHERE device_id = ?`,
      )
      .run(broadcasterName, nowSec, deviceId);
    return result.changes === 0 ? null : getMembership(db, deviceId);
  });

  return rename();
}

export function leaveRoom(db: Database.Database, deviceId: string): void {
  const leave = db.transaction(() => {
    db.prepare('DELETE FROM snapshots WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM memberships WHERE device_id = ?').run(deviceId);
  });
  leave();
}
