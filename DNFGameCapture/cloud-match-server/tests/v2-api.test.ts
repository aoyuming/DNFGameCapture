import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import express from 'express';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { hashLicenseKey } from '../src/auth.js';
import { createBroadcasterAttributionService } from '../src/broadcaster-attribution.js';
import { openDatabase } from '../src/db.js';
import { mergePublicLibrary, mutatePublicLibrary } from '../src/library-admin-data.js';
import {
  approvePlayerLibrarySubmission,
  createV2Api,
  getPlayerLibrarySubmission,
  listPlayerLibrary,
} from '../src/v2-api.js';

const resources: Array<{ directory: string; close(): void }> = [];
const now = 1_800_000_000;
function approveReviewed(db: ReturnType<typeof openDatabase>, id: number) {
  const detail = getPlayerLibrarySubmission(db, id)!;
  return approvePlayerLibrarySubmission(db, id, now, { revision: detail.revision, submissionRevision: detail.submissionRevision });
}

function createFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-v2-'));
  const db = openDatabase(join(directory, 'test.sqlite'));
  db.prepare(
    `INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
     VALUES (?, ?, ?, NULL, NULL, ?, ?)`,
  ).run(hashLicenseKey('CDK-TEST-ONE'), 'test card', now + 86_400, now, now);
  const attribution = createBroadcasterAttributionService(db, {
    resolveRegion: () => '中国 · 浙江 · 杭州',
  });
  const app = express();
  app.use(express.json({ limit: '128kb' }));
  app.use('/api/v2', createV2Api({
    db,
    now: () => now,
    serverUrl: 'http://127.0.0.1:28880',
    sessionTtlSeconds: 86_400,
    resolveClientIp: () => '47.1.2.3',
    attribution,
  }));
  resources.push({ directory, close: () => db.close() });
  return { app, db };
}

afterEach(() => {
  for (const resource of resources.splice(0)) {
    resource.close();
    rmSync(resource.directory, { recursive: true, force: true });
  }
});

describe('test-server v2 API', () => {
  test('records authorization activity with the server-resolved client IP', async () => {
    const { app, db } = createFixture();

    await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);

    expect(db.prepare(`SELECT license_device_id,ip_address,observed_at
      FROM license_ip_observations`).get()).toEqual({
      license_device_id: 'device-test-0001', ip_address: '47.1.2.3', observed_at: now,
    });
  });

  test('downloads full redirects and resolves retired upload IDs with new fields while retaining raw IDs for audit', async () => {
    const { app, db } = createFixture();
    const row = (entityId: string, names = [entityId], gameIds: string[] = []) => ({ entityId, names, gameIds });
    mutatePublicLibrary(db, 0, now, 'import', [row('old'), row('middle'), row('live')]);
    mergePublicLibrary(db, 1, now, 'middle', ['old', 'middle']);
    mergePublicLibrary(db, 2, now, 'live', ['middle', 'live']);
    const activated = await request(app).post('/api/v2/auth/activate').send({ key: 'CDK-TEST-ONE', deviceId: 'device-test-0001' }).expect(200);
    const auth = (value: request.Test) => value.set('Authorization', `Bearer ${activated.body.sessionToken}`).set('X-DNF-Device-Id', 'device-test-0001');
    const library = await auth(request(app).get('/api/v2/player-library')).expect(200);
    expect(library.body.entityRedirects).toEqual([{ fromEntityId: 'middle', toEntityId: 'live' }, { fromEntityId: 'old', toEntityId: 'live' }]);
    const uploaded = [row('old', ['new name'], ['new game']), row('middle', ['middle alias']), row('live', ['live alias'])];
    const submitted = await auth(request(app).post('/api/v2/player-library/submit')).send({ entities: uploaded }).expect(202);
    expect(submitted.body.ownershipConflictCount).toBe(0);
    const detail = getPlayerLibrarySubmission(db, submitted.body.submissionId)!;
    expect(detail.entities).toHaveLength(1);
    expect(detail.entities[0].entityId).toBe('live');
    const raw = db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(submitted.body.submissionId) as { payload_json: string };
    expect(JSON.parse(raw.payload_json).entities).toEqual(uploaded);
    expect(approveReviewed(db, submitted.body.submissionId)).toMatchObject({ ok: true, revision: 4 });
    const result = await auth(request(app).get('/api/v2/player-library')).expect(200);
    expect(result.body.entityRedirects).toEqual(library.body.entityRedirects);
    expect(result.body.entities).toEqual([row('live', ['live', 'live alias', 'middle', 'middle alias', 'new name', 'old'], ['new game'])]);
  });

  test('submission acknowledgements exclude equivalent public identities but retain genuine ownership conflicts', async () => {
    const { app, db } = createFixture();
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const submit = (entities: object[]) => request(app).post('/api/v2/player-library/submit')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-test-0001').send({ entities });
    const publicEntity = { entityId: 'public', names: ['One', 'Two'], gameIds: ['a', 'b', 'c', 'd', 'e'] };
    const initial = await submit([publicEntity]).expect(202);
    expect(approveReviewed(db, initial.body.submissionId)).toMatchObject({ ok: true });
    const repeated = await submit(publicEntity.names.map(name => ({ names: [name], gameIds: publicEntity.gameIds }))).expect(202);
    expect(repeated.body).toMatchObject({ identifierConflictCount: 0, ownershipConflictCount: 0 });
    const repeatedDetail = getPlayerLibrarySubmission(db, repeated.body.submissionId)!;
    expect(repeatedDetail.entities).toHaveLength(2);
    expect(repeatedDetail.automaticGroups).toHaveLength(1);
    expect(repeatedDetail.automaticGroups[0][0]).toBe('public');
    expect(repeatedDetail.automaticGroups[0]).toHaveLength(3);
    const shared = await submit([{ entityId: 'other', names: ['Different'], gameIds: ['a', 'different'] }]).expect(202);
    expect(shared.body).toMatchObject({ identifierConflictCount: 0, ownershipConflictCount: 0 });
    const conflicting = await submit([{ entityId: 'conflict', names: ['One'], gameIds: ['a', 'different'] }]).expect(202);
    expect(conflicting.body).toMatchObject({ ownershipConflictCount: 1 });
    expect(approveReviewed(db, conflicting.body.submissionId)).toEqual({ ok: false, code: 'name_conflict' });
    expect(listPlayerLibrary(db).revision).toBe(1);
  });

  test('preserves a normalized merged group larger than legacy per-name limits through review and download', async () => {
    const { app, db } = createFixture();
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const entity = {
      entityId: 'large-merged-group',
      names: Array.from({ length: 40 }, (_, i) => `Player${i}`),
      adventureGroupIds: { obsolete: ['never-a-game-id'] },
      gameIds: Array.from({ length: 300 }, (_, i) => `Game${i}`),
    };
    const submitted = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-test-0001').send({ entities: [entity] }).expect(202);
    expect(approveReviewed(db, submitted.body.submissionId)).toMatchObject({ ok: true });
    const result = await request(app).get('/api/v2/player-library')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-test-0001').expect(200);
    expect(result.body.entities[0].names).toHaveLength(40);
    expect(result.body.entities[0].gameIds).toHaveLength(300);
    expect(result.body.entities[0]).not.toHaveProperty('adventureGroupIds');
  });

  test('activates a synthetic license, returns the test endpoint, and binds the device', async () => {
    const { app } = createFixture();
    const response = await request(app)
      .post('/api/v2/auth/activate')
      .send({ key: ' cdk-test-one ', deviceId: 'device-test-0001', clientVersion: '5.2.0' })
      .expect(200);

    expect(response.body).toMatchObject({
      ok: true,
      cloudServerUrl: 'http://127.0.0.1:28880',
      licenseExpiresAt: 1_800_086_400,
      capabilities: expect.arrayContaining(['player_library_v2']),
    });
    expect(response.body.sessionToken).toEqual(expect.any(String));
  });

  test('marks a permanent license and advertises append-capable public-library reads', async () => {
    const { app, db } = createFixture();
    db.prepare(
      `INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
       VALUES (?, ?, NULL, NULL, NULL, ?, ?)`,
    ).run(hashLicenseKey('CDK-PERMANENT'), 'permanent test card', now, now);

    const activated = await request(app)
      .post('/api/v2/auth/activate')
      .send({ key: 'CDK-PERMANENT', deviceId: 'device-permanent-0001' })
      .expect(200);
    expect(activated.body.licenseExpiresAt).toBe(0xFFFFFFFF);

    const library = await request(app)
      .get('/api/v2/player-library')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-permanent-0001')
      .expect(200);
    expect(library.body).toMatchObject({
      ok: true,
      revision: 0,
      entities: [],
      aliasAppendSupported: true,
    });
  });

  test('rejects the same license on a different device', async () => {
    const { app } = createFixture();
    await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0002',
    }).expect(409, { ok: false, code: 'license_bound_to_other_device' });
  });

  test('keeps ordinary player-library submissions pending until admin approval', async () => {
    const { app, db } = createFixture();
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const authorization = `Bearer ${activated.body.sessionToken}`;
    const payload = {
      entities: [{
        names: ['老王', '王大枪'],
        gameIds: ['game-a', 'game-a'],
      }],
    };

    const submission = await request(app)
      .post('/api/v2/player-library/submit')
      .set('Authorization', authorization)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send(payload)
      .expect(202);
    expect(submission.body).toMatchObject({ ok: true, status: 'pending_review' });
    expect(listPlayerLibrary(db).entities).toHaveLength(0);

    const approved = approveReviewed(db, submission.body.submissionId);
    expect(approved).toMatchObject({ ok: true });
    const library = listPlayerLibrary(db);
    expect(library.entities[0]).toMatchObject({
      names: expect.arrayContaining(['老王', '王大枪']),
      gameIds: ['game-a'],
    });
  });

  test('approves partial overlap as two independent players sharing one game ID', async () => {
    const { app, db } = createFixture();
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const authorization = `Bearer ${activated.body.sessionToken}`;
    const first = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', authorization)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ names: ['甲'], gameIds: ['same-id'] }] })
      .expect(202);
    expect(approveReviewed(db, first.body.submissionId).ok).toBe(true);

    const second = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', authorization)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ names: ['乙'], gameIds: ['same-id', 'other-id'] }] })
      .expect(202);
    expect(approveReviewed(db, second.body.submissionId)).toEqual({
      ok: true, revision: 2,
    });
    expect(listPlayerLibrary(db).entities).toHaveLength(2);
    expect(listPlayerLibrary(db).entities.every(entity => entity.gameIds.includes('same-id'))).toBe(true);
    expect(listPlayerLibrary(db).entityRedirects).toEqual([]);
  });

  test.each(['gameIds'] as const)(
    'accepts overlapping local %s into review and publishes independent identities on approval', async (field) => {
      const { app, db } = createFixture();
      const activated = await request(app).post('/api/v2/auth/activate').send({
        key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
      }).expect(200);
      const entities = ['A', 'B'].map((name) => ({
        names: [name],
        [field]: ['shared-id', `only-${name}`],
      }));
      const submitted = await request(app).post('/api/v2/player-library/submit')
        .set('Authorization', `Bearer ${activated.body.sessionToken}`)
        .set('X-DNF-Device-Id', 'device-test-0001')
        .send({ entities }).expect(202);
      expect(submitted.body).toMatchObject({
        ok: true, status: 'pending_review', identifierConflictCount: 0, ownershipConflictCount: 0,
      });
      const row = db.prepare('SELECT status, payload_json FROM player_library_submissions WHERE id = ?')
        .get(submitted.body.submissionId) as { status: string; payload_json: string };
      expect(row.status).toBe('pending');
      expect(JSON.parse(row.payload_json).entities).toEqual(entities.map((entity) => ({
        ...entity, entityId: expect.any(String),
      })));
      expect(listPlayerLibrary(db)).toEqual({ revision: 0, entities: [], entityRedirects: [] });
      expect(approveReviewed(db, submitted.body.submissionId)).toEqual({
        ok: true, revision: 1,
      });
      const library = listPlayerLibrary(db);
      expect(library).toMatchObject({ revision: 1, entityRedirects: [] });
      expect(library.entities).toHaveLength(2);
      expect(library.entities.every(entity => entity[field].includes('shared-id'))).toBe(true);
    },
  );

  test('still rejects unauthenticated or malformed submissions without creating review records', async () => {
    const { app, db } = createFixture();
    await request(app).post('/api/v2/player-library/submit').send({ entities: [] }).expect(401);
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ names: [], gameIds: ['shared-id'] }] })
      .expect(400);
    expect(db.prepare('SELECT COUNT(*) AS count FROM player_library_submissions').get()).toEqual({ count: 0 });
  });

  test('resolves shared IDs within a bounded distinct active roster and rejects invalid rosters', async () => {
    const { app, db } = createFixture();
    mutatePublicLibrary(db, 0, now, 'import', [
      { entityId: 'a', names: ['A'], gameIds: ['shared'] },
      { entityId: 'b', names: ['B'], gameIds: ['shared'] },
    ]);
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const resolve = (body: object) => request(app).post('/api/v2/player-library/resolve')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`)
      .set('X-DNF-Device-Id', 'device-test-0001').send(body);
    expect((await resolve({ gameIds: ['shared'] }).expect(200)).body)
      .toMatchObject({ entityId: null, conflict: true, candidateEntityIds: ['a', 'b'] });
    expect((await resolve({ gameIds: ['SHARED'], activeEntityIds: ['b'] }).expect(200)).body)
      .toMatchObject({ entityId: 'b', matchedBy: 'game', candidateEntityIds: ['b'] });
    expect((await resolve({ gameIds: ['shared'], adventureGroupIds: ['guild-a'], activeEntityIds: ['a', 'b'] }).expect(200)).body)
      .toMatchObject({ entityId: null, conflict: true, candidateEntityIds: ['a', 'b'] });
    expect((await resolve({ adventureGroupIds: ['shared'] }).expect(200)).body)
      .toMatchObject({ entityId: null, matchedBy: 'none', candidateEntityIds: [] });
    expect((await resolve({ gameIds: ['shared'], activeEntityIds: [] }).expect(200)).body)
      .toMatchObject({ entityId: null, candidateEntityIds: [] });
    await resolve({ gameIds: ['shared'], activeEntityIds: ['a', ...Array.from({ length: 7 }, (_, i) => `inactive-${i}`)] }).expect(200);
    for (const activeEntityIds of [['a', 'a'], Array.from({ length: 9 }, (_, i) => `player-${i}`)]) {
      await resolve({ gameIds: ['shared'], activeEntityIds }).expect(400, { ok: false, code: 'invalid_request' });
    }
    expect(listPlayerLibrary(db)).toMatchObject({ revision: 1, entityRedirects: [] });
    expect(listPlayerLibrary(db).entities).toHaveLength(2);
    mutatePublicLibrary(db, 1, now, 'import', ['old-a', 'older-a'].map(entityId => ({
      entityId, names: [entityId], gameIds: [],
    })));
    mergePublicLibrary(db, 2, now, 'a', ['a', 'old-a', 'older-a']);
    expect((await resolve({ gameIds: ['shared'], activeEntityIds: ['old-a'] }).expect(200)).body)
      .toMatchObject({ entityId: 'a', candidateEntityIds: ['a'] });
    for (const activeEntityIds of [['old-a', 'older-a'], ['old-a', 'a']]) {
      await resolve({ gameIds: ['shared'], activeEntityIds }).expect(400, { ok: false, code: 'ambiguous_active_roster' });
    }
    expect(listPlayerLibrary(db)).toMatchObject({ revision: 3, entityRedirects: [
      { fromEntityId: 'old-a', toEntityId: 'a' }, { fromEntityId: 'older-a', toEntityId: 'a' },
    ] });
  });

  test('submission acknowledgement and review use original automatic evidence instead of an enlarged public union', async () => {
    const { app, db } = createFixture();
    const left = ['g1', 'g2', 'g3', 'g4', 'g5'], right = ['g6', 'g7', 'g8', 'g9', 'g10'];
    mutatePublicLibrary(db, 0, now, 'import', [
      { entityId: 'a', names: ['A'], gameIds: left },
      { entityId: 'b', names: ['B'], gameIds: [...left, ...right] },
    ]);
    const before = listPlayerLibrary(db);
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const submitted = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', `Bearer ${activated.body.sessionToken}`).set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ entityId: 'c', names: ['A'], gameIds: right }] }).expect(202);
    expect(submitted.body.ownershipConflictCount).toBe(1);
    expect(getPlayerLibrarySubmission(db, submitted.body.submissionId)?.automaticGroups).toEqual([]);
    expect(approveReviewed(db, submitted.body.submissionId)).toEqual({ ok: false, code: 'name_conflict' });
    expect(listPlayerLibrary(db)).toEqual(before);
  });
});
