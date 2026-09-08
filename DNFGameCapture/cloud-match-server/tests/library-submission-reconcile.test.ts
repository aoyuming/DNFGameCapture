import { describe, expect, test, vi } from 'vitest';
import { reconcileLibrarySubmission } from '../src/library-submission-reconcile.js';
import { editAdminSubmission, readAdminSubmission, reviewAdminSubmissions, projectedLibrary } from '../src/library-admin-data.js';
import { createPlayerLibraryEntity, listPlayerLibrary } from '../src/v2-api.js';
import { openDatabase } from '../src/db.js';
import type { PlayerEntity } from '../src/player-library.js';
import request from 'supertest';
import { createCloudMatchAdminApp } from '../src/admin.js';

const row = (entityId: string, names = ['Player'], gameIds = ['a', 'b', 'c', 'd', 'e']): PlayerEntity =>
  ({ entityId, names, gameIds });

describe('submission identity reconciliation', () => {
  test('does not pre-collapse incompatible sources through one broad public target', () => {
    const a = row('a', ['A'], ['1', '2', '3', '4', '5']);
    const c = row('c', ['C'], ['6', '7', '8', '9', '10']);
    const b = row('b', ['B'], [...a.gameIds, ...c.gameIds]);
    expect(reconcileLibrarySubmission([b], [a, c]).entities).toEqual([a, c]);
    expect(reconcileLibrarySubmission([b], [c, a]).entities).toEqual([c, a]);
  });
  test('normalization work grows with incoming identifiers instead of accumulated alias unions', () => {
    const incoming = Array.from({ length: 300 }, (_, index) => row(`local-${index}`, [`Alias-${index}`]));
    const normalize = vi.spyOn(String.prototype, 'normalize');
    try {
      const result = reconcileLibrarySubmission([row('public')], incoming);
      expect(result.entities).toHaveLength(300);
      expect(result.reconciliations).toEqual([]);
      expect(normalize.mock.calls.length).toBeLessThan(30_000);
    } finally { normalize.mockRestore(); }
  });

  test('keeps source evidence intact until the full projection groups local alias rows', () => {
    const existing = [row('public', ['One', 'Two', 'Three'])];
    const incoming = existing[0].names.map((name, i) => row(`local-${i}`, [name]));
    const original = JSON.stringify([existing, incoming]);
    const result = reconcileLibrarySubmission(existing, incoming);
    expect(result.entities).toEqual(incoming);
    expect(projectedLibrary(existing, result.entities).entities).toEqual(existing);
    expect(result).toMatchObject({ rawEntityCount: 3, matchedEntityCount: 0, unchangedCount: 0,
      addedEntityCount: 3, updatedEntityCount: 0 });
    expect(result.reconciliations).toHaveLength(0);
    expect(JSON.stringify([existing, incoming])).toBe(original);
    expect(reconcileLibrarySubmission(existing, incoming)).toEqual(result);
  });

  test('uses five distinct overlapping game IDs, preserving only submitted additions', () => {
    const existing = [row('public', ['Old'], ['a', 'b', 'c', 'd', 'e', 'f'])];
    for (const incoming of [row('local', ['New'], ['a', 'b', 'c', 'd', 'e', 'new'])]) {
      const result = reconcileLibrarySubmission(existing, [incoming]);
      expect(result.entities[0]).toEqual(incoming);
      expect(projectedLibrary(existing, result.entities).entities).toHaveLength(1);
      expect(result.entityChanges[0].additions.names).toEqual(['New']);
    }
    for (const incoming of [row('local', ['Old'], ['a', 'a', 'A', 'b', 'c', 'd']), row('local', ['Old'], [])]) {
      expect(projectedLibrary(existing, [incoming]).entities).toHaveLength(2);
    }
  });

  test('identical sets below five games stay independent', () => {
    expect(projectedLibrary([row('public', ['A'], ['one'])], [row('local', ['B'], ['ONE'])]).entities).toHaveLength(2);
    expect(projectedLibrary([row('public', ['A'], [])], [row('local', ['B'], [])]).entities).toHaveLength(2);
    expect(projectedLibrary([row('public', ['A'], ['a', 'b', 'c', 'd'])],
      [row('local', ['B'], ['a', 'b', 'c', 'd'])]).entities).toHaveLength(2);
    expect(reconcileLibrarySubmission([row('public', ['A'], [])], [row('local', ['A'], [])]).matchedEntityCount).toBe(0);
    expect(reconcileLibrarySubmission([row('public')], [row('local', ['Player'], ['unrelated'])]).matchedEntityCount).toBe(0);
    expect(reconcileLibrarySubmission([row('public', ['A'], ['one'])], [row('local', ['B'], ['one'])]).matchedEntityCount).toBe(0);
  });

  test('a weak shared identifier does not block grouping, but unrelated name owners remain conflicts', () => {
    const existing = [row('first', ['First']), row('second', ['Second'], ['x'])];
    for (const incoming of [row('local', ['New'], ['a', 'b', 'c', 'd', 'e', 'x']), row('local', ['New'], undefined)]) {
      const result = projectedLibrary(existing, [incoming]);
      expect(result.entities).toHaveLength(2);
      expect(result.entities.find(row => row.entityId === 'first')?.names).toContain('New');
      expect(result.conflicts).toEqual([]);
    }
    expect(projectedLibrary(existing, [row('local', ['Second'])]).conflicts.length).toBeGreaterThan(0);
    const explicit = row('second', ['Second'], ['a', 'b', 'c']);
    expect(reconcileLibrarySubmission(existing, [explicit]).entities).toEqual([explicit]);
  });

  test('does not infer identity between unknown submitted entities or alter a known public ID', () => {
    const incoming = [row('a', ['A']), row('b', ['B'])];
    expect(reconcileLibrarySubmission([], incoming).entities).toEqual(incoming);
    expect(reconcileLibrarySubmission([row('a', ['A'])], [row('a', ['Renamed'], ['new'])]).reconciliations).toEqual([]);
  });

  test('normalizes evidence without counting repeated spelling or dropping distinct additions', () => {
    const result = reconcileLibrarySubmission([row('public', ['A'], ['one', 'two', 'three', 'four', 'five'])],
      [row('local', ['A', ' B ', 'b'], [' ONE ', 'two', 'THREE', 'four', 'five', 'new'])]);
    expect(result.entities[0].names).toEqual(['A', 'B']);
    expect(result.entities[0].gameIds).toEqual(['ONE', 'two', 'THREE', 'four', 'five', 'new']);
  });

  test('re-reads old pending records without writing them, then publishes once transactionally', () => {
    const db = openDatabase(':memory:');
    try {
      createPlayerLibraryEntity(db, row('public', ['One', 'Two']), 100, 0);
      const payload = JSON.stringify({ entities: [row('local-a', ['One']), row('local-b', ['Two', 'New'], ['a', 'b', 'c', 'd', 'e', 'f'])] });
      const id = Number(db.prepare("INSERT INTO player_library_submissions (device_id,payload_json,status,created_at) VALUES ('device',?,'pending',100)").run(payload).lastInsertRowid);
      const before = readAdminSubmission(db, id);
      expect(before.conflicts).toEqual([]);
      expect(before.automaticGroups).toHaveLength(1);
      expect(db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(id)).toEqual({ payload_json: payload });
      const ref = { id, submissionRevision: before.submissionRevision };
      expect(reviewAdminSubmissions(db, 1, 101, 'approve', [ref])).toMatchObject({ revision: 2, reviewed: 1,
        acceptedEntityCount: 2, skippedEntityCount: 0, pendingSubmissionCount: 0 });
      expect(listPlayerLibrary(db).entities).toEqual([row('public', ['New', 'One', 'Two'], ['a', 'b', 'c', 'd', 'e', 'f'])]);
      expect(() => reviewAdminSubmissions(db, 2, 102, 'approve', [ref])).toThrow('submission_not_pending');
      const second = Number(db.prepare("INSERT INTO player_library_submissions (device_id,payload_json,status,created_at) VALUES ('device',?,'pending',102)").run(payload).lastInsertRowid);
      const unchanged = readAdminSubmission(db, second);
      expect(unchanged.unchangedCount).toBe(1);
      expect(reviewAdminSubmissions(db, 2, 103, 'approve', [{ id: second, submissionRevision: unchanged.submissionRevision }]).revision).toBe(2);
    } finally { db.close(); }
  });

  test('reconciled review still honors raw submission hashes, public revision and real conflicts', () => {
    const db = openDatabase(':memory:');
    try {
      createPlayerLibraryEntity(db, row('public'), 100, 0);
      const id = Number(db.prepare("INSERT INTO player_library_submissions (device_id,payload_json,status,created_at) VALUES ('device',?,'pending',100)")
        .run(JSON.stringify({ entities: [row('local')] })).lastInsertRowid);
      const before = readAdminSubmission(db, id);
      editAdminSubmission(db, 1, id, before.submissionRevision, [row('local', ['Player'], ['other'])], true);
      expect(() => reviewAdminSubmissions(db, 1, 101, 'approve', [{ id, submissionRevision: before.submissionRevision }])).toThrow('stale_submission');
      const changed = readAdminSubmission(db, id);
      expect(changed.conflicts.length).toBeGreaterThan(0);
      expect(() => reviewAdminSubmissions(db, 1, 101, 'approve', [{ id, submissionRevision: changed.submissionRevision }])).toThrow('ownership_conflict');
      expect(() => reviewAdminSubmissions(db, 0, 101, 'approve', [{ id, submissionRevision: changed.submissionRevision }])).toThrow('stale_revision');
      expect(listPlayerLibrary(db).revision).toBe(1);
    } finally { db.close(); }
  });

  test('editing redirected associations requires confirmation and preserves the original raw submission', () => {
    const db = openDatabase(':memory:');
    try {
      createPlayerLibraryEntity(db, row('public', ['One', 'Two']), 100, 0);
      db.prepare('INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES(?,?)').run('local-1', 'public');
      db.prepare('INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES(?,?)').run('local-2', 'public');
      const payload = JSON.stringify({ entities: [row('local-1', ['One']), row('local-2', ['Two'])] });
      const id = Number(db.prepare("INSERT INTO player_library_submissions (device_id,payload_json,status,created_at) VALUES ('device',?,'pending',100)").run(payload).lastInsertRowid);
      const read = readAdminSubmission(db, id);
      expect(() => editAdminSubmission(db, 1, id, read.submissionRevision, read.entities)).toThrow('reconciliation_confirmation_required');
      expect(readAdminSubmission(db, id).submissionRevision).toBe(read.submissionRevision);
      editAdminSubmission(db, 1, id, read.submissionRevision, [row('public', ['One', 'Two', 'Added'])], true);
      expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(id)).toEqual({ payload_json: payload });
      const edited = readAdminSubmission(db, id);
      expect(edited.reconciliations).toEqual([]);
      editAdminSubmission(db, 1, id, edited.submissionRevision, [row('public', ['One', 'Two', 'Added', 'More'])]);
      expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(id)).toEqual({ payload_json: payload });
      expect(listPlayerLibrary(db).revision).toBe(1);
    } finally { db.close(); }
  });

  test('the authenticated edit API enforces association confirmation and atomically saves the original', async () => {
    const db = openDatabase(':memory:');
    try {
      const app = createCloudMatchAdminApp({ db, now: () => 100, csrfToken: 'csrf', adminPassword: 'password',
        socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false,
          stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
      createPlayerLibraryEntity(db, row('public'), 100, 0);
      db.prepare('INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES(?,?)').run('local', 'public');
      const payload = JSON.stringify({ entities: [row('local')] });
      const id = Number(db.prepare("INSERT INTO player_library_submissions (device_id,payload_json,status,created_at) VALUES ('device',?,'pending',100)").run(payload).lastInsertRowid);
      const read = readAdminSubmission(db, id);
      const body = { revision: 1, submissionRevision: read.submissionRevision, entities: read.entities };
      const edit = (extra: object) => request(app).put('/admin/api/library/submissions/' + id)
        .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send({ ...body, ...extra });
      expect((await edit({}).expect(409)).body.code).toBe('reconciliation_confirmation_required');
      await edit({ confirmReconciliations: false }).expect(409);
      db.exec("CREATE TRIGGER fail_draft BEFORE UPDATE OF payload_json ON player_library_submissions BEGIN SELECT RAISE(ABORT,'fixture failure'); END");
      await edit({ confirmReconciliations: true }).expect(500);
      expect(db.prepare('SELECT * FROM player_library_submission_originals').all()).toEqual([]);
      db.exec('DROP TRIGGER fail_draft');
      await edit({ confirmReconciliations: true }).expect(200);
      expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(id)).toEqual({ payload_json: payload });
      expect(listPlayerLibrary(db).revision).toBe(1);
      db.prepare('DELETE FROM player_library_submissions WHERE id=?').run(id);
      expect(db.prepare('SELECT * FROM player_library_submission_originals').all()).toEqual([]);
    } finally { db.close(); }
  });
});
