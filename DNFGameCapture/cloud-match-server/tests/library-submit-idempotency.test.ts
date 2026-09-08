import express from 'express';
import request from 'supertest';
import { afterEach, expect, test } from 'vitest';
import { hashLicenseKey } from '../src/auth.js';
import { openDatabase } from '../src/db.js';
import { editAdminSubmission, mergePublicLibrary, mutatePublicLibrary, readAdminSubmission, reviewAdminSubmissions } from '../src/library-admin-data.js';
import { listPlayerLibrary } from '../src/library-store.js';
import { createV2Api } from '../src/v2-api.js';

const databases: ReturnType<typeof openDatabase>[] = [];
const now = 1_800_000_000;
const games = ['g1', 'g2', 'g3', 'g4', 'g5'];
const entity = (entityId: string, names = [entityId], gameIds: string[] = []) => ({ entityId, names, gameIds });
afterEach(() => databases.splice(0).forEach(db => db.close()));

async function fixture() {
  const db = openDatabase(':memory:'); databases.push(db);
  const app = express(); app.use(express.json());
  app.use('/api/v2', createV2Api({ db, now: () => now, serverUrl: 'http://localhost' }));
  const clients: Array<(entities: object[]) => request.Test> = [];
  for (const suffix of ['first', 'second']) {
    const key = `test-${suffix}`, deviceId = `device-${suffix}`;
    db.prepare('INSERT INTO licenses(key_hash,created_at,updated_at) VALUES(?,?,?)').run(hashLicenseKey(key), now, now);
    const activated = await request(app).post('/api/v2/auth/activate').send({ key, deviceId }).expect(200);
    clients.push(entities => request(app).post('/api/v2/player-library/submit')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`).set('X-DNF-Device-Id', deviceId).send({ entities }));
  }
  const revision = () => listPlayerLibrary(db).revision;
  const publish = (entities: ReturnType<typeof entity>[]) => mutatePublicLibrary(db, revision(), now, 'import', entities);
  const review = (id: number, action: 'approve' | 'reject' = 'approve') => {
    const pending = readAdminSubmission(db, id);
    return reviewAdminSubmissions(db, revision(), now, action, [{ id, submissionRevision: pending.submissionRevision }]);
  };
  const rows = () => db.prepare('SELECT * FROM player_library_submissions ORDER BY id').all();
  const state = () => ['player_entities', 'player_entity_names', 'player_identifiers', 'player_identifier_spellings',
    'player_entity_identifiers', 'player_entity_redirects', 'player_entity_auto_evidence', 'player_library_merge_audit',
    'player_library_meta', 'player_library_submissions'].map(table => db.prepare(`SELECT * FROM ${table}`).all());
  return { app, db, submit: clients[0], otherSubmit: clients[1], publish, review, rows, state, revision };
}

test('unchanged additive contributions return no_changes without touching library, evidence or submissions', async () => {
  const f = await fixture();
  const original = [entity('a', ['Alpha', 'Alias'], ['Game', 'Other']), entity('b', ['Beta'], [])];
  const submitted = await f.submit(original).expect(202);
  f.review(submitted.body.submissionId);
  const before = f.state();
  const reordered = [entity('b', ['Beta']), entity('a', [' ALIAS ', 'alpha', 'alpha'], ['OTHER', 'game', 'game'])];
  const result = await f.submit([...reordered, reordered[0]]).expect(200);
  expect(result.body).toEqual({ ok: true, status: 'no_changes', revision: 1, identifierConflictCount: 0, ownershipConflictCount: 0 });
  await f.submit([entity('a', ['Alpha'], [])]).expect(200);
  await f.submit([]).expect(200);
  expect(f.state()).toEqual(before);
});

test('pending retries ignore ordering, repeated values and identical rows and reuse the original submission', async () => {
  const f = await fixture();
  const input = [entity('a', ['Caf\u00e9', 'Alias'], ['Game', 'Other']), entity('b', ['Beta'])];
  const first = await f.submit(input).expect(202);
  const before = f.state();
  const repeated = [input[1], entity('a', ['alias', ' CAFE\u0301 ', 'Alias'], ['other', 'GAME', 'GAME']), input[1]];
  const result = await f.submit(repeated).expect(200);
  expect(result.body).toEqual({ ok: true, status: 'already_pending', submissionId: first.body.submissionId,
    revision: 0, identifierConflictCount: 0, ownershipConflictCount: 0 });
  expect(f.state()).toEqual(before);
});

test('anonymous entity retries remain idempotent before and after approval without flattening their groups', async () => {
  const f = await fixture();
  const anonymous = [{ names: ['Alpha', 'Alias'], gameIds: ['Game'] }, { names: ['Beta'], gameIds: [] }];
  const first = await f.submit(anonymous).expect(202);
  const repeat = [{ names: ['Beta', 'beta'], gameIds: [] }, { names: ['alias', 'ALPHA'], gameIds: [' game ', 'Game'] }];
  expect((await f.submit(repeat).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  f.review(first.body.submissionId);
  const before = f.state();
  expect((await f.submit(repeat).expect(200)).body.status).toBe('no_changes');
  expect(f.state()).toEqual(before);
});

test('new names, games and name-only entities still create reviewable submissions', async () => {
  const f = await fixture(); f.publish([entity('a', ['Alpha'], ['Game'])]);
  for (const row of [entity('a', ['Alpha', 'New alias'], ['Game']), entity('a', ['Alpha'], ['Game', 'New game']), entity('b', ['Beta'])]) {
    expect((await f.submit([row]).expect(202)).body).toMatchObject({ ok: true, status: 'pending_review', revision: 1 });
  }
  expect(f.rows()).toHaveLength(3);
  expect(f.revision()).toBe(1);
});

test('pending deduplication is device-scoped and rejected or edited drafts cannot suppress fresh contributions', async () => {
  const f = await fixture(); const input = [entity('a', ['Alpha'], ['Game'])];
  const first = await f.submit(input).expect(202);
  await f.otherSubmit(input).expect(202);
  f.review(first.body.submissionId, 'reject');
  const fresh = await f.submit(input).expect(202);
  const pending = readAdminSubmission(f.db, fresh.body.submissionId);
  editAdminSubmission(f.db, 0, pending.id, pending.submissionRevision, [entity('a', ['Alpha'], ['Edited'])]);
  await f.submit(input).expect(202);
  expect(f.rows()).toHaveLength(4);
});

test('authentication remains mandatory before no-change and pending-duplicate shortcuts', async () => {
  const f = await fixture(); f.publish([entity('public')]);
  await f.submit([entity('pending')]).expect(202);
  const before = f.state();
  for (const entities of [[], [entity('public')], [entity('pending')]]) {
    await request(f.app).post('/api/v2/player-library/submit').send({ entities }).expect(401);
  }
  f.db.prepare("UPDATE licenses SET disabled_at=? WHERE bound_device_id='device-first'").run(now);
  await f.submit([]).expect(401);
  await f.submit([entity('public')]).expect(401);
  await f.submit([entity('pending')]).expect(401);
  expect(f.state()).toEqual(before);
});

test('partial approval reuses remaining pending content when the original also includes unchanged public rows', async () => {
  const f = await fixture(); f.publish([entity('owner', ['Taken'])]);
  const blocked = entity('blocked', ['Taken'], ['new-game']);
  const original = [blocked, entity('safe', ['Safe'])];
  const submitted = await f.submit(original).expect(202);
  const pending = readAdminSubmission(f.db, submitted.body.submissionId);
  const result = reviewAdminSubmissions(f.db, 1, now, 'approve', [{ id: pending.id, submissionRevision: pending.submissionRevision }], true);
  expect(result).toMatchObject({ acceptedEntityCount: 1, skippedEntityCount: 1, revision: 2 });
  expect(readAdminSubmission(f.db, pending.id).entities).toEqual([blocked]);
  expect((await f.submit([blocked]).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: pending.id });
  const before = f.state();
  expect((await f.submit(original).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: pending.id });
  expect(f.state()).toEqual(before);
  await f.submit([blocked, entity('safe', ['Safe', 'New name'])]).expect(202);
  expect(f.rows()).toHaveLength(2);
  expect(f.revision()).toBe(2);
});

test('pending drafts containing newly public no-op rows match the smaller contribution after a pull', async () => {
  const f = await fixture();
  const changed = entity('changed', ['New player'], ['new-game']);
  const safe = entity('safe', ['Safe', 'Alias'], ['known-game']);
  const first = await f.submit([changed, safe]).expect(202);
  f.publish([safe]);
  const before = f.state();
  for (const input of [[changed], [entity('safe', ['Alias'], []), changed],
    [changed, entity('safe', ['SAFE', 'alias'], ['KNOWN-GAME', 'known-game'])]]) {
    expect((await f.submit(input).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  }
  expect(f.state()).toEqual(before);
  expect(readAdminSubmission(f.db, first.body.submissionId).entities).toEqual([changed, safe]);
});

test('redirected known no-op rows are ignored but unknown identities with the same values are not', async () => {
  const f = await fixture();
  f.publish([entity('old', ['Old'], ['shared']), entity('live', ['Live'])]);
  mergePublicLibrary(f.db, 1, now, 'live', ['old', 'live']);
  const changed = entity('changed', ['New player'], ['new-game']);
  const first = await f.submit([changed, entity('old', ['Old'], ['shared'])]).expect(202);
  expect((await f.submit([changed]).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  expect((await f.submit([changed, entity('live', ['Live'])]).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  const unknown = await f.submit([changed, entity('unknown', ['Old'], ['shared'])]).expect(202);
  expect(unknown.body.ownershipConflictCount).toBe(1);
  expect(f.rows()).toHaveLength(2);
});

test('no-op rows remain significant when partial publication can trigger identity and evidence changes', async () => {
  const f = await fixture();
  f.publish([entity('a', ['Alpha'], games), entity('b', ['Beta'], games.slice(0, 4)),
    entity('c', ['Third'], games.slice(4)), entity('d', ['No-op'])]);
  mergePublicLibrary(f.db, 1, now, 'b', ['b', 'c']);
  const blocked = entity('blocked', ['Alpha'], ['unrelated']);
  const first = await f.submit([blocked]).expect(202);
  const original = readAdminSubmission(f.db, first.body.submissionId);
  expect(original.automaticGroups).toEqual([['a', 'b']]);
  expect(reviewAdminSubmissions(f.db, 2, now, 'approve', [{ id: original.id, submissionRevision: original.submissionRevision }], true))
    .toMatchObject({ acceptedEntityCount: 0, skippedEntityCount: 1, revision: 2 });
  const withNoop = await f.submit([blocked, entity('d', ['No-op'])]).expect(202);
  const next = readAdminSubmission(f.db, withNoop.body.submissionId);
  expect(reviewAdminSubmissions(f.db, 2, now, 'approve', [{ id: next.id, submissionRevision: next.submissionRevision }], true))
    .toMatchObject({ acceptedEntityCount: 1, skippedEntityCount: 1, revision: 3 });
  expect(listPlayerLibrary(f.db).entityRedirects).toContainEqual({ fromEntityId: 'b', toEntityId: 'a' });
});

test('durable redirects authorize no-change detection and deduplicate the same pending contribution', async () => {
  const f = await fixture();
  f.publish([entity('old', ['Old'], ['Game']), entity('live', ['Live'])]);
  mergePublicLibrary(f.db, 1, now, 'live', ['old', 'live']);
  expect((await f.submit([entity('old', ['Old'], ['GAME'])]).expect(200)).body).toMatchObject({ status: 'no_changes', revision: 2 });
  const first = await f.submit([entity('old', ['New'], ['New game'])]).expect(202);
  expect((await f.submit([entity('live', ['New'], ['New game'])]).expect(200)).body)
    .toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  f.review(first.body.submissionId);
  const before = f.state();
  expect((await f.submit([entity('old', ['New'], ['New game'])]).expect(200)).body.status).toBe('no_changes');
  expect(f.state()).toEqual(before);
});

test('an unknown explicit identity with identical public values still creates redirect and evidence changes', async () => {
  const f = await fixture(); f.publish([entity('public', ['Alpha'], games)]);
  const submitted = await f.submit([entity('local', ['Alpha'], games)]).expect(202);
  const pending = readAdminSubmission(f.db, submitted.body.submissionId);
  expect(pending.automaticGroups).toEqual([['public', 'local']]);
  expect(pending.conflicts).toEqual([]);
  f.review(pending.id);
  expect(listPlayerLibrary(f.db).entityRedirects).toEqual([{ fromEntityId: 'local', toEntityId: 'public' }]);
  expect(f.revision()).toBe(2);
  const before = f.state();
  expect((await f.submit([entity('local', ['Alpha'], games)]).expect(200)).body.status).toBe('no_changes');
  expect(f.state()).toEqual(before);
});

test('new group boundaries are never treated as pending duplicates even when flattened values match', async () => {
  const f = await fixture();
  await f.submit([entity('a', ['Alpha'], ['one']), entity('b', ['Beta'], ['two'])]).expect(202);
  await f.submit([entity('a', ['Alpha', 'Beta'], ['one', 'two'])]).expect(202);
  await f.submit([entity('a', ['Alpha'], ['two']), entity('b', ['Beta'], ['one'])]).expect(202);
  await f.submit([entity('new-a', ['Alpha'], ['one']), entity('new-b', ['Beta'], ['two'])]).expect(202);
  expect(f.rows()).toHaveLength(4);
});

test('regrouping anonymous public aliases still requires review and preserves new original proof', async () => {
  const f = await fixture();
  const first = await f.submit([{ names: ['Alpha'], gameIds: games }, { names: ['Beta'], gameIds: games }]).expect(202);
  f.review(first.body.submissionId);
  const before = f.revision();
  const regrouped = await f.submit([{ names: ['Alpha', 'Beta'], gameIds: games }]).expect(202);
  expect(readAdminSubmission(f.db, regrouped.body.submissionId).automaticGroups).toHaveLength(1);
  f.review(regrouped.body.submissionId);
  expect(f.revision()).toBe(before + 1);
});

test('flattened public unions cannot suppress conflicts against incompatible original identity evidence', async () => {
  const f = await fixture(); const right = ['g6', 'g7', 'g8', 'g9', 'g10'];
  f.publish([entity('a', ['Alpha'], games), entity('b', ['Beta'], [...games, ...right])]);
  const before = f.state();
  const submitted = await f.submit([entity('c', ['Alpha'], right)]).expect(202);
  expect(submitted.body).toMatchObject({ status: 'pending_review', ownershipConflictCount: 1 });
  expect(readAdminSubmission(f.db, submitted.body.submissionId).automaticGroups).toEqual([]);
  expect(f.state().slice(0, -1)).toEqual(before.slice(0, -1));
  expect((await f.submit([entity('c', ['Alpha'], right)]).expect(200)).body)
    .toMatchObject({ status: 'already_pending', ownershipConflictCount: 1 });
});

test('unchanged known input still enters review when publication would change existing identity groups', async () => {
  const f = await fixture();
  f.publish([entity('a', ['Alpha'], games), entity('b', ['Beta'], games.slice(0, 4)), entity('c', ['Third'], games.slice(4))]);
  mergePublicLibrary(f.db, 1, now, 'b', ['b', 'c']);
  expect(listPlayerLibrary(f.db).entities).toHaveLength(2);
  const before = f.state();
  expect((await f.submit([]).expect(200)).body.status).toBe('no_changes');
  expect(f.state()).toEqual(before);
  const submitted = await f.submit([entity('a', ['Alpha'], games)]).expect(202);
  expect(readAdminSubmission(f.db, submitted.body.submissionId).automaticGroups).toEqual([['a', 'b']]);
  f.review(submitted.body.submissionId);
  expect(f.revision()).toBe(3);
  expect(listPlayerLibrary(f.db).entities).toHaveLength(1);
});

test('invalid legacy pending payloads do not block valid submissions or count as duplicates', async () => {
  const f = await fixture();
  for (const payload of ['not-json', JSON.stringify({ entities: [{ entityId: 'a', names: [], gameIds: [] }] })]) {
    f.db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('device-first',?,'pending',?)").run(payload, now);
  }
  const first = await f.submit([entity('a')]).expect(202);
  expect((await f.submit([entity('a')]).expect(200)).body).toMatchObject({ status: 'already_pending', submissionId: first.body.submissionId });
  expect(f.rows()).toHaveLength(3);
});

test('concurrent same-device retries create exactly one pending row', async () => {
  const f = await fixture(); const input = [entity('a', ['Alpha'], games)];
  const first = f.submit(input), second = f.submit(input);
  const responses = await Promise.all([first, second]);
  expect(responses.map(response => response.status).sort()).toEqual([200, 202]);
  expect(new Set(responses.map(response => response.body.submissionId)).size).toBe(1);
  expect(f.rows()).toHaveLength(1);
});
