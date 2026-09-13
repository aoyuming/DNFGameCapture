import type Database from 'better-sqlite3';

import { isPublicIpAddress, normalizeIpAddress, resolveIpRegion } from './ip-region.js';

export const ATTRIBUTION_MATCH_WINDOW_SECONDS = 120;

export interface LicenseObservation {
  licenseId: number;
  licenseDeviceId: string;
  ipAddress: string;
  observedAt: number;
}

export interface BroadcasterObservation {
  deviceId: string;
  broadcasterName: string;
  ipAddress: string;
  observedAt: number;
}

export interface BroadcasterLicenseLink {
  broadcasterDeviceId: string;
  broadcasterName: string;
  licenseId: number;
  licenseLabel: string;
  licenseDeviceId: string;
  hasKey: boolean;
  source: 'automatic' | 'manual';
  linkedAt: number;
  updatedAt: number;
}

export interface BroadcasterNetwork {
  currentIp: string | null;
  lastIp: string | null;
  region: string;
  observedAt: number | null;
}

export class BroadcasterAttributionError extends Error {
  constructor(public readonly status: number, public readonly code: string) {
    super(code);
  }
}

export interface BroadcasterAttributionService {
  observeLicense(input: LicenseObservation): void;
  connectBroadcaster(input: BroadcasterObservation): void;
  renameBroadcaster(deviceId: string, broadcasterName: string, observedAt: number): void;
  disconnectBroadcaster(deviceId: string): void;
  manualLink(deviceId: string, licenseId: number, nowSec: number): BroadcasterLicenseLink;
  getActiveDeviceIps(): ReadonlyMap<string, string>;
  getBroadcasterAttribution(deviceId: string): BroadcasterLicenseLink | null;
  getBroadcasterNetwork(deviceId: string): BroadcasterNetwork;
}

export function initializeBroadcasterAttributionSchema(db: Database.Database): void {
  db.exec(`
    CREATE TABLE IF NOT EXISTS license_ip_observations (
      license_device_id TEXT PRIMARY KEY,
      license_id INTEGER NOT NULL REFERENCES licenses(id) ON DELETE CASCADE,
      ip_address TEXT NOT NULL,
      observed_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_license_ip_observations_address
      ON license_ip_observations(ip_address, observed_at DESC);

    CREATE TABLE IF NOT EXISTS broadcaster_network_observations (
      broadcaster_device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
      ip_address TEXT NOT NULL,
      region TEXT NOT NULL,
      observed_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_broadcaster_network_address
      ON broadcaster_network_observations(ip_address, observed_at DESC);

    CREATE TABLE IF NOT EXISTS broadcaster_license_links (
      broadcaster_device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
      license_id INTEGER NOT NULL REFERENCES licenses(id) ON DELETE CASCADE,
      license_device_id TEXT NOT NULL,
      broadcaster_name TEXT NOT NULL,
      source TEXT NOT NULL CHECK(source IN ('automatic','manual')),
      linked_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_broadcaster_license_links_license
      ON broadcaster_license_links(license_id, updated_at DESC);
    CREATE INDEX IF NOT EXISTS idx_broadcaster_license_links_device
      ON broadcaster_license_links(license_device_id, updated_at DESC);
  `);
}

function rowToLink(row: {
  broadcaster_device_id: string;
  broadcaster_name: string;
  license_id: number;
  license_label: string;
  license_device_id: string;
  has_key: number;
  source: 'automatic' | 'manual';
  linked_at: number;
  updated_at: number;
}): BroadcasterLicenseLink {
  return {
    broadcasterDeviceId: row.broadcaster_device_id,
    broadcasterName: row.broadcaster_name,
    licenseId: row.license_id,
    licenseLabel: row.license_label,
    licenseDeviceId: row.license_device_id,
    hasKey: row.has_key === 1,
    source: row.source,
    linkedAt: row.linked_at,
    updatedAt: row.updated_at,
  };
}

export function createBroadcasterAttributionService(
  db: Database.Database,
  options: { resolveRegion?: (ipAddress: string) => string } = {},
): BroadcasterAttributionService {
  initializeBroadcasterAttributionSchema(db);
  const activeBroadcasters = new Map<string, { ipAddress: string; broadcasterName: string }>();
  const regionOf = options.resolveRegion ?? resolveIpRegion;

  const getBroadcasterAttribution = (deviceId: string): BroadcasterLicenseLink | null => {
    const row = db.prepare(`
      SELECT link.broadcaster_device_id, link.broadcaster_name, link.license_id,
             license.label AS license_label, link.license_device_id,
             CASE WHEN license.key_ciphertext IS NULL THEN 0 ELSE 1 END AS has_key,
             link.source, link.linked_at, link.updated_at
      FROM broadcaster_license_links AS link
      JOIN licenses AS license ON license.id=link.license_id
        AND license.bound_device_id=link.license_device_id
      WHERE link.broadcaster_device_id=?
    `).get(deviceId) as Parameters<typeof rowToLink>[0] | undefined;
    return row ? rowToLink(row) : null;
  };

  const clearAmbiguousAutomaticLinks = (ipAddress: string): void => {
    const ids = [...activeBroadcasters]
      .filter(([, active]) => active.ipAddress === ipAddress)
      .map(([deviceId]) => deviceId);
    if (!ids.length) return;
    const remove = db.prepare(`DELETE FROM broadcaster_license_links
      WHERE broadcaster_device_id=? AND source='automatic'`);
    db.transaction(() => ids.forEach(deviceId => remove.run(deviceId)))();
  };

  const tryAutomaticLink = (ipAddress: string, nowSec: number): void => {
    if (!isPublicIpAddress(ipAddress)) return;
    const broadcasters = [...activeBroadcasters]
      .filter(([, active]) => active.ipAddress === ipAddress);
    const licenses = db.prepare(`
      SELECT observation.license_id, observation.license_device_id
      FROM license_ip_observations AS observation
      JOIN licenses AS license ON license.id=observation.license_id
        AND license.bound_device_id=observation.license_device_id
      WHERE observation.ip_address=? AND observation.observed_at>=?
        AND license.activated_at IS NOT NULL AND license.disabled_at IS NULL
        AND (license.expires_at IS NULL OR license.expires_at>?)
      ORDER BY observation.observed_at DESC
    `).all(ipAddress, nowSec - ATTRIBUTION_MATCH_WINDOW_SECONDS, nowSec) as Array<{
      license_id: number;
      license_device_id: string;
    }>;
    if (broadcasters.length !== 1 || licenses.length !== 1) {
      clearAmbiguousAutomaticLinks(ipAddress);
      return;
    }
    const [broadcasterDeviceId, active] = broadcasters[0];
    const candidate = licenses[0];
    const existing = db.prepare('SELECT source FROM broadcaster_license_links WHERE broadcaster_device_id=?')
      .get(broadcasterDeviceId) as { source: string } | undefined;
    if (existing?.source === 'manual') return;
    const claimed = db.prepare(`SELECT 1 FROM broadcaster_license_links
      WHERE license_id=? AND source='manual' AND broadcaster_device_id<>? LIMIT 1`)
      .get(candidate.license_id, broadcasterDeviceId);
    if (claimed) return;
    db.prepare(`
      INSERT INTO broadcaster_license_links(
        broadcaster_device_id,license_id,license_device_id,broadcaster_name,source,linked_at,updated_at
      ) VALUES(?,?,?,?,'automatic',?,?)
      ON CONFLICT(broadcaster_device_id) DO UPDATE SET
        license_id=excluded.license_id,
        license_device_id=excluded.license_device_id,
        broadcaster_name=excluded.broadcaster_name,
        updated_at=excluded.updated_at
      WHERE broadcaster_license_links.source='automatic'
    `).run(broadcasterDeviceId, candidate.license_id, candidate.license_device_id,
      active.broadcasterName, nowSec, nowSec);
  };

  return {
    observeLicense(input): void {
      const ipAddress = normalizeIpAddress(input.ipAddress);
      if (!ipAddress) return;
      try {
        db.prepare(`
          INSERT INTO license_ip_observations(license_device_id,license_id,ip_address,observed_at)
          VALUES(?,?,?,?)
          ON CONFLICT(license_device_id) DO UPDATE SET
            license_id=excluded.license_id,
            ip_address=excluded.ip_address,
            observed_at=excluded.observed_at
        `).run(input.licenseDeviceId, input.licenseId, ipAddress, input.observedAt);
        tryAutomaticLink(ipAddress, input.observedAt);
      } catch {
        // Attribution telemetry must never block authorization or library traffic.
      }
    },

    connectBroadcaster(input): void {
      const ipAddress = normalizeIpAddress(input.ipAddress) ?? 'unknown';
      activeBroadcasters.set(input.deviceId, {
        ipAddress,
        broadcasterName: input.broadcasterName,
      });
      try {
        db.prepare(`
          INSERT INTO broadcaster_network_observations(broadcaster_device_id,ip_address,region,observed_at)
          VALUES(?,?,?,?)
          ON CONFLICT(broadcaster_device_id) DO UPDATE SET
            ip_address=excluded.ip_address,
            region=excluded.region,
            observed_at=excluded.observed_at
        `).run(input.deviceId, ipAddress, regionOf(ipAddress), input.observedAt);
        tryAutomaticLink(ipAddress, input.observedAt);
      } catch {
        // Network attribution is diagnostic and cannot break a broadcaster connection.
      }
    },

    renameBroadcaster(deviceId, broadcasterName, observedAt): void {
      const active = activeBroadcasters.get(deviceId);
      if (active) active.broadcasterName = broadcasterName;
      try {
        db.prepare(`UPDATE broadcaster_license_links
          SET broadcaster_name=?,updated_at=? WHERE broadcaster_device_id=?`)
          .run(broadcasterName, observedAt, deviceId);
      } catch {
        // A later observation can refresh the diagnostic name.
      }
    },

    disconnectBroadcaster(deviceId): void {
      activeBroadcasters.delete(deviceId);
    },

    manualLink(deviceId, licenseId, nowSec): BroadcasterLicenseLink {
      const broadcaster = db.prepare('SELECT broadcaster_name FROM memberships WHERE device_id=?')
        .get(deviceId) as { broadcaster_name: string } | undefined;
      if (!broadcaster) throw new BroadcasterAttributionError(404, 'broadcaster_not_found');
      const license = db.prepare(`SELECT bound_device_id,activated_at FROM licenses WHERE id=?`)
        .get(licenseId) as { bound_device_id: string | null; activated_at: number | null } | undefined;
      if (!license) throw new BroadcasterAttributionError(404, 'license_not_found');
      if (!license.bound_device_id || license.activated_at === null) {
        throw new BroadcasterAttributionError(409, 'license_not_activated');
      }
      db.prepare(`
        INSERT INTO broadcaster_license_links(
          broadcaster_device_id,license_id,license_device_id,broadcaster_name,source,linked_at,updated_at
        ) VALUES(?,?,?,?,'manual',?,?)
        ON CONFLICT(broadcaster_device_id) DO UPDATE SET
          license_id=excluded.license_id,
          license_device_id=excluded.license_device_id,
          broadcaster_name=excluded.broadcaster_name,
          source='manual',
          updated_at=excluded.updated_at
      `).run(deviceId, licenseId, license.bound_device_id, broadcaster.broadcaster_name, nowSec, nowSec);
      return getBroadcasterAttribution(deviceId)!;
    },

    getActiveDeviceIps(): ReadonlyMap<string, string> {
      return new Map([...activeBroadcasters].map(([deviceId, active]) => [deviceId, active.ipAddress]));
    },

    getBroadcasterAttribution,

    getBroadcasterNetwork(deviceId): BroadcasterNetwork {
      const stored = db.prepare(`SELECT ip_address,region,observed_at
        FROM broadcaster_network_observations WHERE broadcaster_device_id=?`).get(deviceId) as {
          ip_address: string;
          region: string;
          observed_at: number;
        } | undefined;
      return {
        currentIp: activeBroadcasters.get(deviceId)?.ipAddress ?? null,
        lastIp: stored?.ip_address ?? null,
        region: stored?.region ?? '未知地区',
        observedAt: stored?.observed_at ?? null,
      };
    },
  };
}
