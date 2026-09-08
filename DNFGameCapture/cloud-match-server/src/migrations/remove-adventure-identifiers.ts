import type Database from 'better-sqlite3';
import { matchSnapshotSchema } from '../schemas.js';
import { snapshotContentHash } from '../snapshots.js';

const VERSION = '2026-09-08-remove-adventure-identifiers';
type JsonObject = Record<string, unknown>;
const object = (value: unknown): value is JsonObject => value !== null && typeof value === 'object' && !Array.isArray(value);

// Removal-only compatibility: never promote retired evidence into game IDs or infer players from it.
function cleanEntity(value: unknown): void {
  if (object(value)) delete value.adventureGroupIds;
}
function cleanEntities(value: unknown): void {
  if (Array.isArray(value)) value.forEach(cleanEntity);
}
function cleanPayload(value: unknown): void {
  if (object(value)) cleanEntities(value.entities);
}
function cleanEvidenceMap(value: unknown): void {
  if (object(value)) Object.values(value).forEach(cleanEntities);
}
function cleanSnapshot(value: unknown): void {
  if (object(value)) {
    cleanEntities(value.redPlayers);
    cleanEntities(value.bluePlayers);
  }
}

/** Runs inside schema initialization's IMMEDIATE transaction; any failure rolls back DDL, data and marker. */
export function removeAdventureIdentifiers(db: Database.Database, removedLegacyIdentifiers = false): void {
  if (!db.inTransaction) throw new Error('removal_migration_requires_transaction');
  db.exec('CREATE TABLE IF NOT EXISTS server_schema_migrations(version TEXT PRIMARY KEY)');
  if (db.prepare('SELECT 1 FROM server_schema_migrations WHERE version=?').get(VERSION)) return;
  let libraryChanged = removedLegacyIdentifiers || !!db.prepare("SELECT 1 FROM player_identifiers WHERE kind<>'game' LIMIT 1").get();

  // Rebuild parent and child tables together to enforce game-only storage while retaining numeric IDs and spellings.
  db.exec(`
    CREATE TABLE player_identifiers_game_only (
      identifier_id INTEGER PRIMARY KEY,kind TEXT NOT NULL CHECK(kind='game'),identifier_norm TEXT NOT NULL,UNIQUE(kind,identifier_norm));
    INSERT INTO player_identifiers_game_only SELECT * FROM player_identifiers WHERE kind='game';
    CREATE TABLE player_identifier_spellings_game_only (
      spelling_id INTEGER PRIMARY KEY,identifier_id INTEGER NOT NULL REFERENCES player_identifiers_game_only(identifier_id) ON DELETE CASCADE,
      display_value TEXT NOT NULL,UNIQUE(identifier_id,display_value),UNIQUE(identifier_id,spelling_id));
    INSERT INTO player_identifier_spellings_game_only
      SELECT spelling.* FROM player_identifier_spellings AS spelling JOIN player_identifiers_game_only USING(identifier_id);
    CREATE TABLE player_entity_identifiers_game_only (
      entity_id TEXT NOT NULL REFERENCES player_entities(entity_id) ON DELETE CASCADE,
      identifier_id INTEGER NOT NULL REFERENCES player_identifiers_game_only(identifier_id) ON DELETE RESTRICT,
      spelling_id INTEGER NOT NULL,PRIMARY KEY(entity_id,identifier_id),FOREIGN KEY(identifier_id,spelling_id)
        REFERENCES player_identifier_spellings_game_only(identifier_id,spelling_id) ON DELETE RESTRICT);
    INSERT INTO player_entity_identifiers_game_only
      SELECT link.* FROM player_entity_identifiers AS link JOIN player_identifiers_game_only USING(identifier_id);
    DROP TABLE player_entity_identifiers;
    DROP TABLE player_identifier_spellings;
    DROP TABLE player_identifiers;
    ALTER TABLE player_identifiers_game_only RENAME TO player_identifiers;
    ALTER TABLE player_identifier_spellings_game_only RENAME TO player_identifier_spellings;
    ALTER TABLE player_entity_identifiers_game_only RENAME TO player_entity_identifiers;
    CREATE INDEX idx_player_entity_identifiers_identifier ON player_entity_identifiers(identifier_id,entity_id);
  `);

  const targets: [string, string, string, (value: unknown) => void][] = [
    ['player_library_submissions', 'id', 'payload_json', cleanPayload],
    ['player_library_submission_originals', 'submission_id', 'payload_json', cleanPayload],
    ['player_entity_auto_evidence', 'entity_id', 'evidence_json', cleanEntities],
    ['player_library_merge_audit', 'id', 'before_json', cleanEntities],
    ['player_library_merge_audit', 'id', 'after_json', cleanEntity],
    ['player_library_merge_audit', 'id', 'before_evidence_json', cleanEvidenceMap],
    ['player_library_merge_audit', 'id', 'after_evidence_json', cleanEvidenceMap],
    ['snapshots', 'device_id', 'payload_json', cleanSnapshot],
  ];
  for (const [table, id, column, clean] of targets) {
    const rows = db.prepare(`SELECT ${id} AS id,${column} AS json FROM ${table}`).all() as { id: string | number; json: string }[];
    const update = db.prepare(`UPDATE ${table} SET ${column}=? WHERE ${id}=?`);
    for (const row of rows) {
      let value: unknown;
      try { value = JSON.parse(row.json); } catch { throw new Error(`removal_migration_invalid_json:${table}:${row.id}`); }
      const before = JSON.stringify(value);
      clean(value);
      const after = JSON.stringify(value);
      if (before === after) continue;
      update.run(after, row.id);
      if (table === 'snapshots') {
        const parsed = matchSnapshotSchema.safeParse(value);
        if (!parsed.success) throw new Error(`removal_migration_invalid_snapshot:${row.id}`);
        db.prepare('UPDATE snapshots SET content_hash=? WHERE device_id=?').run(snapshotContentHash(parsed.data), row.id);
      } else libraryChanged = true;
    }
  }
  if ((db.pragma('foreign_key_check') as unknown[]).length) throw new Error('removal_migration_invalid_foreign_keys');
  if (libraryChanged) db.exec('UPDATE player_library_meta SET revision=revision+1 WHERE id=1');
  db.prepare('INSERT INTO server_schema_migrations(version) VALUES(?)').run(VERSION);
}
