import { describe, expect, test } from 'vitest';

import {
  hashLicenseKey,
  hashSessionToken,
  generateLicenseKey,
  isNativeLicenseKey,
  isLicenseUsable,
  normalizeLicenseKey,
  verifyLicenseKey,
  type LicenseRecord,
} from '../src/auth.js';

const active: LicenseRecord = {
  id: 1,
  keyHash: hashLicenseKey('CDK-TEST-ONE'),
  expiresAt: 2_000,
  disabledAt: null,
  boundDeviceId: null,
};

describe('server authorization primitives', () => {
  test('generates license keys accepted by the native CDK signature parser', () => {
    const key = generateLicenseKey();
    expect(isNativeLicenseKey(key)).toBe(true);
    const parts = key.split('-');
    expect(parts).toHaveLength(4);
    expect(parts[0]).toBe('CDK');
    expect(parts[1]).toBe('FFFFFFFF');
    expect(parts[2]).toMatch(/^[0-9A-F]{10}$/);
    expect(parts[3]).toMatch(/^[0-9A-F]+$/);

    let hash = 5381;
    for (const character of `FFFFFFFF-${parts[2]}-MySuperSecretKey2026`) {
      hash = ((hash * 33) + character.charCodeAt(0)) >>> 0;
    }
    expect(Number.parseInt(parts[3], 16)).toBe(hash);
    expect(isNativeLicenseKey(`${key}X`)).toBe(false);
  });

  test('normalizes key input and verifies only its hash', () => {
    expect(normalizeLicenseKey(' cdk-test-one ')).toBe('CDK-TEST-ONE');
    expect(verifyLicenseKey(active, ' cdk-test-one ')).toBe(true);
    expect(verifyLicenseKey(active, 'CDK-OTHER')).toBe(false);
  });

  test('rejects expired and disabled licenses', () => {
    expect(isLicenseUsable(active, 1_999)).toEqual({ ok: true });
    expect(isLicenseUsable(active, 2_000)).toEqual({ ok: false, code: 'expired' });
    expect(isLicenseUsable({ ...active, disabledAt: 1_500 }, 1_500)).toEqual({
      ok: false,
      code: 'disabled',
    });
  });

  test('supports permanent licenses without treating zero as expired', () => {
    expect(isLicenseUsable({ ...active, expiresAt: null }, 9_999_999)).toEqual({ ok: true });
  });

  test('hashes session tokens deterministically without exposing the token', () => {
    const token = 'opaque-session-token';
    expect(hashSessionToken(token)).toHaveLength(64);
    expect(hashSessionToken(token)).toBe(hashSessionToken(token));
    expect(hashSessionToken(token)).not.toContain(token);
  });
});
