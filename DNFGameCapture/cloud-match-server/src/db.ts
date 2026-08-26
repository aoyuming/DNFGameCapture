import Database from 'better-sqlite3';

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
        display_name TEXT NOT NULL UNIQUE
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
    `);

    const insertRoom = db.prepare(
      'INSERT OR IGNORE INTO rooms (id, display_name) VALUES (?, ?)',
    );
    for (const room of ROOM_SEEDS) {
      insertRoom.run(room.id, room.displayName);
    }
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
