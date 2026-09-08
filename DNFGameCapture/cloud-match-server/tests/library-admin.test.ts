import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';
import { createCloudMatchAdminApp } from '../src/admin.js';
import { openDatabase } from '../src/db.js';
import { approvePlayerLibrarySubmission, createPlayerLibraryEntity } from '../src/v2-api.js';

const databases: ReturnType<typeof openDatabase>[] = [];
const entity = (entityId = 'alpha', names = ['Alpha', 'Alias'], gameIds = ['Game']) =>
  ({ entityId, names, gameIds });
function fixture() {
  const db = openDatabase(':memory:');
  databases.push(db);
  const app = createCloudMatchAdminApp({ db, now: () => 1700000000, csrfToken: 'csrf', adminPassword: 'password',
    socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false,
      stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
  const get = (url: string) => request(app).get('/admin/api/library' + url).auth('admin', 'password');
  const post = (url: string, body: object) => request(app).post('/admin/api/library' + url)
    .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send(body);
  const put = (url: string, body: object) => request(app).put('/admin/api/library' + url)
    .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send(body);
  const pending = (entities: unknown[]) => Number(db.prepare(`INSERT INTO player_library_submissions
    (device_id,payload_json,status,created_at) VALUES ('fixture-device',?,'pending',1700000000)`)
    .run(JSON.stringify({ entities })).lastInsertRowid);
  return { db, app, get, post, put, pending };
}
afterEach(() => databases.splice(0).forEach(db => db.close()));

describe('normalized library administration', () => {
  test('edits large normalized identities without truncation and still bounds entity arrays', async () => {
    const { post, put, get } = fixture();
    const large = entity('large', Array.from({ length: 40 }, (_, i) => `Name${i}`),
      Array.from({ length: 300 }, (_, i) => `Game${i}`));
    await post('/entities', { revision: 0, entity: large }).expect(201);
    const read = (await get('/entities/large')).body;
    expect(read.entity.gameIds).toHaveLength(300);
    expect(read.entity).not.toHaveProperty('adventureGroupIds');
    expect(read.entity.names).toHaveLength(40);
    await put('/entities/large', { revision: read.revision, entity: { ...large,
      names: Array.from({ length: 10001 }, (_, i) => `Name${i}`) } }).expect(400);
    expect((await get('/entities/large')).body).toEqual(read);
  });

  test('legacy approval rolls back publication when the final status write fails', async () => {
    const { get, pending, db } = fixture();
    const id = pending([entity()]);
    const detail = (await get('/submissions/' + id)).body;
    db.exec("CREATE TRIGGER fail_status BEFORE UPDATE OF status ON player_library_submissions WHEN NEW.status='approved' BEGIN SELECT RAISE(ABORT,'status failure'); END");
    const result = approvePlayerLibrarySubmission(db, id, 1700000000,
      { revision: detail.revision, submissionRevision: detail.submission.submissionRevision });
    expect(result.ok).toBe(false);
    const state = (await get('/state')).body;
    expect(state.entities).toEqual([]);
    expect(state.revision).toBe(0);
    expect(state.submissions[0].status).toBe('pending');
  });

  test('legacy review rejects missing guards and never approves or rejects unseen edited payloads', async () => {
    const { app, get, put, pending } = fixture();
    const id = pending([entity()]);
    const url = '/admin/api/player-library/submissions/' + id;
    const detail = (await request(app).get(url).auth('admin', 'password')).body.submission;
    const read = (await get('/submissions/' + id)).body;
    const guard = { revision: read.revision, submissionRevision: read.submission.submissionRevision };
    const write = (action: string, body: object) => request(app).post(url + '/' + action).auth('admin', 'password')
      .set('x-dnf-admin-csrf', 'csrf').send(body);
    await write('approve', {}).expect(400);
    await write('reject', {}).expect(400);
    expect(detail).toMatchObject(guard);
    await put('/submissions/' + id, { ...guard, entities: [entity('changed', ['Unseen payload'], [])] }).expect(200);
    await write('approve', guard).expect(409);
    await write('reject', guard).expect(409);
    expect((await get('/state')).body.entities).toEqual([]);
    expect((await get('/submissions/' + id)).body.submission.status).toBe('pending');
  });

  test('legacy review uses the same explicit name ownership policy as the new console', async () => {
    const { app, get, post, pending } = fixture();
    await post('/entities', { revision: 0, entity: entity() }).expect(201);
    const id = pending([entity('different-id', ['Alpha'], ['unrelated-game'])]);
    const detail = (await get('/submissions/' + id)).body;
    const url = '/admin/api/player-library/submissions/' + id;
    await request(app).post(url + '/approve').auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf')
      .send({ revision: detail.revision, submissionRevision: detail.submission.submissionRevision }).expect(409);
    expect((await get('/entities/alpha')).body.entity.gameIds).toEqual(['Game']);
    expect((await get('/submissions/' + id)).body.submission.status).toBe('pending');
    const legacy = (await request(app).get(url).auth('admin', 'password')).body.submission;
    expect(legacy.ownershipConflicts).toEqual(expect.arrayContaining([expect.objectContaining({ kind: 'names', value: 'Alpha' })]));
  });

  test('legacy review also rejects a stale public revision and direct creation cannot merge by name', async () => {
    const { app, get, post, pending, db } = fixture();
    const id = pending([entity('pending', ['Pending'], [])]);
    const viewed = (await get('/submissions/' + id)).body;
    createPlayerLibraryEntity(db, entity(), 1700000000, 0);
    expect(() => createPlayerLibraryEntity(db, entity('other', ['Alpha'], ['unrelated']), 1700000000, 1)).toThrow('ownership_conflict');
    expect((await get('/state')).body.revision).toBe(1);
    for (const action of ['approve', 'reject']) {
      await request(app).post('/admin/api/player-library/submissions/' + id + '/' + action).auth('admin', 'password')
        .set('x-dnf-admin-csrf', 'csrf').send({ revision: viewed.revision, submissionRevision: viewed.submission.submissionRevision }).expect(409);
    }
    expect((await get('/submissions/' + id)).body.submission.status).toBe('pending');
  });

  test('authenticates page/assets/API and requires CSRF on all writes', async () => {
    const { app } = fixture();
    for (const url of ['/admin/library', '/admin/library/app.js', '/admin/library/style.css', '/admin/api/library/state']) {
      await request(app).get(url).expect(401);
      await request(app).get(url).auth('admin', 'password').expect(200);
    }
    const page = await request(app).get('/admin/library').auth('admin', 'password');
    expect(page.text).toContain('DNF 共享库管理台');
    expect(page.headers['content-security-policy']).toContain("script-src 'self'");
    const home = await request(app).get('/admin').auth('admin', 'password');
    expect(home.text).toContain('href="/admin/library"');
    for (const url of ['/entities', '/import', '/review']) {
      await request(app).post('/admin/api/library' + url).auth('admin', 'password').send({}).expect(403);
    }
    await request(app).put('/admin/api/library/entities/alpha').auth('admin', 'password').send({}).expect(403);
    await request(app).delete('/admin/api/library/entities/alpha').auth('admin', 'password').send({}).expect(403);
  });

  test('creates normalized entities, searches every field, replaces and deletes with revision guards', async () => {
    const { app, get, post, put, db } = fixture();
    const created = await post('/entities', { revision: 0, entity: entity() }).expect(201);
    expect(created.body.revision).toBe(1);
    for (const q of ['alias', 'GAME', 'alpha']) {
      expect((await get('/state?q=' + q)).body.entities).toHaveLength(1);
    }
    expect((await get('/state?q=absent')).body.entities).toHaveLength(0);
    expect((await get('/entities/alpha')).body.entity).toEqual({ ...entity(), names: ['Alias', 'Alpha'] });
    await put('/entities/alpha', { revision: 0, entity: entity() }).expect(409);
    await put('/entities/alpha', { revision: 1, entity: entity('alpha', [' Renamed ', 'renamed'], []) }).expect(200);
    expect((await get('/entities/alpha')).body.entity.names).toEqual(['Renamed']);
    expect(db.prepare('SELECT * FROM player_entity_identifiers').all()).toHaveLength(0);
    await request(app).delete('/admin/api/library/entities/alpha').auth('admin', 'password')
      .set('x-dnf-admin-csrf', 'csrf').send({ revision: 1 }).expect(409);
    await request(app).delete('/admin/api/library/entities/alpha').auth('admin', 'password')
      .set('x-dnf-admin-csrf', 'csrf').send({ revision: 2 }).expect(200);
    await get('/entities/alpha').expect(404);
    expect(db.prepare('SELECT * FROM player_entity_names').all()).toHaveLength(0);
  });

  test('shares game IDs but never reassigns owned names, including failed replacement', async () => {
    const { get, post, put } = fixture();
    await post('/entities', { revision: 0, entity: entity() }).expect(201);
    const conflict = await post('/entities', { revision: 1, entity: entity('beta', ['ALIAS'], []) }).expect(409);
    expect(conflict.body.code).toBe('ownership_conflict');
    expect(conflict.body.conflicts[0]).toMatchObject({ kind: 'names', entityIds: ['alpha', 'beta'] });
    await post('/entities', { revision: 1, entity: entity('beta', ['Beta'], ['game']) }).expect(201);
    await put('/entities/beta', { revision: 2, entity: entity('beta', ['ALPHA'], ['Game']) }).expect(409);
    expect((await get('/entities/beta')).body.entity.names).toEqual(['Beta']);
    expect((await get('/state')).body.revision).toBe(2);
    expect((await get('/entities/beta')).body.entity.gameIds).toEqual(['game']);
    expect((await get('/entities/alpha')).body.entity.gameIds).toEqual(['Game']);
  });

  test('imports legacy and v2 atomically without merging ambiguous owners or skipping malformed lines', async () => {
    const { post, get } = fixture();
    await post('/import', { revision: 0, text: 'Legacy=(One)(Two)\nSecond=（Three）' }).expect(200);
    expect((await get('/state')).body.entities).toHaveLength(2);
    await post('/import', { revision: 1, text: 'Valid=(Four)\nmalformed' }).expect(400);
    await post('/import', { revision: 1, text: 'Shared=(One)' }).expect(200);
    await post('/import', { revision: 2, text: JSON.stringify({ entities: [entity('conflict', ['Legacy'], ['Other'])] }) }).expect(409);
    const text = JSON.stringify({ entities: [entity('v2', ['Parallel', 'Other name'], ['v2-game'])] });
    const preview = await post('/import/preview', { revision: 2, text }).expect(200);
    expect(preview.body.entities[0].names).toHaveLength(2);
    expect((await get('/state')).body.revision).toBe(2);
    await post('/import', { revision: 2, text }).expect(200);
    await post('/import', { revision: 2, text }).expect(409);
    expect((await get('/state')).body.entities).toHaveLength(4);
  });

  test('bounds parsing and rejects empty or duplicated entity payloads', async () => {
    const { post, app } = fixture();
    for (const bad of [entity('a', [' ']), entity('a', ['A'], [' ']), entity('a', ['A'], Array(10001).fill('a'))]) {
      await post('/entities', { revision: 0, entity: bad }).expect(400);
    }
    await post('/entities', { entity: entity() }).expect(400);
    await post('/import', { revision: 0, text: JSON.stringify({ entities: [entity(), entity()] }) }).expect(400);
    await post('/import', { revision: 0, text: 'a'.repeat(262145) }).expect(413);
    await request(app).post('/admin/api/library/entities').auth('admin', 'password')
      .set('x-dnf-admin-csrf', 'csrf').set('Content-Type', 'application/json').send('{').expect(400);
    for (const text of ['A=(one) trailing', 'A=(one）', 'A=(one)(two', '{"entities":' + '['.repeat(17) + '0' + ']'.repeat(17) + '}']) {
      await post('/import', { revision: 0, text }).expect(400);
    }
  });

  test('details include public conflicts; explicit review edits use payload tokens before additive approval', async () => {
    const { get, post, put, pending } = fixture();
    await post('/entities', { revision: 0, entity: entity() }).expect(201);
    const id = pending([entity('local', ['Alpha', 'Local name'], ['Game'])]);
    const before = (await get('/submissions/' + id)).body.submission;
    expect(before.conflicts).toEqual([expect.objectContaining({ kind: 'names', value: 'Alpha' })]);
    const ref = { id, submissionRevision: before.submissionRevision };
    await post('/review', { revision: 1, action: 'approve', submissions: [ref] }).expect(409);
    expect((await get('/submissions/' + id)).body.submission.status).toBe('pending');
    await put('/submissions/' + id, { revision: 1, submissionRevision: before.submissionRevision,
      entities: [entity('alpha', ['Local name'], ['Game'])] }).expect(200);
    await post('/review', { revision: 1, action: 'approve', submissions: [ref] }).expect(409);
    const edited = (await get('/submissions/' + id)).body.submission;
    expect(edited.conflicts).toEqual([]);
    await post('/review', { revision: 1, action: 'approve', submissions: [{ id, submissionRevision: edited.submissionRevision }] }).expect(200);
    const publicEntity = (await get('/entities/alpha')).body.entity;
    expect(publicEntity.names).toEqual(expect.arrayContaining(['Alpha', 'Alias', 'Local name']));
    expect(publicEntity).not.toHaveProperty('adventureGroupIds');
  });

  test('batch approvals and rejections are all-or-nothing including cross-submission conflicts and stale status', async () => {
    const { get, post, pending } = fixture();
    const ids = [pending([entity('a', ['A'], ['shared'])]), pending([entity('b', ['A'], ['shared'])])];
    const refs = [];
    for (const id of ids) refs.push({ id, submissionRevision: (await get('/submissions/' + id)).body.submission.submissionRevision });
    await post('/review', { revision: 0, action: 'approve', submissions: refs }).expect(409);
    expect((await get('/state')).body.entities).toEqual([]);
    expect((await get('/state')).body.submissions).toHaveLength(2);
    await post('/review', { revision: 0, action: 'reject', submissions: [refs[0]] }).expect(200);
    await post('/review', { revision: 0, action: 'reject', submissions: [refs[1], refs[0]] }).expect(409);
    expect((await get('/submissions/' + ids[1])).body.submission.status).toBe('pending');
    await post('/review', { revision: 0, action: 'reject', submissions: [refs[1]] }).expect(200);
    const cleanIds = [pending([entity('c', ['C'], [])]), pending([entity('d', ['D'], [])])];
    const clean = [];
    for (const id of cleanIds) clean.push({ id, submissionRevision: (await get('/submissions/' + id)).body.submission.submissionRevision });
    await post('/review', { revision: 0, action: 'approve', submissions: clean }).expect(200);
    expect((await get('/state')).body.entities).toHaveLength(2);
    expect((await get('/state')).body.revision).toBe(1);
  });

  test('filters pending details by aliases, IDs, submitter and conflicts without writes', async () => {
    const { get, pending, db } = fixture();
    pending([entity('a', ['A', 'Search alias'], ['shared']), entity('b', ['A'], ['shared'])]);
    pending([entity('c', ['Clean'], [])]);
    for (const q of ['search alias', 'SHARED']) {
      expect((await get('/state?pendingQ=' + encodeURIComponent(q))).body.submissions).toHaveLength(1);
    }
    expect((await get('/state?pendingQ=fixture-device')).body.submissions).toHaveLength(2);
    expect((await get('/state?filter=conflict')).body.submissions).toHaveLength(1);
    expect((await get('/state?filter=clean')).body.submissions).toHaveLength(1);
    expect(db.prepare('SELECT revision FROM player_library_meta').get()).toEqual({ revision: 0 });
  });

  test('guards review edits and reject against stale public revisions, and allows rejecting corrupt records', async () => {
    const { post, put, get, pending, db } = fixture();
    const id = pending([entity()]);
    const submissionRevision = (await get('/submissions/' + id)).body.submission.submissionRevision;
    await post('/entities', { revision: 0, entity: entity('other', ['Other'], []) }).expect(201);
    await put('/submissions/' + id, { revision: 0, submissionRevision, entities: [entity()] }).expect(409);
    await post('/review', { revision: 0, action: 'reject', submissions: [{ id, submissionRevision }] }).expect(409);
    db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE id=?').run('['.repeat(20), id);
    const damaged = (await get('/submissions/' + id)).body.submission;
    expect(damaged.valid).toBe(false);
    await post('/review', { revision: 1, action: 'approve', submissions: [{ id, submissionRevision: damaged.submissionRevision }] }).expect(400);
    await post('/review', { revision: 1, action: 'reject', submissions: [{ id, submissionRevision: damaged.submissionRevision }] }).expect(200);
    await get('/submissions/1bad').expect(400);
    await get('/submissions/9999').expect(404);
  });

  test('rolls back the whole approval when a later database write fails', async () => {
    const { get, post, pending, db } = fixture();
    const id = pending([entity('first', ['First'], []), entity('second', ['Second'], [])]);
    const submissionRevision = (await get('/submissions/' + id)).body.submission.submissionRevision;
    db.exec("CREATE TRIGGER fail_second BEFORE INSERT ON player_entity_names WHEN NEW.entity_id='second' BEGIN SELECT RAISE(ABORT,'fixture failure'); END");
    await post('/review', { revision: 0, action: 'approve', submissions: [{ id, submissionRevision }] }).expect(500);
    const state = (await get('/state')).body;
    expect(state.revision).toBe(0);
    expect(state.entities).toEqual([]);
    expect(state.submissions[0].status).toBe('pending');
  });

  test('approves shared game IDs without requiring edits or merging entities', async () => {
    const { get, post, pending } = fixture();
    const id = pending([entity('a', ['A'], ['same']), entity('b', ['B'], ['SAME'])]);
    const original = (await get('/submissions/' + id)).body.submission;
    expect(original.conflicts).toEqual([]);
    await post('/review', { revision: 0, action: 'approve', submissions: [{ id, submissionRevision: original.submissionRevision }] }).expect(200);
    expect((await get('/state')).body.entities).toHaveLength(2);
    expect((await get('/entities/b')).body.entity).toEqual(entity('b', ['B'], ['SAME']));
  });

  test('rejects additive unions beyond per-entity limits without partial publication', async () => {
    const { get, post, pending } = fixture();
    await post('/entities', { revision: 0, entity: entity('a', Array.from({ length: 10000 }, (_, i) => 'Name' + i), []) }).expect(201);
    const id = pending([entity('a', ['One more name'], ['one more ID'])]);
    const submission = (await get('/submissions/' + id)).body.submission;
    await post('/review', { revision: 1, action: 'approve', submissions: [{ id, submissionRevision: submission.submissionRevision }] }).expect(400);
    expect((await get('/entities/a')).body.entity.gameIds).toEqual([]);
    expect((await get('/state')).body.revision).toBe(1);
    expect((await get('/submissions/' + id)).body.submission.status).toBe('pending');
  });

  test('import preview keeps names conflicting when only a saved automatic union overlaps the new player', async () => {
    const { post, get } = fixture();
    const left = ['g1', 'g2', 'g3', 'g4', 'g5'], right = ['g6', 'g7', 'g8', 'g9', 'g10'];
    await post('/entities', { revision: 0, entity: entity('a', ['A'], left) }).expect(201);
    await post('/entities', { revision: 1, entity: entity('b', ['B'], [...left, ...right]) }).expect(201);
    const before = (await get('/state')).body;
    const text = JSON.stringify({ entities: [entity('c', ['A'], right)] });
    const preview = await post('/import/preview', { revision: 2, text }).expect(200);
    expect(preview.body.conflicts).toEqual([{ kind: 'names', value: 'A', entityIds: ['a', 'c'] }]);
    await post('/import', { revision: 2, text }).expect(409);
    expect((await get('/state')).body).toEqual(before);
  });
});
