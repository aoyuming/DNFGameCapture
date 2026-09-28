import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, describe, expect, test } from 'vitest';

import { buildAdminState } from '../src/admin-data.js';
import { createBroadcasterAttributionService } from '../src/broadcaster-attribution.js';
import { openDatabase } from '../src/db.js';
import { registerDevice } from '../src/identity.js';
import { initializeSyncRelationSchema } from '../src/sync-relations.js';
import {
  joinUnifiedPool,
  listUnifiedBroadcasters,
  recordDeviceClientVersion,
} from '../src/unified.js';

const now = 1_700_000_000;
const resources: Array<{ directory: string; close(): void }> = [];

afterEach(() => {
  for (const resource of resources.splice(0)) {
    resource.close();
    rmSync(resource.directory, { recursive: true, force: true });
  }
});

function createFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-cloud-version-'));
  const db = openDatabase(join(directory, 'version.sqlite'));
  resources.push({ directory, close: () => db.close() });
  initializeSyncRelationSchema(db);
  const attribution = createBroadcasterAttributionService(db, { resolveRegion: () => '测试地区' });
  return { db, attribution };
}

describe('client version reporting', () => {
  test('stores reported versions and exposes them in the broadcaster directory', () => {
    const { db } = createFixture();
    expect(registerDevice(db, 'named-device-0001', now)).not.toBeNull();
    expect(joinUnifiedPool(db, 'named-device-0001', '版本主播', now)).not.toBeNull();

    expect(recordDeviceClientVersion(db, 'named-device-0001', '5.5.0')).toBe(true);
    expect(recordDeviceClientVersion(db, 'named-device-0001', '5.5.0')).toBe(false);
    expect(recordDeviceClientVersion(db, 'named-device-0001', 'bad version!')).toBe(false);
    expect(recordDeviceClientVersion(db, 'missing-device-0009', '5.5.0')).toBe(false);

    const [item] = listUnifiedBroadcasters(db, new Set(['named-device-0001']), now);
    expect(item).toMatchObject({
      deviceId: 'named-device-0001',
      broadcasterName: '版本主播',
      online: true,
      clientVersion: '5.5.0',
    });
  });

  test('lists devices without a broadcaster name separately in admin state', () => {
    const { db, attribution } = createFixture();
    expect(registerDevice(db, 'named-device-0001', now)).not.toBeNull();
    expect(joinUnifiedPool(db, 'named-device-0001', '已命名主播', now)).not.toBeNull();
    expect(registerDevice(db, 'unnamed-device-0002', now)).not.toBeNull();
    expect(recordDeviceClientVersion(db, 'unnamed-device-0002', '5.4.0')).toBe(true);

    const clientInfo = new Map([
      ['unnamed-device-0002', { clientVersion: '5.5.0', ipAddress: '10.0.0.8', licenseDeviceId: null }],
    ]);
    const state = buildAdminState(db, new Set(['unnamed-device-0002']), now, attribution, '', clientInfo);
    expect(state.broadcasters.map((item) => item.deviceId)).toEqual(['named-device-0001']);
    expect(state.unregisteredDevices).toEqual([
      expect.objectContaining({
        deviceId: 'unnamed-device-0002',
        deviceSuffix: '0002',
        registered: false,
        online: true,
        clientVersion: '5.5.0',
        currentIp: '10.0.0.8',
        region: '内网',
        license: null,
        ban: null,
      }),
    ]);

    const searched = buildAdminState(db, new Set(), now, attribution, '5.4.0');
    expect(searched.broadcasters).toEqual([]);
    expect(searched.unregisteredDevices).toEqual([
      expect.objectContaining({ deviceId: 'unnamed-device-0002', online: false, clientVersion: '5.4.0', currentIp: null }),
    ]);
  });
});
