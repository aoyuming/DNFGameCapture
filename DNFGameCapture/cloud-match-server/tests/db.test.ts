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
        { name: 'devices' },
        { name: 'memberships' },
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
});
