import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { AddressInfo } from 'node:net';
import { io as createSocketClient, type Socket } from 'socket.io-client';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { createCloudMatchApp } from '../src/app.js';
import * as roomStore from '../src/rooms.js';

type CloudMatchApp = ReturnType<typeof createCloudMatchApp>;
type AppOptions = NonNullable<Parameters<typeof createCloudMatchApp>[0]>;

interface RunningApp {
  app: CloudMatchApp;
  directory: string;
  url: string;
}

interface Registration {
  deviceId: string;
  deviceToken: string;
}

interface Deferred<T> {
  promise: Promise<T>;
  resolve(value: T): void;
}

const runningApps: RunningApp[] = [];
const sockets: Socket[] = [];

function createDeferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((resolver) => {
    resolve = resolver;
  });
  return { promise, resolve };
}

async function startApp(options: AppOptions = {}): Promise<RunningApp> {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-identity-'));
  const app = createCloudMatchApp({
    ...options,
    databasePath: join(directory, 'rooms.sqlite'),
    now: options.now ?? (() => 1_700_000_000),
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

function serverRooms(app: CloudMatchApp, socket: Socket): Set<string> {
  const serverSocket = app.io.sockets.sockets.get(socket.id as string);
  expect(serverSocket).toBeDefined();
  return serverSocket?.rooms as Set<string>;
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

  test('rejects duplicate registration without rotating or disclosing a token', async () => {
    const { app, url } = await startApp();
    const first = await register(url, 'capture-device-0002');
    const originalHash = (
      app.db.prepare('select token_hash from devices where id = ?').get(first.deviceId) as {
        token_hash: string;
      }
    ).token_hash;

    const duplicate = await request(url)
      .post('/api/devices/register')
      .send({ deviceId: first.deviceId });

    expect(duplicate.status).toBe(409);
    expect(duplicate.body).toEqual({ ok: false, code: 'device_already_registered' });
    expect(duplicate.body).not.toHaveProperty('deviceToken');
    expect(
      (
        app.db.prepare('select token_hash from devices where id = ?').get(first.deviceId) as {
          token_hash: string;
        }
      ).token_hash,
    ).toBe(originalHash);

    const originalSocket = await connectRegistered(url, first);
    expect(originalSocket.connected).toBe(true);
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

  test('limits registration JSON bodies to 4kb', async () => {
    const { url } = await startApp();

    const response = await request(url)
      .post('/api/devices/register')
      .send({ deviceId: 'capture-device-0004', padding: 'x'.repeat(4_096) });

    expect(response.status).toBe(413);
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

  test('rejects unknown rooms, oversized room IDs, and unsafe broadcaster names', async () => {
    const { url } = await startApp();
    const socket = await connectRegistered(
      url,
      await register(url, 'capture-device-1002'),
    );

    await expect(
      emitAck(socket, 'room:join', { roomId: 'unknown', broadcasterName: '主播甲' }),
    ).resolves.toEqual({ ok: false, code: 'room_not_found' });

    await expect(
      emitAck(socket, 'room:join', {
        roomId: 'x'.repeat(65),
        broadcasterName: '主播甲',
      }),
    ).resolves.toEqual({ ok: false, code: 'invalid_request' });

    for (const broadcasterName of [
      '',
      '   ',
      '\u200b',
      '\ufe0f',
      '\u0301\u20dd',
      '主播\n甲',
      '主播\u202e甲',
      '主播\u2028甲',
      '主播\u2029甲',
      '播'.repeat(33),
      '👍🏽'.repeat(33),
    ]) {
      await expect(
        emitAck(socket, 'room:join', { roomId: 'li-yong', broadcasterName }),
      ).resolves.toEqual({ ok: false, code: 'invalid_broadcaster_name' });
    }

    const decomposedName = `${'e\u0301'.repeat(31)}👍🏽`;
    const normalizedName = `${'é'.repeat(31)}👍🏽`;
    await expect(
      emitAck(socket, 'room:join', {
        roomId: 'li-yong',
        broadcasterName: decomposedName,
      }),
    ).resolves.toMatchObject({ ok: true, broadcasterName: normalizedName });
    await expect(
      emitAck(socket, 'room:rename', { broadcasterName: '😀' }),
    ).resolves.toMatchObject({ ok: true, broadcasterName: '😀' });
    await expect(
      emitAck(socket, 'room:rename', { broadcasterName: '主播甲' }),
    ).resolves.toMatchObject({ ok: true, broadcasterName: '主播甲' });
  });

  test('catches operational Socket errors and remains usable without process errors', async () => {
    let failList = true;
    let failJoin = true;
    const uncaught: unknown[] = [];
    const unhandled: unknown[] = [];
    const onUncaught = (error: unknown) => uncaught.push(error);
    const onUnhandled = (error: unknown) => unhandled.push(error);
    process.on('uncaughtException', onUncaught);
    process.on('unhandledRejection', onUnhandled);

    try {
      const { app, url } = await startApp({
        roomService: {
          listRooms(db) {
            if (failList) {
              failList = false;
              throw new Error('sensitive list failure');
            }
            return roomStore.listRooms(db);
          },
          async joinRoom(...args) {
            if (failJoin) {
              failJoin = false;
              throw new Error('sensitive async database failure');
            }
            return roomStore.joinRoom(...args);
          },
        },
      });
      const socket = await connectRegistered(
        url,
        await register(url, 'capture-device-1003'),
      );

      await expect(emitAck(socket, 'room:list')).resolves.toEqual({
        ok: false,
        code: 'internal_error',
      });
      await expect(emitAck(socket, 'room:list')).resolves.toMatchObject({ ok: true });
      await expect(
        emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
      ).resolves.toEqual({ ok: false, code: 'internal_error' });
      expect(roomStore.getMembership(app.db, 'capture-device-1003')).toBeNull();
      expect(serverRooms(app, socket).has('room:59')).toBe(false);

      await expect(
        emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
      ).resolves.toMatchObject({ ok: true });
      await new Promise<void>((resolve) => setImmediate(resolve));
      expect(uncaught).toEqual([]);
      expect(unhandled).toEqual([]);
    } finally {
      process.off('uncaughtException', onUncaught);
      process.off('unhandledRejection', onUnhandled);
    }
  });

  test('rolls adapter changes back when join or leave persistence fails', async () => {
    let failJoin = true;
    let failLeave = true;
    const { app, url } = await startApp({
      roomService: {
        joinRoom(...args) {
          if (failJoin) {
            failJoin = false;
            throw new Error('join persistence failed');
          }
          return roomStore.joinRoom(...args);
        },
        leaveRoom(...args) {
          if (failLeave) {
            failLeave = false;
            throw new Error('leave persistence failed');
          }
          return roomStore.leaveRoom(...args);
        },
      },
    });
    const registration = await register(url, 'capture-device-1004');
    const socket = await connectRegistered(url, registration);

    await expect(
      emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
    ).resolves.toEqual({ ok: false, code: 'internal_error' });
    expect(roomStore.getMembership(app.db, registration.deviceId)).toBeNull();
    expect(serverRooms(app, socket).has('room:59')).toBe(false);

    await expect(
      emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
    ).resolves.toMatchObject({ ok: true });
    await expect(emitAck(socket, 'room:leave')).resolves.toEqual({
      ok: false,
      code: 'internal_error',
    });
    expect(roomStore.getMembership(app.db, registration.deviceId)?.room.id).toBe('59');
    expect(serverRooms(app, socket).has('room:59')).toBe(true);

    await expect(emitAck(socket, 'room:leave')).resolves.toEqual({ ok: true });
    expect(roomStore.getMembership(app.db, registration.deviceId)).toBeNull();
    expect(serverRooms(app, socket).has('room:59')).toBe(false);
  });

  test('restores adapter state after partial adapter failures', async () => {
    let failJoin = true;
    let failLeave = false;
    const { app, url } = await startApp({
      socketRoomAdapter: {
        async join(socket, room) {
          await socket.join(room);
          if (failJoin) {
            failJoin = false;
            throw new Error('adapter join failed after joining');
          }
        },
        async leave(socket, room) {
          await socket.leave(room);
          if (failLeave) {
            failLeave = false;
            throw new Error('adapter leave failed after leaving');
          }
        },
      },
    });
    const registration = await register(url, 'capture-device-1005');
    const socket = await connectRegistered(url, registration);

    await expect(
      emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
    ).resolves.toEqual({ ok: false, code: 'internal_error' });
    expect(roomStore.getMembership(app.db, registration.deviceId)).toBeNull();
    expect(serverRooms(app, socket).has('room:59')).toBe(false);

    await expect(
      emitAck(socket, 'room:join', { roomId: '59', broadcasterName: '主播甲' }),
    ).resolves.toMatchObject({ ok: true });
    failLeave = true;
    await expect(emitAck(socket, 'room:leave')).resolves.toEqual({
      ok: false,
      code: 'internal_error',
    });
    expect(roomStore.getMembership(app.db, registration.deviceId)?.room.id).toBe('59');
    expect(serverRooms(app, socket).has('room:59')).toBe(true);
  });

  test('serializes overlapping join and leave mutations per socket', async () => {
    const acknowledgementOrder: string[] = [];
    const { app, url } = await startApp({
      roomService: {
        async joinRoom(...args) {
          await new Promise((resolve) => setTimeout(resolve, 25));
          return roomStore.joinRoom(...args);
        },
      },
    });
    const registration = await register(url, 'capture-device-1006');
    const socket = await connectRegistered(url, registration);

    const joined = emitAck(socket, 'room:join', {
      roomId: 'li-yong',
      broadcasterName: '并发主播',
    }).then((result) => {
      acknowledgementOrder.push('join');
      return result;
    });
    const left = emitAck(socket, 'room:leave').then((result) => {
      acknowledgementOrder.push('leave');
      return result;
    });

    await expect(joined).resolves.toMatchObject({ ok: true });
    await expect(left).resolves.toEqual({ ok: true });
    expect(acknowledgementOrder).toEqual(['join', 'leave']);
    expect(roomStore.getMembership(app.db, registration.deviceId)).toBeNull();
    expect(serverRooms(app, socket).has('room:li-yong')).toBe(false);
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
  test('applies Socket payload limits and shares concurrent close calls', async () => {
    const { app } = await startApp();

    expect(app.io.engine.opts.maxHttpBufferSize).toBe(65_536);
    const firstClose = app.close();
    const secondClose = app.close();
    expect(secondClose).toBe(firstClose);

    await firstClose;
    expect(app.httpServer.listening).toBe(false);
    expect(app.db.open).toBe(false);
  });

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

  test('restores persisted room membership before reconnect status is handled', async () => {
    const { app, url } = await startApp();
    const registration = await register(url, 'capture-device-5002');
    const first = await connectRegistered(url, registration);
    await emitAck(first, 'room:join', {
      roomId: 'li-yong',
      broadcasterName: '重连主播',
    });
    first.disconnect();
    await waitUntil(() => expect(app.io.sockets.sockets.size).toBe(0));

    const reconnected = await connectRegistered(url, registration);

    await expect(emitAck(reconnected, 'room:status')).resolves.toMatchObject({
      ok: true,
      online: true,
      membership: { room: { id: 'li-yong' }, broadcasterName: '重连主播' },
    });
    expect(serverRooms(app, reconnected).has('room:li-yong')).toBe(true);
  });

  test('rejects a reconnect session when persisted room restoration fails', async () => {
    let failReconnectJoin = false;
    const { app, url } = await startApp({
      socketRoomAdapter: {
        async join(socket, room) {
          if (failReconnectJoin) {
            throw new Error('sensitive reconnect adapter failure');
          }
          await socket.join(room);
        },
        async leave(socket, room) {
          await socket.leave(room);
        },
      },
    });
    const registration = await register(url, 'capture-device-5003');
    const first = await connectRegistered(url, registration);
    await emitAck(first, 'room:join', { roomId: '59', broadcasterName: '重连主播' });
    first.disconnect();
    await waitUntil(() => expect(app.io.sockets.sockets.size).toBe(0));
    failReconnectJoin = true;

    const reconnecting = createSocketClient(url, {
      auth: { ...registration, protocolVersion: 1 },
      autoConnect: false,
      forceNew: true,
      reconnection: false,
      timeout: 1_000,
      transports: ['websocket'],
    });
    sockets.push(reconnecting);
    const sessionError = waitForEvent<{ code: string }>(reconnecting, 'session:error');
    reconnecting.connect();
    await waitForEvent(reconnecting, 'connect');

    await expect(sessionError).resolves.toEqual({ code: 'internal_error' });
    await waitUntil(() => expect(reconnecting.connected).toBe(false));
    expect(app.io.sockets.sockets.size).toBe(0);
  });

  test('serializes old and replacement socket mutations through one device queue', async () => {
    const oldJoinEntered = createDeferred<void>();
    const releaseOldJoin = createDeferred<void>();
    let blockOldJoin = true;
    const { app, url } = await startApp({
      roomService: {
        async joinRoom(...args) {
          if (blockOldJoin && args[2] === 'li-yong') {
            blockOldJoin = false;
            oldJoinEntered.resolve();
            await releaseOldJoin.promise;
          }
          return roomStore.joinRoom(...args);
        },
      },
    });
    const registration = await register(url, 'capture-device-5004');
    const oldSocket = await connectRegistered(url, registration);
    const oldMutation = emitAck(oldSocket, 'room:join', {
      roomId: 'li-yong',
      broadcasterName: '旧连接',
    });
    await oldJoinEntered.promise;

    const replacement = await connectRegistered(url, registration);
    const replacementMutation = emitAck(replacement, 'room:join', {
      roomId: 'wen-rou',
      broadcasterName: '新连接',
    });
    releaseOldJoin.resolve();

    await expect(oldMutation).resolves.toMatchObject({ ok: true });
    await expect(replacementMutation).resolves.toMatchObject({
      ok: true,
      room: { id: 'wen-rou' },
      broadcasterName: '新连接',
    });
    await waitUntil(() => expect(oldSocket.connected).toBe(false));
    expect(roomStore.getMembership(app.db, registration.deviceId)).toMatchObject({
      room: { id: 'wen-rou' },
      broadcasterName: '新连接',
    });
    expect(serverRooms(app, replacement).has('room:wen-rou')).toBe(true);
    expect(serverRooms(app, replacement).has('room:li-yong')).toBe(false);
    expect(app.io.sockets.sockets.size).toBe(1);
  });

  test('touches last-seen and preserves membership after disconnect', async () => {
    let now = 100;
    const { app, url } = await startApp({ now: () => now });
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
