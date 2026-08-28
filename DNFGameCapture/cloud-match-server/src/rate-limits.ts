export interface CloudMatchRateLimitOptions {
  maxEntriesPerScope?: number;
  entryTtlSec?: number;
  comparisonDeviceCapacity?: number;
  comparisonIpCapacity?: number;
  comparisonWindowSec?: number;
  comparisonMinIntervalSec?: number;
  snapshotDeviceCapacity?: number;
  snapshotIpCapacity?: number;
  snapshotGlobalCapacity?: number;
  snapshotWindowSec?: number;
  membershipDeviceCapacity?: number;
  membershipIpCapacity?: number;
  membershipGlobalCapacity?: number;
  membershipWindowSec?: number;
  registrationIpCapacity?: number;
  registrationGlobalCapacity?: number;
  registrationWindowSec?: number;
  coldGlobalCapacity?: number;
  coldRoomCapacity?: number;
  coldWindowSec?: number;
  coldRoomMinIntervalSec?: number;
  maxColdConcurrent?: number;
}

export interface CloudMatchRateLimitEntryCounts {
  comparisonDevices: number;
  comparisonIps: number;
  registrationIps: number;
  coldRooms: number;
  snapshotDevices: number;
  snapshotIps: number;
  membershipDevices: number;
  membershipIps: number;
}

export interface RateLimitDecision {
  allowed: boolean;
  retryAfterMs: number;
}

export interface CloudMatchRateLimitService {
  allowComparison(deviceId: string, ipAddress: string, nowSec: number): boolean;
  allowRegistration(ipAddress: string, nowSec: number): boolean;
  allowSnapshotMutation(
    deviceId: string,
    ipAddress: string,
    nowSec: number,
  ): RateLimitDecision;
  allowMembershipMutation(
    deviceId: string,
    ipAddress: string,
    nowSec: number,
  ): RateLimitDecision;
  tryAcquireColdComparison(roomId: string, nowSec: number): (() => void) | null;
  activeColdComparisons(): number;
  entryCounts(): CloudMatchRateLimitEntryCounts;
}

interface QuotaEntry {
  lastAllowedAt: number | null;
  lastRefilledAt: number;
  lastSeenAt: number;
  tokens: number;
}

interface QuotaOptions {
  capacity: number;
  entryTtlSec: number;
  maxEntries: number;
  minimumIntervalSec: number;
  windowSec: number;
}

class BoundedTokenBucketMap {
  private readonly entries = new Map<string, QuotaEntry>();

  constructor(private readonly options: QuotaOptions) {}

  consume(key: string, nowSec: number): RateLimitDecision {
    this.prune(nowSec);
    let entry = this.entries.get(key);
    if (!entry) {
      if (this.entries.size >= this.options.maxEntries) {
        return {
          allowed: false,
          retryAfterMs: Math.min(this.options.entryTtlSec * 1_000, 60_000),
        };
      }
      entry = {
        lastAllowedAt: null,
        lastRefilledAt: nowSec,
        lastSeenAt: nowSec,
        tokens: this.options.capacity,
      };
      this.entries.set(key, entry);
    }
    if (nowSec > entry.lastRefilledAt) {
      const elapsed = nowSec - entry.lastRefilledAt;
      entry.tokens = Math.min(
        this.options.capacity,
        entry.tokens + elapsed * this.options.capacity / this.options.windowSec,
      );
      entry.lastRefilledAt = nowSec;
    }
    entry.lastSeenAt = nowSec;
    this.entries.delete(key);
    this.entries.set(key, entry);
    if (
      entry.lastAllowedAt !== null &&
      nowSec - entry.lastAllowedAt < this.options.minimumIntervalSec
    ) {
      return {
        allowed: false,
        retryAfterMs: Math.max(100, Math.ceil(
          (this.options.minimumIntervalSec - (nowSec - entry.lastAllowedAt)) * 1_000,
        )),
      };
    }
    if (entry.tokens < 1) {
      return {
        allowed: false,
        retryAfterMs: Math.max(100, Math.ceil(
          (1 - entry.tokens) * this.options.windowSec /
            this.options.capacity * 1_000,
        )),
      };
    }
    entry.tokens -= 1;
    entry.lastAllowedAt = nowSec;
    return { allowed: true, retryAfterMs: 0 };
  }

  size(): number {
    return this.entries.size;
  }

  private prune(nowSec: number): void {
    for (const [key, entry] of this.entries) {
      if (nowSec - entry.lastSeenAt > this.options.entryTtlSec) {
        this.entries.delete(key);
      } else {
        break;
      }
    }
  }
}

function positiveInteger(value: number | undefined, fallback: number): number {
  return Number.isSafeInteger(value) && (value as number) > 0
    ? (value as number)
    : fallback;
}

function nonNegativeInteger(value: number | undefined, fallback: number): number {
  return Number.isSafeInteger(value) && (value as number) >= 0
    ? (value as number)
    : fallback;
}

export function createCloudMatchRateLimitService(
  options: CloudMatchRateLimitOptions = {},
): CloudMatchRateLimitService {
  const maxEntries = positiveInteger(options.maxEntriesPerScope, 4_096);
  const entryTtlSec = positiveInteger(options.entryTtlSec, 600);
  const comparisonWindowSec = positiveInteger(options.comparisonWindowSec, 60);
  const comparisonMinimumInterval = nonNegativeInteger(
    options.comparisonMinIntervalSec,
    0,
  );
  const registrationWindowSec = positiveInteger(options.registrationWindowSec, 60);
  const snapshotWindowSec = positiveInteger(options.snapshotWindowSec, 4);
  const membershipWindowSec = positiveInteger(options.membershipWindowSec, 60);
  const coldWindowSec = positiveInteger(options.coldWindowSec, 10);
  const quota = (
    capacity: number,
    windowSec: number,
    minimumIntervalSec = 0,
    entries = maxEntries,
  ): BoundedTokenBucketMap =>
    new BoundedTokenBucketMap({
      capacity,
      entryTtlSec,
      maxEntries: entries,
      minimumIntervalSec,
      windowSec,
    });

  const comparisonDevices = quota(
    positiveInteger(options.comparisonDeviceCapacity, 6),
    comparisonWindowSec,
    comparisonMinimumInterval,
  );
  const comparisonIps = quota(
    positiveInteger(options.comparisonIpCapacity, 30),
    comparisonWindowSec,
    0,
  );
  const registrationIps = quota(
    positiveInteger(options.registrationIpCapacity, 60),
    registrationWindowSec,
  );
  const registrationGlobal = quota(
    positiveInteger(options.registrationGlobalCapacity, 240),
    registrationWindowSec,
    0,
    1,
  );
  const snapshotDevices = quota(
    positiveInteger(options.snapshotDeviceCapacity, 8),
    snapshotWindowSec,
  );
  const snapshotIps = quota(
    positiveInteger(options.snapshotIpCapacity, 80),
    snapshotWindowSec,
  );
  const snapshotGlobal = quota(
    positiveInteger(options.snapshotGlobalCapacity, 320),
    snapshotWindowSec,
    0,
    1,
  );
  const membershipDevices = quota(
    positiveInteger(options.membershipDeviceCapacity, 6),
    membershipWindowSec,
  );
  const membershipIps = quota(
    positiveInteger(options.membershipIpCapacity, 64),
    membershipWindowSec,
  );
  const membershipGlobal = quota(
    positiveInteger(options.membershipGlobalCapacity, 512),
    membershipWindowSec,
    0,
    1,
  );
  const coldGlobal = quota(
    positiveInteger(options.coldGlobalCapacity, 6),
    coldWindowSec,
    0,
    1,
  );
  const coldRooms = quota(
    positiveInteger(options.coldRoomCapacity, 2),
    coldWindowSec,
    nonNegativeInteger(options.coldRoomMinIntervalSec, 2),
  );
  const maxColdConcurrent = positiveInteger(options.maxColdConcurrent, 1);
  let activeCold = 0;

  return {
    allowComparison(deviceId, ipAddress, nowSec) {
      if (!comparisonDevices.consume(deviceId, nowSec).allowed) {
        return false;
      }
      return comparisonIps.consume(ipAddress, nowSec).allowed;
    },
    allowRegistration(ipAddress, nowSec) {
      if (!registrationIps.consume(ipAddress, nowSec).allowed) {
        return false;
      }
      return registrationGlobal.consume('global', nowSec).allowed;
    },
    allowSnapshotMutation(deviceId, ipAddress, nowSec) {
      const device = snapshotDevices.consume(deviceId, nowSec);
      if (!device.allowed) return device;
      const ip = snapshotIps.consume(ipAddress, nowSec);
      if (!ip.allowed) return ip;
      return snapshotGlobal.consume('global', nowSec);
    },
    allowMembershipMutation(deviceId, ipAddress, nowSec) {
      const device = membershipDevices.consume(deviceId, nowSec);
      if (!device.allowed) return device;
      const ip = membershipIps.consume(ipAddress, nowSec);
      if (!ip.allowed) return ip;
      return membershipGlobal.consume('global', nowSec);
    },
    tryAcquireColdComparison(roomId, nowSec) {
      if (activeCold >= maxColdConcurrent) {
        return null;
      }
      if (!coldRooms.consume(roomId, nowSec).allowed) {
        return null;
      }
      if (!coldGlobal.consume('global', nowSec).allowed) {
        return null;
      }
      activeCold += 1;
      let released = false;
      return () => {
        if (!released) {
          released = true;
          activeCold -= 1;
        }
      };
    },
    activeColdComparisons() {
      return activeCold;
    },
    entryCounts() {
      return {
        comparisonDevices: comparisonDevices.size(),
        comparisonIps: comparisonIps.size(),
        registrationIps: registrationIps.size(),
        coldRooms: coldRooms.size(),
        snapshotDevices: snapshotDevices.size(),
        snapshotIps: snapshotIps.size(),
        membershipDevices: membershipDevices.size(),
        membershipIps: membershipIps.size(),
      };
    },
  };
}
