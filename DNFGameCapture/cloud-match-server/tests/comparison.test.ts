import { describe, expect, test } from 'vitest';

import {
  compareRoomSnapshots,
  type ComparisonInputRow,
  type SnapshotDifference,
} from '../src/comparison.js';
import type { MatchSnapshot, Player } from '../src/schemas.js';

function player(mainName: string, seed = 0, aliases: string[] = []): Player {
  return {
    mainName,
    aliases,
    kills: seed,
    deaths: seed + 1,
    ak: seed + 2,
    streak: seed + 3,
  };
}

function snapshot(overrides: Partial<MatchSnapshot> = {}): MatchSnapshot {
  return {
    schemaVersion: 1,
    clientRevision: 1,
    clientTime: 100,
    changeSource: 'ocr',
    redScore: 3,
    blueScore: 2,
    redPlayers: [
      player('Red One', 0, ['R1']),
      player('Red Two', 4, ['R2']),
      player('Red Three', 8, ['R3']),
      player('Red Four', 12, ['R4']),
    ],
    bluePlayers: [
      player('Blue One', 16, ['B1']),
      player('Blue Two', 20, ['B2']),
      player('Blue Three', 24, ['B3']),
      player('Blue Four', 28, ['B4']),
    ],
    redPickFirst: true,
    teamsFlipped: false,
    outputSeatLabel: true,
    lastKillTeam: 'red',
    ...overrides,
  };
}

function replaceFirstPlayer(
  players: [Player, Player, Player, Player],
  first: Player,
): [Player, Player, Player, Player] {
  return [first, players[1], players[2], players[3]];
}

function swapSnapshot(source: MatchSnapshot): MatchSnapshot {
  return {
    ...source,
    clientRevision: source.clientRevision + 1,
    redScore: source.blueScore,
    blueScore: source.redScore,
    redPlayers: source.bluePlayers,
    bluePlayers: source.redPlayers,
    redPickFirst: !source.redPickFirst,
    lastKillTeam:
      source.lastKillTeam === 'red'
        ? 'blue'
        : source.lastKillTeam === 'blue'
          ? 'red'
          : '',
  };
}

function row(
  deviceId: string,
  match: MatchSnapshot,
  receivedAt = 1_000,
  online?: boolean,
): ComparisonInputRow {
  return {
    deviceId,
    broadcasterName: `Broadcaster ${deviceId}`,
    deviceSuffix: deviceId.slice(-4),
    snapshot: match,
    receivedAt,
    ...(online === undefined ? {} : { online }),
  };
}

function member(
  result: ReturnType<typeof compareRoomSnapshots>,
  deviceId: string,
) {
  const found = result.members.find((item) => item.deviceId === deviceId);
  expect(found).toBeDefined();
  return found as NonNullable<typeof found>;
}

describe('room snapshot comparison', () => {
  test('ignores local presentation settings when comparing broadcasters', () => {
    const local = snapshot();
    const remote = snapshot({ teamsFlipped: true, outputSeatLabel: false });

    const result = compareRoomSnapshots(
      [row('device-local', local), row('device-remote', remote)],
      1_000,
    );

    expect(member(result, 'device-remote')).toMatchObject({
      similarity: 100,
      differences: [],
    });
  });

  test('normalizes a complete red-blue swap to 100 percent similarity', () => {
    const a = snapshot();
    const b = swapSnapshot(a);

    const result = compareRoomSnapshots(
      [row('device-A', a, 1_000, true), row('device-B', b, 1_000, false)],
      1_000,
    );

    expect(result.consensusDeviceId).toBe('device-A');
    expect(member(result, 'device-A')).toMatchObject({
      swapped: false,
      identityMatchPercent: 100,
      similarity: 100,
      differences: [],
      online: true,
    });
    expect(member(result, 'device-B')).toMatchObject({
      swapped: true,
      identityMatchPercent: 100,
      similarity: 100,
      differences: [],
      online: false,
    });
    expect(result.groups).toEqual([
      { id: 'group-1', memberDeviceIds: ['device-A', 'device-B'] },
    ]);
  });

  test('reports an exact structured stat difference against the consensus', () => {
    const a = snapshot();
    const b = swapSnapshot(a);
    const c = snapshot({
      redPlayers: replaceFirstPlayer(
        a.redPlayers,
        { ...a.redPlayers[0], kills: a.redPlayers[0].kills + 1 },
      ),
    });

    const result = compareRoomSnapshots(
      [row('device-A', a), row('device-B', b), row('device-C', c)],
      1_000,
      { includePairwise: true },
    );
    const expectedDifference: SnapshotDifference = {
      kind: 'stat',
      team: 'red',
      playerName: 'Red One',
      field: 'kills',
      referenceValue: 0,
      memberValue: 1,
      delta: 1,
    };

    expect(result.consensusDeviceId).toBe('device-A');
    expect(member(result, 'device-C').differences).toEqual([expectedDifference]);
    expect(
      result.pairwise?.find(
        (pair) => pair.leftDeviceId === 'device-A' && pair.rightDeviceId === 'device-C',
      ),
    ).toMatchObject({
      similarity: 99,
      identityMatchPercent: 100,
      differences: [expectedDifference],
    });
  });

  test('matches aliases after schema normalization even when main names differ', () => {
    const a = snapshot({
      redPlayers: replaceFirstPlayer(
        snapshot().redPlayers,
        player('  e\u0301clair ', 0, ['Ace']),
      ),
    });
    const b = snapshot({
      redPlayers: replaceFirstPlayer(
        snapshot().redPlayers,
        player('Ace', 0, ['\u00e9clair']),
      ),
    });

    const result = compareRoomSnapshots([row('device-A', a), row('device-B', b)], 1_000);

    expect(member(result, 'device-B')).toMatchObject({
      identityMatchPercent: 100,
      similarity: 100,
      differences: [],
    });
  });

  test('folds only ASCII identity case without changing displayed names', () => {
    const base = snapshot();
    const a = snapshot({
      redPlayers: replaceFirstPlayer(
        base.redPlayers,
        player('Alice', 0, ['ALICE ALT']),
      ),
    });
    const b = snapshot({
      redPlayers: replaceFirstPlayer(
        base.redPlayers,
        { ...player('alice', 0, ['alice alt']), kills: 1 },
      ),
    });

    const result = compareRoomSnapshots([row('device-A', a), row('device-B', b)], 1_000);

    expect(member(result, 'device-B')).toMatchObject({
      identityMatchPercent: 100,
      similarity: 99,
      differences: [
        {
          kind: 'stat',
          team: 'red',
          playerName: 'Alice',
          field: 'kills',
          referenceValue: 0,
          memberValue: 1,
          delta: 1,
        },
      ],
    });
    expect(a.redPlayers[0]).toMatchObject({ mainName: 'Alice', aliases: ['ALICE ALT'] });
    expect(b.redPlayers[0]).toMatchObject({ mainName: 'alice', aliases: ['alice alt'] });
  });

  test('does not case-fold non-ASCII identity characters', () => {
    const base = snapshot();
    const a = snapshot({
      redPlayers: replaceFirstPlayer(
        base.redPlayers,
        player('\u00c9lodie', 0, ['\u00c9 Alias']),
      ),
    });
    const b = snapshot({
      redPlayers: replaceFirstPlayer(
        base.redPlayers,
        player('\u00e9lodie', 0, ['\u00e9 Alias']),
      ),
    });

    const result = compareRoomSnapshots([row('device-A', a), row('device-B', b)], 1_000);

    expect(member(result, 'device-B').identityMatchPercent).toBe(88);
  });

  test('uses deterministic maximum one-to-one matching for overlapping aliases', () => {
    const base = snapshot();
    const a = snapshot({
      redPlayers: [
        player('Alpha', 0, ['Shared A']),
        player('Beta', 4, ['Shared B']),
        base.redPlayers[2],
        base.redPlayers[3],
      ],
    });
    const b = snapshot({
      redPlayers: [
        player('Bridge', 4, ['Shared A', 'Shared B']),
        player('Anchor', 0, ['Shared A']),
        base.redPlayers[2],
        base.redPlayers[3],
      ],
    });

    const result = compareRoomSnapshots([row('device-A', a), row('device-B', b)], 1_000);

    expect(member(result, 'device-B')).toMatchObject({
      identityMatchPercent: 100,
      similarity: 100,
      differences: [],
    });
  });

  test('applies the four component weights exactly', () => {
    const base = snapshot();
    const unrelatedPlayers = [
      player('Other 1', 100),
      player('Other 2', 104),
      player('Other 3', 108),
      player('Other 4', 112),
    ] as [Player, Player, Player, Player];
    const unrelatedBluePlayers = [
      player('Other 5', 116),
      player('Other 6', 120),
      player('Other 7', 124),
      player('Other 8', 128),
    ] as [Player, Player, Player, Player];

    const cases: Array<[string, MatchSnapshot, number]> = [
      [
        'roster-and-stats-mismatch',
        snapshot({ redPlayers: unrelatedPlayers, bluePlayers: unrelatedBluePlayers }),
        25,
      ],
      ['both-scores-mismatch', snapshot({ redScore: 8, blueScore: 9 }), 85],
      [
        'all-stats-mismatch',
        snapshot({
          redPlayers: base.redPlayers.map((item) => ({
            ...item,
            kills: item.kills + 100,
            deaths: item.deaths + 100,
            ak: item.ak + 100,
            streak: item.streak + 100,
          })) as [Player, Player, Player, Player],
          bluePlayers: base.bluePlayers.map((item) => ({
            ...item,
            kills: item.kills + 100,
            deaths: item.deaths + 100,
            ak: item.ak + 100,
            streak: item.streak + 100,
          })) as [Player, Player, Player, Player],
        }),
        65,
      ],
      [
        'all-state-mismatch',
        snapshot({
          redPickFirst: false,
          teamsFlipped: true,
          outputSeatLabel: false,
          lastKillTeam: 'blue',
        }),
        90,
      ],
    ];

    for (const [deviceId, changed, expected] of cases) {
      const result = compareRoomSnapshots(
        [row('device-reference', base, 1_001), row(deviceId, changed, 1_000)],
        1_001,
      );
      expect(member(result, deviceId).similarity).toBe(expected);
    }
  });

  test('uses strict older-than boundaries for stale and consensus exclusion', () => {
    const rows = [
      row('age-30', snapshot(), 970, true),
      row('age-31', snapshot(), 969, false),
      row('age-120', snapshot(), 880),
      row('age-121', snapshot(), 879),
    ];

    const result = compareRoomSnapshots(rows, 1_000);

    expect(member(result, 'age-30')).toMatchObject({
      stale: false,
      excludedFromConsensus: false,
      online: true,
    });
    expect(member(result, 'age-31')).toMatchObject({
      stale: true,
      excludedFromConsensus: false,
      online: false,
    });
    expect(member(result, 'age-120')).toMatchObject({
      stale: true,
      excludedFromConsensus: false,
    });
    expect(member(result, 'age-121')).toMatchObject({
      stale: true,
      excludedFromConsensus: true,
    });
    expect(
      result.groups.flatMap((group) => group.memberDeviceIds),
    ).not.toContain('age-121');
  });

  test('deduplicates cloud-sync votes through multi-hop roots and protects cycles', () => {
    const base = snapshot();
    const divergent = snapshot({ redScore: 9, blueScore: 9 });
    const copiedFromA = snapshot({
      clientRevision: 2,
      changeSource: 'cloud_sync',
      syncedFrom: { deviceId: 'device-A', revision: 1 },
    });
    const copiedFromD = snapshot({
      clientRevision: 3,
      changeSource: 'cloud_sync',
      syncedFrom: { deviceId: 'device-D', revision: 2 },
    });
    const cycleF = snapshot({
      changeSource: 'cloud_sync',
      syncedFrom: { deviceId: 'device-G', revision: 1 },
    });
    const cycleG = snapshot({
      changeSource: 'cloud_sync',
      syncedFrom: { deviceId: 'device-F', revision: 1 },
    });
    const missing = snapshot({
      changeSource: 'cloud_sync',
      syncedFrom: { deviceId: 'device-missing', revision: 1 },
    });

    const voteResult = compareRoomSnapshots(
      [
        row('device-A', base, 1_000),
        row('device-D', copiedFromA, 1_001),
        row('device-E', copiedFromD, 1_002),
        row('device-B', divergent, 1_010),
      ],
      1_010,
    );
    expect(member(voteResult, 'device-D').sourceRoot).toBe('device-A');
    expect(member(voteResult, 'device-E').sourceRoot).toBe('device-A');
    expect(voteResult.consensusDeviceId).toBe('device-B');

    const chainResult = compareRoomSnapshots(
      [
        row('device-A', base, 1_000),
        row('device-D', copiedFromA, 1_001),
        row('device-E', copiedFromD, 1_002),
        row('device-F', cycleF, 1_000),
        row('device-G', cycleG, 1_000),
        row('device-H', missing, 1_000),
      ],
      1_010,
    );

    expect(member(chainResult, 'device-D').sourceRoot).toBe('device-A');
    expect(member(chainResult, 'device-E').sourceRoot).toBe('device-A');
    expect(member(chainResult, 'device-F').sourceRoot).toBe('device-F');
    expect(member(chainResult, 'device-G').sourceRoot).toBe('device-F');
    expect(member(chainResult, 'device-H').sourceRoot).toBe('device-missing');
  });

  test('selects the medoid deterministically by newest time and then device ID', () => {
    const base = snapshot();
    const newest = compareRoomSnapshots(
      [row('device-A', base, 1_000), row('device-B', base, 1_001)],
      1_001,
    );
    expect(newest.consensusDeviceId).toBe('device-B');

    const lexical = compareRoomSnapshots(
      [row('device-Z', base, 1_000), row('device-A', base, 1_000)],
      1_000,
    );
    expect(lexical.consensusDeviceId).toBe('device-A');
  });

  test('includes similarity 90 in groups, excludes 89, and omits expired rows', () => {
    const base = snapshot();
    const atThreshold = snapshot({
      redPickFirst: false,
      teamsFlipped: true,
      outputSeatLabel: false,
      lastKillTeam: 'blue',
    });
    const belowThreshold = snapshot({
      ...atThreshold,
      redPlayers: replaceFirstPlayer(
        atThreshold.redPlayers,
        { ...atThreshold.redPlayers[0], kills: atThreshold.redPlayers[0].kills + 1 },
      ),
    });

    const included = compareRoomSnapshots(
      [row('device-A', base), row('device-B', atThreshold)],
      1_000,
    );
    expect(member(included, 'device-B')).toMatchObject({
      similarity: 90,
      identityMatchPercent: 100,
    });
    expect(included.groups).toEqual([
      { id: 'group-1', memberDeviceIds: ['device-A', 'device-B'] },
    ]);

    const excluded = compareRoomSnapshots(
      [row('device-A', base), row('device-C', belowThreshold)],
      1_000,
    );
    expect(member(excluded, 'device-C').similarity).toBe(89);
    expect(excluded.groups).toEqual([]);

    const expired = compareRoomSnapshots(
      [row('device-A', base, 1_000), row('device-B', base, 879)],
      1_000,
    );
    expect(expired.groups).toEqual([]);
  });

  test('uses lightweight output by default and compares 128 simple members quickly', () => {
    const base = snapshot();
    const rows = Array.from({ length: 128 }, (_, index) =>
      row(`performance-${index.toString().padStart(4, '0')}`, base),
    );
    const startedAt = performance.now();

    const result = compareRoomSnapshots(rows, 1_000);
    const elapsedMs = performance.now() - startedAt;

    expect(result.members).toHaveLength(128);
    expect(result).not.toHaveProperty('pairwise');
    expect(elapsedMs).toBeLessThan(1_000);
  });
});
