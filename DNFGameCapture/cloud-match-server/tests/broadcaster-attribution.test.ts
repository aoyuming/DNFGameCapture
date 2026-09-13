import { afterEach, describe, expect, test } from 'vitest';
import type Database from 'better-sqlite3';

import { generateLicenseKey } from '../src/auth.js';
import {
  createBroadcasterAttributionService,
  initializeBroadcasterAttributionSchema,
} from '../src/broadcaster-attribution.js';
import { openDatabase } from '../src/db.js';
import { normalizeIpAddress, resolveIpRegion } from '../src/ip-region.js';
import { activateStoredLicense, createLicense } from '../src/license-store.js';
import { joinUnifiedPool } from '../src/unified.js';

const databases: Database.Database[] = [];

function fixture() {
  const db = openDatabase(':memory:');
  databases.push(db);
  initializeBroadcasterAttributionSchema(db);
  const service = createBroadcasterAttributionService(db, {
    resolveRegion: (ipAddress) => ipAddress === '47.1.2.3'
      ? '中国 · 浙江 · 杭州'
      : '未知地区',
  });
  const addLicense = (deviceId: string, nowSec = 100) => {
    const key = generateLicenseKey();
    const license = createLicense(db, { key, nowSec });
    activateStoredLicense(db, key, deviceId, nowSec, 600);
    return license.id;
  };
  const addBroadcaster = (deviceId: string, name: string, nowSec = 100) => {
    db.prepare('INSERT INTO devices(id,token_hash,created_at,last_seen_at) VALUES(?,?,?,?)')
      .run(deviceId, 'unused', nowSec, nowSec);
    expect(joinUnifiedPool(db, deviceId, name, nowSec)).not.toBeNull();
  };
  return { db, service, addLicense, addBroadcaster };
}

afterEach(() => {
  while (databases.length) databases.pop()!.close();
});

describe('IP normalization and local region lookup', () => {
  test('normalizes IPv4-mapped IPv6 and classifies private addresses locally', () => {
    expect(normalizeIpAddress('::ffff:47.1.2.3')).toBe('47.1.2.3');
    expect(resolveIpRegion('127.0.0.1')).toBe('内网');
    expect(resolveIpRegion('192.168.1.20')).toBe('内网');
    expect(resolveIpRegion('not-an-ip')).toBe('未知地区');
  });

  test('formats the most specific available offline GeoIP fields', () => {
    expect(resolveIpRegion('47.1.2.3', () => ({
      range: [0, 0], country: 'CN', region: 'ZJ', eu: '0', timezone: 'Asia/Shanghai',
      city: 'Hangzhou', ll: [30, 120], metro: 0, area: 10,
    }))).toBe('中国 · ZJ · Hangzhou');
  });
});

describe('server-side broadcaster attribution', () => {
  test('initializes its schema repeatedly without changing existing rows', () => {
    const { db, addLicense } = fixture();
    const licenseId = addLicense('machine-a');
    db.prepare(`INSERT INTO license_ip_observations
      (license_device_id,license_id,ip_address,observed_at) VALUES(?,?,?,?)`)
      .run('machine-a', licenseId, '47.1.2.3', 100);

    initializeBroadcasterAttributionSchema(db);

    expect(db.prepare('SELECT license_id,ip_address FROM license_ip_observations').all())
      .toEqual([{ license_id: licenseId, ip_address: '47.1.2.3' }]);
  });

  test('automatically links the unique recent license and active broadcaster on one public IP', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const licenseId = addLicense('machine-a');
    addBroadcaster('socket-a', '主播甲');

    service.observeLicense({ licenseId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 100 });
    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '::ffff:47.1.2.3', observedAt: 101 });

    expect(service.getBroadcasterAttribution('socket-a')).toMatchObject({
      broadcasterDeviceId: 'socket-a', broadcasterName: '主播甲', licenseId,
      licenseDeviceId: 'machine-a', source: 'automatic',
    });
    expect(service.getActiveDeviceIps().get('socket-a')).toBe('47.1.2.3');
    expect(service.getBroadcasterNetwork('socket-a')).toMatchObject({
      currentIp: '47.1.2.3', lastIp: '47.1.2.3', region: '中国 · 浙江 · 杭州',
    });
  });

  test('does not guess when an IP has multiple recent license candidates', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const firstId = addLicense('machine-a');
    const secondId = addLicense('machine-b');
    addBroadcaster('socket-a', '主播甲');

    service.observeLicense({ licenseId: firstId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 100 });
    service.observeLicense({ licenseId: secondId, licenseDeviceId: 'machine-b', ipAddress: '47.1.2.3', observedAt: 100 });
    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 101 });

    expect(service.getBroadcasterAttribution('socket-a')).toBeNull();
  });

  test('does not guess when an IP has multiple active broadcaster candidates', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const licenseId = addLicense('machine-a');
    addBroadcaster('socket-a', '主播甲');
    addBroadcaster('socket-b', '主播乙');

    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 100 });
    service.connectBroadcaster({ deviceId: 'socket-b', broadcasterName: '主播乙', ipAddress: '47.1.2.3', observedAt: 100 });
    service.observeLicense({ licenseId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 101 });

    expect(service.getBroadcasterAttribution('socket-a')).toBeNull();
    expect(service.getBroadcasterAttribution('socket-b')).toBeNull();
  });

  test('ignores private and stale IP evidence', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const licenseId = addLicense('machine-a');
    addBroadcaster('socket-a', '主播甲');
    addBroadcaster('socket-b', '主播乙');

    service.observeLicense({ licenseId, licenseDeviceId: 'machine-a', ipAddress: '192.168.1.2', observedAt: 100 });
    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '192.168.1.2', observedAt: 101 });
    service.observeLicense({ licenseId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 200 });
    service.connectBroadcaster({ deviceId: 'socket-b', broadcasterName: '主播乙', ipAddress: '47.1.2.3', observedAt: 321 });

    expect(service.getBroadcasterAttribution('socket-a')).toBeNull();
    expect(service.getBroadcasterAttribution('socket-b')).toBeNull();
  });

  test('manual attribution wins over later automatic evidence and follows broadcaster renames', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const firstId = addLicense('machine-a');
    const secondId = addLicense('machine-b');
    addBroadcaster('socket-a', '主播甲');

    service.manualLink('socket-a', firstId, 100);
    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 101 });
    service.observeLicense({ licenseId: secondId, licenseDeviceId: 'machine-b', ipAddress: '47.1.2.3', observedAt: 102 });
    service.renameBroadcaster('socket-a', '主播甲改名', 103);

    expect(service.getBroadcasterAttribution('socket-a')).toMatchObject({
      licenseId: firstId, licenseDeviceId: 'machine-a', broadcasterName: '主播甲改名', source: 'manual',
    });
  });

  test('manual attribution transfers one license away from its previous broadcaster', () => {
    const { service, addLicense, addBroadcaster } = fixture();
    const licenseId = addLicense('machine-a');
    addBroadcaster('socket-a', '主播甲');
    addBroadcaster('socket-b', '主播乙');

    service.manualLink('socket-a', licenseId, 100);
    service.manualLink('socket-b', licenseId, 101);

    expect(service.getBroadcasterAttribution('socket-a')).toBeNull();
    expect(service.getBroadcasterAttribution('socket-b')).toMatchObject({
      licenseId, broadcasterName: '主播乙', source: 'manual',
    });
  });

  test('rejects a manual link when the license is not activated or no longer bound', () => {
    const { db, service, addBroadcaster } = fixture();
    addBroadcaster('socket-a', '主播甲');
    const key = generateLicenseKey();
    const pending = createLicense(db, { key, nowSec: 100 });

    expect(() => service.manualLink('socket-a', pending.id, 101)).toThrow('license_not_activated');
  });

  test('keeps the last IP after disconnect while clearing the current IP', () => {
    const { service, addBroadcaster } = fixture();
    addBroadcaster('socket-a', '主播甲');
    service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 100 });

    service.disconnectBroadcaster('socket-a');

    expect(service.getActiveDeviceIps().has('socket-a')).toBe(false);
    expect(service.getBroadcasterNetwork('socket-a')).toMatchObject({
      currentIp: null, lastIp: '47.1.2.3', region: '中国 · 浙江 · 杭州', observedAt: 100,
    });
  });
});
