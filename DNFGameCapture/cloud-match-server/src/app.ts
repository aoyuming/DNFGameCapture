import { createServer, type Server as HttpServer } from 'node:http';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
} from 'express';
import { Server as SocketIoServer, type Socket } from 'socket.io';

import { serverConfig } from './config.js';
import { openDatabase } from './db.js';
import { authenticateDevice, registerDevice, touchLastSeen } from './identity.js';
import * as defaultRoomService from './rooms.js';
import type { MembershipDto, RoomDto } from './rooms.js';
import {
  registerBodySchema,
  roomJoinSchema,
  roomRenameSchema,
  socketAuthSchema,
} from './schemas.js';

type DatabaseConnection = ReturnType<typeof openDatabase>;
type MaybePromise<T> = T | Promise<T>;
type Acknowledge = (response: unknown) => void;

export interface RoomService {
  listRooms(db: DatabaseConnection): MaybePromise<RoomDto[]>;
  getMembership(db: DatabaseConnection, deviceId: string): MaybePromise<MembershipDto | null>;
  joinRoom(
    db: DatabaseConnection,
    deviceId: string,
    roomId: string,
    broadcasterName: string,
    nowSec: number,
  ): MaybePromise<MembershipDto | null>;
  renameBroadcaster(
    db: DatabaseConnection,
    deviceId: string,
    broadcasterName: string,
    nowSec: number,
  ): MaybePromise<MembershipDto | null>;
  leaveRoom(db: DatabaseConnection, deviceId: string): MaybePromise<void>;
}

export interface SocketRoomAdapter {
  join(socket: Socket, room: string): Promise<void>;
  leave(socket: Socket, room: string): Promise<void>;
}

export interface CreateCloudMatchAppOptions {
  databasePath?: string;
  now?: () => number;
  roomService?: Partial<RoomService>;
  socketRoomAdapter?: SocketRoomAdapter;
}

export interface CloudMatchApp {
  expressApp: Express;
  httpServer: HttpServer;
  io: SocketIoServer;
  db: DatabaseConnection;
  close(): Promise<void>;
}

interface AuthenticatedSocketData {
  deviceId: string;
}

function createSocketError(code: string): Error & { data: { code: string } } {
  const message = code === 'unsupported_protocol' ? 'Unsupported protocol' : 'Authentication failed';
  const error = new Error(message) as Error & { data: { code: string } };
  error.data = { code };
  return error;
}

function invalidBroadcasterName(error: { issues: Array<{ path: Array<string | number> }> }): boolean {
  return error.issues.some((issue) => issue.path[0] === 'broadcasterName');
}

function roomNamespace(roomId: string): string {
  return `room:${roomId}`;
}

function handleExpressError(
  error: unknown,
  _request: Request,
  response: Response,
  _next: NextFunction,
): void {
  const status =
    typeof error === 'object' &&
    error !== null &&
    'status' in error &&
    typeof error.status === 'number' &&
    error.status >= 400 &&
    error.status < 500
      ? error.status
      : 500;
  response.status(status).json({
    ok: false,
    code: status === 500 ? 'internal_error' : 'invalid_request',
  });
}

async function setAdapterMembership(
  adapter: SocketRoomAdapter,
  socket: Socket,
  previousRoomId: string | null,
  nextRoomId: string | null,
): Promise<void> {
  if (previousRoomId === nextRoomId) {
    if (nextRoomId) {
      await adapter.join(socket, roomNamespace(nextRoomId));
    }
    return;
  }
  if (previousRoomId) {
    await adapter.leave(socket, roomNamespace(previousRoomId));
  }
  if (nextRoomId) {
    await adapter.join(socket, roomNamespace(nextRoomId));
  }
}

async function restoreAdapterMembership(
  adapter: SocketRoomAdapter,
  socket: Socket,
  expectedRoomId: string | null,
  attemptedRoomId: string | null,
): Promise<void> {
  if (attemptedRoomId && attemptedRoomId !== expectedRoomId) {
    const attemptedNamespace = roomNamespace(attemptedRoomId);
    if (socket.rooms.has(attemptedNamespace)) {
      await adapter.leave(socket, attemptedNamespace);
    }
  }
  if (expectedRoomId) {
    const expectedNamespace = roomNamespace(expectedRoomId);
    if (!socket.rooms.has(expectedNamespace)) {
      await adapter.join(socket, expectedNamespace);
    }
  }
}

function safeAcknowledge(acknowledge: Acknowledge, response: unknown): void {
  try {
    acknowledge(response);
  } catch {
    // The client may disconnect between completing an operation and its ACK.
  }
}

export function createCloudMatchApp(
  options: CreateCloudMatchAppOptions = {},
): CloudMatchApp {
  const now = options.now ?? (() => Math.floor(Date.now() / 1_000));
  const roomService: RoomService = { ...defaultRoomService, ...options.roomService };
  const socketRoomAdapter: SocketRoomAdapter =
    options.socketRoomAdapter ?? {
      async join(socket, room): Promise<void> {
        await socket.join(room);
      },
      async leave(socket, room): Promise<void> {
        await socket.leave(room);
      },
    };
  const db = openDatabase(options.databasePath ?? serverConfig.databasePath);
  const expressApp = express();
  const httpServer = createServer(expressApp);
  const io = new SocketIoServer(httpServer, { maxHttpBufferSize: 65_536 });
  const activeSockets = new Map<string, Socket>();
  let closePromise: Promise<void> | undefined;

  expressApp.use(express.json({ limit: '4kb' }));
  expressApp.get('/health', (_request, response) => {
    response.json({ ok: true });
  });
  expressApp.post('/api/devices/register', (request, response) => {
    const parsed = registerBodySchema.safeParse(request.body);
    if (!parsed.success) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }

    const registration = registerDevice(db, parsed.data.deviceId, now());
    if (!registration) {
      response.status(409).json({ ok: false, code: 'device_already_registered' });
      return;
    }
    response.status(201).json(registration);
  });
  expressApp.use(handleExpressError);

  io.use((socket, next) => {
    const auth = socket.handshake.auth as Record<string, unknown>;
    const parsed = socketAuthSchema.safeParse(auth);
    if (!parsed.success) {
      const code =
        typeof auth?.protocolVersion === 'number' && auth.protocolVersion !== 1
          ? 'unsupported_protocol'
          : 'invalid_auth';
      next(createSocketError(code));
      return;
    }

    if (!authenticateDevice(db, parsed.data.deviceId, parsed.data.deviceToken)) {
      next(createSocketError('authentication_failed'));
      return;
    }

    (socket.data as AuthenticatedSocketData).deviceId = parsed.data.deviceId;
    next();
  });

  io.on('connection', (socket) => {
    const deviceId = (socket.data as AuthenticatedSocketData).deviceId;
    const previousSocket = activeSockets.get(deviceId);
    if (previousSocket && previousSocket.id !== socket.id) {
      previousSocket.emit('connection:replaced', {
        code: 'replaced_by_new_connection',
      });
      previousSocket.disconnect(true);
    }
    activeSockets.set(deviceId, socket);
    try {
      touchLastSeen(db, deviceId, now());
    } catch {
      socket.disconnect(true);
      return;
    }

    let mutationQueue = Promise.resolve();
    const bindAckEvent = (
      event: string,
      handler: (payload: unknown) => MaybePromise<unknown>,
      serializeMutation = false,
    ): void => {
      socket.on(event, (...args: unknown[]) => {
        const acknowledge = args.at(-1);
        if (typeof acknowledge !== 'function') {
          return;
        }
        const payload = args.length > 1 ? args[0] : undefined;
        const run = async (): Promise<void> => {
          let response: unknown;
          try {
            response = await handler(payload);
          } catch {
            response = { ok: false, code: 'internal_error' };
          }
          safeAcknowledge(acknowledge as Acknowledge, response);
        };

        if (serializeMutation) {
          mutationQueue = mutationQueue.then(run, run);
        } else {
          void run();
        }
      });
    };

    bindAckEvent('room:list', async () => ({ ok: true, rooms: await roomService.listRooms(db) }));

    bindAckEvent(
      'room:join',
      async (payload) => {
        const parsed = roomJoinSchema.safeParse(payload);
        if (!parsed.success) {
          return {
            ok: false,
            code: invalidBroadcasterName(parsed.error)
              ? 'invalid_broadcaster_name'
              : 'invalid_request',
          };
        }

        const rooms = await roomService.listRooms(db);
        if (!rooms.some((room) => room.id === parsed.data.roomId)) {
          return { ok: false, code: 'room_not_found' };
        }
        const previousMembership = await roomService.getMembership(db, deviceId);
        const previousRoomId = previousMembership?.room.id ?? null;
        try {
          await setAdapterMembership(
            socketRoomAdapter,
            socket,
            previousRoomId,
            parsed.data.roomId,
          );
          const membership = await roomService.joinRoom(
            db,
            deviceId,
            parsed.data.roomId,
            parsed.data.broadcasterName,
            now(),
          );
          if (!membership || membership.room.id !== parsed.data.roomId) {
            throw new Error('Membership persistence did not match the requested room');
          }
          return { ok: true, ...membership };
        } catch (error) {
          await restoreAdapterMembership(
            socketRoomAdapter,
            socket,
            previousRoomId,
            parsed.data.roomId,
          );
          throw error;
        }
      },
      true,
    );

    bindAckEvent(
      'room:rename',
      async (payload) => {
        const parsed = roomRenameSchema.safeParse(payload);
        if (!parsed.success) {
          return {
            ok: false,
            code: invalidBroadcasterName(parsed.error)
              ? 'invalid_broadcaster_name'
              : 'invalid_request',
          };
        }
        const membership = await roomService.renameBroadcaster(
          db,
          deviceId,
          parsed.data.broadcasterName,
          now(),
        );
        return membership
          ? { ok: true, ...membership }
          : { ok: false, code: 'not_in_room' };
      },
      true,
    );

    bindAckEvent(
      'room:leave',
      async () => {
        const previousMembership = await roomService.getMembership(db, deviceId);
        const previousRoomId = previousMembership?.room.id ?? null;
        try {
          await setAdapterMembership(socketRoomAdapter, socket, previousRoomId, null);
          await roomService.leaveRoom(db, deviceId);
          return { ok: true };
        } catch (error) {
          await restoreAdapterMembership(socketRoomAdapter, socket, previousRoomId, null);
          throw error;
        }
      },
      true,
    );

    bindAckEvent('room:status', async () => ({
      ok: true,
      membership: await roomService.getMembership(db, deviceId),
      online: activeSockets.get(deviceId) === socket,
    }));

    socket.on('disconnect', () => {
      try {
        touchLastSeen(db, deviceId, now());
      } catch {
        // Shutdown may close the database while disconnect notifications drain.
      }
      if (activeSockets.get(deviceId) === socket) {
        activeSockets.delete(deviceId);
      }
    });
  });

  const close = (): Promise<void> => {
    if (!closePromise) {
      closePromise = (async () => {
        try {
          await new Promise<void>((resolve) => {
            io.close(() => resolve());
          });
          if (httpServer.listening) {
            await new Promise<void>((resolve, reject) => {
              httpServer.close((error) => (error ? reject(error) : resolve()));
            });
          }
        } finally {
          if (db.open) {
            db.close();
          }
        }
      })();
    }
    return closePromise;
  };

  return { expressApp, httpServer, io, db, close };
}
