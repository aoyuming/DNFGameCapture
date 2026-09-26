import type Database from 'better-sqlite3';

import type { BroadcasterAttributionService } from './broadcaster-attribution.js';
import { getActiveClientBan, type ClientBan } from './client-ban.js';
import { resolveIpRegion } from './ip-region.js';
import type { MatchSnapshot } from './schemas.js';
import { getSnapshot } from './snapshots.js';
import {
  listAllRealtimeSync,
  listAllSyncHistory,
  pruneSyncRelationData,
} from './sync-relations.js';
import {
  BROADCASTER_RETENTION_SECONDS,
  listUnifiedBroadcasters,
  pruneExpiredBroadcasters,
  type ActiveClientInfo,
} from './unified.js';

export interface AdminBroadcasterState {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
  online: boolean;
  snapshotRevision: number | null;
  receivedAt: number | null;
  offlineExpiresAt: number | null;
  clientVersion: string | null;
  snapshot: MatchSnapshot | null;
  currentIp: string | null;
  lastIp: string | null;
  region: string;
  ipObservedAt: number | null;
  license: {
    id: number;
    label: string;
    deviceId: string;
    hasKey: boolean;
    source: 'automatic' | 'manual' | 'authenticated';
  } | null;
  ban: ClientBan | null;
}

/** 已连接过但还没有填写主播名称的客户端（24 小时内活跃或当前在线）。 */
export interface AdminUnregisteredDevice {
  deviceId: string;
  deviceSuffix: string;
  registered: false;
  online: boolean;
  clientVersion: string | null;
  createdAt: number;
  lastSeenAt: number;
  currentIp: string | null;
  region: string;
  licenseDeviceId: string | null;
  license: { id: number; label: string; deviceId: string } | null;
  ban: ClientBan | null;
}

const MAX_ADMIN_UNREGISTERED_DEVICES = 300;

function listAdminUnregisteredDevices(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
  nowSec: number,
  clientInfo: ReadonlyMap<string, ActiveClientInfo> | undefined,
  normalizedQuery: string,
): AdminUnregisteredDevice[] {
  const rows = db.prepare(
    `SELECT d.id, d.created_at, d.last_seen_at, d.client_version
     FROM devices AS d
     LEFT JOIN memberships AS m ON m.device_id = d.id
     WHERE m.device_id IS NULL AND d.last_seen_at >= ?
     ORDER BY d.last_seen_at DESC
     LIMIT ?`,
  ).all(nowSec - BROADCASTER_RETENTION_SECONDS, MAX_ADMIN_UNREGISTERED_DEVICES) as Array<{
    id: string; created_at: number; last_seen_at: number; client_version: string | null;
  }>;
  const findLicense = db.prepare(
    'SELECT id, label, bound_device_id FROM licenses WHERE bound_device_id = ? ORDER BY id DESC LIMIT 1',
  );
  return rows
    .map((row) => {
      const online = activeDeviceIds.has(row.id);
      const info = online ? clientInfo?.get(row.id) : undefined;
      const currentIp = info?.ipAddress ?? null;
      let region = '未知地区';
      if (currentIp) {
        try { region = resolveIpRegion(currentIp) || region; } catch { /* diagnostic only */ }
      }
      const licenseDeviceId = info?.licenseDeviceId ?? null;
      const licenseRow = licenseDeviceId
        ? findLicense.get(licenseDeviceId) as { id: number; label: string; bound_device_id: string } | undefined
        : undefined;
      return {
        deviceId: row.id,
        deviceSuffix: row.id.slice(-4),
        registered: false as const,
        online,
        clientVersion: info?.clientVersion ?? row.client_version ?? null,
        createdAt: row.created_at,
        lastSeenAt: row.last_seen_at,
        currentIp,
        region,
        licenseDeviceId,
        license: licenseRow
          ? { id: licenseRow.id, label: licenseRow.label, deviceId: licenseRow.bound_device_id }
          : null,
        ban: getActiveClientBan(db, [row.id, licenseDeviceId], nowSec),
      };
    })
    .filter((item) => {
      if (!normalizedQuery) return true;
      return item.deviceId.toLocaleLowerCase().includes(normalizedQuery) ||
        (item.clientVersion ?? '').toLocaleLowerCase().includes(normalizedQuery) ||
        (item.currentIp ?? '').toLocaleLowerCase().includes(normalizedQuery) ||
        item.region.toLocaleLowerCase().includes(normalizedQuery) ||
        (item.license?.label ?? '').toLocaleLowerCase().includes(normalizedQuery) ||
        '未注册'.includes(normalizedQuery);
    })
    .sort((a, b) => Number(b.online) - Number(a.online) || b.lastSeenAt - a.lastSeenAt);
}

export interface AdminState {
  generatedAt: number;
  broadcasters: AdminBroadcasterState[];
  unregisteredDevices: AdminUnregisteredDevice[];
  relations: ReturnType<typeof listAllRealtimeSync>;
  history: ReturnType<typeof listAllSyncHistory>;
}

export interface AdminCleanupResult {
  deletedCount: number;
  deletedDeviceIds: string[];
}

export function buildAdminState(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
  nowSec: number,
  attribution: BroadcasterAttributionService,
  query = '',
  clientInfo?: ReadonlyMap<string, ActiveClientInfo>,
): AdminState {
  const normalizedQuery = query.normalize('NFC').trim().toLocaleLowerCase();
  const broadcasters = listUnifiedBroadcasters(db, activeDeviceIds, nowSec)
    .map((item) => {
      const network = attribution.getBroadcasterNetwork(item.deviceId);
      const link = attribution.getBroadcasterAttribution(item.deviceId);
      const ban = getActiveClientBan(db, [item.deviceId, link?.licenseDeviceId], nowSec);
      const liveVersion = item.online ? clientInfo?.get(item.deviceId)?.clientVersion : null;
      return {
        ...item,
        clientVersion: liveVersion ?? item.clientVersion ?? null,
        currentIp: network.currentIp,
        lastIp: network.lastIp,
        region: network.region,
        ipObservedAt: network.observedAt,
        license: link ? {
          id: link.licenseId,
          label: link.licenseLabel,
          deviceId: link.licenseDeviceId,
          hasKey: link.hasKey,
          source: link.source,
        } : null,
        ban,
      };
    })
    .filter((item) => {
      if (!normalizedQuery) return true;
      return item.broadcasterName.toLocaleLowerCase().includes(normalizedQuery) ||
        item.deviceId.toLocaleLowerCase().includes(normalizedQuery) ||
        item.deviceSuffix.toLocaleLowerCase().includes(normalizedQuery) ||
        item.currentIp?.toLocaleLowerCase().includes(normalizedQuery) ||
        item.lastIp?.toLocaleLowerCase().includes(normalizedQuery) ||
        item.region.toLocaleLowerCase().includes(normalizedQuery) ||
        item.license?.label.toLocaleLowerCase().includes(normalizedQuery) ||
        item.license?.deviceId.toLocaleLowerCase().includes(normalizedQuery) ||
        (item.clientVersion ?? '').toLocaleLowerCase().includes(normalizedQuery);
    })
    .map((item) => ({
      ...item,
      snapshot: getSnapshot(db, item.deviceId)?.snapshot ?? null,
    }));
  return {
    generatedAt: nowSec,
    broadcasters,
    unregisteredDevices: listAdminUnregisteredDevices(db, activeDeviceIds, nowSec, clientInfo, normalizedQuery),
    relations: listAllRealtimeSync(db, nowSec),
    history: listAllSyncHistory(db, nowSec),
  };
}

export function listAdminTemporaryBroadcasterIds(
  db: Database.Database,
): string[] {
  const rows = db.prepare(
    `SELECT device_id
     FROM memberships
     WHERE room_id = 'all-broadcasters' AND device_id LIKE 'dnf-tmp-%'
     ORDER BY device_id`,
  ).all() as Array<{ device_id: string }>;
  return rows.map((row) => row.device_id);
}

export function deleteBroadcasterLobbyData(
  db: Database.Database,
  deviceId: string,
): boolean {
  return db.transaction(() => {
    const membership = db.prepare(
      'SELECT room_id FROM memberships WHERE device_id = ?',
    ).get(deviceId) as { room_id: string } | undefined;
    if (!membership) return false;

    db.prepare(
      `DELETE FROM sync_history
       WHERE source_device_id = ? OR target_device_id = ?`,
    ).run(deviceId, deviceId);
    db.prepare(
      `DELETE FROM realtime_sync
       WHERE viewer_device_id = ? OR target_device_id = ?`,
    ).run(deviceId, deviceId);
    db.prepare('DELETE FROM snapshot_audit WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM snapshots WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM memberships WHERE device_id = ?').run(deviceId);
    db.prepare(
      `UPDATE rooms
       SET revision = revision + 1,
           presence_revision = presence_revision + 1
       WHERE id = ?`,
    ).run(membership.room_id);
    return true;
  })();
}

export function clearOfflineAndTemporaryBroadcasterData(
  db: Database.Database,
  activeDeviceIds: ReadonlySet<string>,
): AdminCleanupResult {
  const rows = db.prepare(
    `SELECT device_id
     FROM memberships
     WHERE room_id = 'all-broadcasters'
     ORDER BY device_id`,
  ).all() as Array<{ device_id: string }>;
  const deletedDeviceIds = rows
    .map((row) => row.device_id)
    .filter((deviceId) =>
      deviceId.startsWith('dnf-tmp-') || !activeDeviceIds.has(deviceId))
    .filter((deviceId) => deleteBroadcasterLobbyData(db, deviceId));
  return { deletedCount: deletedDeviceIds.length, deletedDeviceIds };
}

export function pruneExpiredAdminData(
  db: Database.Database,
  nowSec: number,
): {
  removedBroadcasters: number;
  removedSyncRecords: number;
  removedSnapshotAudit: number;
} {
  const historyBefore = (db.prepare(
    'SELECT COUNT(*) AS count FROM sync_history',
  ).get() as { count: number }).count;
  const auditBefore = (db.prepare(
    'SELECT COUNT(*) AS count FROM snapshot_audit',
  ).get() as { count: number }).count;
  const removedBroadcasters = pruneExpiredBroadcasters(db, nowSec);
  pruneSyncRelationData(db, nowSec);
  db.prepare(
    `DELETE FROM snapshot_audit
     WHERE device_id NOT IN (SELECT device_id FROM memberships)`,
  ).run();
  const historyAfter = (db.prepare(
    'SELECT COUNT(*) AS count FROM sync_history',
  ).get() as { count: number }).count;
  const auditAfter = (db.prepare(
    'SELECT COUNT(*) AS count FROM snapshot_audit',
  ).get() as { count: number }).count;
  return {
    removedBroadcasters,
    removedSyncRecords: historyBefore - historyAfter,
    removedSnapshotAudit: auditBefore - auditAfter,
  };
}
