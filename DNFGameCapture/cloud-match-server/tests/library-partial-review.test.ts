import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createCloudMatchAdminApp } from '../src/admin.js';
import { openDatabase } from '../src/db.js';
import { canonicalEntityRedirects, listPlayerLibrary, MAX_ENTITY_REDIRECTS } from '../src/library-store.js';
import { mergePublicLibrary, mutatePublicLibrary } from '../src/library-admin-data.js';

const databases: ReturnType<typeof openDatabase>[] = [];
const entity = (entityId: string, names = [entityId], gameIds: string[] = []) =>
  ({ entityId, names, gameIds });
function fixture() {
  const db = openDatabase(':memory:'); databases.push(db);
  const app = createCloudMatchAdminApp({ db, now: () => 100, csrfToken: 'csrf', adminPassword: 'password',
    socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false,
      stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
  const post = (url: string, body: object) => request(app).post('/admin/api/library' + url)
    .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send(body);
  const get = (url: string) => request(app).get('/admin/api/library' + url).auth('admin', 'password');
  const pending = (entities: unknown[]) => {
    const payload = JSON.stringify({ entities }, null, 2);
    const id = Number(db.prepare("INSERT INTO player_library_submissions(device_id,payload_json,status,created_at) VALUES('fixture',?,'pending',99)")
      .run(payload).lastInsertRowid);
    return { id, payload };
  };
  const ref = async (id: number) => ({ id, submissionRevision: (await get('/submissions/' + id)).body.submission.submissionRevision });
  const create = async (...entities: ReturnType<typeof entity>[]) => {
    for (const value of entities) await post('/entities', { revision: listPlayerLibrary(db).revision, entity: value }).expect(201);
  };
  const merge = (targetEntityId: string, entityIds: string[], revision = listPlayerLibrary(db).revision) =>
    post('/entities/merge', { revision, targetEntityId, entityIds, confirm: true });
  return { db, app, post, get, pending, ref, create, merge };
}
afterEach(() => databases.splice(0).forEach(db => db.close()));

describe('partial review and public identity redirects', () => {
  test('strict stays atomic; partial skips a whole entity and preserves the byte-exact original through retries', async () => {
    const { db, post, get, pending, ref, create } = fixture();
    await create(entity('public', ['Zhuang', 'Lao'], ['one']), entity('da', ['Da'], ['two']));
    const blocked = entity('public', ['Zhuang', 'Da', 'Do not publish'], ['one', 'two', 'new-blocked']);
    const input = pending([entity('safe', ['Safe'], ['new-safe', 'one', 'two']), blocked]);
    const refs = [await ref(input.id)];
    await post('/review', { revision: 2, action: 'approve', submissions: refs }).expect(409);
    const result = await post('/review', { revision: 2, action: 'approve', submissions: refs, skipConflicts: true }).expect(200);
    expect(result.body).toMatchObject({ revision: 3, acceptedEntityCount: 1, skippedEntityCount: 1, pendingSubmissionCount: 1 });
    expect(result.body.skippedEntities[0]).toMatchObject({ submissionId: input.id, entityId: 'public' });
    expect(result.body.conflicts).toEqual([expect.objectContaining({ kind: 'names', value: 'Da' })]);
    expect(listPlayerLibrary(db).entities.find(value => value.entityId === 'safe')?.gameIds).toEqual(['new-safe', 'one', 'two']);
    expect(listPlayerLibrary(db).entities.find(value => value.entityId === 'public')?.gameIds).toEqual(['one']);
    expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(input.id)).toEqual({ payload_json: input.payload });
    const remaining = (await get('/submissions/' + input.id)).body.submission;
    expect(remaining).toMatchObject({ status: 'pending', reviewReason: 'admin_partially_approved', rawEntityCount: 1 });
    expect(remaining.entities).toEqual([blocked]);
    expect(result.body.pendingSubmissions[0].submissionRevision).toBe(remaining.submissionRevision);
    await post('/review', { revision: 3, action: 'approve', submissions: refs, skipConflicts: true }).expect(409);
    const again = await post('/review', { revision: 3, action: 'approve', submissions: [await ref(input.id)], skipConflicts: true }).expect(200);
    expect(again.body).toMatchObject({ revision: 3, acceptedEntityCount: 0, skippedEntityCount: 1 });
    expect((await get('/submissions/' + input.id)).body.submission.status).toBe('pending');
    expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(input.id)).toEqual({ payload_json: input.payload });
  });

  test.each([false, true])('combined batch skips every implicated incoming target regardless of order: %s', async reverse => {
    const { db, post, pending, ref } = fixture();
    const a = pending([entity('a', ['A', 'Same name'], ['shared', 'unique-a']), entity('safe-a')]);
    const b = pending([entity('b', ['B', 'Same name'], ['shared']), entity('safe-b')]);
    const c = pending([entity('a', ['Another A'], ['otherwise-clean'])]);
    const refs = await Promise.all([a, b, c].map(item => ref(item.id)));
    const result = await post('/review', { revision: 0, action: 'approve', submissions: reverse ? refs.reverse() : refs, skipConflicts: true }).expect(200);
    expect(result.body).toMatchObject({ revision: 1, acceptedEntityCount: 2, skippedEntityCount: 3, pendingSubmissionCount: 3 });
    expect(listPlayerLibrary(db).entities.map(value => value.entityId)).toEqual(['safe-a', 'safe-b']);
    expect(result.body.conflicts).toHaveLength(1);
    expect(result.body.conflicts[0].kind).toBe('names');
  });

  test('merge requires confirmation, selection, revision, auth and CSRF; unions disjoint fields and audits prior content', async () => {
    const { db, app, post, create, merge } = fixture();
    const first = entity('zhuang', ['Zhuang', 'Lao'], ['one']);
    const second = entity('da', ['Da'], ['two']);
    await create(first, second);
    const body = { revision: 2, targetEntityId: 'zhuang', entityIds: ['zhuang', 'da'], confirm: true };
    await request(app).post('/admin/api/library/entities/merge').send(body).expect(401);
    await request(app).post('/admin/api/library/entities/merge').auth('admin', 'password').send(body).expect(403);
    await post('/entities/merge', { ...body, confirm: false }).expect(400);
    await post('/entities/merge', { ...body, targetEntityId: 'missing' }).expect(400);
    await post('/entities/merge', { ...body, entityIds: ['zhuang', 'zhuang'] }).expect(400);
    await merge('zhuang', ['zhuang', 'da'], 1).expect(409);
    await merge('zhuang', ['zhuang', 'missing']).expect(404);
    await merge('zhuang', ['zhuang', 'da']).expect(200);
    expect(listPlayerLibrary(db)).toEqual({ revision: 3,
      entities: [entity('zhuang', ['Da', 'Lao', 'Zhuang'], ['one', 'two'])],
      entityRedirects: [{ fromEntityId: 'da', toEntityId: 'zhuang' }] });
    const audit = db.prepare('SELECT * FROM player_library_merge_audit').get() as { before_json: string; redirects_json: string; revision: number };
    expect(JSON.parse(audit.before_json)).toEqual(expect.arrayContaining([second, { ...first, names: ['Lao', 'Zhuang'] }]));
    expect(JSON.parse(audit.redirects_json)).toEqual([{ fromEntityId: 'da', toEntityId: 'zhuang' }]);
    expect(audit.revision).toBe(3);
  });

  test('chains flatten and old known IDs with new data resolve; retired IDs cannot be recreated or orphaned', async () => {
    const { db, app, post, get, create, merge, pending, ref } = fixture();
    await create(entity('a'), entity('b'), entity('c'));
    const submitted = pending([entity('a', ['new alias'], ['brand-new-game'])]);
    await merge('b', ['a', 'b']).expect(200);
    await merge('c', ['b', 'c']).expect(200);
    const redirects = [{ fromEntityId: 'a', toEntityId: 'c' }, { fromEntityId: 'b', toEntityId: 'c' }];
    expect(listPlayerLibrary(db).entityRedirects).toEqual(redirects);
    const audit = db.prepare('SELECT * FROM player_library_merge_audit ORDER BY id DESC LIMIT 1').get() as { before_redirects_json: string };
    expect(audit.before_redirects_json).toBe(JSON.stringify([{ fromEntityId: 'a', toEntityId: 'b' }]));
    const detail = (await get('/submissions/' + submitted.id)).body.submission;
    expect(detail.entities[0].entityId).toBe('c');
    expect(detail.reconciliations[0].reason).toBe('entity_redirect');
    await post('/review', { revision: 5, action: 'approve', submissions: [await ref(submitted.id)] }).expect(200);
    expect(listPlayerLibrary(db).entities[0]).toEqual(entity('c', ['a', 'b', 'c', 'new alias'], ['brand-new-game']));
    await post('/entities', { revision: 6, entity: entity('a', ['Fresh']) }).expect(409);
    await post('/import', { revision: 6, text: JSON.stringify({ entities: [entity('b', ['Fresh'])] }) }).expect(409);
    await request(app).delete('/admin/api/library/entities/c').auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send({ revision: 6 }).expect(409);
    expect(listPlayerLibrary(db).entityRedirects).toEqual(redirects);
    expect(() => db.prepare('INSERT INTO player_entities VALUES (?,1,1)').run('a')).toThrow();
  });

  test('after an explicit merge, the skipped original can be approved without losing its new fields', async () => {
    const { db, post, pending, ref, create, merge } = fixture();
    await create(entity('chou', ['Chou'], ['one']), entity('shuai', ['Shuai'], ['two']));
    const input = pending([entity('chou', ['Chou', 'Shuai'], ['one', 'two', 'three']), entity('safe')]);
    await post('/review', { revision: 2, action: 'approve', submissions: [await ref(input.id)], skipConflicts: true }).expect(200);
    await merge('shuai', ['chou', 'shuai']).expect(200);
    const result = await post('/review', { revision: 4, action: 'approve', submissions: [await ref(input.id)], skipConflicts: true }).expect(200);
    expect(result.body).toMatchObject({ revision: 5, acceptedEntityCount: 1, skippedEntityCount: 0, pendingSubmissionCount: 0 });
    expect(listPlayerLibrary(db).entities.find(value => value.entityId === 'shuai')?.gameIds).toEqual(['one', 'three', 'two']);
    expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(input.id)).toEqual({ payload_json: input.payload });
  });

  test('partial mode still aborts invalid payloads and rolls back publication, originals and pending edits on failure', async () => {
    const { db, post, pending, ref, create } = fixture();
    await create(entity('owner'));
    const input = pending([entity('blocked', ['owner']), entity('safe')]);
    const invalid = pending([entity('bad', [])]);
    await post('/review', { revision: 1, action: 'approve', submissions: [await ref(input.id), await ref(invalid.id)], skipConflicts: true }).expect(400);
    db.exec("CREATE TRIGGER fail_partial BEFORE UPDATE ON player_library_submissions BEGIN SELECT RAISE(ABORT,'fixture failure'); END");
    await post('/review', { revision: 1, action: 'approve', submissions: [await ref(input.id)], skipConflicts: true }).expect(500);
    expect(listPlayerLibrary(db).entities).toEqual([entity('owner')]);
    expect(listPlayerLibrary(db).revision).toBe(1);
    expect(db.prepare('SELECT * FROM player_library_submission_originals').all()).toEqual([]);
    expect(db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(input.id)).toEqual({ payload_json: input.payload });
  });

  test('merge audit failure rolls back content, revision and every redirect', async () => {
    const { db, create, merge } = fixture();
    await create(entity('a'), entity('b'), entity('c'));
    await merge('b', ['a', 'b']).expect(200);
    const before = listPlayerLibrary(db);
    db.exec("CREATE TRIGGER fail_merge BEFORE INSERT ON player_library_merge_audit BEGIN SELECT RAISE(ABORT,'fixture failure'); END");
    await merge('c', ['b', 'c']).expect(500);
    expect(listPlayerLibrary(db)).toEqual(before);
  });

  test('redirect validation rejects cycles, duplicates, missing targets, live sources and oversized maps', () => {
    const live = [entity('live')];
    for (const redirects of [
      [{ fromEntityId: 'a', toEntityId: 'a' }],
      [{ fromEntityId: 'a', toEntityId: 'b' }, { fromEntityId: 'b', toEntityId: 'a' }],
      [{ fromEntityId: 'a', toEntityId: 'missing' }],
      [{ fromEntityId: 'live', toEntityId: 'live' }],
      [{ fromEntityId: 'a', toEntityId: 'live' }, { fromEntityId: 'a', toEntityId: 'live' }],
      [{ fromEntityId: 'bad id', toEntityId: 'live' }],
      Array.from({ length: MAX_ENTITY_REDIRECTS + 1 }, (_, i) => ({ fromEntityId: `old-${i}`, toEntityId: 'live' })),
    ]) expect(() => canonicalEntityRedirects(live, redirects)).toThrow('invalid_entity_redirects');
    const chain = Array.from({ length: MAX_ENTITY_REDIRECTS }, (_, i) => ({ fromEntityId: `old-${i}`, toEntityId: i === MAX_ENTITY_REDIRECTS - 1 ? 'live' : `old-${i + 1}` }));
    expect(canonicalEntityRedirects(live, chain)).toHaveLength(MAX_ENTITY_REDIRECTS);
    expect(canonicalEntityRedirects(live, chain).every(item => item.toEntityId === 'live')).toBe(true);
  });

  test('bounds total public bytes including redirects and rolls back a merge that would exceed them', async () => {
    const { db, create, merge } = fixture();
    await create(entity('a'), entity('b'));
    // Compact old targets fit; retargeting all aliases to a long canonical ID exceeds the wire limit.
    const insert = db.prepare('INSERT INTO player_entity_redirects VALUES(?,?)');
    db.transaction(() => { for (let i = 0; i < 3000; i++) insert.run(`retired-${i}`, 'a'); })();
    const longId = 'target-' + 'x'.repeat(120);
    await create(entity(longId, ['Long target']));
    const before = listPlayerLibrary(db);
    expect(Buffer.byteLength(JSON.stringify(before))).toBeLessThan(262144);
    const result = await merge(longId, ['a', longId]).expect(413);
    expect(result.body.code).toBe('library_too_large');
    expect(listPlayerLibrary(db)).toEqual(before);
    expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toEqual([]);
  });

  test('redirects and full merge audit survive reopening an isolated fixture database', () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-redirect-fixture-'));
    const path = join(directory, 'fixture.sqlite'); let db: ReturnType<typeof openDatabase> | undefined;
    try {
      db = openDatabase(path);
      mutatePublicLibrary(db, 0, 100, 'import', [entity('a'), entity('b'), entity('c')]);
      mergePublicLibrary(db, 1, 101, 'b', ['a', 'b']);
      mergePublicLibrary(db, 2, 102, 'c', ['b', 'c']);
      const before = listPlayerLibrary(db);
      db.close(); db = undefined;
      db = openDatabase(path);
      expect(listPlayerLibrary(db)).toEqual(before);
      expect(db.prepare('SELECT * FROM player_library_merge_audit').all()).toHaveLength(2);
      expect(db.pragma('foreign_key_check')).toEqual([]);
    } finally { db?.close(); rmSync(directory, { recursive: true, force: true }); }
  });

  test('materializes missing source IDs before removing preceding rows and never replaces an earlier original audit', async () => {
    const { db, post, pending, ref, create, get } = fixture();
    await create(entity('owner'));
    const { entityId: _ignored, ...withoutId } = entity('local', ['owner']);
    const input = pending([entity('safe'), withoutId]);
    db.prepare('INSERT INTO player_library_submission_originals VALUES(?,?)').run(input.id, 'earlier-original');
    await post('/review', { revision: 1, action: 'approve', submissions: [await ref(input.id)], skipConflicts: true }).expect(200);
    const remaining = (await get('/submissions/' + input.id)).body.submission;
    expect(remaining.entities[0].entityId).toBe(`submission-${input.id}-1`);
    expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(input.id)).toEqual({ payload_json: 'earlier-original' });
  });
});
