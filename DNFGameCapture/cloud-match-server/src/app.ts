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
import {
  getMembership,
  joinRoom,
  leaveRoom,
  listRooms,
  renameBroadcaster,
} from './rooms.js';
import {
  registerBodySchema,
  roomJoinSchema,
  roomRenameSchema,
  socketAuthSchema,
} from './schemas.js';

export interface CreateCloudMatchAppOptions {
  databasePath?: string;
  now?: () => number;
}

export interface CloudMatchApp {
  expressApp: Express;
  httpServer: HttpServer;
  io: SocketIoServer;
  db: ReturnType<typeof openDatabase>;
  close(): Promise<void>;
}

interface AuthenticatedSocketData {
  deviceId: string;
}

type Acknowledge = (response: unknown) => void;

function createSocketError(code: string): Error & { data: { code: string } } {
  const error = new Error(code === 'unsupported_protocol' ? 'Unsupported protocol' : 'Authentication failed') as Error & {
    data: { code: string };
  };
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

export function createCloudMatchApp(
  options: CreateCloudMatchAppOptions = {},
): CloudMatchApp {
  const now = options.now ?? (() => Math.floor(Date.now() / 1_000));
  const db = openDatabase(options.databasePath ?? serverConfig.databasePath);
  const expressApp = express();
  const httpServer = createServer(expressApp);
  const io = new SocketIoServer(httpServer);
  const activeSockets = new Map<string, Socket>();
  let closed = false;

  expressApp.use(express.json());
  expressApp.get('/health', (_request, response) => {
    response.json({ ok: true });
  });
  expressApp.post('/api/devices/register', (request, response) => {
    const parsed = registerBodySchema.safeParse(request.body);
    if (!parsed.success) {
      response.status(400).json({ ok: false, code: 'invalid_request' });
      return;
    }

    response.status(201).json(registerDevice(db, parsed.data.deviceId, now()));
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
    touchLastSeen(db, deviceId, now());

    socket.on('room:list', (...args: unknown[]) => {
      const acknowledge = args.at(-1) as Acknowledge | undefined;
      if (typeof acknowledge === 'function') {
        acknowledge({ ok: true, rooms: listRooms(db) });
      }
    });

    socket.on('room:join', async (payload: unknown, acknowledge?: Acknowledge) => {
      if (typeof acknowledge !== 'function') {
        return;
      }
      const parsed = roomJoinSchema.safeParse(payload);
      if (!parsed.success) {
        acknowledge({
          ok: false,
          code: invalidBroadcasterName(parsed.error)
            ? 'invalid_broadcaster_name'
            : 'invalid_request',
        });
        return;
      }

      const previousMembership = getMembership(db, deviceId);
      const membership = joinRoom(
        db,
        deviceId,
        parsed.data.roomId,
        parsed.data.broadcasterName,
        now(),
      );
      if (!membership) {
        acknowledge({ ok: false, code: 'room_not_found' });
        return;
      }

      if (previousMembership && previousMembership.room.id !== membership.room.id) {
        await socket.leave(roomNamespace(previousMembership.room.id));
      }
      await socket.join(roomNamespace(membership.room.id));
      acknowledge({ ok: true, ...membership });
    });

    socket.on('room:rename', (payload: unknown, acknowledge?: Acknowledge) => {
      if (typeof acknowledge !== 'function') {
        return;
      }
      const parsed = roomRenameSchema.safeParse(payload);
      if (!parsed.success) {
        acknowledge({
          ok: false,
          code: invalidBroadcasterName(parsed.error)
            ? 'invalid_broadcaster_name'
            : 'invalid_request',
        });
        return;
      }

      const membership = renameBroadcaster(db, deviceId, parsed.data.broadcasterName, now());
      if (!membership) {
        acknowledge({ ok: false, code: 'not_in_room' });
        return;
      }
      acknowledge({ ok: true, ...membership });
    });

    socket.on('room:leave', (...args: unknown[]) => {
      const acknowledge = args.at(-1) as Acknowledge | undefined;
      if (typeof acknowledge !== 'function') {
        return;
      }
      const membership = getMembership(db, deviceId);
      leaveRoom(db, deviceId);
      if (membership) {
        void socket.leave(roomNamespace(membership.room.id));
      }
      acknowledge({ ok: true });
    });

    socket.on('room:status', (...args: unknown[]) => {
      const acknowledge = args.at(-1) as Acknowledge | undefined;
      if (typeof acknowledge === 'function') {
        acknowledge({
          ok: true,
          membership: getMembership(db, deviceId),
          online: activeSockets.get(deviceId) === socket,
        });
      }
    });

    socket.on('disconnect', () => {
      touchLastSeen(db, deviceId, now());
      if (activeSockets.get(deviceId) === socket) {
        activeSockets.delete(deviceId);
      }
    });
  });

  return {
    expressApp,
    httpServer,
    io,
    db,
    async close(): Promise<void> {
      if (closed) {
        return;
      }
      closed = true;
      await new Promise<void>((resolve) => {
        io.close(() => resolve());
      });
      if (httpServer.listening) {
        await new Promise<void>((resolve, reject) => {
          httpServer.close((error) => (error ? reject(error) : resolve()));
        });
      }
      db.close();
    },
  };
}
