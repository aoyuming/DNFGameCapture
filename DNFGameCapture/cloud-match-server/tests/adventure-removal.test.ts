import Database from 'better-sqlite3';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterAll, afterEach, expect, test } from 'vitest';
import { openDatabase } from '../src/db.js';
import { automaticIdentityGroups } from '../src/library-identity-policy.js';
import { mutatePublicLibrary, parseAdminEntities, readAdminSubmission, reviewAdminSubmissions } from '../src/library-admin-data.js';
import { listPlayerLibrary } from '../src/library-store.js';
import { playerSchema } from '../src/schemas.js';
import { getSnapshot, saveSnapshot } from '../src/snapshots.js';

const db = openDatabase(':memory:');
afterAll(() => db.close());
afterEach(() => {
  db.exec('DELETE FROM player_library_submissions; DELETE FROM player_entity_redirects; DELETE FROM player_entities; UPDATE player_library_meta SET revision=0');
});

const legacy = (entityId: string, gameIds: string[] = []) => ({
  entityId, names: [entityId], gameIds, adventureGroupIds: ['guild1', 'guild2', 'guild3'],
});

test('legacy and unknown identity fields are stripped without losing names or game IDs', () => {
  expect(parseAdminEntities({ entities: [{ ...legacy('a'), unknownIdentity: ['not-a-game'] }] }))
    .toEqual([{ entityId: 'a', names: ['a'], gameIds: [] }]);
  expect(playerSchema.parse({ mainName: 'Player', aliases: ['Game'], kills: 3, deaths: 2, ak: 1, streak: 0,
    adventureGroupIds: { malformed: true } }))
    .toEqual({ mainName: 'Player', aliases: ['Game'], kills: 3, deaths: 2, ak: 1, streak: 0 });
});

test('removed identifiers never satisfy or supplement the five-game threshold', () => {
  expect(automaticIdentityGroups([legacy('a'), legacy('b')])).toEqual([]);
  const four = ['1', '2', '3', '4'];
  expect(automaticIdentityGroups([legacy('a', four), legacy('b', [...four, '1'])])).toEqual([]);
  expect(automaticIdentityGroups([legacy('a', [...four, '5']), legacy('b', [...four, '5'])])).toEqual([['a', 'b']]);
});

test('publication strips unknown evidence before retaining automatic merge originals and audits', () => {
  const games = ['1', '2', '3', '4', '5'];
  mutatePublicLibrary(db, 0, 1, 'import', [legacy('a', games), legacy('b', games)]);
  expect(JSON.stringify(db.prepare('SELECT evidence_json FROM player_entity_auto_evidence').all())).not.toContain('adventureGroupIds');
  expect(JSON.stringify(db.prepare('SELECT before_json,after_json FROM player_library_merge_audit').all())).not.toContain('adventureGroupIds');
});

test('old pending submissions remain reviewable but cannot publish removed identifiers or merge players', () => {
  mutatePublicLibrary(db, 0, 1, 'create', parseAdminEntities({ entities: [legacy('a')] }));
  const id = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES(?,?,'pending',?)")
    .run('old-device', JSON.stringify({ entities: [legacy('b')] }), 2).lastInsertRowid);
  const submission = readAdminSubmission(db, id);
  expect(submission.valid).toBe(true);
  expect(submission.entities).toEqual([{ entityId: 'b', names: ['b'], gameIds: [] }]);
  reviewAdminSubmissions(db, 1, 3, 'approve', [{ id, submissionRevision: submission.submissionRevision }]);
  expect(listPlayerLibrary(db)).toEqual({ revision: 2, entityRedirects: [], entities: [
    { entityId: 'a', names: ['a'], gameIds: [] }, { entityId: 'b', names: ['b'], gameIds: [] },
  ] });
});

function migrationFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-remove-adventure-'));
  const path = join(directory, 'fixture.sqlite');
  const raw = new Database(path);
  raw.exec(`
    CREATE TABLE player_entities(entity_id TEXT PRIMARY KEY,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);
    CREATE TABLE player_entity_names(entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
      name_norm TEXT NOT NULL UNIQUE,display_name TEXT NOT NULL,PRIMARY KEY(entity_id,name_norm));
    CREATE TABLE player_identifiers(identifier_id INTEGER PRIMARY KEY,kind TEXT NOT NULL CHECK(kind IN ('game','adventure')),
      identifier_norm TEXT NOT NULL,UNIQUE(kind,identifier_norm));
    CREATE TABLE player_identifier_spellings(spelling_id INTEGER PRIMARY KEY,
      identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE CASCADE,
      display_value TEXT NOT NULL,UNIQUE(identifier_id,display_value),UNIQUE(identifier_id,spelling_id));
    CREATE TABLE player_entity_identifiers(entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
      identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE RESTRICT,spelling_id INTEGER NOT NULL,
      PRIMARY KEY(entity_id,identifier_id),FOREIGN KEY(identifier_id,spelling_id)
      REFERENCES player_identifier_spellings(identifier_id,spelling_id) ON DELETE RESTRICT);
    CREATE TABLE player_library_meta(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL);
    INSERT INTO player_library_meta VALUES(1,42);
    INSERT INTO player_entities VALUES('a',10,20),('b',11,21);
    INSERT INTO player_entity_names VALUES('a','alpha','Alpha'),('a','alias','Alias'),('b','beta','Beta');
    INSERT INTO player_identifiers VALUES(7,'game','same'),(8,'adventure','same'),(9,'adventure','guild-only');
    INSERT INTO player_identifier_spellings VALUES(70,7,'Same'),(71,7,'SAME'),(80,8,'Same'),(90,9,'Guild-only');
    INSERT INTO player_entity_identifiers VALUES('a',7,70),('a',8,80),('b',9,90);
    CREATE TABLE player_library_submissions(id INTEGER PRIMARY KEY AUTOINCREMENT,device_id TEXT NOT NULL,payload_json TEXT NOT NULL,
      status TEXT NOT NULL CHECK(status IN ('pending','approved','rejected')),created_at INTEGER NOT NULL,reviewed_at INTEGER,review_reason TEXT);
    CREATE TABLE player_library_submission_originals(submission_id INTEGER PRIMARY KEY REFERENCES player_library_submissions(id) ON DELETE CASCADE,
      payload_json TEXT NOT NULL);
    CREATE TABLE player_entity_auto_evidence(entity_id TEXT PRIMARY KEY REFERENCES player_entities(entity_id) ON DELETE CASCADE,evidence_json TEXT NOT NULL);
    CREATE TABLE player_library_merge_audit(id INTEGER PRIMARY KEY AUTOINCREMENT,target_entity_id TEXT NOT NULL,revision INTEGER NOT NULL,
      created_at INTEGER NOT NULL,before_json TEXT NOT NULL,before_redirects_json TEXT NOT NULL,after_json TEXT NOT NULL,redirects_json TEXT NOT NULL,
      merge_kind TEXT NOT NULL DEFAULT 'legacy',before_evidence_json TEXT NOT NULL DEFAULT '{}',after_evidence_json TEXT NOT NULL DEFAULT '{}');
    CREATE TABLE devices(id TEXT PRIMARY KEY,token_hash TEXT NOT NULL,created_at INTEGER NOT NULL,last_seen_at INTEGER NOT NULL);
    INSERT INTO devices VALUES('old-device','preserved-token',10,20);
    CREATE TABLE rooms(id TEXT PRIMARY KEY,display_name TEXT NOT NULL UNIQUE,revision INTEGER NOT NULL DEFAULT 0,presence_revision INTEGER NOT NULL DEFAULT 0);
    INSERT INTO rooms VALUES('59','59 room',8,9);
    CREATE TABLE memberships(device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,room_id TEXT NOT NULL REFERENCES rooms(id),
      broadcaster_name TEXT NOT NULL,updated_at INTEGER NOT NULL);
    INSERT INTO memberships VALUES('old-device','59','Broadcaster',30);
    CREATE TABLE licenses(id INTEGER PRIMARY KEY AUTOINCREMENT,key_hash TEXT NOT NULL UNIQUE,label TEXT NOT NULL DEFAULT '',expires_at INTEGER,
      disabled_at INTEGER,bound_device_id TEXT,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);
    INSERT INTO licenses VALUES(12,'preserved-key','License',NULL,NULL,'old-device',10,20);
    CREATE TABLE auth_sessions(token_hash TEXT PRIMARY KEY,license_id INTEGER NOT NULL REFERENCES licenses(id) ON DELETE CASCADE,device_id TEXT NOT NULL,
      created_at INTEGER NOT NULL,last_seen_at INTEGER NOT NULL,expires_at INTEGER NOT NULL);
    INSERT INTO auth_sessions VALUES('session',12,'old-device',1,2,9999999999);
    CREATE TABLE snapshots(device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,room_id TEXT NOT NULL REFERENCES rooms(id),
      client_revision INTEGER NOT NULL,content_hash TEXT NOT NULL,change_source TEXT NOT NULL,synced_from_device_id TEXT,synced_from_revision INTEGER,
      payload_json TEXT NOT NULL,received_at INTEGER NOT NULL);
  `);
  const payload = JSON.stringify({ entities: [legacy('pending-player')] });
  raw.prepare("INSERT INTO player_library_submissions VALUES(5,'old-device',?,'pending',1,NULL,NULL)").run(payload);
  raw.prepare('INSERT INTO player_library_submission_originals VALUES(5,?)').run(payload);
  raw.prepare('INSERT INTO player_entity_auto_evidence VALUES(?,?)').run('a', JSON.stringify([legacy('a', ['Same']), legacy('retired')]));
  raw.prepare("INSERT INTO player_library_merge_audit VALUES(1,'a',42,1,?,'[]',?,'[]','automatic',?,?)")
    .run(JSON.stringify([legacy('a')]), JSON.stringify(legacy('a')), JSON.stringify({ a: [legacy('a')] }), JSON.stringify({ a: [legacy('a')] }));
  const player = { mainName: 'Player', aliases: ['Game'], kills: 7, deaths: 3, ak: 1, streak: 2, adventureGroupIds: ['Guild'] };
  const snapshot = { schemaVersion: 1, clientRevision: 1, clientTime: 100, changeSource: 'manual', redScore: 8, blueScore: 9,
    redPlayers: Array.from({ length: 4 }, () => player), bluePlayers: Array.from({ length: 4 }, () => player), redPickFirst: true, lastKillTeam: 'red' };
  raw.prepare("INSERT INTO snapshots VALUES('old-device','59',1,'old-hash','manual',NULL,NULL,?,200)").run(JSON.stringify(snapshot));
  return { raw, directory, path, snapshot };
}

test('versioned migration purges only removed evidence, preserves identities and unrelated data, and is idempotent', () => {
  const fixture = migrationFixture();
  let migrated: Database.Database | undefined;
  try {
    const preserved = ['player_entities', 'player_entity_names', 'devices', 'memberships', 'auth_sessions']
      .map(table => [table, fixture.raw.prepare(`SELECT * FROM ${table}`).all()] as const);
    fixture.raw.close();
    migrated = openDatabase(fixture.path);
    for (const [table, rows] of preserved) expect(migrated.prepare(`SELECT * FROM ${table}`).all()).toEqual(rows);
    expect(migrated.prepare('SELECT key_hash,bound_device_id FROM licenses').get()).toEqual({ key_hash: 'preserved-key', bound_device_id: 'old-device' });
    expect(listPlayerLibrary(migrated)).toEqual({ revision: 43, entityRedirects: [], entities: [
      { entityId: 'a', names: ['Alias', 'Alpha'], gameIds: ['Same'] }, { entityId: 'b', names: ['Beta'], gameIds: [] },
    ] });
    expect(migrated.prepare('SELECT * FROM player_identifiers').all()).toEqual([{ identifier_id: 7, kind: 'game', identifier_norm: 'same' }]);
    expect(migrated.prepare('SELECT spelling_id FROM player_identifier_spellings ORDER BY spelling_id').all()).toEqual([{ spelling_id: 70 }, { spelling_id: 71 }]);
    expect(() => migrated!.prepare("INSERT INTO player_identifiers(kind,identifier_norm) VALUES('adventure','no')").run()).toThrow(/CHECK/);
    expect(migrated.prepare('SELECT status,created_at,reviewed_at FROM player_library_submissions').get()).toEqual({ status: 'pending', created_at: 1, reviewed_at: null });
    for (const table of ['player_library_submissions', 'player_library_submission_originals', 'player_entity_auto_evidence', 'player_library_merge_audit', 'snapshots']) {
      expect(JSON.stringify(migrated.prepare(`SELECT * FROM ${table}`).all())).not.toContain('adventureGroupIds');
    }
    expect(getSnapshot(migrated, 'old-device')?.snapshot).toMatchObject({ redScore: 8, blueScore: 9, redPlayers: [
      { mainName: 'Player', aliases: ['Game'], kills: 7 }, {}, {}, {},
    ] });
    expect(saveSnapshot(migrated, { deviceId: 'old-device', roomId: '59', receivedAt: 300,
      snapshot: { ...fixture.snapshot, clientRevision: 2 } })).toEqual({ ok: false, code: 'duplicate_snapshot' });
    expect(migrated.pragma('foreign_key_check')).toEqual([]);
    const before = listPlayerLibrary(migrated);
    migrated.close(); migrated = openDatabase(fixture.path);
    expect(listPlayerLibrary(migrated)).toEqual(before);
    expect(migrated.prepare('SELECT COUNT(*) AS count FROM server_schema_migrations').get()).toEqual({ count: 1 });
    const submission = readAdminSubmission(migrated, 5);
    expect(submission.valid).toBe(true);
    reviewAdminSubmissions(migrated, 43, 400, 'approve', [{ id: 5, submissionRevision: submission.submissionRevision }]);
    expect(listPlayerLibrary(migrated).entities).toHaveLength(3);
  } finally {
    if (fixture.raw.open) fixture.raw.close();
    if (migrated?.open) migrated.close();
    rmSync(fixture.directory, { recursive: true, force: true });
  }
});

test('migrated original evidence cannot turn a historical union into fresh automatic merge proof', () => {
  const fixture = migrationFixture();
  let migrated: Database.Database | undefined;
  try {
    for (let i = 2; i <= 5; i++) {
      fixture.raw.prepare("INSERT INTO player_identifiers VALUES(?,'game',?)").run(i + 10, `g${i}`);
      fixture.raw.prepare('INSERT INTO player_identifier_spellings VALUES(?,?,?)').run(i + 100, i + 10, `g${i}`);
      fixture.raw.prepare("INSERT INTO player_entity_identifiers VALUES('a',?,?)").run(i + 10, i + 100);
    }
    fixture.raw.prepare("UPDATE player_entity_auto_evidence SET evidence_json=? WHERE entity_id='a'")
      .run(JSON.stringify([legacy('a', ['Same', 'g2', 'g3']), legacy('retired', ['g4', 'g5'])]));
    fixture.raw.close(); migrated = openDatabase(fixture.path);
    mutatePublicLibrary(migrated, 43, 500, 'create', [{ entityId: 'candidate', names: ['Candidate'], gameIds: ['Same', 'g2', 'g3', 'g4', 'g5'] }]);
    expect(listPlayerLibrary(migrated).entities.map(entity => entity.entityId)).toEqual(['a', 'b', 'candidate']);
    expect(listPlayerLibrary(migrated).entityRedirects).toEqual([]);
  } finally {
    if (fixture.raw.open) fixture.raw.close();
    if (migrated?.open) migrated.close();
    rmSync(fixture.directory, { recursive: true, force: true });
  }
});

test('cleanup and version marker roll back on invalid historical JSON without losing pending data', () => {
  const fixture = migrationFixture();
  let raw: Database.Database | undefined;
  try {
    fixture.raw.prepare("UPDATE player_library_submissions SET payload_json='invalid-json'").run();
    fixture.raw.close();
    expect(() => openDatabase(fixture.path).close()).toThrow(/migration.*invalid_json/);
    raw = new Database(fixture.path);
    expect(raw.prepare('SELECT * FROM player_identifiers').all()).toHaveLength(3);
    expect(raw.prepare('SELECT revision FROM player_library_meta').get()).toEqual({ revision: 42 });
    expect(raw.prepare('SELECT payload_json,status FROM player_library_submissions').get()).toEqual({ payload_json: 'invalid-json', status: 'pending' });
    expect(raw.prepare("SELECT name FROM sqlite_master WHERE name='server_schema_migrations'").get()).toBeUndefined();
  } finally {
    if (fixture.raw.open) fixture.raw.close();
    if (raw?.open) raw.close();
    rmSync(fixture.directory, { recursive: true, force: true });
  }
});
