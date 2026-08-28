import type Database from 'better-sqlite3';
import { afterEach, describe, expect, test } from 'vitest';

import { openDatabase } from '../src/db.js';
import {
  matchSnapshotSchema,
  type MatchSnapshot,
  type Player,
} from '../src/schemas.js';
import {
  getRoomSnapshots,
  getSnapshot,
  pruneSnapshotAudit,
  saveSnapshot,
} from '../src/snapshots.js';

const databases: Database.Database[] = [];

function createDatabase(): Database.Database {
  const db = openDatabase(':memory:');
  databases.push(db);
  return db;
}

function addDevice(
  db: Database.Database,
  deviceId: string,
  roomId?: string,
  broadcasterName = 'Test Broadcaster',
): void {
  db.prepare(
    `INSERT INTO devices (id, token_hash, created_at, last_seen_at)
     VALUES (?, ?, ?, ?)`,
  ).run(deviceId, `secret-token-hash-${deviceId}`, 1, 1);
  if (roomId) {
    db.prepare(
      `INSERT INTO memberships (device_id, room_id, broadcaster_name, updated_at)
       VALUES (?, ?, ?, ?)`,
    ).run(deviceId, roomId, broadcasterName, 1);
  }
}

function player(mainName: string, seed = 0, aliases: string[] = []): Player {
  return {
    mainName,
    aliases,
    kills: seed,
    deaths: seed + 1,
    ak: seed + 2,
    streak: seed + 3,
  };
}

function snapshot(overrides: Partial<MatchSnapshot> = {}): MatchSnapshot {
  return {
    schemaVersion: 1,
    clientRevision: 1,
    clientTime: 123_456,
    changeSource: 'ocr',
    redScore: 2,
    blueScore: 1,
    redPlayers: [
      player('Red One', 0, ['R1']),
      player('Red Two', 4, ['R2']),
      player('Red Three', 8, ['R3']),
      player('Red Four', 12, ['R4']),
    ],
    bluePlayers: [
      player('Blue One', 16, ['B1']),
      player('Blue Two', 20, ['B2']),
      player('Blue Three', 24, ['B3']),
      player('Blue Four', 28, ['B4']),
    ],
    redPickFirst: true,
    teamsFlipped: false,
    outputSeatLabel: true,
    lastKillTeam: 'red',
    ...overrides,
  };
}

function replaceFirstPlayer(
  players: [Player, Player, Player, Player],
  first: Player,
): [Player, Player, Player, Player] {
  return [first, players[1], players[2], players[3]];
}

function snapshotCount(db: Database.Database): number {
  return (
    db.prepare('SELECT count(*) AS count FROM snapshots').get() as { count: number }
  ).count;
}

afterEach(() => {
  for (const db of databases.splice(0)) {
    db.close();
  }
});

describe('match snapshot schema and persistence', () => {
  test('accepts snapshots without local presentation settings', () => {
    const source = snapshot();
    delete source.teamsFlipped;
    delete source.outputSeatLabel;

    expect(matchSnapshotSchema.safeParse(source).success).toBe(true);
  });

  test('accepts up to ten recent recognition summaries and rejects oversized history', () => {
    const recentEvents = Array.from({ length: 10 }, (_, index) => ({
      time: `19:25:${String(index).padStart(2, '0')}`,
      killer: `杀手${index}`,
      dead: `死者${index}`,
      status: '已确认',
    }));

    expect(matchSnapshotSchema.safeParse(snapshot({ recentEvents })).success).toBe(true);
    expect(matchSnapshotSchema.safeParse(snapshot({
      recentEvents: [...recentEvents, recentEvents[0]],
    })).success).toBe(false);
  });

  test('bounds audit history per device and keeps the newest rejected records', () => {
    const db = createDatabase();
    addDevice(db, 'audit-device-0001', '59');
    saveSnapshot(db, {
      deviceId: 'audit-device-0001',
      roomId: '59',
      snapshot: snapshot(),
      receivedAt: 0,
    });

    for (let receivedAt = 1; receivedAt <= 1_005; receivedAt += 1) {
      saveSnapshot(db, {
        deviceId: 'audit-device-0001',
        roomId: '59',
        snapshot: snapshot(),
        receivedAt,
      });
    }

    expect(
      db.prepare(
        `SELECT count(*) AS count, min(received_at) AS oldest,
                max(received_at) AS newest
         FROM snapshot_audit WHERE device_id = ?`,
      ).get('audit-device-0001'),
    ).toEqual({ count: 1_000, oldest: 6, newest: 1_005 });
    expect(getSnapshot(db, 'audit-device-0001')?.snapshot.clientRevision).toBe(1);
  });

  test('prunes global audit history while retaining newest rows and per-device bounds', () => {
    const db = createDatabase();
    const insert = db.prepare(
      `INSERT INTO snapshot_audit (
         device_id, room_id, client_revision, accepted, reason, received_at
       ) VALUES (?, '59', ?, 0, 'stale_revision', ?)`,
    );
    db.transaction(() => {
      for (let index = 1; index <= 4; index += 1) {
        insert.run('audit-device-a', index, index);
      }
      for (let index = 5; index <= 8; index += 1) {
        insert.run('audit-device-b', index, index);
      }
    })();

    pruneSnapshotAudit(db, { perDeviceLimit: 3, globalLimit: 5 });

    expect(
      db.prepare('SELECT id FROM snapshot_audit ORDER BY id').all(),
    ).toEqual([{ id: 3 }, { id: 4 }, { id: 6 }, { id: 7 }, { id: 8 }]);
  });

  test('accepts, normalizes, hashes, and persists a legal snapshot', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-0001', '59', 'Broadcaster A');
    const legal = snapshot({
      clientTime: 9_999_999,
      redPlayers: [
        player('  e\u0301clair  ', 0, ['  side name  ', 'side name']),
        player('Red Two', 4, ['R2']),
        player('Red Three', 8, ['R3']),
        player('Red Four', 12, ['R4']),
      ],
    });

    const result = saveSnapshot(db, {
      deviceId: 'capture-device-0001',
      roomId: '59',
      snapshot: legal,
      receivedAt: 777,
    });

    expect(result).toEqual({
      ok: true,
      acceptedRevision: 1,
      contentHash: expect.stringMatching(/^[a-f0-9]{64}$/),
    });
    const stored = getSnapshot(db, 'capture-device-0001');
    expect(stored).toMatchObject({
      deviceId: 'capture-device-0001',
      roomId: '59',
      receivedAt: 777,
      snapshot: {
        clientRevision: 1,
        clientTime: 9_999_999,
      },
    });
    expect(stored?.snapshot.redPlayers[0]).toMatchObject({
      mainName: '\u00e9clair',
      aliases: ['side name'],
    });
    expect(
      db.prepare(
        `SELECT s.client_revision, s.received_at, a.accepted, a.reason
         FROM snapshots AS s
         JOIN snapshot_audit AS a USING (device_id, room_id)
         WHERE s.device_id = ?`,
      ).get('capture-device-0001'),
    ).toEqual({
      client_revision: 1,
      received_at: 777,
      accepted: 1,
      reason: 'accepted',
    });
  });

  test('accepts increasing revisions, rejects stale revisions, and deduplicates match content', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-0002', '59');
    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0002',
        roomId: '59',
        snapshot: snapshot(),
        receivedAt: 100,
      }).ok,
    ).toBe(true);

    const changed = snapshot({ clientRevision: 3, clientTime: 200, redScore: 3 });
    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0002',
        roomId: '59',
        snapshot: changed,
        receivedAt: 200,
      }),
    ).toMatchObject({ ok: true, acceptedRevision: 3 });

    for (const clientRevision of [2, 3]) {
      expect(
        saveSnapshot(db, {
          deviceId: 'capture-device-0002',
          roomId: '59',
          snapshot: snapshot({ clientRevision, redScore: 4 }),
          receivedAt: 300 + clientRevision,
        }),
      ).toEqual({ ok: false, code: 'stale_revision' });
    }

    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0002',
        roomId: '59',
        snapshot: { ...changed, clientRevision: 4, clientTime: 999_999 },
        receivedAt: 400,
      }),
    ).toEqual({ ok: false, code: 'duplicate_snapshot' });
    expect(getSnapshot(db, 'capture-device-0002')).toMatchObject({
      receivedAt: 200,
      snapshot: { clientRevision: 3, clientTime: 200, redScore: 3 },
    });
    expect(
      db.prepare(
        `SELECT accepted, reason FROM snapshot_audit
         WHERE device_id = ? ORDER BY id`,
      ).all('capture-device-0002'),
    ).toEqual([
      { accepted: 1, reason: 'accepted' },
      { accepted: 1, reason: 'accepted' },
      { accepted: 0, reason: 'stale_revision' },
      { accepted: 0, reason: 'stale_revision' },
      { accepted: 0, reason: 'duplicate_snapshot' },
    ]);
  });

  test('rejects oversized and malformed snapshots without mutating the latest snapshot', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-0003', '59');
    const original = snapshot();
    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0003',
        roomId: '59',
        snapshot: original,
        receivedAt: 100,
      }).ok,
    ).toBe(true);
    const originalPayload = (
      db.prepare('SELECT payload_json FROM snapshots WHERE device_id = ?').get(
        'capture-device-0003',
      ) as { payload_json: string }
    ).payload_json;

    const hugeAliases = Array.from(
      { length: 32 },
      (_, index) => `${index.toString().padStart(2, '0')}${'\ud83d\ude00'.repeat(62)}`,
    );
    const huge = snapshot({
      clientRevision: 2,
      redPlayers: [
        player('Huge Red One', 0, hugeAliases),
        player('Huge Red Two', 4, hugeAliases),
        player('Huge Red Three', 8, hugeAliases),
        player('Huge Red Four', 12, hugeAliases),
      ],
      bluePlayers: [
        player('Huge Blue One', 16, hugeAliases),
        player('Huge Blue Two', 20, hugeAliases),
        player('Huge Blue Three', 24, hugeAliases),
        player('Huge Blue Four', 28, hugeAliases),
      ],
    });
    expect(Buffer.byteLength(JSON.stringify(huge), 'utf8')).toBeGreaterThan(65_536);

    const invalidCases: Array<{ payload: unknown; code: string }> = [
      { payload: huge, code: 'snapshot_too_large' },
      {
        payload: { ...snapshot({ clientRevision: 2 }), redPlayers: original.redPlayers.slice(0, 3) },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          bluePlayers: [...original.bluePlayers, player('Blue Five', 32)],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          redPlayers: [
            { ...original.redPlayers[0], kills: -1 },
            ...original.redPlayers.slice(1),
          ],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          redPlayers: [
            { ...original.redPlayers[0], streak: 1_000 },
            ...original.redPlayers.slice(1),
          ],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          redPlayers: [
            { ...original.redPlayers[0], mainName: 'x'.repeat(65) },
            ...original.redPlayers.slice(1),
          ],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          redPlayers: [
            { ...original.redPlayers[0], mainName: 'Bad\u202eName' },
            ...original.redPlayers.slice(1),
          ],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: { ...snapshot({ clientRevision: 2 }), unexpected: true },
        code: 'invalid_snapshot',
      },
      {
        payload: {
          ...snapshot({ clientRevision: 2 }),
          redPlayers: [
            { ...original.redPlayers[0], unexpected: true },
            ...original.redPlayers.slice(1),
          ],
        },
        code: 'invalid_snapshot',
      },
      {
        payload: { ...snapshot({ clientRevision: 2 }), changeSource: 'network' },
        code: 'invalid_snapshot',
      },
    ];

    for (const invalid of invalidCases) {
      expect(
        saveSnapshot(db, {
          deviceId: 'capture-device-0003',
          roomId: '59',
          snapshot: invalid.payload,
          receivedAt: 200,
        }),
      ).toEqual({ ok: false, code: invalid.code });
      expect(snapshotCount(db)).toBe(1);
      expect(
        (
          db.prepare('SELECT payload_json FROM snapshots WHERE device_id = ?').get(
            'capture-device-0003',
          ) as { payload_json: string }
        ).payload_json,
      ).toBe(originalPayload);
    }
    expect(
      db.prepare(
        `SELECT reason, count(*) AS count FROM snapshot_audit
         WHERE accepted = 0 GROUP BY reason ORDER BY reason`,
      ).all(),
    ).toEqual([
      { reason: 'invalid_snapshot', count: 9 },
      { reason: 'snapshot_too_large', count: 1 },
    ]);
  });

  test('enforces revision, time, score, name, alias, and sync-source boundaries', () => {
    for (const payload of [
      snapshot({ clientRevision: 0 }),
      snapshot({ clientRevision: 1.5 }),
      snapshot({ clientRevision: Number.MAX_SAFE_INTEGER + 1 }),
      snapshot({ clientTime: -1 }),
      snapshot({ clientTime: 1.5 }),
      snapshot({ redScore: -1 }),
      snapshot({ blueScore: 1_000 }),
      snapshot({ changeSource: 'cloud_sync' }),
      snapshot({ syncedFrom: { deviceId: 'capture-device-source', revision: 1 } }),
      snapshot({
        changeSource: 'cloud_sync',
        syncedFrom: { deviceId: 'short', revision: 1 },
      }),
      snapshot({
        changeSource: 'cloud_sync',
        syncedFrom: { deviceId: 'capture-device-source', revision: 0 },
      }),
      snapshot({
        redPlayers: replaceFirstPlayer(
          snapshot().redPlayers,
          player('Same Name', 0, [' Same Name ']),
        ),
      }),
      snapshot({
        redPlayers: replaceFirstPlayer(
          snapshot().redPlayers,
          player(
            'Alias Owner',
            0,
            Array.from({ length: 33 }, (_, index) => `Alias ${index}`),
          ),
        ),
      }),
    ]) {
      expect(matchSnapshotSchema.safeParse(payload).success).toBe(false);
    }

    expect(
      matchSnapshotSchema.parse(
        snapshot({
          changeSource: 'cloud_sync',
          syncedFrom: { deviceId: 'capture-device-source', revision: 5 },
        }),
      ).syncedFrom,
    ).toEqual({ deviceId: 'capture-device-source', revision: 5 });
  });

  test('rejects missing and mismatched memberships and audits safe revisions', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-0004');
    addDevice(db, 'capture-device-0005', '59');

    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0004',
        roomId: '59',
        snapshot: snapshot(),
        receivedAt: 500,
      }),
    ).toEqual({ ok: false, code: 'no_membership' });
    expect(
      saveSnapshot(db, {
        deviceId: 'capture-device-0005',
        roomId: 'li-yong',
        snapshot: snapshot(),
        receivedAt: 501,
      }),
    ).toEqual({ ok: false, code: 'room_mismatch' });
    expect(snapshotCount(db)).toBe(0);
    expect(
      db.prepare(
        `SELECT device_id, room_id, client_revision, accepted, reason, received_at
         FROM snapshot_audit ORDER BY id`,
      ).all(),
    ).toEqual([
      {
        device_id: 'capture-device-0004',
        room_id: '59',
        client_revision: 1,
        accepted: 0,
        reason: 'no_membership',
        received_at: 500,
      },
      {
        device_id: 'capture-device-0005',
        room_id: 'li-yong',
        client_revision: 1,
        accepted: 0,
        reason: 'room_mismatch',
        received_at: 501,
      },
    ]);
  });

  test('rolls back snapshot persistence when accepted audit insertion fails', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-0006', '59');
    db.exec(`
      CREATE TRIGGER reject_accepted_audit
      BEFORE INSERT ON snapshot_audit
      WHEN NEW.accepted = 1
      BEGIN
        SELECT RAISE(ABORT, 'audit unavailable');
      END;
    `);

    expect(() =>
      saveSnapshot(db, {
        deviceId: 'capture-device-0006',
        roomId: '59',
        snapshot: snapshot(),
        receivedAt: 600,
      }),
    ).toThrow('audit unavailable');
    expect(snapshotCount(db)).toBe(0);
  });

  test('returns room snapshot DTOs without token or content hashes', () => {
    const db = createDatabase();
    addDevice(db, 'capture-device-1001', '59', 'Same Name');
    addDevice(db, 'capture-device-1002', '59', 'Same Name');
    addDevice(db, 'capture-device-2001', 'li-yong', 'Other Room');
    saveSnapshot(db, {
      deviceId: 'capture-device-1001',
      roomId: '59',
      snapshot: snapshot(),
      receivedAt: 701,
    });
    saveSnapshot(db, {
      deviceId: 'capture-device-1002',
      roomId: '59',
      snapshot: snapshot({ clientRevision: 2, redScore: 3 }),
      receivedAt: 702,
    });
    saveSnapshot(db, {
      deviceId: 'capture-device-2001',
      roomId: 'li-yong',
      snapshot: snapshot(),
      receivedAt: 703,
    });

    const rows = getRoomSnapshots(db, '59');

    expect(rows).toHaveLength(2);
    expect(rows).toEqual([
      expect.objectContaining({
        deviceId: 'capture-device-1001',
        broadcasterName: 'Same Name',
        deviceSuffix: '1001',
        receivedAt: 701,
        snapshot: expect.objectContaining({ clientRevision: 1 }),
      }),
      expect.objectContaining({
        deviceId: 'capture-device-1002',
        broadcasterName: 'Same Name',
        deviceSuffix: '1002',
        receivedAt: 702,
        snapshot: expect.objectContaining({ clientRevision: 2 }),
      }),
    ]);
    const serialized = JSON.stringify(rows);
    expect(serialized).not.toContain('token_hash');
    expect(serialized).not.toContain('secret-token-hash');
    expect(serialized).not.toContain('contentHash');
    expect(serialized).not.toContain('content_hash');
  });
});
