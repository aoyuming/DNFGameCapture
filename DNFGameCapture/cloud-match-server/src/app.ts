import { createServer, type Server as HttpServer } from 'node:http';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
} from 'express';
import { Server as SocketIoServer } from 'socket.io';

import { compareRoomSnapshots } from './comparison.js';
import { serverConfig } from './config.js';
import { openDatabase } from './db.js';
import { registerDevice } from './identity.js';
import * as defaultRoomService from './rooms.js';
import { registerBodySchema } from './schemas.js';
import {
  registerCloudMatchSocketHandlers,
  type ComparisonService,
  type RoomService,
  type SnapshotService,
  type SocketRoomAdapter,
} from './socket.js';
import { getRoomSnapshots, getSnapshot, saveSnapshot } from './snapshots.js';

type DatabaseConnection = ReturnType<typeof openDatabase>;

export type {
  ComparisonService,
  RoomService,
  SnapshotService,
  SocketRoomAdapter,
} from './socket.js';

export interface CreateCloudMatchAppOptions {
  databasePath?: string;
  now?: () => number;
  roomService?: Partial<RoomService>;
  snapshotService?: Partial<SnapshotService>;
  comparisonService?: Partial<ComparisonService>;
  socketRoomAdapter?: SocketRoomAdapter;
}

export interface CloudMatchApp {
  expressApp: Express;
  httpServer: HttpServer;
  io: SocketIoServer;
  db: DatabaseConnection;
  close(): Promise<void>;
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
  const roomService: RoomService = {
    ...defaultRoomService,
    ...options.roomService,
  };
  const snapshotService: SnapshotService = {
    saveSnapshot,
    getSnapshot,
    getRoomSnapshots,
    ...options.snapshotService,
  };
  const comparisonService: ComparisonService = {
    compareRoomSnapshots,
    ...options.comparisonService,
  };
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

  const socketHandlers = registerCloudMatchSocketHandlers({
    io,
    db,
    now,
    roomService,
    snapshotService,
    comparisonService,
    socketRoomAdapter,
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
          socketHandlers.close();
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
