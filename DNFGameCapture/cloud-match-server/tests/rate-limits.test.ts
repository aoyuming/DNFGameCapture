import { describe, expect, test } from 'vitest';

import { createCloudMatchRateLimitService } from '../src/rate-limits.js';

describe('bounded cloud match rate limits', () => {
  test('refills quotas continuously instead of resetting a fixed window', () => {
    const limits = createCloudMatchRateLimitService({
      registrationIpCapacity: 2,
      registrationGlobalCapacity: 100,
      registrationWindowSec: 10,
    });

    expect(limits.allowRegistration('ip-a', 0)).toBe(true);
    expect(limits.allowRegistration('ip-a', 0)).toBe(true);
    expect(limits.allowRegistration('ip-a', 4)).toBe(false);
    expect(limits.allowRegistration('ip-a', 5)).toBe(true);
    expect(limits.allowRegistration('ip-a', 9)).toBe(false);
    expect(limits.allowRegistration('ip-a', 10)).toBe(true);
  });

  test('keeps comparison quotas across reconnects and bounds tracked device/IP keys', () => {
    const limits = createCloudMatchRateLimitService({
      maxEntriesPerScope: 2,
      entryTtlSec: 10,
      comparisonDeviceCapacity: 2,
      comparisonIpCapacity: 4,
      comparisonWindowSec: 60,
      comparisonMinIntervalSec: 1,
    });

    expect(limits.allowComparison('device-a', 'ip-a', 0)).toBe(true);
    expect(limits.allowComparison('device-a', 'ip-a', 0)).toBe(false);
    expect(limits.allowComparison('device-a', 'ip-a', 1)).toBe(true);
    expect(limits.allowComparison('device-a', 'ip-a', 2)).toBe(false);
    expect(limits.allowComparison('device-b', 'ip-b', 2)).toBe(true);
    expect(limits.allowComparison('device-c', 'ip-c', 2)).toBe(false);
    expect(limits.entryCounts()).toMatchObject({
      comparisonDevices: 2,
      comparisonIps: 2,
    });

    expect(limits.allowComparison('device-c', 'ip-c', 13)).toBe(true);
    expect(limits.entryCounts().comparisonDevices).toBeLessThanOrEqual(2);
    expect(limits.entryCounts().comparisonIps).toBeLessThanOrEqual(2);
  });

  test('limits registration globally and per IP without growing its maps', () => {
    const limits = createCloudMatchRateLimitService({
      maxEntriesPerScope: 2,
      entryTtlSec: 60,
      registrationIpCapacity: 2,
      registrationGlobalCapacity: 3,
      registrationWindowSec: 60,
    });

    expect(limits.allowRegistration('ip-a', 0)).toBe(true);
    expect(limits.allowRegistration('ip-a', 0)).toBe(true);
    expect(limits.allowRegistration('ip-a', 0)).toBe(false);
    expect(limits.allowRegistration('ip-b', 0)).toBe(true);
    expect(limits.allowRegistration('ip-c', 0)).toBe(false);
    expect(limits.entryCounts().registrationIps).toBeLessThanOrEqual(2);
  });

  test('enforces the registration global bucket across distinct IPs', () => {
    const limits = createCloudMatchRateLimitService({
      maxEntriesPerScope: 10,
      registrationIpCapacity: 10,
      registrationGlobalCapacity: 2,
      registrationWindowSec: 60,
    });

    expect(limits.allowRegistration('ip-a', 0)).toBe(true);
    expect(limits.allowRegistration('ip-b', 0)).toBe(true);
    expect(limits.allowRegistration('ip-c', 0)).toBe(false);
  });

  test('bounds cold computation globally, per room, and by concurrent work', () => {
    const limits = createCloudMatchRateLimitService({
      maxColdConcurrent: 1,
      coldGlobalCapacity: 3,
      coldRoomCapacity: 1,
      coldWindowSec: 60,
      coldRoomMinIntervalSec: 5,
    });

    const releaseFirst = limits.tryAcquireColdComparison('room-a', 0);
    expect(releaseFirst).toBeTypeOf('function');
    expect(limits.tryAcquireColdComparison('room-b', 0)).toBeNull();
    releaseFirst?.();
    expect(limits.tryAcquireColdComparison('room-a', 1)).toBeNull();
    const releaseSecond = limits.tryAcquireColdComparison('room-b', 1);
    expect(releaseSecond).toBeTypeOf('function');
    releaseSecond?.();
    const releaseThird = limits.tryAcquireColdComparison('room-c', 1);
    expect(releaseThird).toBeTypeOf('function');
    releaseThird?.();
    expect(limits.tryAcquireColdComparison('room-d', 1)).toBeNull();
    expect(limits.activeColdComparisons()).toBe(0);
  });
});
