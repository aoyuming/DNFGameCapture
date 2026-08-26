import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, test } from 'vitest';

import { openDatabase } from '../src/db.js';

describe('openDatabase', () => {
  test('initializes the room database and required SQLite settings', () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-match-db-'));
    const db = openDatabase(join(directory, 'rooms.sqlite'));

    try {
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
      expect(db.prepare('select id, display_name from rooms order by id').all()).toEqual([
        { id: '59', display_name: '59房' },
        { id: 'li-yong', display_name: '李永房' },
        { id: 'wen-rou', display_name: '温柔房' },
      ]);
      expect(db.pragma('journal_mode', { simple: true })).toBe('wal');
      expect(db.pragma('foreign_keys', { simple: true })).toBe(1);
      expect(db.pragma('busy_timeout', { simple: true })).toBe(5000);
    } finally {
      db.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });
});
