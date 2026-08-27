import { z } from 'zod';

const DEVICE_ID_PATTERN = /^[A-Za-z0-9_-]+$/;
const DEVICE_TOKEN_PATTERN = /^[A-Za-z0-9_-]+$/;
const FORBIDDEN_BROADCASTER_CHARACTERS = /[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/u;
const VISIBLE_BROADCASTER_BASE = /[\p{L}\p{N}\p{P}\p{S}]/u;
const graphemeSegmenter = new Intl.Segmenter('zh-CN', { granularity: 'grapheme' });

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
