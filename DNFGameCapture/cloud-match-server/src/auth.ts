import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';

export interface LicenseRecord {
  id: number;
  keyHash: string;
  expiresAt: number | null;
  disabledAt: number | null;
  boundDeviceId: string | null;
}

export type LicenseFailureCode = 'expired' | 'disabled' | 'invalid_time';

export function normalizeLicenseKey(value: string): string {
  return value.normalize('NFC').trim().toUpperCase();
}

export function hashLicenseKey(value: string): string {
  return createHash('sha256').update(normalizeLicenseKey(value), 'utf8').digest('hex');
}

export function hashSessionToken(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex');
}

export function createSessionToken(): string {
  return randomBytes(32).toString('base64url');
}

// The native client validates the compact CDK signature before it contacts
// the authorization service. Keep server-generated cards compatible with that
// legacy parser while storing only their SHA-256 hash in SQLite.
const NATIVE_LICENSE_SIGNATURE_SECRET = 'MySuperSecretKey2026';
const NATIVE_LICENSE_KEY_PATTERN = /^CDK-([0-9A-F]+)-([A-Z0-9]+)-([0-9A-F]+)$/i;

function nativeLicenseSignature(value: string): number {
  let hash = 5381;
  for (const character of value) {
    hash = ((hash * 33) + character.charCodeAt(0)) >>> 0;
  }
  return hash >>> 0;
}

export function generateLicenseKey(duration = 0xFFFFFFFF): string {
  const safeDuration = Number.isSafeInteger(duration) && duration > 0 &&
    duration <= 0xFFFFFFFF ? duration : 0xFFFFFFFF;
  const durationHex = safeDuration.toString(16).toUpperCase();
  const nonce = randomBytes(5).toString('hex').toUpperCase();
  const signature = nativeLicenseSignature(
    `${durationHex}-${nonce}-${NATIVE_LICENSE_SIGNATURE_SECRET}`,
  ).toString(16).toUpperCase();
  return `CDK-${durationHex}-${nonce}-${signature}`;
}

/** Keep administrator-entered cards compatible with the native client parser. */
export function isNativeLicenseKey(value: string): boolean {
  const match = NATIVE_LICENSE_KEY_PATTERN.exec(normalizeLicenseKey(value));
  if (!match) return false;

  let duration: bigint;
  let signature: bigint;
  try {
    duration = BigInt(`0x${match[1]}`);
    signature = BigInt(`0x${match[3]}`);
  } catch {
    return false;
  }
  if (duration <= 0n || duration > 0x7FFFFFFFFFFFFFFFn || signature > 0xFFFFFFFFn) {
    return false;
  }
  const expected = nativeLicenseSignature(
    `${duration.toString(16).toUpperCase()}-${match[2].toUpperCase()}-${NATIVE_LICENSE_SIGNATURE_SECRET}`,
  );
  return signature === BigInt(expected);
}

export function verifyLicenseKey(record: LicenseRecord, value: string): boolean {
  const expected = Buffer.from(record.keyHash, 'hex');
  const supplied = Buffer.from(hashLicenseKey(value), 'hex');
  return expected.length === supplied.length && expected.length > 0 && timingSafeEqual(expected, supplied);
}

export function isLicenseUsable(
  record: Pick<LicenseRecord, 'expiresAt' | 'disabledAt'>,
  nowSec: number,
): { ok: true } | { ok: false; code: LicenseFailureCode } {
  if (!Number.isSafeInteger(nowSec) || nowSec < 0) return { ok: false, code: 'invalid_time' };
  if (record.disabledAt !== null) return { ok: false, code: 'disabled' };
  if (record.expiresAt !== null && nowSec >= record.expiresAt) return { ok: false, code: 'expired' };
  return { ok: true };
}
