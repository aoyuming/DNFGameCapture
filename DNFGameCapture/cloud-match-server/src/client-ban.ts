import type Database from 'better-sqlite3';

export interface ClientBan {
  broadcasterDeviceId: string;
  licenseDeviceId: string | null;
  bannedAt: number;
  expiresAt: number | null;
  updatedAt: number;
}

export interface SetClientBanInput {
  broadcasterDeviceId: string;
  licenseDeviceId?: string | null;
  bannedAt: number;
  expiresAt: number | null;
}

interface ClientBanRow {
  broadcaster_device_id: string;
  license_device_id: string | null;
  banned_at: number;
  expires_at: number | null;
  updated_at: number;
}

function toClientBan(row: ClientBanRow): ClientBan {
  return {
    broadcasterDeviceId: row.broadcaster_device_id,
    licenseDeviceId: row.license_device_id,
    bannedAt: row.banned_at,
    expiresAt: row.expires_at,
    updatedAt: row.updated_at,
  };
}

function normalizedIds(values: readonly (string | null | undefined)[]): string[] {
  return [...new Set(values.map(value => value?.trim() ?? '').filter(Boolean))];
}

function relatedRows(
  db: Database.Database,
  identifiers: readonly string[],
): ClientBanRow[] {
  if (!identifiers.length) return [];
  const placeholders = identifiers.map(() => '?').join(',');
  return db.prepare(
    `SELECT broadcaster_device_id,license_device_id,banned_at,expires_at,updated_at
     FROM client_bans
     WHERE broadcaster_device_id IN (${placeholders})
        OR license_device_id IN (${placeholders})`,
  ).all(...identifiers, ...identifiers) as ClientBanRow[];
}

function collectRelatedIds(
  db: Database.Database,
  initialIdentifiers: readonly (string | null | undefined)[],
): string[] {
  const identifiers = new Set(normalizedIds(initialIdentifiers));
  while (identifiers.size) {
    let changed = false;
    for (const row of relatedRows(db, [...identifiers])) {
      for (const value of [row.broadcaster_device_id, row.license_device_id]) {
        if (value && !identifiers.has(value)) {
          identifiers.add(value);
          changed = true;
        }
      }
    }
    if (!changed) break;
  }
  return [...identifiers];
}

function deleteRelatedBans(
  db: Database.Database,
  initialIdentifiers: readonly (string | null | undefined)[],
): number {
  const identifiers = collectRelatedIds(db, initialIdentifiers);
  if (!identifiers.length) return 0;
  const placeholders = identifiers.map(() => '?').join(',');
  return db.prepare(
    `DELETE FROM client_bans
     WHERE broadcaster_device_id IN (${placeholders})
        OR license_device_id IN (${placeholders})`,
  ).run(...identifiers, ...identifiers).changes;
}

export function getActiveClientBan(
  db: Database.Database,
  identifiers: readonly (string | null | undefined)[],
  nowSec: number,
): ClientBan | null {
  const ids = normalizedIds(identifiers);
  if (!ids.length) return null;
  const placeholders = ids.map(() => '?').join(',');
  const row = db.prepare(
    `SELECT broadcaster_device_id,license_device_id,banned_at,expires_at,updated_at
     FROM client_bans
     WHERE (broadcaster_device_id IN (${placeholders})
        OR license_device_id IN (${placeholders}))
       AND (expires_at IS NULL OR expires_at>?)
     ORDER BY expires_at IS NULL DESC,expires_at DESC,banned_at DESC
     LIMIT 1`,
  ).get(...ids, ...ids, nowSec) as ClientBanRow | undefined;
  return row ? toClientBan(row) : null;
}

export function setClientBan(
  db: Database.Database,
  input: SetClientBanInput,
): ClientBan {
  const broadcasterDeviceId = input.broadcasterDeviceId.trim();
  const licenseDeviceId = input.licenseDeviceId?.trim() || null;
  if (!broadcasterDeviceId || !Number.isSafeInteger(input.bannedAt) ||
      (input.expiresAt !== null && (!Number.isSafeInteger(input.expiresAt) ||
        input.expiresAt <= input.bannedAt))) {
    throw new RangeError('invalid_client_ban');
  }
  return db.transaction(() => {
    deleteRelatedBans(db, [broadcasterDeviceId, licenseDeviceId]);
    db.prepare(
      `INSERT INTO client_bans(
         broadcaster_device_id,license_device_id,banned_at,expires_at,updated_at
       ) VALUES(?,?,?,?,?)`,
    ).run(broadcasterDeviceId, licenseDeviceId, input.bannedAt, input.expiresAt, input.bannedAt);
    return {
      broadcasterDeviceId,
      licenseDeviceId,
      bannedAt: input.bannedAt,
      expiresAt: input.expiresAt,
      updatedAt: input.bannedAt,
    };
  }).immediate();
}

export function clearClientBan(
  db: Database.Database,
  identifiers: readonly (string | null | undefined)[],
): boolean {
  const ids = normalizedIds(identifiers);
  if (!ids.length) return false;
  return db.transaction(() => deleteRelatedBans(db, ids) > 0).immediate();
}
