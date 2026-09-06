import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import express from 'express';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { hashLicenseKey } from '../src/auth.js';
import { openDatabase } from '../src/db.js';
import {
  approvePlayerLibrarySubmission,
  createV2Api,
  listPlayerLibrary,
} from '../src/v2-api.js';

const resources: Array<{ directory: string; close(): void }> = [];
const now = 1_800_000_000;

function createFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-v2-'));
  const db = openDatabase(join(directory, 'test.sqlite'));
  db.prepare(
    `INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
     VALUES (?, ?, ?, NULL, NULL, ?, ?)`,
  ).run(hashLicenseKey('CDK-TEST-ONE'), 'test card', now + 86_400, now, now);
  const app = express();
  app.use(express.json({ limit: '128kb' }));
  app.use('/api/v2', createV2Api({
    db,
    now: () => now,
    serverUrl: 'http://127.0.0.1:28880',
    sessionTtlSeconds: 86_400,
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
        adventureGroupIds: ['guild-a'],
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

    const approved = approvePlayerLibrarySubmission(db, submission.body.submissionId, now);
    expect(approved).toMatchObject({ ok: true });
    const library = listPlayerLibrary(db);
    expect(library.entities[0]).toMatchObject({
      names: expect.arrayContaining(['老王', '王大枪']),
      gameIds: ['game-a'],
      adventureGroupIds: ['guild-a'],
    });
  });

  test('rejects a public-library approval that would give one game ID two owners', async () => {
    const { app, db } = createFixture();
    const activated = await request(app).post('/api/v2/auth/activate').send({
      key: 'CDK-TEST-ONE', deviceId: 'device-test-0001',
    }).expect(200);
    const authorization = `Bearer ${activated.body.sessionToken}`;
    const first = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', authorization)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ names: ['甲'], gameIds: ['same-id'], adventureGroupIds: [] }] })
      .expect(202);
    expect(approvePlayerLibrarySubmission(db, first.body.submissionId, now).ok).toBe(true);

    const second = await request(app).post('/api/v2/player-library/submit')
      .set('Authorization', authorization)
      .set('X-DNF-Device-Id', 'device-test-0001')
      .send({ entities: [{ names: ['乙'], gameIds: ['same-id'], adventureGroupIds: [] }] })
      .expect(202);
    expect(approvePlayerLibrarySubmission(db, second.body.submissionId, now)).toEqual({
      ok: false,
      code: 'identifier_conflict',
    });
  });
});
