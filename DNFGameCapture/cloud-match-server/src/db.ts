import Database from 'better-sqlite3';

import { pruneSnapshotAudit } from './snapshots.js';
export const ALL_BROADCASTERS_ROOM_ID = 'all-broadcasters';

const ROOM_SEEDS = [
  { id: '59', displayName: '59房' },
  { id: 'li-yong', displayName: '李永房' },
  { id: 'wen-rou', displayName: '温柔房' },
] as const;

function initializeSchema(db: Database.Database): void {
  const initialize = db.transaction(() => {
    db.exec(`
      CREATE TABLE IF NOT EXISTS devices (
        id TEXT PRIMARY KEY,
        token_hash TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        last_seen_at INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS rooms (
        id TEXT PRIMARY KEY,
        display_name TEXT NOT NULL UNIQUE,
        revision INTEGER NOT NULL DEFAULT 0,
        presence_revision INTEGER NOT NULL DEFAULT 0
      );

      CREATE TABLE IF NOT EXISTS memberships (
        device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
        room_id TEXT NOT NULL REFERENCES rooms(id),
        broadcaster_name TEXT NOT NULL,
        updated_at INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS snapshots (
        device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
        room_id TEXT NOT NULL REFERENCES rooms(id),
        client_revision INTEGER NOT NULL,
        content_hash TEXT NOT NULL,
        change_source TEXT NOT NULL,
        synced_from_device_id TEXT,
        synced_from_revision INTEGER,
        payload_json TEXT NOT NULL,
        received_at INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS snapshot_audit (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id TEXT NOT NULL,
        room_id TEXT NOT NULL,
        client_revision INTEGER NOT NULL,
        accepted INTEGER NOT NULL,
        reason TEXT NOT NULL,
        received_at INTEGER NOT NULL
      );

      CREATE INDEX IF NOT EXISTS idx_snapshot_audit_device_id_desc
        ON snapshot_audit (device_id, id DESC);

      -- v2 authorization is deliberately separate from the legacy Socket.IO
      -- device token tables. This lets the test service evolve without
      -- invalidating existing clients or their session tokens.
      CREATE TABLE IF NOT EXISTS licenses (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        key_hash TEXT NOT NULL UNIQUE,
        label TEXT NOT NULL DEFAULT '',
        expires_at INTEGER,
        disabled_at INTEGER,
        bound_device_id TEXT,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL
      );

      CREATE INDEX IF NOT EXISTS idx_licenses_bound_device
        ON licenses (bound_device_id);
      CREATE INDEX IF NOT EXISTS idx_licenses_active
        ON licenses (disabled_at, expires_at);

      CREATE TABLE IF NOT EXISTS auth_sessions (
        token_hash TEXT PRIMARY KEY,
        license_id INTEGER NOT NULL REFERENCES licenses(id) ON DELETE CASCADE,
        device_id TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        last_seen_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL
      );

      CREATE INDEX IF NOT EXISTS idx_auth_sessions_device
        ON auth_sessions (device_id);
      CREATE INDEX IF NOT EXISTS idx_auth_sessions_expiry
        ON auth_sessions (expires_at);

      CREATE TABLE IF NOT EXISTS player_entities (
        entity_id TEXT PRIMARY KEY,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS player_entity_names (
        entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        name_norm TEXT NOT NULL UNIQUE,
        display_name TEXT NOT NULL,
        PRIMARY KEY (entity_id, name_norm)
      );

      CREATE INDEX IF NOT EXISTS idx_player_names_entity
        ON player_entity_names (entity_id);

      CREATE TABLE IF NOT EXISTS player_entity_identifiers (
        entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        kind TEXT NOT NULL CHECK (kind IN ('game', 'adventure')),
        identifier_norm TEXT NOT NULL,
        display_value TEXT NOT NULL,
        PRIMARY KEY (kind, identifier_norm)
      );

      CREATE INDEX IF NOT EXISTS idx_player_identifiers_entity
        ON player_entity_identifiers (entity_id, kind);

      CREATE TABLE IF NOT EXISTS player_library_meta (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        revision INTEGER NOT NULL
      );
      INSERT OR IGNORE INTO player_library_meta (id, revision) VALUES (1, 0);

      CREATE TABLE IF NOT EXISTS player_library_submissions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id TEXT NOT NULL,
        payload_json TEXT NOT NULL,
        status TEXT NOT NULL CHECK (status IN ('pending', 'approved', 'rejected')),
        created_at INTEGER NOT NULL,
        reviewed_at INTEGER,
        review_reason TEXT
      );

      CREATE INDEX IF NOT EXISTS idx_player_submissions_status
        ON player_library_submissions (status, created_at DESC);

      CREATE TABLE IF NOT EXISTS broadcaster_policies (
        device_id TEXT PRIMARY KEY,
        ocr_disabled_until INTEGER,
        updated_at INTEGER NOT NULL
      );
    `);

    const roomColumns = db.pragma('table_info(rooms)') as Array<{ name: string }>;
    if (!roomColumns.some((column) => column.name === 'revision')) {
      db.exec('ALTER TABLE rooms ADD COLUMN revision INTEGER NOT NULL DEFAULT 0');
    }
    if (!roomColumns.some((column) => column.name === 'presence_revision')) {
      db.exec(
        'ALTER TABLE rooms ADD COLUMN presence_revision INTEGER NOT NULL DEFAULT 0',
      );
    }

    const insertRoom = db.prepare(
      'INSERT OR IGNORE INTO rooms (id, display_name) VALUES (?, ?)',
    );
    for (const room of ROOM_SEEDS) {
      insertRoom.run(room.id, room.displayName);
    }
    pruneSnapshotAudit(db);
  });

  initialize();
}

export function openDatabase(path: string): Database.Database {
  const db = new Database(path);

  try {
    db.pragma('journal_mode = WAL');
    db.pragma('foreign_keys = ON');
    db.pragma('busy_timeout = 5000');
    initializeSchema(db);
    return db;
  } catch (error) {
    db.close();
    throw error;
  }
}
