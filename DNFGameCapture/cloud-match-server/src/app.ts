import { randomBytes } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { createServer, type Server as HttpServer } from 'node:http';
import express, {
  type Express,
  type NextFunction,
  type Request,
  type Response,
} from 'express';
import { Server as SocketIoServer } from 'socket.io';

import { createCloudMatchAdminApp } from './admin.js';
import { doubaoSynthesize, VoiceStore, type Synthesize } from './voices.js';
import { TtsGenerator, ttsLimitsFromEnv } from './tts-generate.js';
import { createPublicLibraryRoute, VoiceLibrary } from './voice-library.js';
import { createPublicVoiceApi } from './voice-routes.js';
import { createBroadcasterAttributionService } from './broadcaster-attribution.js';
import { compareRoomSnapshots } from './comparison.js';
import { serverConfig } from './config.js';
import { openDatabase } from './db.js';
import { registerDevice } from './identity.js';
import {
  createCloudMatchRateLimitService,
  type CloudMatchRateLimitService,
} from './rate-limits.js';
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
import {
  initializeSyncRelationSchema,
  pruneSyncRelationData,
} from './sync-relations.js';
import { createV2Api } from './v2-api.js';

type DatabaseConnection = ReturnType<typeof openDatabase>;

export type {
  ComparisonService,
  RoomService,
  SnapshotService,
  SocketRoomAdapter,
} from './socket.js';

export interface CreateCloudMatchAppOptions {
  databasePath?: string;
  voiceSynthesize?: Synthesize;
  now?: () => number;
  roomService?: Partial<RoomService>;
  snapshotService?: Partial<SnapshotService>;
  comparisonService?: Partial<ComparisonService>;
  rateLimitService?: CloudMatchRateLimitService;
  resolveClientIp?: (
    transport: 'http' | 'socket',
    remoteAddress: string,
  ) => string;
  socketRoomAdapter?: SocketRoomAdapter;
  adminCsrfToken?: string;
  adminPassword?: string;
  v2ServerUrl?: string;
  allowLegacyPermanentKeys?: boolean;
}

export interface CloudMatchApp {
  expressApp: Express;
  httpServer: HttpServer;
  adminExpressApp: Express;
  adminHttpServer: HttpServer;
  io: SocketIoServer;
  db: DatabaseConnection;
  close(): Promise<void>;
}

// 可选：TTS_BLOCKLIST_FILE 指向一个 UTF-8 文本文件，每行一个额外的敏感词（# 开头为注释）。
function readTtsBlocklist(): string[] {
  const file = process.env.TTS_BLOCKLIST_FILE?.trim();
  if (!file) return [];
  try {
    return readFileSync(file, 'utf8').split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith('#'));
  } catch (error) {
    console.warn('[tts] 无法读取 TTS_BLOCKLIST_FILE：', error instanceof Error ? error.message : error);
    return [];
  }
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
  const rateLimitService =
    options.rateLimitService ?? createCloudMatchRateLimitService();
  const resolveClientIp = (
    transport: 'http' | 'socket',
    remoteAddress: string,
  ): string => {
    const resolved = options.resolveClientIp
      ? options.resolveClientIp(transport, remoteAddress)
      : remoteAddress;
    return resolved.trim().slice(0, 128) || 'unknown';
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
  const attribution = createBroadcasterAttributionService(db);
  const voices = new VoiceStore(db, options.voiceSynthesize);
  const tts = new TtsGenerator(db, voices, options.voiceSynthesize ?? doubaoSynthesize, now,
    ttsLimitsFromEnv(), readTtsBlocklist());
  const voiceLibrary = new VoiceLibrary(db, voices, options.voiceSynthesize ?? doubaoSynthesize, now);
  initializeSyncRelationSchema(db);
  pruneSyncRelationData(db, now());
  const expressApp = express();
  const httpServer = createServer(expressApp);
  const io = new SocketIoServer(httpServer, { maxHttpBufferSize: 65_536 });
  let closePromise: Promise<void> | undefined;

  expressApp.get('/health', (_request, response) => {
    response.json({ ok: true });
  });
  expressApp.get('/api/voice/library', createPublicLibraryRoute(voiceLibrary));
  expressApp.use('/api/voice', createPublicVoiceApi(voices));
  expressApp.use('/api/v2', createV2Api({
    db,
    now,
    serverUrl: options.v2ServerUrl ?? serverConfig.publicUrl,
    allowLegacyPermanentKeys: options.allowLegacyPermanentKeys ?? serverConfig.allowLegacyPermanentKeys,
    resolveClientIp: (remoteAddress) => resolveClientIp('http', remoteAddress),
    attribution,
    tts,
  }));
  // Keep the legacy registration endpoint small, while letting the v2
  // player-library router enforce its own larger payload limit.
  expressApp.use(express.json({ limit: '4kb' }));
  expressApp.post('/api/devices/register', (request, response) => {
    const ipAddress = resolveClientIp(
      'http',
      request.socket.remoteAddress ?? 'unknown',
    );
    if (!rateLimitService.allowRegistration(ipAddress, now())) {
      response.status(429).json({ ok: false, code: 'rate_limited' });
      return;
    }
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
    rateLimitService,
    resolveClientIp: (remoteAddress) => resolveClientIp('socket', remoteAddress),
    socketRoomAdapter,
    attribution,
  });
  const adminCsrfToken = options.adminCsrfToken ?? randomBytes(32).toString('base64url');
  const adminPassword = options.adminPassword ?? randomBytes(24).toString('base64url');
  const adminExpressApp = createCloudMatchAdminApp({
    db,
    now,
    csrfToken: adminCsrfToken,
    voices,
    tts,
    voiceLibrary,
    adminPassword,
    socketController: socketHandlers,
    attribution,
  });
  const adminHttpServer = createServer(adminExpressApp);

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
          if (adminHttpServer.listening) {
            await new Promise<void>((resolve, reject) => {
              adminHttpServer.close((error) => (error ? reject(error) : resolve()));
            });
          }
        } finally {
          tts.close();
          voiceLibrary.close();
          await voices.close();
          socketHandlers.close();
          if (db.open) {
            db.close();
          }
        }
      })();
    }
    return closePromise;
  };

  return {
    expressApp,
    httpServer,
    adminExpressApp,
    adminHttpServer,
    io,
    db,
    close,
  };
}
