import Database from 'better-sqlite3';

import { pruneSnapshotAudit } from './snapshots.js';
import { removeAdventureIdentifiers } from './migrations/remove-adventure-identifiers.js';
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

      CREATE TABLE IF NOT EXISTS player_entity_auto_evidence (
        entity_id TEXT PRIMARY KEY REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        evidence_json TEXT NOT NULL
      );

      CREATE TABLE IF NOT EXISTS player_entity_names (
        entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        name_norm TEXT NOT NULL UNIQUE,
        display_name TEXT NOT NULL,
        PRIMARY KEY (entity_id, name_norm)
      );

      CREATE INDEX IF NOT EXISTS idx_player_names_entity
        ON player_entity_names (entity_id);

      CREATE TABLE IF NOT EXISTS player_identifiers (
        identifier_id INTEGER PRIMARY KEY,
        kind TEXT NOT NULL CHECK (kind = 'game'),
        identifier_norm TEXT NOT NULL,
        UNIQUE (kind, identifier_norm)
      );

      CREATE TABLE IF NOT EXISTS player_identifier_spellings (
        spelling_id INTEGER PRIMARY KEY,
        identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE CASCADE,
        display_value TEXT NOT NULL,
        UNIQUE (identifier_id, display_value),
        UNIQUE (identifier_id, spelling_id)
      );

      CREATE TABLE IF NOT EXISTS player_entity_identifiers (
        entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
        identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE RESTRICT,
        spelling_id INTEGER NOT NULL,
        PRIMARY KEY (entity_id, identifier_id),
        FOREIGN KEY (identifier_id, spelling_id)
          REFERENCES player_identifier_spellings(identifier_id, spelling_id) ON DELETE RESTRICT
      );

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

      CREATE TABLE IF NOT EXISTS player_library_submission_originals (
        submission_id INTEGER PRIMARY KEY REFERENCES player_library_submissions(id) ON DELETE CASCADE,
        payload_json TEXT NOT NULL
      );

      CREATE TABLE IF NOT EXISTS player_entity_redirects (
        from_entity_id TEXT PRIMARY KEY,
        to_entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE RESTRICT,
        CHECK (from_entity_id <> to_entity_id)
      );
      CREATE INDEX IF NOT EXISTS idx_player_redirect_target ON player_entity_redirects(to_entity_id);
      CREATE TRIGGER IF NOT EXISTS prevent_retired_player_insert BEFORE INSERT ON player_entities
        WHEN EXISTS (SELECT 1 FROM player_entity_redirects WHERE from_entity_id=NEW.entity_id)
        BEGIN SELECT RAISE(ABORT, 'retired_entity_id'); END;
      CREATE TRIGGER IF NOT EXISTS prevent_retired_player_update BEFORE UPDATE OF entity_id ON player_entities
        WHEN EXISTS (SELECT 1 FROM player_entity_redirects WHERE from_entity_id=NEW.entity_id)
        BEGIN SELECT RAISE(ABORT, 'retired_entity_id'); END;
      CREATE TRIGGER IF NOT EXISTS prevent_live_redirect_source BEFORE INSERT ON player_entity_redirects
        WHEN EXISTS (SELECT 1 FROM player_entities WHERE entity_id=NEW.from_entity_id)
        BEGIN SELECT RAISE(ABORT, 'live_redirect_source'); END;
      CREATE TRIGGER IF NOT EXISTS prevent_live_redirect_update BEFORE UPDATE ON player_entity_redirects
        WHEN EXISTS (SELECT 1 FROM player_entities WHERE entity_id=NEW.from_entity_id)
        BEGIN SELECT RAISE(ABORT, 'live_redirect_source'); END;
      CREATE TABLE IF NOT EXISTS player_library_merge_audit (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        target_entity_id TEXT NOT NULL,
        revision INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        before_json TEXT NOT NULL,
        before_redirects_json TEXT NOT NULL,
        after_json TEXT NOT NULL,
        redirects_json TEXT NOT NULL,
        merge_kind TEXT NOT NULL DEFAULT 'legacy' CHECK (merge_kind IN ('legacy', 'automatic', 'manual')),
        before_evidence_json TEXT NOT NULL DEFAULT '{}',
        after_evidence_json TEXT NOT NULL DEFAULT '{}'
      );

      CREATE TABLE IF NOT EXISTS player_library_conflict_resolution_audit (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        revision INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        groups_json TEXT NOT NULL,
        before_entities_json TEXT NOT NULL,
        after_entities_json TEXT NOT NULL,
        before_redirects_json TEXT NOT NULL,
        after_redirects_json TEXT NOT NULL,
        submissions_json TEXT NOT NULL
      );

      CREATE TABLE IF NOT EXISTS broadcaster_policies (
        device_id TEXT PRIMARY KEY,
        ocr_disabled_until INTEGER,
        updated_at INTEGER NOT NULL
      );
    `);

    const identifierColumns = db.pragma('table_info(player_entity_identifiers)') as Array<{ name: string }>;
    const removedLegacyIdentifiers = identifierColumns.some(column => column.name === 'kind') &&
      !!db.prepare("SELECT 1 FROM player_entity_identifiers WHERE kind<>'game' LIMIT 1").get();
    if (identifierColumns.some(column => column.name === 'display_value')) {
      const legacy = identifierColumns.some(column => column.name === 'identifier_norm');
      if (legacy) db.exec(`INSERT INTO player_identifiers(kind,identifier_norm)
        SELECT DISTINCT kind,identifier_norm FROM player_entity_identifiers
        WHERE kind='game' ON CONFLICT(kind,identifier_norm) DO NOTHING`);
      const identifierId = legacy ? 'identifier.identifier_id' : 'link.identifier_id';
      const identifierJoin = legacy ? `JOIN player_identifiers AS identifier
        ON identifier.kind=link.kind AND identifier.identifier_norm=link.identifier_norm` : '';
      // Both the global-owner schema and the interim per-link spelling schema migrate atomically.
      db.exec(`
        INSERT INTO player_identifier_spellings(identifier_id,display_value)
          SELECT DISTINCT ${identifierId},link.display_value FROM player_entity_identifiers AS link ${identifierJoin}
          WHERE true ON CONFLICT(identifier_id,display_value) DO NOTHING;
        CREATE TABLE player_entity_identifier_links (
          entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
          identifier_id INTEGER NOT NULL REFERENCES player_identifiers(identifier_id) ON DELETE RESTRICT,
          spelling_id INTEGER NOT NULL,
          PRIMARY KEY (entity_id, identifier_id),
          FOREIGN KEY (identifier_id, spelling_id)
            REFERENCES player_identifier_spellings(identifier_id, spelling_id) ON DELETE RESTRICT
        );
        INSERT INTO player_entity_identifier_links(entity_id,identifier_id,spelling_id)
          SELECT link.entity_id,${identifierId},spelling.spelling_id
          FROM player_entity_identifiers AS link ${identifierJoin}
          JOIN player_identifier_spellings AS spelling
            ON spelling.identifier_id=${identifierId} AND spelling.display_value=link.display_value;
        DROP TABLE player_entity_identifiers;
        ALTER TABLE player_entity_identifier_links RENAME TO player_entity_identifiers;
      `);
    }
    db.exec(`CREATE INDEX IF NOT EXISTS idx_player_entity_identifiers_identifier
      ON player_entity_identifiers(identifier_id,entity_id)`);

    const auditColumns = db.pragma('table_info(player_library_merge_audit)') as Array<{ name: string }>;
    for (const [name, definition] of [
      ['merge_kind', "TEXT NOT NULL DEFAULT 'legacy' CHECK (merge_kind IN ('legacy', 'automatic', 'manual'))"],
      ['before_evidence_json', "TEXT NOT NULL DEFAULT '{}'"],
      ['after_evidence_json', "TEXT NOT NULL DEFAULT '{}'"],
    ]) {
      if (!auditColumns.some(column => column.name === name)) db.exec(`ALTER TABLE player_library_merge_audit ADD COLUMN ${name} ${definition}`);
    }

    const licenseColumns = db.pragma('table_info(licenses)') as Array<{ name: string }>;
    for (const [name, definition] of [
      ['activation_mode', "TEXT NOT NULL DEFAULT 'fixed' CHECK(activation_mode IN ('fixed','first_use'))"],
      ['duration_seconds', 'INTEGER CHECK(duration_seconds IS NULL OR duration_seconds>0)'],
      ['activated_at', 'INTEGER'], ['key_ciphertext', 'TEXT'], ['revision', 'INTEGER NOT NULL DEFAULT 0'],
    ]) {
      if (!licenseColumns.some(column => column.name === name)) db.exec(`ALTER TABLE licenses ADD COLUMN ${name} ${definition}`);
    }
    db.exec(`
      CREATE TABLE IF NOT EXISTS license_audit (
        id INTEGER PRIMARY KEY AUTOINCREMENT, license_id INTEGER NOT NULL REFERENCES licenses(id),
        action TEXT NOT NULL, created_at INTEGER NOT NULL, details_json TEXT NOT NULL);
      CREATE INDEX IF NOT EXISTS idx_license_audit ON license_audit(license_id,id);
      CREATE TABLE IF NOT EXISTS license_admin_operations (
        request_id TEXT PRIMARY KEY, fingerprint TEXT NOT NULL, result_json TEXT NOT NULL, created_at INTEGER NOT NULL);
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
    removeAdventureIdentifiers(db, removedLegacyIdentifiers);
    pruneSnapshotAudit(db);
  });

  initialize.immediate();
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
