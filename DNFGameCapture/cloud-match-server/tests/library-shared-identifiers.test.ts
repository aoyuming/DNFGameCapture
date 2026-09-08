import Database from 'better-sqlite3';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, describe, expect, test } from 'vitest';
import { openDatabase } from '../src/db.js';
import { mergePublicLibrary, mutatePublicLibrary, parseAdminEntities, projectedLibrary, readAdminSubmission, reviewAdminSubmissions } from '../src/library-admin-data.js';
import { listPlayerLibrary, MAX_AUTOMATIC_EVIDENCE_BYTES, MAX_ENTITY_REDIRECTS, readAutomaticIdentityEvidence } from '../src/library-store.js';

const databases: Database.Database[] = [];
const directories: string[] = [];
const entity = (entityId: string, names = [entityId], gameIds: string[] = []) =>
  ({ entityId, names, gameIds });
const open = (path = ':memory:') => {
  const db = openDatabase(path); databases.push(db); return db;
};
const dictionary = (db: Database.Database) => db.prepare(
  'SELECT kind,identifier_norm FROM player_identifiers ORDER BY kind,identifier_norm',
).all();

function legacyFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-shared-identifiers-'));
  directories.push(directory);
  const path = join(directory, 'library.sqlite');
  const db = new Database(path); databases.push(db);
  db.exec(`
    CREATE TABLE player_entities (entity_id TEXT PRIMARY KEY, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);
    CREATE TABLE player_entity_names (
      entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
      name_norm TEXT NOT NULL UNIQUE, display_name TEXT NOT NULL, PRIMARY KEY(entity_id,name_norm));
    CREATE TABLE player_entity_identifiers (
      entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
      kind TEXT NOT NULL CHECK(kind IN ('game','adventure')), identifier_norm TEXT NOT NULL,
      display_value TEXT NOT NULL, PRIMARY KEY(kind,identifier_norm));
    CREATE INDEX idx_player_identifiers_entity ON player_entity_identifiers(entity_id,kind);
    CREATE TABLE player_library_meta (id INTEGER PRIMARY KEY CHECK(id=1), revision INTEGER NOT NULL);
    INSERT INTO player_library_meta VALUES(1,42);
    INSERT INTO player_entities VALUES('alpha',10,20),('beta',11,21);
    INSERT INTO player_entity_names VALUES('alpha','alpha','Alpha'),('beta','beta','Beta');
  `);
  const insert = db.prepare('INSERT INTO player_entity_identifiers VALUES(?,?,?,?)');
  insert.run('alpha', 'game', 'caf\u00e9', 'Caf\u00e9');
  insert.run('alpha', 'adventure', 'caf\u00e9', 'CAF\u00c9');
  insert.run('beta', 'game', 'other', 'Other');
  return { db, path };
}

afterEach(() => {
  for (const db of databases.splice(0)) if (db.open) db.close();
  for (const directory of directories.splice(0)) rmSync(directory, { recursive: true, force: true });
});

describe('shared identifier storage', () => {
  test('projection only reports normalized name collisions, never shared identifiers', () => {
    const first = entity('a', ['Caf\u00e9'], ['Game']);
    const second = entity('b', ['Other'], ['game']);
    expect(projectedLibrary([first], [second]).conflicts).toEqual([]);
    expect(projectedLibrary([first], [{ ...second, names: [' CAFE\u0301 '] }]).conflicts)
      .toEqual([{ kind: 'names', value: 'Caf\u00e9', entityIds: ['a', 'b'] }]);
  });

  test('deduplicates dictionary keys while preserving per-player spelling and independent relations', () => {
    const db = open();
    const [a, b] = parseAdminEntities({ entities: [
      entity('a', ['A'], [' Caf\u00e9 ', 'CAFE\u0301']),
      entity('b', ['B'], ['CAFE\u0301']),
    ] });
    mutatePublicLibrary(db, 0, 100, 'create', [a]);
    mutatePublicLibrary(db, 1, 101, 'create', [b]);
    expect(listPlayerLibrary(db)).toEqual({ revision: 2, entities: [a, b], entityRedirects: [] });
    expect(dictionary(db)).toEqual([
      { kind: 'game', identifier_norm: 'caf\u00e9' },
    ]);
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(2);
    expect((db.pragma('table_info(player_entity_identifiers)') as { name: string }[]).map(column => column.name))
      .toEqual(['entity_id', 'identifier_id', 'spelling_id']);
    expect(db.prepare('SELECT display_value FROM player_identifier_spellings ORDER BY identifier_id,display_value').all()).toHaveLength(2);
    expect(mutatePublicLibrary(db, 2, 102, 'import', [a, b]).revision).toBe(2);
    mutatePublicLibrary(db, 2, 103, 'replace', [entity('a', ['A'], [])], 'a');
    expect(listPlayerLibrary(db).entities[1]).toEqual(b);
    mutatePublicLibrary(db, 3, 104, 'delete', [], 'a');
    expect(listPlayerLibrary(db)).toEqual({ revision: 4, entities: [b], entityRedirects: [] });
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(1);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('enforces dictionary and link uniqueness and foreign keys with entity-only cascade deletion', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('a', ['A'], ['shared']), entity('b', ['B'], ['shared'])]);
    const { identifier_id: id } = db.prepare('SELECT identifier_id FROM player_identifiers').get() as { identifier_id: number };
    const { spelling_id: spellingId } = db.prepare('SELECT spelling_id FROM player_identifier_spellings').get() as { spelling_id: number };
    const insert = db.prepare('INSERT INTO player_entity_identifiers(entity_id,identifier_id,spelling_id) VALUES(?,?,?)');
    expect(() => insert.run('a', id, spellingId)).toThrow(/UNIQUE/);
    expect(() => insert.run('missing', id, spellingId)).toThrow(/FOREIGN KEY/);
    expect(() => insert.run('a', id + 1, spellingId)).toThrow(/FOREIGN KEY/);
    db.prepare("INSERT INTO player_entities VALUES('c',100,100)").run();
    expect(() => insert.run('c', id, spellingId + 1)).toThrow(/FOREIGN KEY/);
    db.prepare("DELETE FROM player_entities WHERE entity_id='c'").run();
    expect(() => db.prepare("INSERT INTO player_identifiers(kind,identifier_norm) VALUES('game','shared')").run()).toThrow(/UNIQUE/);
    expect(() => db.prepare("INSERT INTO player_identifiers(kind,identifier_norm) VALUES('invalid','shared')").run()).toThrow(/CHECK/);
    expect(() => db.prepare('DELETE FROM player_identifiers WHERE identifier_id=?').run(id)).toThrow(/FOREIGN KEY/);
    expect(() => db.prepare('DELETE FROM player_identifier_spellings WHERE spelling_id=?').run(spellingId)).toThrow(/FOREIGN KEY/);
    expect(() => db.prepare("INSERT INTO player_identifier_spellings(identifier_id,display_value) VALUES(?,'shared')").run(id)).toThrow(/UNIQUE/);
    expect(db.prepare('SELECT display_value FROM player_identifier_spellings').all()).toEqual([{ display_value: 'shared' }]);
    db.prepare("DELETE FROM player_entities WHERE entity_id='a'").run();
    expect(listPlayerLibrary(db).entities).toEqual([entity('b', ['B'], ['shared'])]);
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(1);
  });

  test('rolls back new dictionary entries, links and revision when a later link write fails', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('a', ['A'], ['shared'])]);
    const before = listPlayerLibrary(db);
    const keys = dictionary(db);
    const spellings = db.prepare('SELECT * FROM player_identifier_spellings').all();
    db.exec("CREATE TRIGGER fail_link BEFORE INSERT ON player_entity_identifiers WHEN NEW.entity_id='b' BEGIN SELECT RAISE(ABORT,'link failure'); END");
    expect(() => mutatePublicLibrary(db, 1, 101, 'create', [entity('b', ['B'], ['new', 'shared'])])).toThrow('link failure');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(dictionary(db)).toEqual(keys);
    expect(db.prepare('SELECT * FROM player_identifier_spellings').all()).toEqual(spellings);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('explicit merge preserves a third independent player sharing the same identifiers', () => {
    const db = open();
    const values = ['a', 'b', 'c'].map(id => entity(id, [id], ['shared']));
    mutatePublicLibrary(db, 0, 100, 'import', values);
    mergePublicLibrary(db, 1, 101, 'a', ['a', 'b']);
    expect(listPlayerLibrary(db)).toEqual({ revision: 2,
      entities: [entity('a', ['a', 'b'], ['shared']), values[2]],
      entityRedirects: [{ fromEntityId: 'b', toEntityId: 'a' }] });
    expect(dictionary(db)).toHaveLength(1);
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(2);
  });
});

describe('legacy identifier migration', () => {
  test('preserves games and timestamps, increments cleanup revision once and supports sharing', () => {
    const legacy = legacyFixture();
    const timestamps = legacy.db.prepare('SELECT * FROM player_entities ORDER BY entity_id').all();
    legacy.db.close();
    const db = open(legacy.path);
    expect(listPlayerLibrary(db)).toEqual({ revision: 43, entityRedirects: [], entities: [
      entity('alpha', ['Alpha'], ['Caf\u00e9']), entity('beta', ['Beta'], ['Other']),
    ] });
    expect(db.prepare('SELECT * FROM player_entities ORDER BY entity_id').all()).toEqual(timestamps);
    expect(dictionary(db)).toHaveLength(2);
    mutatePublicLibrary(db, 43, 100, 'import', [entity('beta', ['Beta'], ['CAF\u00c9'])]);
    const before = listPlayerLibrary(db);
    expect(before.entities).toHaveLength(2);
    expect(before.revision).toBe(44);
    const rows = db.prepare('SELECT * FROM player_entity_identifiers ORDER BY entity_id,identifier_id').all();
    db.close();
    const reopened = open(legacy.path);
    expect(listPlayerLibrary(reopened)).toEqual(before);
    expect(dictionary(reopened)).toHaveLength(2);
    expect(reopened.prepare('SELECT * FROM player_entity_identifiers ORDER BY entity_id,identifier_id').all()).toEqual(rows);
    expect(reopened.pragma('foreign_keys', { simple: true })).toBe(1);
    expect(reopened.pragma('foreign_key_check')).toEqual([]);
  });

  test('rolls back migration DDL and data if later initialization fails, then retries successfully', () => {
    const legacy = legacyFixture();
    legacy.db.exec(`CREATE TABLE rooms(id TEXT PRIMARY KEY,display_name TEXT NOT NULL UNIQUE);
      CREATE TRIGGER fail_seed BEFORE INSERT ON rooms BEGIN SELECT RAISE(ABORT,'seed failure'); END;`);
    const rows = legacy.db.prepare('SELECT * FROM player_entity_identifiers').all();
    legacy.db.close();
    expect(() => openDatabase(legacy.path)).toThrow('seed failure');
    const raw = new Database(legacy.path); databases.push(raw);
    expect(raw.prepare('SELECT * FROM player_entity_identifiers').all()).toEqual(rows);
    expect(raw.prepare("SELECT name FROM sqlite_master WHERE name='player_identifiers'").get()).toBeUndefined();
    expect(raw.prepare('SELECT revision FROM player_library_meta').get()).toEqual({ revision: 42 });
    expect(() => raw.prepare("INSERT INTO player_entity_identifiers VALUES('beta','game','caf\u00e9','duplicate')").run()).toThrow(/UNIQUE/);
    raw.exec('DROP TRIGGER fail_seed'); raw.close();
    expect(dictionary(open(legacy.path))).toHaveLength(2);
  });

  test('fails atomically on an orphaned legacy relation instead of silently dropping data', () => {
    const legacy = legacyFixture();
    legacy.db.pragma('foreign_keys = OFF');
    legacy.db.prepare("INSERT INTO player_entity_identifiers VALUES('missing','game','orphan','Orphan')").run();
    const rows = legacy.db.prepare('SELECT * FROM player_entity_identifiers').all();
    legacy.db.close();
    expect(() => open(legacy.path)).toThrow(/FOREIGN KEY/);
    const raw = new Database(legacy.path); databases.push(raw);
    expect(raw.prepare('SELECT * FROM player_entity_identifiers').all()).toEqual(rows);
    expect(raw.prepare("SELECT name FROM sqlite_master WHERE name='player_identifiers'").get()).toBeUndefined();
    raw.prepare("INSERT INTO player_entities VALUES('missing',12,22)").run(); raw.close();
    expect(dictionary(open(legacy.path))).toHaveLength(3);
  });

  test('migrates the intermediate dictionary schema without repeating shared spelling text', () => {
    const legacy = legacyFixture();
    legacy.db.exec(`
      ALTER TABLE player_entity_identifiers RENAME TO old_identifiers;
      CREATE TABLE player_identifiers(identifier_id INTEGER PRIMARY KEY,kind TEXT NOT NULL,identifier_norm TEXT NOT NULL,UNIQUE(kind,identifier_norm));
      INSERT INTO player_identifiers(kind,identifier_norm) SELECT kind,identifier_norm FROM old_identifiers;
      CREATE TABLE player_entity_identifiers(entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE RESTRICT,
        display_value TEXT NOT NULL,PRIMARY KEY(entity_id,identifier_id));
      INSERT INTO player_entity_identifiers SELECT entity_id,identifier_id,display_value FROM old_identifiers JOIN player_identifiers USING(kind,identifier_norm);
      INSERT INTO player_entity_identifiers SELECT 'beta',identifier_id,display_value FROM player_entity_identifiers WHERE entity_id='alpha';
      DROP TABLE old_identifiers;
    `);
    legacy.db.close();
    const db = open(legacy.path);
    const before = listPlayerLibrary(db);
    expect(before).toEqual({ revision: 43, entityRedirects: [], entities: [
      entity('alpha', ['Alpha'], ['Caf\u00e9']), entity('beta', ['Beta'], ['Caf\u00e9', 'Other']),
    ] });
    expect(dictionary(db)).toHaveLength(2);
    expect(db.prepare('SELECT * FROM player_identifier_spellings').all()).toHaveLength(2);
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(3);
    db.close();
    expect(listPlayerLibrary(open(legacy.path))).toEqual(before);
  });
});

describe('approved publication identity normalization', () => {
  const games = ['g1', 'g2', 'g3', 'g4', 'g5'];
  const pending = (db: Database.Database, entities: ReturnType<typeof entity>[]) => {
    const payload = JSON.stringify({ entities });
    const id = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',100)")
      .run(payload).lastInsertRowid);
    return { id, submissionRevision: readAdminSubmission(db, id).submissionRevision };
  };

  test('projects grouped name ownership without mutating a pending draft or the database', () => {
    const db = open();
    const existing = entity('z-existing', ['Shared name'], games);
    mutatePublicLibrary(db, 0, 100, 'create', [existing]);
    const incoming = entity('a-new', ['Shared name', 'New alias'], games);
    expect(projectedLibrary([existing], [incoming])).toMatchObject({
      conflicts: [], entities: [entity('z-existing', ['Shared name', 'New alias'], games)],
    });
    const ref = pending(db, [incoming]);
    readAdminSubmission(db, ref.id);
    expect(listPlayerLibrary(db)).toEqual({ revision: 1, entities: [existing], entityRedirects: [] });
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
    expect(JSON.parse((db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(ref.id) as { payload_json: string }).payload_json).entities).toEqual([incoming]);
  });

  test.each(['gameIds'] as const)('coalesces known players at the %s threshold with durable redirects and one revision', field => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-auto-identity-')); directories.push(directory);
    const path = join(directory, 'library.sqlite');
    const db = open(path);
    const ids = games;
    const a = { ...entity('a'), [field]: ids };
    const b = { ...entity('b'), [field]: ids.slice(1) };
    mutatePublicLibrary(db, 0, 100, 'import', [b, a]);
    db.prepare("INSERT INTO player_entity_redirects VALUES('retired','b')").run();
    const ref = pending(db, [{ ...b, [field]: ids }]);
    expect(readAdminSubmission(db, ref.id).automaticGroups).toEqual([['a', 'b']]);
    expect(listPlayerLibrary(db).entities).toHaveLength(2);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
    const result = reviewAdminSubmissions(db, 1, 101, 'approve', [ref]);
    expect(result).toMatchObject({ revision: 2, acceptedEntityCount: 1, skippedEntityCount: 0 });
    const after = listPlayerLibrary(db);
    expect(after).toEqual({ revision: 2, entities: [{ ...a, names: ['a', 'b'] }], entityRedirects: [
      { fromEntityId: 'b', toEntityId: 'a' }, { fromEntityId: 'retired', toEntityId: 'a' },
    ] });
    const audit = db.prepare('SELECT * FROM player_library_merge_audit').all() as { target_entity_id: string; revision: number; before_json: string }[];
    expect(audit).toHaveLength(1);
    expect(audit[0]).toMatchObject({ target_entity_id: 'a', revision: 2 });
    expect(JSON.parse(audit[0].before_json)).toEqual(expect.arrayContaining([a, b]));
    expect(() => mutatePublicLibrary(db, 2, 102, 'create', [entity('b', ['New B'])])).toThrow('retired_entity_id');
    db.close();
    const reopened = open(path);
    expect(listPlayerLibrary(reopened)).toEqual(after);
    expect(reopened.pragma('foreign_key_check')).toEqual([]);
  });

  test('partial review blocks every original group member when its grouped name conflicts with another owner', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('owner', ['Taken'])]);
    const refs = [pending(db, [entity('a', ['A', 'Taken'], games)]),
      pending(db, [entity('b', ['B'], games), entity('safe', ['Safe'], ['g1'])])];
    expect(() => reviewAdminSubmissions(db, 1, 101, 'approve', refs)).toThrow('ownership_conflict');
    const result = reviewAdminSubmissions(db, 1, 101, 'approve', refs, true);
    expect(result).toMatchObject({ revision: 2, acceptedEntityCount: 1, skippedEntityCount: 2, pendingSubmissionCount: 2 });
    expect(result.skippedEntities.map(item => item.submittedEntityId)).toEqual(['a', 'b']);
    expect(result.skippedEntities.every(item => item.conflicts.some(conflict => conflict.kind === 'names'))).toBe(true);
    expect(listPlayerLibrary(db)).toEqual({ revision: 2, entities: [entity('owner', ['Taken']), entity('safe', ['Safe'], ['g1'])], entityRedirects: [] });
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
  });

  test('automatic audit failure rolls back new data, redirects, dictionary rows, revision and review status', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('a', ['A'], games), entity('b', ['B'], games.slice(1))]);
    db.prepare("INSERT INTO player_entity_redirects VALUES('retired','b')").run();
    const ref = pending(db, [entity('b', ['B', 'Alias'], [...games, 'new'])]);
    const before = listPlayerLibrary(db), keys = dictionary(db);
    db.exec("CREATE TRIGGER fail_auto_audit BEFORE INSERT ON player_library_merge_audit BEGIN SELECT RAISE(ABORT,'audit failure'); END");
    expect(() => reviewAdminSubmissions(db, 1, 101, 'approve', [ref])).toThrow('audit failure');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(dictionary(db)).toEqual(keys);
    expect(readAdminSubmission(db, ref.id).status).toBe('pending');
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('uses original clique evidence once per publication instead of transitively regrouping a union', () => {
    const db = open();
    const more = ['g6', 'g7', 'g8', 'g9', 'g10'];
    const ref = pending(db, [entity('a', ['A'], games), entity('b', ['B'], [...games, ...more]), entity('c', ['C'], more)]);
    reviewAdminSubmissions(db, 0, 100, 'approve', [ref]);
    expect(listPlayerLibrary(db).entities.map(item => item.entityId)).toEqual(['a', 'c']);
    expect(listPlayerLibrary(db).entityRedirects).toEqual([{ fromEntityId: 'b', toEntityId: 'a' }]);
  });

  test('returns the library size error and rolls back when automatic redirects exceed the count bound', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('a', ['A'], games), entity('b', ['B'], games.slice(1))]);
    const insert = db.prepare("INSERT INTO player_entity_redirects VALUES(?,'b')");
    db.transaction(() => { for (let i = 0; i < MAX_ENTITY_REDIRECTS; i++) insert.run(`r${i}`); })();
    const before = listPlayerLibrary(db);
    expect(() => mutatePublicLibrary(db, 1, 101, 'import', [entity('b', ['B'], games)])).toThrow('library_too_large');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
  });

  test('bounds automatic retargeting bytes before changing redirect targets or reviewing submissions', () => {
    const db = open();
    const targetId = 'a' + 'x'.repeat(120);
    mutatePublicLibrary(db, 0, 100, 'import', [entity(targetId, ['A'], games), entity('b', ['B'], games.slice(1))]);
    const insert = db.prepare("INSERT INTO player_entity_redirects VALUES(?,'b')");
    db.transaction(() => { for (let i = 0; i < 3000; i++) insert.run(`r${i}`); })();
    const ref = pending(db, [entity('b', ['B'], games)]);
    const before = listPlayerLibrary(db);
    expect(Buffer.byteLength(JSON.stringify(before))).toBeLessThan(262144);
    expect(() => reviewAdminSubmissions(db, 1, 101, 'approve', [ref])).toThrow('library_too_large');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(readAdminSubmission(db, ref.id).status).toBe('pending');
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
  });

  test('records redirect-only changes once and returns the existing canonical entity for a new strong alias', () => {
    const db = open();
    const existing = entity('z-existing', ['Same'], games);
    mutatePublicLibrary(db, 0, 100, 'create', [existing]);
    expect(mutatePublicLibrary(db, 1, 101, 'create', [{ ...existing, entityId: 'a-new' }]))
      .toEqual({ revision: 2, entity: existing });
    expect(listPlayerLibrary(db).entityRedirects).toEqual([{ fromEntityId: 'a-new', toEntityId: 'z-existing' }]);
    expect(mutatePublicLibrary(db, 2, 102, 'import', [existing]).revision).toBe(2);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toHaveLength(1);
  });

  test.each(['approve', 'reject'] as const)('%s with no accepted incoming entities leaves an unrelated mergeable public pair untouched', action => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('owner', ['Taken']),
      entity('a', ['A'], games), entity('b', ['B'], games.slice(1))]);
    // Simulate stored evidence predating automatic publication normalization.
    db.prepare(`INSERT INTO player_entity_identifiers(entity_id,identifier_id,spelling_id)
      SELECT 'b',identifier_id,spelling_id FROM player_identifier_spellings JOIN player_identifiers USING(identifier_id)
      WHERE kind='game' AND identifier_norm='g1' AND display_value='g1'`).run();
    const before = listPlayerLibrary(db);
    const ref = pending(db, [entity('blocked', ['Taken'])]);
    expect(listPlayerLibrary(db)).toEqual(before);
    const result = reviewAdminSubmissions(db, 1, 101, action, [ref], true);
    expect(result).toMatchObject({ revision: 1, acceptedEntityCount: 0 });
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
    expect(readAdminSubmission(db, ref.id).status).toBe(action === 'approve' ? 'pending' : 'rejected');
  });
});

describe('durable automatic identity evidence', () => {
  const left = ['g1', 'g2', 'g3', 'g4', 'g5'];
  const right = ['g6', 'g7', 'g8', 'g9', 'g10'];
  const chain = () => [entity('a', ['A'], left), entity('b', ['B'], [...left, ...right]), entity('c', ['C'], right)];
  const pending = (db: Database.Database, entities: ReturnType<typeof entity>[]) => {
    const id = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',100)")
      .run(JSON.stringify({ entities })).lastInsertRowid);
    return { id, submissionRevision: readAdminSubmission(db, id).submissionRevision };
  };

  test.each(['import', 'approve'] as const)('repeated %s and restart never treat the saved A/B union as new evidence against C', mode => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-original-evidence-')); directories.push(directory);
    const path = join(directory, 'library.sqlite');
    let db = open(path);
    const originals = chain();
    mutatePublicLibrary(db, 0, 100, 'import', originals);
    const before = listPlayerLibrary(db);
    expect(before.entities.map(item => item.entityId)).toEqual(['a', 'c']);
    for (let i = 0; i < 2; i++) {
      if (mode === 'import') mutatePublicLibrary(db, 1, 101 + i, 'import', before.entities);
      else reviewAdminSubmissions(db, 1, 101 + i, 'approve', [pending(db, before.entities)]);
      expect(listPlayerLibrary(db)).toEqual(before);
      expect(readAutomaticIdentityEvidence(db).get('a')).toEqual(originals.slice(0, 2));
      const preview = projectedLibrary(before.entities, before.entities, readAutomaticIdentityEvidence(db));
      expect(preview.automaticGroups).toEqual([]);
      expect(readAdminSubmission(db, pending(db, [originals[2]]).id).automaticGroups).toEqual([]);
      db.close(); db = open(path);
    }
    reviewAdminSubmissions(db, 1, 104, 'approve', [pending(db, originals)]);
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(readAutomaticIdentityEvidence(db).get('a')).toEqual(originals.slice(0, 2));
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toHaveLength(1);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test.each([false, true])('unknown narrowing aliases retain their original proof across submissions: batch=%s', batch => {
    const db = open();
    const [a, b, c] = chain();
    mutatePublicLibrary(db, 0, 100, 'create', [b]);
    const storedB = listPlayerLibrary(db).entities[0];
    const aRef = pending(db, [a]);
    if (batch) reviewAdminSubmissions(db, 1, 101, 'approve', [aRef, pending(db, [c])]);
    else {
      reviewAdminSubmissions(db, 1, 101, 'approve', [aRef]);
      const cRef = pending(db, [c]);
      expect(readAdminSubmission(db, cRef.id).automaticGroups).toEqual([]);
      reviewAdminSubmissions(db, 2, 102, 'approve', [cRef]);
    }
    const library = listPlayerLibrary(db);
    expect(library.entities.map(item => item.entityId)).toEqual(['b', 'c']);
    expect(library.entityRedirects).toEqual([{ fromEntityId: 'a', toEntityId: 'b' }]);
    expect(readAutomaticIdentityEvidence(db).get('b')).toEqual([storedB, a]);
    mutatePublicLibrary(db, library.revision, 103, 'import', library.entities);
    expect(listPlayerLibrary(db)).toEqual(library);
  });

  test('manual merge resets selected authority without any same-write automatic chain and audits original evidence', () => {
    const db = open();
    const originals = chain();
    mutatePublicLibrary(db, 0, 100, 'import', [...originals, entity('d', ['D'], ['different'])]);
    const merged = mergePublicLibrary(db, 1, 101, 'a', ['a', 'd']);
    expect(listPlayerLibrary(db).entities.map(item => item.entityId)).toEqual(['a', 'c']);
    expect(merged.entityRedirects).toEqual([{ fromEntityId: 'b', toEntityId: 'a' }, { fromEntityId: 'd', toEntityId: 'a' }]);
    expect(readAutomaticIdentityEvidence(db).has('a')).toBe(false);
    const audit = db.prepare('SELECT * FROM player_library_merge_audit ORDER BY id DESC LIMIT 1').get() as {
      merge_kind: string; before_evidence_json: string; after_evidence_json: string;
    };
    expect(audit.merge_kind).toBe('manual');
    expect(JSON.parse(audit.before_evidence_json).a).toEqual(originals.slice(0, 2));
    expect(JSON.parse(audit.after_evidence_json)).toEqual({ a: [merged.entity] });
    // A later publication can use the explicit manual authority, never an inferred intermediate union.
    mutatePublicLibrary(db, 2, 102, 'import', listPlayerLibrary(db).entities);
    expect(listPlayerLibrary(db).entities).toHaveLength(1);
  });

  test('stores the complete incoming originals before their first automatic union and never replaces them with a later union', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('a', ['A'], left), entity('b', ['B'], left.slice(1))]);
    const updated = entity('b', ['B', 'New alias'], [...left, ...right]);
    mutatePublicLibrary(db, 1, 101, 'import', [updated]);
    const evidence = readAutomaticIdentityEvidence(db);
    const complete = evidence.get('a')![1];
    expect(evidence.get('a')).toHaveLength(2);
    expect(evidence.get('a')![0]).toEqual(entity('a', ['A'], left));
    expect({ ...complete, gameIds: [...complete.gameIds].sort() }).toEqual({ ...updated, gameIds: [...updated.gameIds].sort() });
    const audit = db.prepare('SELECT merge_kind,before_evidence_json,after_evidence_json FROM player_library_merge_audit').get() as {
      merge_kind: string; before_evidence_json: string; after_evidence_json: string;
    };
    expect(audit.merge_kind).toBe('automatic');
    expect(JSON.parse(audit.before_evidence_json)).toEqual({ a: [entity('a', ['A'], left)], b: [complete] });
    expect(JSON.parse(audit.after_evidence_json)).toEqual({ a: evidence.get('a') });
    mutatePublicLibrary(db, 2, 102, 'import', listPlayerLibrary(db).entities);
    expect(readAutomaticIdentityEvidence(db)).toEqual(evidence);
    expect(listPlayerLibrary(db).revision).toBe(2);
  });

  test('late review and manual audit failures roll back evidence with content, redirects and revision', () => {
    const db = open();
    const ref = pending(db, chain());
    db.exec("CREATE TRIGGER fail_evidence_status BEFORE UPDATE OF status ON player_library_submissions BEGIN SELECT RAISE(ABORT,'status failure'); END");
    expect(() => reviewAdminSubmissions(db, 0, 100, 'approve', [ref])).toThrow('status failure');
    expect(readAutomaticIdentityEvidence(db).size).toBe(0);
    expect(listPlayerLibrary(db)).toEqual({ revision: 0, entities: [], entityRedirects: [] });
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
    db.exec('DROP TRIGGER fail_evidence_status');
    reviewAdminSubmissions(db, 0, 101, 'approve', [ref]);
    const before = listPlayerLibrary(db), evidence = readAutomaticIdentityEvidence(db);
    db.exec("CREATE TRIGGER fail_evidence_audit BEFORE INSERT ON player_library_merge_audit BEGIN SELECT RAISE(ABORT,'audit failure'); END");
    expect(() => mergePublicLibrary(db, 1, 102, 'a', ['a', 'c'])).toThrow('audit failure');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(readAutomaticIdentityEvidence(db)).toEqual(evidence);
  });

  test('invalid stored evidence fails closed instead of silently falling back to merged identifiers', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', chain());
    const before = listPlayerLibrary(db);
    for (const text of ['not json', '[]', '[{}]']) {
      db.prepare("UPDATE player_entity_auto_evidence SET evidence_json=? WHERE entity_id='a'").run(text);
      expect(() => readAutomaticIdentityEvidence(db)).toThrow('invalid_automatic_identity_evidence');
      expect(() => mutatePublicLibrary(db, 1, 101, 'import', before.entities)).toThrow('invalid_automatic_identity_evidence');
      expect(listPlayerLibrary(db)).toEqual(before);
    }
  });

  test('bounds retained originals before changing publication state', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('a', ['A'], left)]);
    const originalIds = Array.from({ length: 8000 }, (_, i) => `original-${i}-` + 'x'.repeat(110));
    const json = JSON.stringify([entity('a', ['A'], [...left, ...originalIds])]);
    expect(Buffer.byteLength(json)).toBeLessThan(MAX_AUTOMATIC_EVIDENCE_BYTES);
    db.prepare('INSERT INTO player_entity_auto_evidence VALUES(?,?)').run('a', json);
    const incoming = entity('b', ['B'], [...left, ...Array.from({ length: 800 }, (_, i) => `incoming-${i}-` + 'x'.repeat(110))]);
    const before = listPlayerLibrary(db);
    expect(() => mutatePublicLibrary(db, 1, 101, 'create', [incoming])).toThrow('library_too_large');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(db.prepare('SELECT evidence_json FROM player_entity_auto_evidence').get()).toEqual({ evidence_json: json });
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
    db.prepare("UPDATE player_entity_auto_evidence SET evidence_json=?").run(' '.repeat(MAX_AUTOMATIC_EVIDENCE_BYTES + 1));
    expect(() => readAutomaticIdentityEvidence(db)).toThrow('invalid_automatic_identity_evidence');
  });

  test('later compatible automatic groups flatten saved originals instead of capturing another union', () => {
    const db = open();
    const originals = chain();
    mutatePublicLibrary(db, 0, 100, 'import', originals);
    const d = entity('d', ['D'], left);
    mutatePublicLibrary(db, 1, 101, 'create', [d]);
    expect(readAutomaticIdentityEvidence(db).get('a')).toEqual([...originals.slice(0, 2), d]);
    const before = listPlayerLibrary(db);
    mutatePublicLibrary(db, 2, 102, 'import', before.entities);
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(before.entities.map(item => item.entityId)).toEqual(['a', 'c']);
  });

  test('name-only players stay independent across repeated publication', () => {
    const db = open();
    const a = entity('a', ['A'], []), c = entity('c', ['C'], []);
    mutatePublicLibrary(db, 0, 100, 'import', [a, entity('b', ['B'], []), c]);
    const before = listPlayerLibrary(db);
    mutatePublicLibrary(db, 1, 101, 'import', before.entities);
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(before.entities.map(item => item.entityId)).toEqual(['a', 'b', 'c']);
  });

  test('replace removes old IDs from live matching without rewriting original automatic evidence', () => {
    const db = open();
    const originals = chain().slice(0, 2);
    mutatePublicLibrary(db, 0, 100, 'import', originals);
    const stored = db.prepare('SELECT evidence_json FROM player_entity_auto_evidence').get();
    mutatePublicLibrary(db, 1, 101, 'replace', [entity('a', ['A', 'B'], right)], 'a');
    expect(readAutomaticIdentityEvidence(db).get('a')).toEqual(originals);
    const c = entity('c', ['C'], left);
    mutatePublicLibrary(db, 2, 102, 'create', [c]);
    expect(listPlayerLibrary(db)).toEqual({ revision: 3,
      entities: [entity('a', ['A', 'B'], [...right].sort()), c],
      entityRedirects: [{ fromEntityId: 'b', toEntityId: 'a' }] });
    expect(db.prepare('SELECT evidence_json FROM player_entity_auto_evidence').get()).toEqual(stored);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toHaveLength(1);
    expect(projectedLibrary(listPlayerLibrary(db).entities, [c], readAutomaticIdentityEvidence(db)).automaticGroups).toEqual([]);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('upgrades old audit rows without inventing original proof and preserves evidence foreign keys on reopen', () => {
    const legacy = legacyFixture();
    legacy.db.exec(`CREATE TABLE player_library_merge_audit (
      id INTEGER PRIMARY KEY AUTOINCREMENT,target_entity_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at INTEGER NOT NULL,
      before_json TEXT NOT NULL,before_redirects_json TEXT NOT NULL,after_json TEXT NOT NULL,redirects_json TEXT NOT NULL);
      INSERT INTO player_library_merge_audit VALUES(1,'alpha',41,99,'[]','[]','{}','[]');`);
    legacy.db.close();
    const db = open(legacy.path);
    expect(readAutomaticIdentityEvidence(db).size).toBe(0);
    expect(listPlayerLibrary(db).revision).toBe(43);
    expect(db.prepare('SELECT merge_kind,before_evidence_json,after_evidence_json FROM player_library_merge_audit').get())
      .toEqual({ merge_kind: 'legacy', before_evidence_json: '{}', after_evidence_json: '{}' });
    const proof = [entity('alpha', ['Alpha'], ['Caf\u00e9'])];
    const insert = db.prepare('INSERT INTO player_entity_auto_evidence VALUES(?,?)');
    expect(() => insert.run('missing', JSON.stringify(proof))).toThrow(/FOREIGN KEY/);
    insert.run('alpha', JSON.stringify(proof));
    db.close();
    const reopened = open(legacy.path);
    expect(readAutomaticIdentityEvidence(reopened).get('alpha')).toEqual(proof);
    reopened.prepare("DELETE FROM player_entities WHERE entity_id='alpha'").run();
    expect(readAutomaticIdentityEvidence(reopened).size).toBe(0);
    expect(reopened.pragma('foreign_key_check')).toEqual([]);
    expect(reopened.prepare('SELECT * FROM player_library_merge_audit').all()).toHaveLength(1);
  });
});
