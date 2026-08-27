import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { AddressInfo } from 'node:net';
import { io as createSocketClient, type Socket } from 'socket.io-client';
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';

import { createCloudMatchApp } from '../src/app.js';
import * as comparisonStore from '../src/comparison.js';
import * as roomStore from '../src/rooms.js';
import type { MatchSnapshot, Player } from '../src/schemas.js';
import * as snapshotStore from '../src/snapshots.js';

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

interface ConnectedDevice extends Registration {
  socket: Socket;
}

interface RoomChanged {
  roomId: string;
  roomRevision: number;
}

interface RoomPresence extends RoomChanged {
  deviceId: string;
  online: boolean;
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

function player(mainName: string, seed: number): Player {
  return {
    mainName,
    aliases: [`${mainName} Alt`],
    kills: seed,
    deaths: seed + 1,
    ak: seed + 2,
    streak: seed + 3,
  };
}

function snapshot(
  clientRevision: number,
  overrides: Partial<MatchSnapshot> = {},
): MatchSnapshot {
  return {
    schemaVersion: 1,
    clientRevision,
    clientTime: 1_700_000_000 + clientRevision,
    changeSource: 'ocr',
    redScore: 3,
    blueScore: 2,
    redPlayers: [
      player('Red One', 0),
      player('Red Two', 4),
      player('Red Three', 8),
      player('Red Four', 12),
    ],
    bluePlayers: [
      player('Blue One', 16),
      player('Blue Two', 20),
      player('Blue Three', 24),
      player('Blue Four', 28),
    ],
    redPickFirst: true,
    teamsFlipped: false,
    outputSeatLabel: true,
    lastKillTeam: 'red',
    ...overrides,
  };
}

async function startApp(options: AppOptions = {}): Promise<RunningApp> {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-socket-'));
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

async function connectRegistered(
  url: string,
  registration: Registration,
): Promise<Socket> {
  return new Promise((resolve, reject) => {
    const socket = createSocketClient(url, {
      auth: { ...registration, protocolVersion: 1 },
      forceNew: true,
      reconnection: false,
      timeout: 2_000,
      transports: ['websocket'],
    });
    sockets.push(socket);
    socket.once('connect', () => resolve(socket));
    socket.once('connect_error', reject);
  });
}

async function createDevice(
  url: string,
  deviceId: string,
): Promise<ConnectedDevice> {
  const registration = await register(url, deviceId);
  return {
    ...registration,
    socket: await connectRegistered(url, registration),
  };
}

function emitAck<T>(
  socket: Socket,
  event: string,
  payload?: unknown,
  timeoutMs = 5_000,
): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(
      () => reject(new Error(`Timed out waiting for ${event} ACK`)),
      timeoutMs,
    );
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

function waitForEvent<T>(
  socket: Socket,
  event: string,
  timeoutMs = 2_000,
): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(
      () => reject(new Error(`Timed out waiting for ${event}`)),
      timeoutMs,
    );
    socket.once(event, (payload: T) => {
      clearTimeout(timer);
      resolve(payload);
    });
  });
}

async function waitUntil(
  assertion: () => void | Promise<void>,
  timeoutMs = 2_000,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  let lastError: unknown;
  do {
    try {
      await assertion();
      return;
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
  } while (Date.now() < deadline);
  throw lastError;
}

async function joinRoom(
  socket: Socket,
  roomId: string,
  broadcasterName: string,
): Promise<unknown> {
  return emitAck(socket, 'room:join', { roomId, broadcasterName });
}

function roomRevision(app: CloudMatchApp, roomId: string): number {
  return (
    app.db.prepare('select revision from rooms where id = ?').get(roomId) as {
      revision: number;
    }
  ).revision;
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

describe('cloud match Socket.IO integration', () => {
  test('compares eight authenticated room members and exposes only safe summaries and previews', async () => {
    const { url } = await startApp();
    const devices: ConnectedDevice[] = [];
    for (let index = 1; index <= 8; index += 1) {
      const suffix = index.toString().padStart(4, '0');
      const device = await createDevice(url, `capture-sync-${suffix}`);
      await joinRoom(device.socket, '59', `Broadcaster ${index}`);
      devices.push(device);
    }

    const uploaded = await Promise.all(
      devices.map((device, index) =>
        emitAck<Record<string, unknown>>(device.socket, 'snapshot:upload', {
          snapshot: snapshot(index + 1),
        }),
      ),
    );
    expect(uploaded.map((result) => result.ok)).toEqual(Array(8).fill(true));
    expect(uploaded.map((result) => result.acceptedRevision)).toEqual([
      1, 2, 3, 4, 5, 6, 7, 8,
    ]);

    const comparison = await emitAck<{
      ok: boolean;
      generatedAt: number;
      roomRevision: number;
      consensusDeviceId: string;
      members: Array<Record<string, unknown>>;
      groups: Array<{ memberDeviceIds: string[] }>;
    }>(devices[0].socket, 'room:comparison', {});

    expect(comparison).toMatchObject({
      ok: true,
      generatedAt: 1_700_000_000,
      roomRevision: 16,
      consensusDeviceId: 'capture-sync-0001',
    });
    expect(comparison.members).toHaveLength(8);
    expect(comparison.members).toEqual(
      expect.arrayContaining(
        devices.map((device, index) =>
          expect.objectContaining({
            deviceId: device.deviceId,
            broadcasterName: `Broadcaster ${index + 1}`,
            deviceSuffix: (index + 1).toString().padStart(4, '0'),
            online: true,
            state: 'updated',
            clientRevision: index + 1,
            receivedAt: 1_700_000_000,
            identityMatchPercent: 100,
            similarity: 100,
          }),
        ),
      ),
    );
    expect(comparison.groups).toEqual([
      { id: 'group-1', memberDeviceIds: devices.map((device) => device.deviceId) },
    ]);

    const preview = await emitAck<Record<string, unknown>>(
      devices[0].socket,
      'snapshot:get',
      { targetDeviceId: devices[7].deviceId, clientRevision: 8 },
    );
    expect(preview).toEqual({
      ok: true,
      targetDeviceId: devices[7].deviceId,
      broadcasterName: 'Broadcaster 8',
      deviceSuffix: '0008',
      receivedAt: 1_700_000_000,
      clientRevision: 8,
      snapshot: snapshot(8),
    });
    await expect(
      emitAck(devices[0].socket, 'snapshot:get', {
        targetDeviceId: devices[7].deviceId,
        clientRevision: 7,
      }),
    ).resolves.toEqual({ ok: false, code: 'snapshot_revision_changed' });

    const serialized = JSON.stringify({ comparison, preview });
    for (const device of devices) {
      expect(serialized).not.toContain(device.deviceToken);
    }
    expect(serialized).not.toMatch(/token_hash|payload_json|content_hash|contentHash/);
    expect(comparison).not.toHaveProperty('pairwise');
    expect(comparison.members.every((member) => !('snapshot' in member))).toBe(true);
  });

  test('keeps comparisons and manual previews isolated to current membership', async () => {
    const { url } = await startApp();
    const caller = await createDevice(url, 'isolation-caller-0001');
    const noData = await createDevice(url, 'isolation-nodata-0002');
    const otherRoom = await createDevice(url, 'isolation-other-0003');
    const unjoined = await createDevice(url, 'isolation-unjoined-0004');
    await joinRoom(caller.socket, '59', 'Caller');
    await joinRoom(noData.socket, '59', 'No Data');
    await joinRoom(otherRoom.socket, 'li-yong', 'Other Room');
    await emitAck(caller.socket, 'snapshot:upload', { snapshot: snapshot(1) });
    await emitAck(otherRoom.socket, 'snapshot:upload', {
      snapshot: snapshot(1, { redScore: 9 }),
    });

    const comparison = await emitAck<{
      ok: boolean;
      members: Array<Record<string, unknown>>;
    }>(caller.socket, 'room:comparison', {});
    expect(comparison.ok).toBe(true);
    expect(comparison.members).toHaveLength(2);
    expect(comparison.members).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          deviceId: caller.deviceId,
          state: 'updated',
          online: true,
        }),
        {
          deviceId: noData.deviceId,
          broadcasterName: 'No Data',
          deviceSuffix: '0002',
          online: true,
          state: 'no_data',
        },
      ]),
    );
    expect(comparison.members).not.toEqual(
      expect.arrayContaining([expect.objectContaining({ deviceId: otherRoom.deviceId })]),
    );

    await expect(
      emitAck(caller.socket, 'snapshot:get', {
        targetDeviceId: otherRoom.deviceId,
        clientRevision: 1,
      }),
    ).resolves.toEqual({ ok: false, code: 'target_not_found' });
    await expect(
      emitAck(caller.socket, 'snapshot:get', {
        targetDeviceId: noData.deviceId,
        clientRevision: 1,
      }),
    ).resolves.toEqual({ ok: false, code: 'target_has_no_snapshot' });
    await expect(
      emitAck(caller.socket, 'snapshot:get', {
        targetDeviceId: 'bad id',
        clientRevision: 1,
      }),
    ).resolves.toEqual({ ok: false, code: 'invalid_request' });
    await expect(
      emitAck(caller.socket, 'room:comparison', { extra: true }),
    ).resolves.toEqual({ ok: false, code: 'invalid_request' });
    await expect(
      emitAck(unjoined.socket, 'room:comparison', {}),
    ).resolves.toEqual({ ok: false, code: 'not_in_room' });
    await expect(
      emitAck(unjoined.socket, 'snapshot:get', {
        targetDeviceId: caller.deviceId,
        clientRevision: 1,
      }),
    ).resolves.toEqual({ ok: false, code: 'not_in_room' });
  });

  test('fetches only the explicitly requested snapshot revision', async () => {
    const { url } = await startApp();
    const viewer = await createDevice(url, 'revision-viewer-0001');
    const source = await createDevice(url, 'revision-source-0002');
    await joinRoom(viewer.socket, '59', 'Viewer');
    await joinRoom(source.socket, '59', 'Source');
    await emitAck(source.socket, 'snapshot:upload', { snapshot: snapshot(4) });

    await expect(
      emitAck(viewer.socket, 'snapshot:get', {
        targetDeviceId: source.deviceId,
        clientRevision: 4,
      }),
    ).resolves.toMatchObject({
      ok: true,
      targetDeviceId: source.deviceId,
      clientRevision: 4,
      snapshot: { clientRevision: 4 },
    });
    await expect(
      emitAck(viewer.socket, 'snapshot:get', {
        targetDeviceId: source.deviceId,
        clientRevision: 3,
      }),
    ).resolves.toEqual({ ok: false, code: 'snapshot_revision_changed' });
  });

  test('bumps presence revisions across disconnect, reconnect, and disconnect', async () => {
    const { app, url } = await startApp();
    const uploader = await createDevice(url, 'presence-uploader-0001');
    const viewer = await createDevice(url, 'presence-viewer-0002');
    await joinRoom(uploader.socket, '59', 'Uploader');
    await joinRoom(viewer.socket, '59', 'Viewer');
    await emitAck(uploader.socket, 'snapshot:upload', { snapshot: snapshot(7) });
    const revisionBeforeDisconnect = roomRevision(app, '59');

    const firstOffline = waitForEvent<RoomPresence>(viewer.socket, 'room:presence');
    uploader.socket.disconnect();
    await expect(firstOffline).resolves.toEqual({
      roomId: '59',
      roomRevision: revisionBeforeDisconnect + 1,
      deviceId: uploader.deviceId,
      online: false,
    });
    await waitUntil(async () => {
      const comparison = await emitAck<{
        members: Array<Record<string, unknown>>;
      }>(viewer.socket, 'room:comparison', {});
      expect(comparison.members).toEqual(
        expect.arrayContaining([
          expect.objectContaining({
            deviceId: uploader.deviceId,
            online: false,
            clientRevision: 7,
            receivedAt: 1_700_000_000,
          }),
        ]),
      );
    }, 5_000);

    const backOnline = waitForEvent<RoomPresence>(viewer.socket, 'room:presence');
    const reconnected = await connectRegistered(url, {
      deviceId: uploader.deviceId,
      deviceToken: uploader.deviceToken,
    });
    await expect(backOnline).resolves.toEqual({
      roomId: '59',
      roomRevision: revisionBeforeDisconnect + 2,
      deviceId: uploader.deviceId,
      online: true,
    });

    const secondOffline = waitForEvent<RoomPresence>(viewer.socket, 'room:presence');
    reconnected.disconnect();
    await expect(secondOffline).resolves.toEqual({
      roomId: '59',
      roomRevision: revisionBeforeDisconnect + 3,
      deviceId: uploader.deviceId,
      online: false,
    });

    await expect(
      emitAck(viewer.socket, 'snapshot:get', {
        targetDeviceId: uploader.deviceId,
        clientRevision: 7,
      }),
    ).resolves.toMatchObject({
      ok: true,
      targetDeviceId: uploader.deviceId,
      receivedAt: 1_700_000_000,
      clientRevision: 7,
      snapshot: { clientRevision: 7 },
    });
    expect(roomRevision(app, '59')).toBe(revisionBeforeDisconnect + 3);
  });

  test('replacement connection emits no transient offline presence', async () => {
    const { app, url } = await startApp();
    const device = await createDevice(url, 'replacement-device-0001');
    const observer = await createDevice(url, 'replacement-observer-0002');
    await joinRoom(device.socket, '59', 'Replaced');
    await joinRoom(observer.socket, '59', 'Observer');
    const presence: RoomPresence[] = [];
    observer.socket.on('room:presence', (event: RoomPresence) => presence.push(event));
    const replaced = waitForEvent(device.socket, 'connection:replaced');

    const replacement = await connectRegistered(url, {
      deviceId: device.deviceId,
      deviceToken: device.deviceToken,
    });

    await replaced;
    await waitUntil(() => expect(presence).toHaveLength(1));
    await new Promise((resolve) => setTimeout(resolve, 30));
    expect(replacement.connected).toBe(true);
    expect(presence).toEqual([
      {
        roomId: '59',
        roomRevision: 3,
        deviceId: device.deviceId,
        online: true,
      },
    ]);
    expect(roomRevision(app, '59')).toBe(3);
  });

  test('switching rooms notifies both rooms and never copies the old snapshot', async () => {
    const { app, url } = await startApp();
    const mover = await createDevice(url, 'switch-mover-0001');
    const observer = await createDevice(url, 'switch-observer-0002');
    await joinRoom(mover.socket, '59', 'Mover');
    await joinRoom(observer.socket, '59', 'Observer');
    await emitAck(mover.socket, 'snapshot:upload', { snapshot: snapshot(1) });
    const moverChanges: RoomChanged[] = [];
    const observerChanges: RoomChanged[] = [];
    mover.socket.on('room:changed', (event: RoomChanged) => moverChanges.push(event));
    observer.socket.on('room:changed', (event: RoomChanged) => observerChanges.push(event));

    await expect(
      joinRoom(mover.socket, 'li-yong', 'Mover'),
    ).resolves.toMatchObject({ ok: true, room: { id: 'li-yong' } });
    await waitUntil(() => {
      expect(observerChanges).toContainEqual({ roomId: '59', roomRevision: 4 });
      expect(moverChanges).toEqual(
        expect.arrayContaining([
          { roomId: '59', roomRevision: 4 },
          { roomId: 'li-yong', roomRevision: 1 },
        ]),
      );
    });

    const oldComparison = await emitAck<{ members: Array<{ deviceId: string }> }>(
      observer.socket,
      'room:comparison',
      {},
    );
    expect(oldComparison.members.map((member) => member.deviceId)).toEqual([
      observer.deviceId,
    ]);

    const newComparison = await emitAck<{
      roomRevision: number;
      members: Array<Record<string, unknown>>;
    }>(mover.socket, 'room:comparison', {});
    expect(newComparison).toMatchObject({ roomRevision: 1 });
    expect(newComparison.members).toEqual([
      {
        deviceId: mover.deviceId,
        broadcasterName: 'Mover',
        deviceSuffix: '0001',
        online: true,
        state: 'no_data',
      },
    ]);
    await expect(
      emitAck(observer.socket, 'snapshot:get', {
        targetDeviceId: mover.deviceId,
        clientRevision: 1,
      }),
    ).resolves.toEqual({ ok: false, code: 'target_not_found' });
    expect(
      (
        app.db
          .prepare('select count(*) as count from snapshots where device_id = ?')
          .get(mover.deviceId) as { count: number }
      ).count,
    ).toBe(0);
  });

  test('increments and emits revisions only for accepted actual changes', async () => {
    const { app, url } = await startApp();
    const device = await createDevice(url, 'revision-device-0001');
    const changes: RoomChanged[] = [];
    device.socket.on('room:changed', (event: RoomChanged) => changes.push(event));

    await joinRoom(device.socket, '59', 'Original');
    await waitUntil(() => expect(changes).toHaveLength(1));
    expect(changes[0]).toEqual({ roomId: '59', roomRevision: 1 });

    await joinRoom(device.socket, '59', 'Original');
    await emitAck(device.socket, 'room:rename', { broadcasterName: 'Original' });
    await new Promise((resolve) => setTimeout(resolve, 30));
    expect(roomRevision(app, '59')).toBe(1);
    expect(changes).toHaveLength(1);

    await emitAck(device.socket, 'room:rename', { broadcasterName: 'Renamed' });
    await expect(
      emitAck<Record<string, unknown>>(device.socket, 'snapshot:upload', {
        snapshot: snapshot(1),
      }),
    ).resolves.toEqual({ ok: true, acceptedRevision: 1, roomRevision: 3 });
    await waitUntil(() => expect(changes).toHaveLength(3));

    await expect(
      emitAck(device.socket, 'snapshot:upload', { snapshot: snapshot(2) }),
    ).resolves.toEqual({ ok: false, code: 'duplicate_snapshot' });
    await expect(
      emitAck(device.socket, 'snapshot:upload', {
        snapshot: snapshot(1, { redScore: 9 }),
      }),
    ).resolves.toEqual({ ok: false, code: 'stale_revision' });
    await expect(
      emitAck(device.socket, 'snapshot:upload', { snapshot: {} }),
    ).resolves.toEqual({ ok: false, code: 'invalid_snapshot' });
    expect(roomRevision(app, '59')).toBe(3);
    expect(changes).toHaveLength(3);

    await expect(
      emitAck(device.socket, 'snapshot:upload', {
        snapshot: snapshot(2, { redScore: 4 }),
      }),
    ).resolves.toEqual({ ok: true, acceptedRevision: 2, roomRevision: 4 });
    await emitAck(device.socket, 'room:leave');
    await waitUntil(() => expect(changes).toHaveLength(5));
    expect(changes).toEqual([
      { roomId: '59', roomRevision: 1 },
      { roomId: '59', roomRevision: 2 },
      { roomId: '59', roomRevision: 3 },
      { roomId: '59', roomRevision: 4 },
      { roomId: '59', roomRevision: 5 },
    ]);

    await emitAck(device.socket, 'room:leave');
    await new Promise((resolve) => setTimeout(resolve, 30));
    expect(roomRevision(app, '59')).toBe(5);
    expect(changes).toHaveLength(5);
  });

  test('drops a delayed older changed revision after a newer announcement', async () => {
    const delayedLookupEntered = createDeferred<void>();
    const releaseDelayedLookup = createDeferred<void>();
    const delayedLookupReturned = createDeferred<void>();
    let revisionToDelay: number | null = null;
    const { url } = await startApp({
      roomService: {
        async getRoomRevision(db, roomId) {
          const revision = roomStore.getRoomRevision(db, roomId);
          if (revisionToDelay === revision) {
            revisionToDelay = null;
            delayedLookupEntered.resolve();
            await releaseDelayedLookup.promise;
            delayedLookupReturned.resolve();
          }
          return revision;
        },
      },
    });
    const delayed = await createDevice(url, 'ordering-device-0001');
    const observer = await createDevice(url, 'ordering-device-0002');
    await joinRoom(delayed.socket, '59', 'Delayed');
    await joinRoom(observer.socket, '59', 'Observer');

    const notifications: RoomChanged[] = [];
    observer.socket.on('room:changed', (event: RoomChanged) => {
      notifications.push(event);
    });

    revisionToDelay = 3;
    const olderRename = emitAck(delayed.socket, 'room:rename', {
      broadcasterName: 'Delayed Rename',
    });
    await delayedLookupEntered.promise;
    await emitAck(observer.socket, 'room:rename', { broadcasterName: 'Renamed' });
    releaseDelayedLookup.resolve();
    await delayedLookupReturned.promise;
    await olderRename;

    const revisions = notifications.map((notification) => notification.roomRevision);
    expect(revisions.every((revision, index) => index === 0 || revision > revisions[index - 1]))
      .toBe(true);
    expect(revisions.at(-1)).toBe(4);
    expect(notifications).toEqual([
      { roomId: '59', roomRevision: 4 },
    ]);
  });

  test('serializes 400 in-flight revisions without starving the event loop', async () => {
    const { app, url } = await startApp();
    const device = await createDevice(url, 'rapid-device-0001');
    await joinRoom(device.socket, '59', 'Rapid');
    let timerTicks = 0;
    const timer = setInterval(() => {
      timerTicks += 1;
    }, 1);

    let responses: Array<{ ok: boolean; acceptedRevision: number }>;
    try {
      responses = await Promise.all(
        Array.from({ length: 400 }, (_, index) => {
          const revision = index + 1;
          return emitAck<{ ok: boolean; acceptedRevision: number }>(
            device.socket,
            'snapshot:upload',
            { snapshot: snapshot(revision, { redScore: revision }) },
            15_000,
          );
        }),
      );
    } finally {
      clearInterval(timer);
    }

    expect(responses).toHaveLength(400);
    expect(responses.every((response) => response.ok)).toBe(true);
    expect(responses.at(-1)?.acceptedRevision).toBe(400);
    expect(timerTicks).toBeGreaterThan(0);
    expect(
      app.db
        .prepare('select client_revision from snapshots where device_id = ?')
        .get(device.deviceId),
    ).toEqual({ client_revision: 400 });
    expect(roomRevision(app, '59')).toBe(401);
    await expect(
      emitAck(device.socket, 'room:comparison', {}),
    ).resolves.toMatchObject({
      ok: true,
      roomRevision: 401,
      members: [expect.objectContaining({ clientRevision: 400 })],
    });
  }, 30_000);

  test('bounds malformed and oversized input without crashing or mutating state', async () => {
    const { app, url } = await startApp();
    const joined = await createDevice(url, 'validation-joined-0001');
    const unjoined = await createDevice(url, 'validation-unjoined-0002');
    await joinRoom(joined.socket, '59', 'Joined');

    await expect(
      emitAck(joined.socket, 'snapshot:upload', { snapshot: { clientRevision: 1 } }),
    ).resolves.toEqual({ ok: false, code: 'invalid_snapshot' });
    await expect(
      emitAck(joined.socket, 'snapshot:upload', {
        snapshot: snapshot(1),
        extra: true,
      }),
    ).resolves.toEqual({ ok: false, code: 'invalid_request' });
    await expect(
      emitAck(unjoined.socket, 'snapshot:upload', { snapshot: snapshot(1) }),
    ).resolves.toEqual({ ok: false, code: 'no_membership' });
    expect(roomRevision(app, '59')).toBe(1);

    const disconnected = waitForEvent<string>(joined.socket, 'disconnect', 3_000);
    let acknowledged = false;
    joined.socket.emit(
      'snapshot:upload',
      {
        snapshot: {
          ...snapshot(1),
          padding: 'x'.repeat(70_000),
        },
      },
      () => {
        acknowledged = true;
      },
    );
    await expect(disconnected).resolves.toBeTypeOf('string');
    expect(acknowledged).toBe(false);
    expect(
      (
        app.db.prepare('select count(*) as count from snapshots').get() as {
          count: number;
        }
      ).count,
    ).toBe(0);
    expect(roomRevision(app, '59')).toBe(2);

    await expect(emitAck(unjoined.socket, 'room:list')).resolves.toMatchObject({
      ok: true,
    });
  });

  test('sanitizes injected snapshot and comparison service failures', async () => {
    let failSave = true;
    let failComparison = true;
    const { app, url } = await startApp({
      snapshotService: {
        saveSnapshot(db, input) {
          const result = snapshotStore.saveSnapshot(db, input);
          if (failSave) {
            failSave = false;
            throw new Error('sensitive post-save storage failure');
          }
          return result;
        },
      },
      comparisonService: {
        compareRoomSnapshots(rows, nowSec) {
          if (failComparison) {
            failComparison = false;
            throw new Error('sensitive comparison failure');
          }
          return comparisonStore.compareRoomSnapshots(rows, nowSec);
        },
      },
    });
    const device = await createDevice(url, 'failure-device-0001');
    await joinRoom(device.socket, '59', 'Failure Test');

    await expect(
      emitAck(device.socket, 'snapshot:upload', { snapshot: snapshot(1) }),
    ).resolves.toEqual({ ok: false, code: 'internal_error' });
    expect(
      (app.db.prepare('select count(*) as count from snapshots').get() as { count: number })
        .count,
    ).toBe(0);
    expect(roomRevision(app, '59')).toBe(1);

    await expect(
      emitAck(device.socket, 'snapshot:upload', { snapshot: snapshot(1) }),
    ).resolves.toEqual({ ok: true, acceptedRevision: 1, roomRevision: 2 });
    await expect(
      emitAck(device.socket, 'room:comparison', {}),
    ).resolves.toEqual({ ok: false, code: 'internal_error' });
    await expect(
      emitAck(device.socket, 'room:comparison', {}),
    ).resolves.toMatchObject({ ok: true, roomRevision: 2 });
  });
});
