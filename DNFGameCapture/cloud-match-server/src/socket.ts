import { createHash, randomBytes } from 'node:crypto';
import type { Server as SocketIoServer, Socket } from 'socket.io';
import { z } from 'zod';

import { compareRoomSnapshots, type RoomComparison } from './comparison.js';
import { openDatabase } from './db.js';
import { authenticateDevice, touchLastSeen } from './identity.js';
import type { CloudMatchRateLimitService } from './rate-limits.js';
import type {
  BoundedRoomMembers,
  MembershipDto,
  RoomDto,
  RoomMemberDto,
} from './rooms.js';
import {
  deviceIdSchema,
  roomJoinSchema,
  roomRenameSchema,
  socketAuthSchema,
} from './schemas.js';
import {
  getRoomSnapshots,
  getSnapshot,
  saveSnapshot,
  type RoomSnapshotRow,
  type SaveSnapshotInput,
  type SaveSnapshotResult,
  type StoredSnapshot,
} from './snapshots.js';

type DatabaseConnection = ReturnType<typeof openDatabase>;
type MaybePromise<T> = T | Promise<T>;
type Acknowledge = (response: unknown) => void;

const MAX_SNAPSHOT_BYTES = 65_536;
const MAX_COMPARISON_PAGE_SIZE = 8;
const MAX_COMPARISON_MEMBERS = 128;
const MAX_COMPARISON_ACK_BYTES = 96 * 1_024;
const MAX_PROTOCOL_ACK_BYTES = 128 * 1_024;
const MAX_DEVICE_PENDING_OPERATIONS = 64;
const MAX_SHARED_COMPARISONS = 8;
const SHARED_COMPARISON_TTL_SECONDS = 5;
const COMPARISON_SESSION_TTL_SECONDS = 30;
const FIXED_ROOM_IDS = ['59', 'li-yong', 'wen-rou'] as const;
const emptyPayloadSchema = z.object({}).strict();
const comparisonRequestSchema = z
  .object({
    cursor: deviceIdSchema.optional(),
    comparisonToken: z.string().min(32).max(128).regex(/^[A-Za-z0-9_-]+$/).optional(),
    limit: z.number().int().min(1).max(64).optional(),
  })
  .strict()
  .superRefine((request, context) => {
    if ((request.cursor === undefined) !== (request.comparisonToken === undefined)) {
      context.addIssue({ code: z.ZodIssueCode.custom });
    }
  });
const snapshotUploadSchema = z.object({ snapshot: z.unknown() }).strict();
const snapshotGetSchema = z
  .object({
    targetDeviceId: deviceIdSchema,
    clientRevision: z.number().int().safe().min(1),
  })
  .strict();

export interface RoomService {
  listRooms(db: DatabaseConnection): MaybePromise<RoomDto[]>;
  getMembership(
    db: DatabaseConnection,
    deviceId: string,
  ): MaybePromise<MembershipDto | null>;
  listRoomMembers(
    db: DatabaseConnection,
    roomId: string,
  ): MaybePromise<RoomMemberDto[]>;
  listRoomMembersBounded(
    db: DatabaseConnection,
    roomId: string,
    requiredDeviceId: string,
    limit: number,
  ): MaybePromise<BoundedRoomMembers>;
  getRoomRevision(
    db: DatabaseConnection,
    roomId: string,
  ): MaybePromise<number>;
  incrementRoomRevision(db: DatabaseConnection, roomId: string): number;
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

export interface SnapshotService {
  saveSnapshot(
    db: DatabaseConnection,
    input: SaveSnapshotInput,
  ): SaveSnapshotResult;
  getSnapshot(
    db: DatabaseConnection,
    deviceId: string,
  ): MaybePromise<StoredSnapshot | null>;
  getRoomSnapshots(
    db: DatabaseConnection,
    roomId: string,
    deviceIds?: readonly string[],
  ): MaybePromise<RoomSnapshotRow[]>;
}

export interface ComparisonService {
  compareRoomSnapshots(
    rows: RoomSnapshotRow[],
    nowSec: number,
  ): MaybePromise<RoomComparison>;
}

export interface SocketRoomAdapter {
  join(socket: Socket, room: string): Promise<void>;
  leave(socket: Socket, room: string): Promise<void>;
}

export interface RegisterCloudMatchSocketHandlersContext {
  io: SocketIoServer;
  db: DatabaseConnection;
  now(): number;
  roomService: RoomService;
  snapshotService?: SnapshotService;
  comparisonService?: ComparisonService;
  rateLimitService: CloudMatchRateLimitService;
  resolveClientIp(remoteAddress: string): string;
  socketRoomAdapter: SocketRoomAdapter;
}

export interface CloudMatchSocketHandlerRegistration {
  close(): void;
}

interface AuthenticatedSocketData {
  deviceId: string;
}

interface MaterializedComparison {
  roomId: string;
  roomRevision: number;
  generatedAt: number;
  consensusDeviceId: string | null;
  members: Array<Record<string, unknown>>;
  groups: Array<{ id: string; memberDeviceIds: string[] }>;
  totalMembers: number;
  boundedMembers: number;
  truncated: boolean;
}

interface ComparisonSession extends MaterializedComparison {
  token: string;
  expiresAt: number;
  expectedCursor: string | null;
}

interface SharedComparison {
  expiresAt: number;
  pending: Promise<MaterializedComparison>;
}

type MaterializeComparisonResult =
  | { ok: true; comparison: MaterializedComparison }
  | { ok: false; code: 'comparison_changed' | 'rate_limited' };

function createSocketError(code: string): Error & { data: { code: string } } {
  const message =
    code === 'unsupported_protocol' ? 'Unsupported protocol' : 'Authentication failed';
  const error = new Error(message) as Error & { data: { code: string } };
  error.data = { code };
  return error;
}

function invalidBroadcasterName(error: {
  issues: Array<{ path: Array<string | number> }>;
}): boolean {
  return error.issues.some((issue) => issue.path[0] === 'broadcasterName');
}

function roomNamespace(roomId: string): string {
  return `room:${roomId}`;
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

function serializedByteLength(value: unknown): number | null {
  try {
    const serialized = JSON.stringify(value);
    return typeof serialized === 'string'
      ? Buffer.byteLength(serialized, 'utf8')
      : null;
  } catch {
    return null;
  }
}

function boundedResponse(
  response: unknown,
  maximumBytes: number,
): unknown {
  const bytes = serializedByteLength(response);
  if (bytes === null || bytes > maximumBytes) {
    return { ok: false, code: 'response_too_large' };
  }
  return response;
}

export function registerCloudMatchSocketHandlers(
  context: RegisterCloudMatchSocketHandlersContext,
): CloudMatchSocketHandlerRegistration {
  const {
    io,
    db,
    now,
    roomService,
    rateLimitService,
    resolveClientIp,
    socketRoomAdapter,
    snapshotService = { saveSnapshot, getSnapshot, getRoomSnapshots },
    comparisonService = { compareRoomSnapshots },
  } = context;
  const activeSockets = new Map<string, Socket>();
  const deviceQueues = new Map<string, Promise<void>>();
  const comparisonCache = new Map<string, SharedComparison>();
  const lastAnnouncedRevision = new Map<string, number>(
    FIXED_ROOM_IDS.map((roomId) => [roomId, -1]),
  );
  let notificationsClosed = false;

  const removeSettledDeviceQueue = (
    deviceId: string,
    tail: Promise<void>,
  ): void => {
    void tail.then(() => {
      if (deviceQueues.get(deviceId) === tail && !activeSockets.has(deviceId)) {
        deviceQueues.delete(deviceId);
      }
    });
  };

  const enqueueDeviceOperation = <T>(
    deviceId: string,
    operation: () => Promise<T>,
  ): Promise<T> => {
    const previous = deviceQueues.get(deviceId) ?? Promise.resolve();
    const result = previous.then(operation, operation);
    const tail = result.then(
      () => undefined,
      () => undefined,
    );
    deviceQueues.set(deviceId, tail);
    removeSettledDeviceQueue(deviceId, tail);
    return result;
  };

  const rememberComparison = (
    key: string,
    acquireColdLease: () => boolean,
    build: () => Promise<MaterializedComparison>,
  ): Promise<MaterializedComparison> | null => {
    const existing = comparisonCache.get(key);
    if (existing && now() <= existing.expiresAt) {
      return existing.pending;
    }
    if (existing) {
      comparisonCache.delete(key);
    }
    if (!acquireColdLease()) {
      return null;
    }
    const pending = build();
    const shared = {
      expiresAt: now() + SHARED_COMPARISON_TTL_SECONDS,
      pending,
    };
    comparisonCache.set(key, shared);
    void pending.catch(() => {
      if (comparisonCache.get(key) === shared) {
        comparisonCache.delete(key);
      }
    });
    while (comparisonCache.size > MAX_SHARED_COMPARISONS) {
      const oldest = comparisonCache.keys().next().value as string | undefined;
      if (oldest === undefined) {
        break;
      }
      comparisonCache.delete(oldest);
    }
    return pending;
  };

  const materializeComparison = async (
    roomId: string,
    requiredDeviceId: string,
  ): Promise<MaterializeComparisonResult> => {
    const coldLease: { release: (() => void) | null } = { release: null };
    const acquireColdLease = (): boolean => {
      if (coldLease.release) {
        return true;
      }
      coldLease.release = rateLimitService.tryAcquireColdComparison(roomId, now());
      return coldLease.release !== null;
    };
    try {
      for (let attempt = 0; attempt < 2; attempt += 1) {
        const roomRevision = await roomService.getRoomRevision(db, roomId);
        const generatedAt = now();
        const boundedRoom = await roomService.listRoomMembersBounded(
          db,
          roomId,
          requiredDeviceId,
          MAX_COMPARISON_MEMBERS,
        );
        const roomMembers = boundedRoom.members;
        const boundedMemberIds = new Set(
          roomMembers.map((member) => member.deviceId),
        );
        const snapshots = await snapshotService.getRoomSnapshots(
          db,
          roomId,
          [...boundedMemberIds],
        );
        const dataFingerprint = createHash('sha256')
          .update(JSON.stringify({
            totalMembers: boundedRoom.totalMembers,
            members: roomMembers.map((member) => [
              member.deviceId,
              member.broadcasterName,
            ]),
            snapshots: snapshots.map((row) => [
              row.deviceId,
              row.snapshot.clientRevision,
              row.receivedAt,
            ]),
          }))
          .digest('base64url');
        const cacheKey = `${roomId}:${dataFingerprint}`;
        const cachedComparison = rememberComparison(
          cacheKey,
          acquireColdLease,
          async () => {
            const inputRows = snapshots
              .filter((row) => boundedMemberIds.has(row.deviceId))
              .map((row) => ({
                ...row,
                online: activeSockets.has(row.deviceId),
              }));
            const compared = await comparisonService.compareRoomSnapshots(
              inputRows,
              generatedAt,
            );
            const comparedByDeviceId = new Map(
              compared.members.map((member) => [member.deviceId, member]),
            );
            const members = roomMembers.map((member): Record<string, unknown> => {
              const item = comparedByDeviceId.get(member.deviceId);
              if (!item) {
                return {
                  ...member,
                  online: activeSockets.has(member.deviceId),
                  state: 'no_data' as const,
                };
              }
              return {
                deviceId: item.deviceId,
                broadcasterName: item.broadcasterName,
                deviceSuffix: item.deviceSuffix,
                online: activeSockets.has(item.deviceId),
                state: item.stale ? ('stale' as const) : ('updated' as const),
                clientRevision: item.clientRevision,
                receivedAt: item.receivedAt,
                stale: item.stale,
                excludedFromConsensus: item.excludedFromConsensus,
                sourceRoot: item.sourceRoot,
                swapped: item.swapped,
                identityMatchPercent: item.identityMatchPercent,
                similarity: item.similarity,
              };
            });
            const memberIds = new Set(
              members.map((member) => member.deviceId as string),
            );
            const groups = compared.groups.flatMap((group) => {
              const memberDeviceIds = [...new Set(group.memberDeviceIds)].filter(
                (id) => memberIds.has(id),
              );
              return memberDeviceIds.length > 0
                ? [{ id: group.id, memberDeviceIds }]
                : [];
            });
            return {
              roomId,
              roomRevision,
              generatedAt,
              consensusDeviceId: compared.consensusDeviceId,
              members,
              groups,
              totalMembers: boundedRoom.totalMembers,
              boundedMembers: members.length,
              truncated: boundedRoom.totalMembers > members.length,
            };
          },
        );
        if (!cachedComparison) {
          return { ok: false, code: 'rate_limited' };
        }
        const sharedComparison = await cachedComparison;
        const revisionAfter = await roomService.getRoomRevision(db, roomId);
        if (revisionAfter === roomRevision) {
          return {
            ok: true,
            comparison: {
              ...sharedComparison,
              roomRevision,
              members: sharedComparison.members.map((member) => ({
                ...member,
                online: activeSockets.has(member.deviceId as string),
              })),
            },
          };
        }
      }
      return { ok: false, code: 'comparison_changed' };
    } finally {
      coldLease.release?.();
    }
  };

  const announceRoomNotification = (
    roomId: string,
    roomRevision: number,
    emit: () => void,
  ): void => {
    const previousRevision = lastAnnouncedRevision.get(roomId);
    if (
      notificationsClosed ||
      previousRevision === undefined ||
      roomRevision <= previousRevision
    ) {
      return;
    }
    lastAnnouncedRevision.set(roomId, roomRevision);
    emit();
  };

  const emitRoomChanged = (
    socket: Socket,
    roomId: string,
    roomRevision: number,
  ): void => {
    announceRoomNotification(roomId, roomRevision, () => {
      const namespace = roomNamespace(roomId);
      const payload = { roomId, roomRevision };
      io.to(namespace).emit('room:changed', payload);
      if (socket.connected && !socket.rooms.has(namespace)) {
        socket.emit('room:changed', payload);
      }
    });
  };

  const emitRoomPresence = (
    roomId: string,
    roomRevision: number,
    deviceId: string,
    online: boolean,
  ): void => {
    announceRoomNotification(roomId, roomRevision, () => {
      io.to(roomNamespace(roomId)).emit('room:presence', {
        roomId,
        roomRevision,
        deviceId,
        online,
      });
    });
  };

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
    const socketIpAddress = resolveClientIp(socket.handshake.address || 'unknown');
    let restoredRoomId: string | null = null;
    let pendingSerializedOperations = 0;
    let comparisonSession: ComparisonSession | null = null;

    const initialization = enqueueDeviceOperation(deviceId, async () => {
      if (!socket.connected) {
        return;
      }

      const membership = await roomService.getMembership(db, deviceId);
      restoredRoomId = membership?.room.id ?? null;
      if (restoredRoomId) {
        await socketRoomAdapter.join(socket, roomNamespace(restoredRoomId));
      }
      touchLastSeen(db, deviceId, now());

      if (!socket.connected) {
        return;
      }
      const previousSocket = activeSockets.get(deviceId);
      activeSockets.set(deviceId, socket);
      if (previousSocket && previousSocket.id !== socket.id) {
        previousSocket.emit('connection:replaced', {
          code: 'replaced_by_new_connection',
        });
        previousSocket.disconnect(true);
      }
      if (restoredRoomId) {
        const roomRevision = roomService.incrementRoomRevision(db, restoredRoomId);
        emitRoomPresence(restoredRoomId, roomRevision, deviceId, true);
      }
    });
    void initialization.catch(async () => {
      if (activeSockets.get(deviceId) === socket) {
        activeSockets.delete(deviceId);
      }
      if (restoredRoomId && socket.rooms.has(roomNamespace(restoredRoomId))) {
        try {
          await socketRoomAdapter.leave(socket, roomNamespace(restoredRoomId));
        } catch {
          // Disconnecting below removes any adapter state rollback could not clear.
        }
      }
      socket.emit('session:error', { code: 'internal_error' });
      socket.disconnect(true);
    });

    const bindAckEvent = (
      event: string,
      handler: (payload: unknown) => MaybePromise<unknown>,
      serializeForDevice = false,
    ): void => {
      socket.on(event, (...args: unknown[]) => {
        const acknowledge = args.at(-1);
        if (typeof acknowledge !== 'function') {
          return;
        }
        const payload = args.length > 1 ? args[0] : undefined;
        const run = async (): Promise<void> => {
          let response: unknown;
          if (serializeForDevice && activeSockets.get(deviceId) !== socket) {
            response = { ok: false, code: 'internal_error' };
          } else {
            try {
              response = await handler(payload);
            } catch {
              response = { ok: false, code: 'internal_error' };
            }
          }
          safeAcknowledge(acknowledge as Acknowledge, response);
        };

        if (serializeForDevice) {
          if (pendingSerializedOperations >= MAX_DEVICE_PENDING_OPERATIONS) {
            safeAcknowledge(acknowledge as Acknowledge, {
              ok: false,
              code: 'rate_limited',
            });
            return;
          }
          pendingSerializedOperations += 1;
          void enqueueDeviceOperation(deviceId, run).finally(() => {
            pendingSerializedOperations -= 1;
          });
        } else {
          void run();
        }
      });
    };

    bindAckEvent('room:list', async () => ({
      ok: true,
      rooms: await roomService.listRooms(db),
    }));

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
        let persisted = false;
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
          persisted = true;

          const roomChanged =
            previousRoomId !== membership.room.id ||
            previousMembership?.broadcasterName !== membership.broadcasterName;
          if (roomChanged) {
            if (previousRoomId && previousRoomId !== membership.room.id) {
              emitRoomChanged(
                socket,
                previousRoomId,
                await roomService.getRoomRevision(db, previousRoomId),
              );
            }
            emitRoomChanged(
              socket,
              membership.room.id,
              await roomService.getRoomRevision(db, membership.room.id),
            );
          }
          return { ok: true, ...membership };
        } catch (error) {
          if (!persisted) {
            await restoreAdapterMembership(
              socketRoomAdapter,
              socket,
              previousRoomId,
              parsed.data.roomId,
            );
          }
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
        const previousMembership = await roomService.getMembership(db, deviceId);
        const membership = await roomService.renameBroadcaster(
          db,
          deviceId,
          parsed.data.broadcasterName,
          now(),
        );
        if (!membership) {
          return { ok: false, code: 'not_in_room' };
        }
        if (previousMembership?.broadcasterName !== membership.broadcasterName) {
          emitRoomChanged(
            socket,
            membership.room.id,
            await roomService.getRoomRevision(db, membership.room.id),
          );
        }
        return { ok: true, ...membership };
      },
      true,
    );

    bindAckEvent(
      'room:leave',
      async () => {
        const previousMembership = await roomService.getMembership(db, deviceId);
        const previousRoomId = previousMembership?.room.id ?? null;
        let persisted = false;
        try {
          await setAdapterMembership(socketRoomAdapter, socket, previousRoomId, null);
          await roomService.leaveRoom(db, deviceId);
          persisted = true;
          if (previousRoomId) {
            emitRoomChanged(
              socket,
              previousRoomId,
              await roomService.getRoomRevision(db, previousRoomId),
            );
          }
          return { ok: true };
        } catch (error) {
          if (!persisted) {
            await restoreAdapterMembership(
              socketRoomAdapter,
              socket,
              previousRoomId,
              null,
            );
          }
          throw error;
        }
      },
      true,
    );

    bindAckEvent(
      'room:status',
      async () => ({
        ok: true,
        membership: await roomService.getMembership(db, deviceId),
        online: activeSockets.get(deviceId) === socket,
      }),
      true,
    );

    bindAckEvent(
      'snapshot:upload',
      async (payload) => {
        const parsed = snapshotUploadSchema.safeParse(payload);
        if (!parsed.success) {
          return { ok: false, code: 'invalid_request' };
        }
        const snapshotBytes = serializedByteLength(parsed.data.snapshot);
        if (snapshotBytes === null) {
          return { ok: false, code: 'invalid_snapshot' };
        }
        if (snapshotBytes > MAX_SNAPSHOT_BYTES) {
          return { ok: false, code: 'snapshot_too_large' };
        }

        const membership = await roomService.getMembership(db, deviceId);
        if (!membership) {
          return { ok: false, code: 'no_membership' };
        }
        const roomId = membership.room.id;
        const accept = db.transaction(() => {
          const saved = snapshotService.saveSnapshot(db, {
            deviceId,
            roomId,
            snapshot: parsed.data.snapshot,
            receivedAt: now(),
          });
          if (!saved.ok) {
            return saved;
          }
          return {
            ok: true as const,
            acceptedRevision: saved.acceptedRevision,
            roomRevision: roomService.incrementRoomRevision(db, roomId),
          };
        });
        const result = accept();
        if (result.ok) {
          emitRoomChanged(socket, roomId, result.roomRevision);
        }
        return result;
      },
      true,
    );

    bindAckEvent(
      'room:comparison',
      async (payload) => {
        const parsed = comparisonRequestSchema.safeParse(payload);
        if (!parsed.success) {
          return { ok: false, code: 'invalid_request' };
        }
        const membership = await roomService.getMembership(db, deviceId);
        if (!membership) {
          return { ok: false, code: 'not_in_room' };
        }
        const roomId = membership.room.id;
        const requestedToken = parsed.data.comparisonToken;
        const requestedCursor = parsed.data.cursor ?? null;
        if (requestedToken) {
          if (!comparisonSession || comparisonSession.token !== requestedToken) {
            return { ok: false, code: 'invalid_comparison_token' };
          }
          if (comparisonSession.roomId !== roomId) {
            comparisonSession = null;
            return { ok: false, code: 'comparison_changed' };
          }
          if (now() > comparisonSession.expiresAt) {
            comparisonSession = null;
            return { ok: false, code: 'comparison_expired' };
          }
          if (
            (await roomService.getRoomRevision(db, roomId)) !==
            comparisonSession.roomRevision
          ) {
            comparisonSession = null;
            return { ok: false, code: 'comparison_changed' };
          }
          if (requestedCursor !== comparisonSession.expectedCursor) {
            return { ok: false, code: 'invalid_comparison_cursor' };
          }
        } else {
          if (!rateLimitService.allowComparison(deviceId, socketIpAddress, now())) {
            return { ok: false, code: 'rate_limited' };
          }
          comparisonSession = null;
          const materialized = await materializeComparison(roomId, deviceId);
          if (!materialized.ok) {
            return materialized;
          }
          comparisonSession = {
            ...materialized.comparison,
            token: randomBytes(32).toString('base64url'),
            expiresAt: now() + COMPARISON_SESSION_TTL_SECONDS,
            expectedCursor: null,
          };
        }

        const session = comparisonSession;
        if (!session) {
          return { ok: false, code: 'invalid_comparison_token' };
        }
        const startIndex = requestedCursor
          ? session.members.findIndex(
              (member) => (member.deviceId as string) > requestedCursor,
            )
          : 0;
        const pageStart = startIndex < 0 ? session.members.length : startIndex;
        const requestedLimit = parsed.data.limit ?? MAX_COMPARISON_PAGE_SIZE;
        const pageSize = Math.min(requestedLimit, MAX_COMPARISON_PAGE_SIZE);
        const pageMembers = session.members.slice(pageStart, pageStart + pageSize);
        const hasMore = pageStart + pageMembers.length < session.members.length;
        const nextCursor = hasMore && pageMembers.length > 0
          ? pageMembers.at(-1)!.deviceId as string
          : null;
        const pageMemberIds = new Set(pageMembers.map((member) => member.deviceId));
        const groups = session.groups
          .map((group) => ({
            id: group.id,
            memberDeviceIds: [...new Set(group.memberDeviceIds)].filter((id) =>
              pageMemberIds.has(id),
            ),
          }))
          .filter((group) => group.memberDeviceIds.length > 0);

        const response = {
          ok: true,
          comparisonToken: session.token,
          cursor: requestedCursor,
          generatedAt: session.generatedAt,
          roomRevision: session.roomRevision,
          consensusDeviceId: session.consensusDeviceId,
          members: pageMembers,
          groups,
          totalMembers: session.totalMembers,
          boundedMembers: session.boundedMembers,
          truncated: session.truncated,
          hasMore,
          nextCursor,
        };
        const bounded = boundedResponse(response, MAX_COMPARISON_ACK_BYTES);
        if ((bounded as { ok?: boolean }).ok === true) {
          session.expectedCursor = nextCursor;
        }
        return bounded;
      },
      true,
    );

    bindAckEvent(
      'snapshot:get',
      async (payload) => {
        const parsed = snapshotGetSchema.safeParse(payload);
        if (!parsed.success) {
          return { ok: false, code: 'invalid_request' };
        }
        const callerMembership = await roomService.getMembership(db, deviceId);
        if (!callerMembership) {
          return { ok: false, code: 'not_in_room' };
        }
        const targetMembership = await roomService.getMembership(
          db,
          parsed.data.targetDeviceId,
        );
        if (
          !targetMembership ||
          targetMembership.room.id !== callerMembership.room.id
        ) {
          return { ok: false, code: 'target_not_found' };
        }
        const stored = await snapshotService.getSnapshot(
          db,
          parsed.data.targetDeviceId,
        );
        if (!stored || stored.roomId !== callerMembership.room.id) {
          return { ok: false, code: 'target_has_no_snapshot' };
        }
        if (stored.snapshot.clientRevision !== parsed.data.clientRevision) {
          return { ok: false, code: 'snapshot_revision_changed' };
        }
        return boundedResponse({
          ok: true,
          targetDeviceId: parsed.data.targetDeviceId,
          broadcasterName: targetMembership.broadcasterName,
          deviceSuffix: targetMembership.deviceSuffix,
          receivedAt: stored.receivedAt,
          clientRevision: stored.snapshot.clientRevision,
          snapshot: stored.snapshot,
        }, MAX_PROTOCOL_ACK_BYTES);
      },
      true,
    );

    socket.on('disconnect', () => {
      comparisonSession = null;
      const wasActive = activeSockets.get(deviceId) === socket;
      if (wasActive) {
        activeSockets.delete(deviceId);
      }
      void enqueueDeviceOperation(deviceId, async () => {
        try {
          touchLastSeen(db, deviceId, now());
          if (wasActive) {
            const membership = await roomService.getMembership(db, deviceId);
            if (membership) {
              const roomRevision = roomService.incrementRoomRevision(
                db,
                membership.room.id,
              );
              emitRoomPresence(
                membership.room.id,
                roomRevision,
                deviceId,
                false,
              );
            }
          }
        } catch {
          // Shutdown may close the database while disconnect notifications drain.
        }
      });
    });
  });

  return {
    close(): void {
      notificationsClosed = true;
      lastAnnouncedRevision.clear();
      comparisonCache.clear();
    },
  };
}
