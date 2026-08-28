import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, describe, expect, test } from 'vitest';

import { openDatabase } from '../src/db.js';
import {
  initializeSyncRelationSchema,
  isReverseRealtimeSyncBlocked,
  listAllRealtimeSync,
  listAllSyncHistory,
  listRealtimeSyncForDevice,
  listSyncHistory,
  pruneStaleRealtimeSync,
  recordSuccessfulSync,
  startRealtimeSync,
  stopRealtimeSync,
} from '../src/sync-relations.js';

const databases: Array<{ path: string; close(): void }> = [];

function createDatabase() {
  const directory = mkdtempSync(join(tmpdir(), 'dnf-sync-relations-'));
  const path = join(directory, 'sync.sqlite');
  const db = openDatabase(path);
  initializeSyncRelationSchema(db);
  databases.push({ path, close: () => db.close() });
  return { db, directory };
}

afterEach(() => {
  for (const database of databases.splice(0)) database.close();
});

describe('sync relation persistence', () => {
  test('records successful incoming and outgoing syncs for 24 hours', () => {
    const { db } = createDatabase();
    const createdAt = 1_700_000_000;

    const record = recordSuccessfulSync(db, {
      sourceDeviceId: 'viewer-a',
      sourceName: '主播甲',
      targetDeviceId: 'target-b',
      targetName: '主播乙',
      syncType: 'once',
      snapshotRevision: 7,
      merged: true,
      createdAt,
    });

    expect(record.expiresAt).toBe(createdAt + 24 * 60 * 60);
    expect(listSyncHistory(db, 'viewer-a', createdAt)).toMatchObject({
      incoming: [],
      outgoing: [record],
    });
    expect(listSyncHistory(db, 'target-b', createdAt)).toMatchObject({
      incoming: [record],
      outgoing: [],
    });
  });

  test('removes expired history but keeps the newest valid record', () => {
    const { db } = createDatabase();
    recordSuccessfulSync(db, {
      sourceDeviceId: 'viewer-a',
      sourceName: '主播甲',
      targetDeviceId: 'target-b',
      targetName: '主播乙',
      syncType: 'once',
      snapshotRevision: 1,
      merged: false,
      createdAt: 1_600_000_000,
    });
    const valid = recordSuccessfulSync(db, {
      sourceDeviceId: 'viewer-a',
      sourceName: '主播甲',
      targetDeviceId: 'target-c',
      targetName: '主播丙',
      syncType: 'realtime',
      snapshotRevision: 2,
      merged: true,
      createdAt: 1_700_000_000,
    });

    expect(listSyncHistory(db, 'viewer-a', 1_700_000_000)).toEqual({
      incoming: [],
      outgoing: [valid],
    });
  });

  test('lists all unexpired sync history for broadcaster relationship summaries', () => {
    const { db } = createDatabase();
    const createdAt = 1_700_000_000;
    const first = recordSuccessfulSync(db, {
      sourceDeviceId: 'source-a',
      sourceName: '主播甲',
      targetDeviceId: 'viewer-b',
      targetName: '主播乙',
      syncType: 'once',
      snapshotRevision: 3,
      merged: true,
      createdAt,
    });
    const newest = recordSuccessfulSync(db, {
      sourceDeviceId: 'source-c',
      sourceName: '主播丙',
      targetDeviceId: 'viewer-d',
      targetName: '主播丁',
      syncType: 'once',
      snapshotRevision: 8,
      merged: true,
      createdAt: createdAt + 1,
    });

    expect(listAllSyncHistory(db, createdAt + 2)).toEqual([newest, first]);
  });

  test('keeps one current target per viewer and exposes both directions', () => {
    const { db } = createDatabase();
    startRealtimeSync(db, {
      viewerDeviceId: 'viewer-a',
      viewerName: '主播甲',
      targetDeviceId: 'target-b',
      targetName: '主播乙',
      startedAt: 1_700_000_000,
    });
    startRealtimeSync(db, {
      viewerDeviceId: 'viewer-a',
      viewerName: '主播甲',
      targetDeviceId: 'target-c',
      targetName: '主播丙',
      startedAt: 1_700_000_001,
    });

    expect(listRealtimeSyncForDevice(db, 'target-c', 1_700_000_002).incoming).toHaveLength(1);
    expect(listRealtimeSyncForDevice(db, 'viewer-a', 1_700_000_002).outgoing[0]).toMatchObject({
      targetDeviceId: 'target-c',
    });
    expect(listAllRealtimeSync(db, 1_700_000_002)).toHaveLength(1);

    expect(stopRealtimeSync(db, 'viewer-a')).toBe(true);
    expect(listAllRealtimeSync(db, 1_700_000_003)).toEqual([]);
  });

  test('blocks only the reverse direction of an active realtime relation', () => {
    const { db } = createDatabase();
    startRealtimeSync(db, {
      viewerDeviceId: 'viewer-a',
      viewerName: '主播甲',
      targetDeviceId: 'target-b',
      targetName: '主播乙',
      startedAt: 1_700_000_000,
    });

    expect(
      isReverseRealtimeSyncBlocked(
        db,
        'target-b',
        'viewer-a',
        1_700_000_001,
      ),
    ).toBe(true);
    expect(
      isReverseRealtimeSyncBlocked(
        db,
        'viewer-a',
        'target-b',
        1_700_000_001,
      ),
    ).toBe(false);
    expect(
      isReverseRealtimeSyncBlocked(
        db,
        'unrelated-c',
        'viewer-a',
        1_700_000_001,
      ),
    ).toBe(false);

    stopRealtimeSync(db, 'viewer-a');
    expect(
      isReverseRealtimeSyncBlocked(
        db,
        'target-b',
        'viewer-a',
        1_700_000_002,
      ),
    ).toBe(false);
  });

  test('reports stale realtime relations removed by heartbeat cleanup', () => {
    const { db } = createDatabase();
    startRealtimeSync(db, {
      viewerDeviceId: 'viewer-a',
      viewerName: '主播甲',
      targetDeviceId: 'target-b',
      targetName: '主播乙',
      startedAt: 1_700_000_000,
    });

    expect(pruneStaleRealtimeSync(db, 1_700_000_006)).toBe(1);
    expect(pruneStaleRealtimeSync(db, 1_700_000_007)).toBe(0);
  });
});
