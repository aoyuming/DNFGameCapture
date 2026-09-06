import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { createCloudMatchAdminApp } from '../src/admin.js';
import { createCloudMatchApp } from '../src/app.js';
import { generateLicenseKey, hashLicenseKey } from '../src/auth.js';
import { openDatabase } from '../src/db.js';
import { registerDevice } from '../src/identity.js';
import type { MatchSnapshot, Player } from '../src/schemas.js';
import { saveSnapshot } from '../src/snapshots.js';
import {
  initializeSyncRelationSchema,
  listAllRealtimeSync,
  recordSuccessfulSync,
  startRealtimeSync,
  stopRealtimeSync,
} from '../src/sync-relations.js';
import { joinUnifiedPool } from '../src/unified.js';

const resources: Array<{ directory: string; close(): void }> = [];
const now = 1_700_000_000;
const csrfToken = 'admin-test-csrf-token';
const adminPassword = 'test-admin-password-2026';

function player(mainName: string, seed: number): Player {
  return {
    mainName,
    aliases: [`${mainName}小号`],
    kills: seed,
    deaths: seed + 1,
    ak: seed + 2,
    streak: seed + 3,
  };
}

function snapshot(clientRevision: number): MatchSnapshot {
  return {
    schemaVersion: 1,
    clientRevision,
    clientTime: now,
    changeSource: 'ocr',
    redScore: 4,
    blueScore: 3,
    redPlayers: [
      player('红一', 0), player('红二', 4), player('红三', 8), player('红四', 12),
    ],
    bluePlayers: [
      player('蓝一', 16), player('蓝二', 20), player('蓝三', 24), player('蓝四', 28),
    ],
    redPickFirst: true,
    teamsFlipped: false,
    outputSeatLabel: true,
    lastKillTeam: 'red',
  };
}

function seedBroadcaster(
  db: ReturnType<typeof openDatabase>,
  deviceId: string,
  name: string,
  lastSeenAt: number,
  revision = 1,
): void {
  expect(registerDevice(db, deviceId, lastSeenAt)).not.toBeNull();
  expect(joinUnifiedPool(db, deviceId, name, lastSeenAt)).not.toBeNull();
  expect(saveSnapshot(db, {
    deviceId,
    roomId: 'all-broadcasters',
    snapshot: snapshot(revision),
    receivedAt: lastSeenAt,
  })).toMatchObject({ ok: true });
}

function createFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-admin-'));
  const db = openDatabase(join(directory, 'admin.sqlite'));
  initializeSyncRelationSchema(db);
  const activeDeviceIds = new Set<string>();
  const disconnected: string[] = [];
  const stopped: string[] = [];
  const notifications: string[] = [];
  const socketController = {
    getActiveDeviceIds: () => new Set(activeDeviceIds),
    disconnectDevice: (deviceId: string) => {
      if (!activeDeviceIds.delete(deviceId)) return false;
      disconnected.push(deviceId);
      return true;
    },
    stopRealtimeViewer: (viewerDeviceId: string) => {
      const didStop = stopRealtimeSync(db, viewerDeviceId);
      if (didStop) stopped.push(viewerDeviceId);
      return didStop;
    },
    notifyDirectoryChanged: (reason: string) => notifications.push(reason),
  };
  const app = createCloudMatchAdminApp({
    db,
    now: () => now,
    csrfToken,
    adminPassword,
    socketController,
  });
  resources.push({ directory, close: () => db.close() });
  return {
    app,
    db,
    activeDeviceIds,
    disconnected,
    stopped,
    notifications,
  };
}

afterEach(() => {
  for (const resource of resources.splice(0)) {
    resource.close();
    rmSync(resource.directory, { recursive: true, force: true });
  }
});

describe('localhost admin console', () => {
  test('wires the admin console to an independently startable HTTP server', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-admin-app-'));
    const cloudApp = createCloudMatchApp({
      databasePath: join(directory, 'app.sqlite'),
      now: () => now,
      adminCsrfToken: csrfToken,
      adminPassword,
    });
    try {
      expect(cloudApp.adminHttpServer).not.toBe(cloudApp.httpServer);
      await new Promise<void>((resolve, reject) => {
        cloudApp.httpServer.once('error', reject);
        cloudApp.httpServer.listen(0, '127.0.0.1', resolve);
      });
      await new Promise<void>((resolve, reject) => {
        cloudApp.adminHttpServer.once('error', reject);
        cloudApp.adminHttpServer.listen(0, '127.0.0.1', resolve);
      });
      await request(cloudApp.adminExpressApp)
        .get('/admin/health')
        .expect(200, { ok: true });
      await request(cloudApp.adminExpressApp)
        .get('/admin')
        .expect('WWW-Authenticate', /Basic/)
        .expect(401);
      await request(cloudApp.adminExpressApp)
        .get('/admin')
        .auth('admin', adminPassword)
        .expect(200);
      await request(cloudApp.expressApp)
        .get('/admin')
        .expect(404);
    } finally {
      await cloudApp.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });

  test('serves a secured no-cache page and rejects mutations without CSRF', async () => {
    const { app } = createFixture();

    await request(app)
      .get('/admin')
      .expect('WWW-Authenticate', /Basic/)
      .expect(401);
    await request(app)
      .get('/admin')
      .auth('admin', 'wrong-password')
      .expect(401);

    const page = await request(app)
      .get('/admin')
      .auth('admin', adminPassword)
      .expect(200);
    expect(page.headers['x-frame-options']).toBe('DENY');
    expect(page.headers['cache-control']).toContain('no-store');
    expect(page.headers['content-security-policy']).toContain("default-src 'self'");
    expect(page.text).toContain('DNF 云端同步管理');
    expect(page.text).toContain(`content="${csrfToken}"`);
    expect(page.text).not.toContain('修改比分');

    await request(app)
      .get('/admin/app.js')
      .auth('admin', adminPassword)
      .expect('Content-Type', /application\/javascript/)
      .expect(200);
    await request(app)
      .get('/admin/style.css')
      .auth('admin', adminPassword)
      .expect('Content-Type', /text\/css/)
      .expect(200);

    await request(app)
      .post('/admin/api/cleanup/expired')
      .auth('admin', adminPassword)
      .expect(403, { ok: false, code: 'invalid_csrf' });
  });

  test('lists and searches safe broadcaster match data with relations and history', async () => {
    const { app, db, activeDeviceIds } = createFixture();
    seedBroadcaster(db, 'online-device-0001', '在线主播', now);
    seedBroadcaster(db, 'offline-device-0002', '离线主播', now - 60);
    activeDeviceIds.add('online-device-0001');
    startRealtimeSync(db, {
      viewerDeviceId: 'offline-device-0002',
      viewerName: '离线主播',
      targetDeviceId: 'online-device-0001',
      targetName: '在线主播',
      startedAt: now,
    });
    recordSuccessfulSync(db, {
      sourceDeviceId: 'online-device-0001',
      sourceName: '在线主播',
      targetDeviceId: 'offline-device-0002',
      targetName: '离线主播',
      syncType: 'once',
      snapshotRevision: 1,
      merged: true,
      createdAt: now,
    });

    const response = await request(app)
      .get('/admin/api/state')
      .auth('admin', adminPassword)
      .expect(200);
    expect(response.body).toMatchObject({
      ok: true,
      broadcasters: [
        {
          deviceId: 'online-device-0001',
          broadcasterName: '在线主播',
          online: true,
          snapshot: { redScore: 4, blueScore: 3 },
        },
        {
          deviceId: 'offline-device-0002',
          broadcasterName: '离线主播',
          online: false,
        },
      ],
    });
    expect(response.body.broadcasters[0].snapshot.redPlayers[0].mainName).toBe('红一');
    expect(response.body.relations).toHaveLength(1);
    expect(response.body.history).toHaveLength(1);
    expect(JSON.stringify(response.body)).not.toContain('token_hash');
    expect(JSON.stringify(response.body)).not.toContain('deviceToken');

    const filtered = await request(app)
      .get('/admin/api/state')
      .auth('admin', adminPassword)
      .query({ q: '离线' })
      .expect(200);
    expect(filtered.body.broadcasters.map((item: { deviceId: string }) => item.deviceId))
      .toEqual(['offline-device-0002']);
  });

  test('forces disconnects and stops realtime relations through the socket controller', async () => {
    const { app, db, activeDeviceIds, disconnected, stopped, notifications } = createFixture();
    seedBroadcaster(db, 'online-device-0001', '在线主播', now);
    seedBroadcaster(db, 'viewer-device-0002', '跟随主播', now);
    activeDeviceIds.add('online-device-0001');
    startRealtimeSync(db, {
      viewerDeviceId: 'viewer-device-0002',
      viewerName: '跟随主播',
      targetDeviceId: 'online-device-0001',
      targetName: '在线主播',
      startedAt: now,
    });

    await request(app)
      .post('/admin/api/broadcasters/online-device-0001/disconnect')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(200, { ok: true });
    await request(app)
      .post('/admin/api/realtime/viewer-device-0002/stop')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(200, { ok: true });

    expect(disconnected).toEqual(['online-device-0001']);
    expect(stopped).toEqual(['viewer-device-0002']);
    expect(listAllRealtimeSync(db, now)).toEqual([]);
    expect(notifications).toEqual(['admin_disconnect', 'admin_realtime_stop']);
  });

  test('deletes only offline lobby data while preserving permanent device identity', async () => {
    const { app, db, activeDeviceIds } = createFixture();
    seedBroadcaster(db, 'online-device-0001', '在线主播', now);
    seedBroadcaster(db, 'offline-device-0002', '离线主播', now - 60);
    activeDeviceIds.add('online-device-0001');

    await request(app)
      .delete('/admin/api/broadcasters/online-device-0001/data')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(409, { ok: false, code: 'broadcaster_online' });
    await request(app)
      .delete('/admin/api/broadcasters/offline-device-0002/data')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(200, { ok: true, deleted: true });

    expect(db.prepare('SELECT COUNT(*) AS count FROM devices WHERE id = ?')
      .get('offline-device-0002')).toEqual({ count: 1 });
    expect(db.prepare('SELECT COUNT(*) AS count FROM memberships WHERE device_id = ?')
      .get('offline-device-0002')).toEqual({ count: 0 });
    expect(db.prepare('SELECT COUNT(*) AS count FROM snapshots WHERE device_id = ?')
      .get('offline-device-0002')).toEqual({ count: 0 });
  });

  test('clears offline and temporary test lobby data without deleting online permanent data', async () => {
    const { app, db, activeDeviceIds, disconnected } = createFixture();
    seedBroadcaster(db, 'online-device-0001', '正式在线', now);
    seedBroadcaster(db, 'offline-device-0002', '正式离线', now - 60);
    seedBroadcaster(db, 'dnf-tmp-session-0003', '临时测试', now);
    activeDeviceIds.add('online-device-0001');
    activeDeviceIds.add('dnf-tmp-session-0003');

    const response = await request(app)
      .post('/admin/api/cleanup/offline')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(200);

    expect(response.body).toMatchObject({ ok: true, deletedCount: 2 });
    expect(disconnected).toEqual(['dnf-tmp-session-0003']);
    const remaining = db.prepare(
      'SELECT device_id FROM memberships ORDER BY device_id',
    ).all() as Array<{ device_id: string }>;
    expect(remaining.map((row) => row.device_id)).toEqual(['online-device-0001']);
  });

  test('prunes expired offline broadcasters and synchronization records on demand', async () => {
    const { app, db } = createFixture();
    seedBroadcaster(db, 'expired-device-0001', '过期主播', now - 86_401);
    db.prepare(
      `INSERT INTO sync_history (
         source_device_id, source_name, target_device_id, target_name,
         sync_type, snapshot_revision, merged, created_at, expires_at
       ) VALUES (?, ?, ?, ?, 'once', 1, 1, ?, ?)`,
    ).run('expired-device-0001', '过期主播', 'other-device', '其他', now - 90_000, now - 1);

    const response = await request(app)
      .post('/admin/api/cleanup/expired')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .expect(200);

    expect(response.body.ok).toBe(true);
    expect(response.body.removedBroadcasters).toBe(1);
    expect(db.prepare('SELECT COUNT(*) AS count FROM memberships WHERE device_id = ?')
      .get('expired-device-0001')).toEqual({ count: 0 });
    expect(db.prepare('SELECT COUNT(*) AS count FROM sync_history').get())
      .toEqual({ count: 0 });
  });

  test('lets the v2 player-library payload use its own larger body limit', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-v2-limit-'));
    const cloudApp = createCloudMatchApp({
      databasePath: join(directory, 'app.sqlite'),
      now: () => now,
      adminCsrfToken: csrfToken,
      adminPassword,
      v2ServerUrl: 'http://127.0.0.1:28880',
    });
    try {
      cloudApp.db.prepare(
        `INSERT INTO licenses (key_hash, label, expires_at, disabled_at, bound_device_id, created_at, updated_at)
         VALUES (?, ?, ?, NULL, NULL, ?, ?)`,
      ).run(hashLicenseKey('CDK-V2-LIMIT'), 'v2 payload limit', now + 86_400, now, now);

      const activated = await request(cloudApp.expressApp)
        .post('/api/v2/auth/activate')
        .send({ key: 'CDK-V2-LIMIT', deviceId: 'device-v2-limit-0001' })
        .expect(200);
      const payload = {
        entities: Array.from({ length: 100 }, (_, index) => ({
          names: [`测试选手-${index}-${'名称'.repeat(12)}`],
          gameIds: [`game-id-${index}-${'x'.repeat(16)}`],
          adventureGroupIds: [`guild-id-${index}`],
        })),
      };
      expect(Buffer.byteLength(JSON.stringify(payload), 'utf8')).toBeGreaterThan(4 * 1024);

      await request(cloudApp.expressApp)
        .post('/api/v2/player-library/submit')
        .set('Authorization', `Bearer ${activated.body.sessionToken}`)
        .set('X-DNF-Device-Id', 'device-v2-limit-0001')
        .send(payload)
        .expect(202);
    } finally {
      await cloudApp.close();
      rmSync(directory, { recursive: true, force: true });
    }
  });

  test('manages licenses, approves player-library submissions, and sets OCR policy', async () => {
    const { app, db } = createFixture();

    const created = await request(app)
      .post('/admin/api/licenses')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({ label: '夜间测试卡', expiresAt: now + 3_600 })
      .expect(201);
    expect(created.body).toMatchObject({ ok: true, key: expect.stringMatching(/^CDK-/) });
    expect(created.body.keyHash).toBeUndefined();

    await request(app)
      .post('/admin/api/licenses')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({ key: 'manual-key-that-native-client-rejects', label: '非法测试卡', expiresAt: null })
      .expect(400, { ok: false, code: 'invalid_license_format' });

    const suppliedKey = generateLicenseKey();
    const supplied = await request(app)
      .post('/admin/api/licenses')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({ key: suppliedKey, label: '手工测试卡', expiresAt: null })
      .expect(201);
    expect(supplied.body).toMatchObject({ ok: true, key: suppliedKey });

    const licenses = await request(app)
      .get('/admin/api/licenses')
      .auth('admin', adminPassword)
      .expect(200);
    expect(licenses.body.licenses).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ label: '夜间测试卡' }),
        expect.objectContaining({ label: '手工测试卡' }),
      ]),
    );
    expect(JSON.stringify(licenses.body)).not.toContain(created.body.key);
    expect(JSON.stringify(licenses.body)).not.toContain(suppliedKey);

    const inserted = db.prepare(
      `INSERT INTO player_library_submissions (device_id, payload_json, status, created_at)
       VALUES (?, ?, 'pending', ?)`,
    ).run('device-test-0001', JSON.stringify({ entities: [{ names: ['测试选手'], gameIds: ['测试ID'], adventureGroupIds: [] }] }), now);
    await request(app)
      .post(`/admin/api/player-library/submissions/${inserted.lastInsertRowid}/approve`)
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({})
      .expect(200, { ok: true, revision: 1 });

    const library = await request(app)
      .get('/admin/api/player-library')
      .auth('admin', adminPassword)
      .expect(200);
    expect(library.body.entities[0]).toMatchObject({ names: ['测试选手'], gameIds: ['测试ID'] });

    await request(app)
      .put('/admin/api/broadcasters/device-test-0001/ocr-policy')
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({ disabledUntil: now + 600 })
      .expect(200, { ok: true, deviceId: 'device-test-0001', disabledUntil: now + 600 });
    expect(db.prepare('SELECT ocr_disabled_until FROM broadcaster_policies WHERE device_id = ?')
      .get('device-test-0001')).toEqual({ ocr_disabled_until: now + 600 });

    await request(app)
      .post(`/admin/api/licenses/${created.body.id}/disable`)
      .auth('admin', adminPassword)
      .set('x-dnf-admin-csrf', csrfToken)
      .send({ disabled: true })
      .expect(200, { ok: true, disabled: true });
  });
});
