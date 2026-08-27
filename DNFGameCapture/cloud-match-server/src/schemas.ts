import { z } from 'zod';

const DEVICE_ID_PATTERN = /^[A-Za-z0-9_-]+$/;
const DEVICE_TOKEN_PATTERN = /^[A-Za-z0-9_-]+$/;

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
  .transform((value) => value.trim())
  .refine((value) => {
    const codePointLength = Array.from(value).length;
    return codePointLength >= 1 && codePointLength <= 32;
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
    roomId: z.string().min(1),
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
