import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, test } from 'vitest';

import { openDatabase } from '../src/db.js';

const EXPECTED_ROOMS = [
  { id: '59', display_name: '59房' },
  { id: 'li-yong', display_name: '李永房' },
  { id: 'wen-rou', display_name: '温柔房' },
];

describe('openDatabase', () => {
  test('initializes the room database and required SQLite settings', () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-match-db-'));
    let db: ReturnType<typeof openDatabase> | undefined;

    try {
      db = openDatabase(join(directory, 'rooms.sqlite'));
      expect(
        db
          .prepare('select name from sqlite_master where type = ? and name not like ? order by name')
          .all('table', 'sqlite_%'),
      ).toEqual([
        { name: 'auth_sessions' },
        { name: 'broadcaster_policies' },
        { name: 'devices' },
        { name: 'licenses' },
        { name: 'memberships' },
        { name: 'player_entities' },
        { name: 'player_entity_identifiers' },
        { name: 'player_entity_names' },
        { name: 'player_library_meta' },
        { name: 'player_library_submissions' },
        { name: 'rooms' },
        { name: 'snapshot_audit' },
        { name: 'snapshots' },
      ]);
      expect(db.prepare('select id, display_name from rooms order by id').all()).toEqual(
        EXPECTED_ROOMS,
      );
      expect(db.pragma('journal_mode', { simple: true })).toBe('wal');
      expect(db.pragma('foreign_keys', { simple: true })).toBe(1);
      expect(db.pragma('busy_timeout', { simple: true })).toBe(5000);
    } finally {
      db?.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });

  test('reopens an initialized database without duplicating room seeds', () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-match-db-'));
    const databasePath = join(directory, 'rooms.sqlite');
    let db: ReturnType<typeof openDatabase> | undefined;

    try {
      db = openDatabase(databasePath);
      db.close();
      db = undefined;

      db = openDatabase(databasePath);
      expect(db.prepare('select id, display_name from rooms order by id').all()).toEqual(
        EXPECTED_ROOMS,
      );
      expect(
        db
          .prepare(
            'insert into devices (id, token_hash, created_at, last_seen_at) values (?, ?, ?, ?)',
          )
          .run('device-1', 'token-hash', 1, 1).changes,
      ).toBe(1);
    } finally {
      db?.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });

  test('enforces audit retention when reopening an existing database', () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-match-db-'));
    const databasePath = join(directory, 'rooms.sqlite');
    let db: ReturnType<typeof openDatabase> | undefined;

    try {
      db = openDatabase(databasePath);
      const insert = db.prepare(
        `insert into snapshot_audit (
           device_id, room_id, client_revision, accepted, reason, received_at
         ) values (?, '59', ?, 0, 'stale_revision', ?)`,
      );
      db.transaction(() => {
        for (let index = 1; index <= 50_128; index += 1) {
          insert.run(`restart-device-${index % 64}`, index, index);
        }
      })();
      db.close();
      db = undefined;

      db = openDatabase(databasePath);
      expect(
        db.prepare(
          `select count(*) as count, min(id) as oldest, max(id) as newest
           from snapshot_audit`,
        ).get(),
      ).toEqual({ count: 50_000, oldest: 129, newest: 50_128 });
      expect(
        db.prepare(
          `select max(device_count) as maximum
           from (
             select count(*) as device_count
             from snapshot_audit
             group by device_id
           )`,
        ).get(),
      ).toEqual({ maximum: 782 });
    } finally {
      db?.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });
});
