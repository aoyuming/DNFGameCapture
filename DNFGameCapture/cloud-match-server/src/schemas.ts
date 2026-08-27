import { z } from 'zod';

const DEVICE_ID_PATTERN = /^[A-Za-z0-9_-]+$/;
const DEVICE_TOKEN_PATTERN = /^[A-Za-z0-9_-]+$/;
const FORBIDDEN_BROADCASTER_CHARACTERS = /[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/u;
const VISIBLE_BROADCASTER_BASE = /[\p{L}\p{N}\p{P}\p{S}]/u;
const graphemeSegmenter = new Intl.Segmenter('zh-CN', { granularity: 'grapheme' });

export type ChangeSource = 'ocr' | 'manual' | 'local_restore' | 'cloud_sync';

export type Player = {
  mainName: string;
  aliases: string[];
  kills: number;
  deaths: number;
  ak: number;
  streak: number;
};

export type MatchSnapshot = {
  schemaVersion: 1;
  clientRevision: number;
  clientTime: number;
  changeSource: ChangeSource;
  syncedFrom?: { deviceId: string; revision: number };
  redScore: number;
  blueScore: number;
  redPlayers: [Player, Player, Player, Player];
  bluePlayers: [Player, Player, Player, Player];
  redPickFirst: boolean;
  teamsFlipped: boolean;
  outputSeatLabel: boolean;
  lastKillTeam: 'red' | 'blue' | '';
};

export function normalizePlayerName(value: string): string {
  return value.normalize('NFC').trim();
}

export const deviceIdSchema = z
  .string()
  .min(8)
  .max(128)
  .regex(DEVICE_ID_PATTERN);

export const deviceTokenSchema = z
  .string()
  .min(40)
  .max(128)
  .regex(DEVICE_TOKEN_PATTERN);

export const broadcasterNameSchema = z
  .string()
  .superRefine((value, context) => {
    const normalized = value.normalize('NFC');
    const trimmed = normalized.trim();
    const graphemeCount = Array.from(graphemeSegmenter.segment(trimmed)).length;
    if (
      FORBIDDEN_BROADCASTER_CHARACTERS.test(normalized) ||
      !VISIBLE_BROADCASTER_BASE.test(trimmed) ||
      graphemeCount < 1 ||
      graphemeCount > 32
    ) {
      context.addIssue({ code: z.ZodIssueCode.custom });
    }
  })
  .transform((value) => value.normalize('NFC').trim());

export const playerNameSchema = z
  .string()
  .superRefine((value, context) => {
    const normalized = value.normalize('NFC');
    const trimmed = normalized.trim();
    const graphemeCount = Array.from(graphemeSegmenter.segment(trimmed)).length;
    if (
      FORBIDDEN_BROADCASTER_CHARACTERS.test(normalized) ||
      !VISIBLE_BROADCASTER_BASE.test(trimmed) ||
      graphemeCount < 1 ||
      graphemeCount > 64
    ) {
      context.addIssue({ code: z.ZodIssueCode.custom });
    }
  })
  .transform(normalizePlayerName);

const boundedSnapshotInteger = z.number().int().safe().min(0).max(999);
const snapshotRevisionSchema = z.number().int().safe().min(1);
const snapshotTimeSchema = z.number().int().safe().min(0);

export const playerSchema: z.ZodType<Player> = z
  .object({
    mainName: playerNameSchema,
    aliases: z
      .array(playerNameSchema)
      .max(32)
      .transform((aliases) => [...new Set(aliases)]),
    kills: boundedSnapshotInteger,
    deaths: boundedSnapshotInteger,
    ak: boundedSnapshotInteger,
    streak: boundedSnapshotInteger,
  })
  .strict()
  .superRefine((value, context) => {
    if (value.aliases.includes(value.mainName)) {
      context.addIssue({
        code: z.ZodIssueCode.custom,
        path: ['aliases'],
      });
    }
  });

const syncedFromSchema = z
  .object({
    deviceId: deviceIdSchema,
    revision: snapshotRevisionSchema,
  })
  .strict();

export const matchSnapshotSchema: z.ZodType<MatchSnapshot> = z
  .object({
    schemaVersion: z.literal(1),
    clientRevision: snapshotRevisionSchema,
    clientTime: snapshotTimeSchema,
    changeSource: z.enum(['ocr', 'manual', 'local_restore', 'cloud_sync']),
    syncedFrom: syncedFromSchema.optional(),
    redScore: boundedSnapshotInteger,
    blueScore: boundedSnapshotInteger,
    redPlayers: z.tuple([playerSchema, playerSchema, playerSchema, playerSchema]),
    bluePlayers: z.tuple([playerSchema, playerSchema, playerSchema, playerSchema]),
    redPickFirst: z.boolean(),
    teamsFlipped: z.boolean(),
    outputSeatLabel: z.boolean(),
    lastKillTeam: z.enum(['red', 'blue', '']),
  })
  .strict()
  .superRefine((value, context) => {
    const isCloudSync = value.changeSource === 'cloud_sync';
    if (isCloudSync !== (value.syncedFrom !== undefined)) {
      context.addIssue({
        code: z.ZodIssueCode.custom,
        path: ['syncedFrom'],
      });
    }
  });

export const registerBodySchema = z
  .object({
    deviceId: deviceIdSchema,
  })
  .strict();

export const socketAuthSchema = z
  .object({
    deviceId: deviceIdSchema,
    deviceToken: deviceTokenSchema,
    protocolVersion: z.literal(1),
  })
  .strict();

export const roomJoinSchema = z
  .object({
    roomId: z.string().min(1).max(64),
    broadcasterName: broadcasterNameSchema,
  })
  .strict();

export const roomRenameSchema = z
  .object({
    broadcasterName: broadcasterNameSchema,
  })
  .strict();

export type RegisterBody = z.infer<typeof registerBodySchema>;
export type SocketAuth = z.infer<typeof socketAuthSchema>;
export type RoomJoin = z.infer<typeof roomJoinSchema>;
export type RoomRename = z.infer<typeof roomRenameSchema>;
