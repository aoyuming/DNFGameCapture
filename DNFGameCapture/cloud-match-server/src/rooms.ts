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

export interface RoomMemberDto {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
}

export interface BoundedRoomMembers {
  members: RoomMemberDto[];
  totalMembers: number;
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
  broadcaster_name: string;
}

interface RoomRevisionRow {
  revision: number;
}

interface RoomMemberRow {
  device_id: string;
  broadcaster_name: string;
  activity_at?: number;
}

interface RoomMemberCountRow {
  count: number;
}

function toRoomMemberDto(row: RoomMemberRow): RoomMemberDto {
  return {
    deviceId: row.device_id,
    broadcasterName: row.broadcaster_name,
    deviceSuffix: row.device_id.slice(-4),
  };
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

export function listRoomMembers(
  db: Database.Database,
  roomId: string,
): RoomMemberDto[] {
  const rows = db
    .prepare(
      `SELECT device_id, broadcaster_name
       FROM memberships
       WHERE room_id = ?
       ORDER BY device_id`,
    )
    .all(roomId) as RoomMemberRow[];
  return rows.map(toRoomMemberDto);
}

export function listRoomMembersBounded(
  db: Database.Database,
  roomId: string,
  requiredDeviceId: string,
  limit: number,
): BoundedRoomMembers {
  if (!Number.isSafeInteger(limit) || limit < 1 || limit > 512) {
    throw new RangeError('Invalid room member limit');
  }

  const countRow = db
    .prepare('SELECT COUNT(*) AS count FROM memberships WHERE room_id = ?')
    .get(roomId) as RoomMemberCountRow;
  const rows = db
    .prepare(
      `SELECT device_id, broadcaster_name, activity_at
       FROM (
         SELECT m.device_id, m.broadcaster_name,
                MAX(m.updated_at, d.last_seen_at, COALESCE(s.received_at, 0)) AS activity_at
         FROM memberships AS m
         JOIN devices AS d ON d.id = m.device_id
         LEFT JOIN snapshots AS s ON s.device_id = m.device_id
         WHERE m.room_id = ?
         ORDER BY activity_at DESC, m.device_id
         LIMIT ?
       )
       ORDER BY device_id`,
    )
    .all(roomId, limit) as RoomMemberRow[];

  if (
    countRow.count > rows.length &&
    !rows.some((row) => row.device_id === requiredDeviceId)
  ) {
    const caller = db
      .prepare(
        `SELECT device_id, broadcaster_name
         FROM memberships
         WHERE room_id = ? AND device_id = ?`,
      )
      .get(roomId, requiredDeviceId) as RoomMemberRow | undefined;
    if (caller) {
      let replacementIndex = 0;
      for (let index = 1; index < rows.length; index += 1) {
        const candidateActivity = rows[index].activity_at ?? 0;
        const replacementActivity = rows[replacementIndex].activity_at ?? 0;
        if (
          candidateActivity < replacementActivity ||
          (candidateActivity === replacementActivity &&
            rows[index].device_id > rows[replacementIndex].device_id)
        ) {
          replacementIndex = index;
        }
      }
      rows[replacementIndex] = caller;
      rows.sort((left, right) =>
        left.device_id < right.device_id ? -1 : left.device_id > right.device_id ? 1 : 0,
      );
    }
  }

  return {
    members: rows.map(toRoomMemberDto),
    totalMembers: countRow.count,
  };
}

export function getRoomRevision(db: Database.Database, roomId: string): number {
  const row = db
    .prepare('SELECT revision FROM rooms WHERE id = ?')
    .get(roomId) as RoomRevisionRow | undefined;
  if (!row) {
    throw new Error('Room not found');
  }
  return row.revision;
}

export function incrementRoomRevision(
  db: Database.Database,
  roomId: string,
): number {
  const result = db
    .prepare('UPDATE rooms SET revision = revision + 1 WHERE id = ?')
    .run(roomId);
  if (result.changes !== 1) {
    throw new Error('Room not found');
  }
  return getRoomRevision(db, roomId);
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
      .prepare(
        'SELECT room_id, broadcaster_name FROM memberships WHERE device_id = ?',
      )
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

    if (!existing) {
      incrementRoomRevision(db, roomId);
    } else if (existing.room_id !== roomId) {
      incrementRoomRevision(db, existing.room_id);
      incrementRoomRevision(db, roomId);
    } else if (existing.broadcaster_name !== broadcasterName) {
      incrementRoomRevision(db, roomId);
    }

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
    const existing = db
      .prepare(
        'SELECT room_id, broadcaster_name FROM memberships WHERE device_id = ?',
      )
      .get(deviceId) as ExistingMembershipRow | undefined;
    if (!existing) {
      return null;
    }
    if (existing.broadcaster_name === broadcasterName) {
      return getMembership(db, deviceId);
    }
    const result = db
      .prepare(
        `UPDATE memberships
         SET broadcaster_name = ?, updated_at = ?
         WHERE device_id = ?`,
      )
      .run(broadcasterName, nowSec, deviceId);
    if (result.changes !== 1) {
      return null;
    }
    incrementRoomRevision(db, existing.room_id);
    return getMembership(db, deviceId);
  });

  return rename();
}

export function leaveRoom(db: Database.Database, deviceId: string): void {
  const leave = db.transaction(() => {
    const existing = db
      .prepare(
        'SELECT room_id, broadcaster_name FROM memberships WHERE device_id = ?',
      )
      .get(deviceId) as ExistingMembershipRow | undefined;
    if (!existing) {
      return;
    }
    db.prepare('DELETE FROM snapshots WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM memberships WHERE device_id = ?').run(deviceId);
    incrementRoomRevision(db, existing.room_id);
  });
  leave();
}
