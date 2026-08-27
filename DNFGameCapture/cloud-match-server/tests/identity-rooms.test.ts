import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { AddressInfo } from 'node:net';
import { io as createSocketClient, type Socket } from 'socket.io-client';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { createCloudMatchApp } from '../src/app.js';

type CloudMatchApp = ReturnType<typeof createCloudMatchApp>;

interface RunningApp {
  app: CloudMatchApp;
  directory: string;
  url: string;
}

interface Registration {
  deviceId: string;
  deviceToken: string;
}

const runningApps: RunningApp[] = [];
const sockets: Socket[] = [];

async function startApp(now: () => number = () => 1_700_000_000): Promise<RunningApp> {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-identity-'));
  const app = createCloudMatchApp({
    databasePath: join(directory, 'rooms.sqlite'),
    now,
  });

  try {
    await new Promise<void>((resolve, reject) => {
      app.httpServer.once('error', reject);
      app.httpServer.listen(0, '127.0.0.1', () => {
        app.httpServer.off('error', reject);
        resolve();
      });
    });
  } catch (error) {
    try {
      await app.close();
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
    throw error;
  }

  const address = app.httpServer.address() as AddressInfo;
  const running = { app, directory, url: `http://127.0.0.1:${address.port}` };
  runningApps.push(running);
  return running;
}

async function register(url: string, deviceId: string): Promise<Registration> {
  const response = await request(url).post('/api/devices/register').send({ deviceId });
  expect(response.status).toBe(201);
  return response.body as Registration;
}

function connect(
  url: string,
  auth: Record<string, unknown>,
): Promise<{ socket?: Socket; error?: Error & { data?: { code?: string } } }> {
  return new Promise((resolve) => {
    const socket = createSocketClient(url, {
      auth,
      forceNew: true,
      reconnection: false,
      timeout: 1_000,
      transports: ['websocket'],
    });
    sockets.push(socket);

    socket.once('connect', () => resolve({ socket }));
    socket.once('connect_error', (error: Error & { data?: { code?: string } }) => {
      socket.close();
      resolve({ error });
    });
  });
}

async function connectRegistered(url: string, registration: Registration): Promise<Socket> {
  const result = await connect(url, { ...registration, protocolVersion: 1 });
  expect(result.error).toBeUndefined();
  expect(result.socket?.connected).toBe(true);
  return result.socket as Socket;
}

function emitAck<T>(socket: Socket, event: string, payload?: unknown): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Timed out waiting for ${event} ACK`)), 1_000);
    const acknowledge = (response: T) => {
      clearTimeout(timer);
      resolve(response);
    };

    if (payload === undefined) {
      socket.emit(event, acknowledge);
    } else {
      socket.emit(event, payload, acknowledge);
    }
  });
}

function waitForEvent<T>(socket: Socket, event: string): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Timed out waiting for ${event}`)), 1_000);
    socket.once(event, (payload: T) => {
      clearTimeout(timer);
      resolve(payload);
    });
  });
}

async function waitUntil(assertion: () => void): Promise<void> {
  const deadline = Date.now() + 1_000;
  let lastError: unknown;
  do {
    try {
      assertion();
      return;
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
  } while (Date.now() < deadline);
  throw lastError;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) {
    socket.disconnect();
  }
  for (const running of runningApps.splice(0)) {
    try {
      await running.app.close();
    } finally {
      rmSync(running.directory, { recursive: true, force: true });
    }
  }
});

describe('device identity', () => {
  test('registers a device while storing only a token hash', async () => {
    const { app, url } = await startApp();

    const response = await request(url)
      .post('/api/devices/register')
      .send({ deviceId: 'capture-device-0001' });

    expect(response.status).toBe(201);
    expect(response.body).toEqual({
      deviceId: 'capture-device-0001',
      deviceToken: expect.stringMatching(/^[A-Za-z0-9_-]{40,}$/),
    });
    const stored = app.db
      .prepare('select token_hash from devices where id = ?')
      .get('capture-device-0001') as { token_hash: string };
    expect(stored.token_hash).toMatch(/^[a-f0-9]{64}$/);
    expect(stored.token_hash).not.toBe(response.body.deviceToken);
  });

  test('rotates a token when the same device registers again', async () => {
    const { app, url } = await startApp();
    const first = await register(url, 'capture-device-0002');
    const second = await register(url, 'capture-device-0002');

    expect(second.deviceToken).not.toBe(first.deviceToken);

    const oldConnection = await connect(url, { ...first, protocolVersion: 1 });
    expect(oldConnection.socket).toBeUndefined();
    expect(oldConnection.error?.data).toEqual({ code: 'authentication_failed' });

    const currentSocket = await connectRegistered(url, second);
    expect(currentSocket.connected).toBe(true);
    expect(
      (app.db.prepare('select count(*) as count from devices where id = ?').get(first.deviceId) as {
        count: number;
      }).count,
    ).toBe(1);
  });

  test('validates device registration identifiers', async () => {
    const { url } = await startApp();

    for (const deviceId of ['short', 'contains space', '设备标识-0001', 'x'.repeat(129)]) {
      const response = await request(url).post('/api/devices/register').send({ deviceId });
      expect(response.status).toBe(400);
      expect(response.body).toEqual({ ok: false, code: 'invalid_request' });
    }
  });

  test('returns a structured error for malformed registration JSON', async () => {
    const { url } = await startApp();

    const response = await request(url)
      .post('/api/devices/register')
      .set('Content-Type', 'application/json')
      .send('{"deviceId":');

    expect(response.status).toBe(400);
    expect(response.body).toEqual({ ok: false, code: 'invalid_request' });
  });

  test('accepts only the exact version-one Socket authentication shape', async () => {
    const { url } = await startApp();
    const registration = await register(url, 'capture-device-0003');

    const valid = await connect(url, { ...registration, protocolVersion: 1 });
    expect(valid.socket?.connected).toBe(true);

    const wrongToken = await connect(url, {
      ...registration,
      deviceToken: 'A'.repeat(43),
      protocolVersion: 1,
    });
    expect(wrongToken.error?.data).toEqual({ code: 'authentication_failed' });

    for (const auth of [
      { deviceId: registration.deviceId, protocolVersion: 1 },
      { deviceToken: registration.deviceToken, protocolVersion: 1 },
      { ...registration },
      { ...registration, protocolVersion: 1, extra: true },
    ]) {
      const result = await connect(url, auth);
      expect(result.error?.data).toEqual({ code: 'invalid_auth' });
    }

    const unsupported = await connect(url, { ...registration, protocolVersion: 2 });
    expect(unsupported.error?.data).toEqual({ code: 'unsupported_protocol' });
  });
});

describe('fixed room membership', () => {
  test('lists exactly the seeded rooms and joins a known room', async () => {
    const { url } = await startApp();
    const registration = await register(url, 'capture-device-1001');
    const socket = await connectRegistered(url, registration);

    await expect(emitAck(socket, 'room:list')).resolves.toEqual({
      ok: true,
      rooms: [
        { id: '59', displayName: '59房' },
        { id: 'li-yong', displayName: '李永房' },
        { id: 'wen-rou', displayName: '温柔房' },
      ],
    });
    await expect(
      emitAck(socket, 'room:join', { roomId: 'li-yong', broadcasterName: '主播甲' }),
    ).resolves.toEqual({
      ok: true,
      room: { id: 'li-yong', displayName: '李永房' },
      broadcasterName: '主播甲',
      deviceSuffix: '1001',
    });
  });

  test('rejects unknown rooms and invalid broadcaster names', async () => {
    const { url } = await startApp();
    const socket = await connectRegistered(
      url,
      await register(url, 'capture-device-1002'),
    );

    await expect(
      emitAck(socket, 'room:join', { roomId: 'unknown', broadcasterName: '主播甲' }),
    ).resolves.toEqual({ ok: false, code: 'room_not_found' });

    for (const broadcasterName of ['', '   ', '播'.repeat(33)]) {
      await expect(
        emitAck(socket, 'room:join', { roomId: 'li-yong', broadcasterName }),
      ).resolves.toEqual({ ok: false, code: 'invalid_broadcaster_name' });
    }

    await expect(
      emitAck(socket, 'room:join', {
        roomId: 'li-yong',
        broadcasterName: '😀'.repeat(32),
      }),
    ).resolves.toMatchObject({ ok: true, broadcasterName: '😀'.repeat(32) });
  });

  test('allows duplicate names and returns suffixes for disambiguation', async () => {
    const { url } = await startApp();
    const first = await connectRegistered(url, await register(url, 'capture-device-2001'));
    const second = await connectRegistered(url, await register(url, 'capture-device-2002'));

    const firstJoin = await emitAck(first, 'room:join', {
      roomId: '59',
      broadcasterName: '同名主播',
    });
    const secondJoin = await emitAck(second, 'room:join', {
      roomId: '59',
      broadcasterName: '同名主播',
    });

    expect(firstJoin).toMatchObject({ ok: true, broadcasterName: '同名主播', deviceSuffix: '2001' });
    expect(secondJoin).toMatchObject({ ok: true, broadcasterName: '同名主播', deviceSuffix: '2002' });
  });

  test('renames only the broadcaster and reports current membership', async () => {
    const { url } = await startApp();
    const socket = await connectRegistered(
      url,
      await register(url, 'capture-device-3001'),
    );
    await emitAck(socket, 'room:join', { roomId: 'wen-rou', broadcasterName: '原名称' });

    await expect(
      emitAck(socket, 'room:rename', { broadcasterName: '  新名称  ' }),
    ).resolves.toEqual({
      ok: true,
      room: { id: 'wen-rou', displayName: '温柔房' },
      broadcasterName: '新名称',
      deviceSuffix: '3001',
    });
    await expect(emitAck(socket, 'room:status')).resolves.toEqual({
      ok: true,
      membership: {
        room: { id: 'wen-rou', displayName: '温柔房' },
        broadcasterName: '新名称',
        deviceSuffix: '3001',
      },
      online: true,
    });
  });

  test('deletes stale snapshots when switching rooms or leaving', async () => {
    const { app, url } = await startApp();
    const registration = await register(url, 'capture-device-4001');
    const socket = await connectRegistered(url, registration);
    await emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' });

    const insertSnapshot = () =>
      app.db
        .prepare(
          `insert into snapshots (
             device_id, room_id, client_revision, content_hash, change_source,
             synced_from_device_id, synced_from_revision, payload_json, received_at
           ) values (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
        )
        .run(registration.deviceId, '59', 1, 'hash', 'ocr', null, null, '{}', 100);
    insertSnapshot();

    await emitAck(socket, 'room:join', {
      roomId: 'li-yong',
      broadcasterName: '主播甲',
    });
    expect(
      (app.db.prepare('select count(*) as count from snapshots where device_id = ?').get(
        registration.deviceId,
      ) as { count: number }).count,
    ).toBe(0);

    app.db
      .prepare(
        `insert into snapshots (
           device_id, room_id, client_revision, content_hash, change_source,
           synced_from_device_id, synced_from_revision, payload_json, received_at
         ) values (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      )
      .run(registration.deviceId, 'li-yong', 2, 'hash-2', 'ocr', null, null, '{}', 200);

    await expect(emitAck(socket, 'room:leave')).resolves.toEqual({ ok: true });
    expect(
      (app.db.prepare('select count(*) as count from snapshots where device_id = ?').get(
        registration.deviceId,
      ) as { count: number }).count,
    ).toBe(0);
    expect(
      (app.db.prepare('select count(*) as count from memberships where device_id = ?').get(
        registration.deviceId,
      ) as { count: number }).count,
    ).toBe(0);
    await expect(emitAck(socket, 'room:status')).resolves.toEqual({
      ok: true,
      membership: null,
      online: true,
    });
  });
});

describe('Socket connection lifecycle', () => {
  test('replaces an older connection for the same device', async () => {
    const { app, url } = await startApp();
    const registration = await register(url, 'capture-device-5001');
    const first = await connectRegistered(url, registration);
    const replacementNotice = waitForEvent<{ code: string }>(first, 'connection:replaced');
    const firstDisconnect = waitForEvent<string>(first, 'disconnect');

    const second = await connectRegistered(url, registration);

    await expect(replacementNotice).resolves.toEqual({
      code: 'replaced_by_new_connection',
    });
    await expect(firstDisconnect).resolves.toBe('io server disconnect');
    expect(second.connected).toBe(true);
    expect(app.io.sockets.sockets.size).toBe(1);
    await expect(emitAck(second, 'room:status')).resolves.toMatchObject({
      ok: true,
      online: true,
    });
  });

  test('touches last-seen and preserves membership after disconnect', async () => {
    let now = 100;
    const { app, url } = await startApp(() => now);
    const registration = await register(url, 'capture-device-6001');
    const socket = await connectRegistered(url, registration);
    await emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '离线主播' });

    now = 777;
    socket.disconnect();

    await waitUntil(() => {
      const device = app.db
        .prepare('select last_seen_at from devices where id = ?')
        .get(registration.deviceId) as { last_seen_at: number };
      expect(device.last_seen_at).toBe(777);
      expect(app.io.sockets.sockets.size).toBe(0);
    });
    expect(
      app.db
        .prepare('select room_id, broadcaster_name from memberships where device_id = ?')
        .get(registration.deviceId),
    ).toEqual({ room_id: '59', broadcaster_name: '离线主播' });
  });
});
