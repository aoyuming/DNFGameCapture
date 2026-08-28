import {
  normalizePlayerName,
  type ChangeSource,
  type MatchSnapshot,
  type Player,
} from './schemas.js';

type Team = 'red' | 'blue';
type StatField = 'kills' | 'deaths' | 'ak' | 'streak';
type ScoreField = 'redScore' | 'blueScore';
type StateField =
  | 'redPickFirst'
  | 'lastKillTeam';

const STAT_FIELDS: StatField[] = ['kills', 'deaths', 'ak', 'streak'];
const SCORE_FIELDS: ScoreField[] = ['redScore', 'blueScore'];
const STATE_FIELDS: StateField[] = [
  'redPickFirst',
  'lastKillTeam',
];

export interface ComparisonInputRow {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
  snapshot: MatchSnapshot;
  receivedAt: number;
  online?: boolean;
}

export interface RosterDifference {
  kind: 'roster';
  team: Team;
  referenceValue: string | null;
  memberValue: string | null;
}

export interface StatDifference {
  kind: 'stat';
  team: Team;
  playerName: string;
  field: StatField;
  referenceValue: number;
  memberValue: number;
  delta: number;
}

export interface ScoreDifference {
  kind: 'score';
  field: ScoreField;
  referenceValue: number;
  memberValue: number;
}

export interface StateDifference {
  kind: 'state';
  field: StateField;
  referenceValue: boolean | string;
  memberValue: boolean | string;
}

export type SnapshotDifference =
  | RosterDifference
  | StatDifference
  | ScoreDifference
  | StateDifference;

export interface PairwiseComparison {
  leftDeviceId: string;
  rightDeviceId: string;
  rightSwapped: boolean;
  identityMatchPercent: number;
  similarity: number;
  differences: SnapshotDifference[];
}

export interface ComparisonMember {
  deviceId: string;
  broadcasterName: string;
  deviceSuffix: string;
  online?: boolean;
  clientRevision: number;
  changeSource: ChangeSource;
  receivedAt: number;
  stale: boolean;
  excludedFromConsensus: boolean;
  sourceRoot: string;
  swapped: boolean;
  identityMatchPercent: number;
  similarity: number;
  differences: SnapshotDifference[];
  redScore: number;
  blueScore: number;
  redPickFirst: boolean;
  lastKillTeam: 'red' | 'blue' | '';
}

export interface SimilarGroup {
  id: string;
  memberDeviceIds: string[];
}

export interface RoomComparison {
  consensusDeviceId: string | null;
  groups: SimilarGroup[];
  members: ComparisonMember[];
  pairwise?: PairwiseComparison[];
}

export interface RoomComparisonOptions {
  includePairwise?: boolean;
}

interface IndexedPlayer {
  player: Player;
  team: Team;
  index: number;
}

interface MatchedPair {
  reference: IndexedPlayer;
  member: IndexedPlayer;
}

interface MatchingResult {
  pairs: MatchedPair[];
  statDifference: number;
}

interface OrientedState {
  redScore: number;
  blueScore: number;
  redPickFirst: boolean;
  lastKillTeam: 'red' | 'blue' | '';
}

interface SnapshotComparison {
  swapped: boolean;
  identityMatchPercent: number;
  similarity: number;
  differences: SnapshotDifference[];
  orientedState: OrientedState;
  matchedCount: number;
  statDifference: number;
}

const playerIdentityKeys = new WeakMap<Player, Set<string>>();

function compareDeviceIds(left: string, right: string): number {
  if (left < right) {
    return -1;
  }
  if (left > right) {
    return 1;
  }
  return 0;
}

function identityKey(value: string): string {
  return normalizePlayerName(value).replace(/[A-Z]/g, (character) =>
    String.fromCharCode(character.charCodeAt(0) + ('a'.charCodeAt(0) - 'A'.charCodeAt(0))),
  );
}

function identityKeys(player: Player): Set<string> {
  const cached = playerIdentityKeys.get(player);
  if (cached) {
    return cached;
  }
  const names = [player.mainName, ...player.aliases]
    .map(identityKey)
    .filter((name) => name.length > 0);
  const keys = new Set(names);
  playerIdentityKeys.set(player, keys);
  return keys;
}

function identitiesIntersect(left: Player, right: Player): boolean {
  const leftNames = identityKeys(left);
  const rightNames = identityKeys(right);
  for (const name of leftNames) {
    if (rightNames.has(name)) {
      return true;
    }
  }
  return false;
}

function playerStatDifference(left: Player, right: Player): number {
  return STAT_FIELDS.reduce(
    (total, field) => total + Math.abs(left[field] - right[field]),
    0,
  );
}

function compareAssignments(left: number[], right: number[]): number {
  for (let index = 0; index < left.length; index += 1) {
    const leftValue = left[index] < 0 ? Number.MAX_SAFE_INTEGER : left[index];
    const rightValue = right[index] < 0 ? Number.MAX_SAFE_INTEGER : right[index];
    if (leftValue !== rightValue) {
      return leftValue - rightValue;
    }
  }
  return 0;
}

function matchTeam(
  referencePlayers: IndexedPlayer[],
  memberPlayers: IndexedPlayer[],
): MatchingResult {
  let bestAssignment: number[] | null = null;
  let bestCount = -1;
  let bestStatDifference = Number.POSITIVE_INFINITY;
  const assignment = Array<number>(referencePlayers.length).fill(-1);
  const usedMembers = new Set<number>();

  const search = (referenceIndex: number, statDifference: number): void => {
    if (referenceIndex === referencePlayers.length) {
      const count = usedMembers.size;
      const candidateAssignment = [...assignment];
      if (
        count > bestCount ||
        (count === bestCount && statDifference < bestStatDifference) ||
        (count === bestCount &&
          statDifference === bestStatDifference &&
          bestAssignment !== null &&
          compareAssignments(candidateAssignment, bestAssignment) < 0)
      ) {
        bestAssignment = candidateAssignment;
        bestCount = count;
        bestStatDifference = statDifference;
      }
      return;
    }

    if (usedMembers.size + referencePlayers.length - referenceIndex < bestCount) {
      return;
    }

    for (let memberIndex = 0; memberIndex < memberPlayers.length; memberIndex += 1) {
      if (
        usedMembers.has(memberIndex) ||
        !identitiesIntersect(
          referencePlayers[referenceIndex].player,
          memberPlayers[memberIndex].player,
        )
      ) {
        continue;
      }
      assignment[referenceIndex] = memberIndex;
      usedMembers.add(memberIndex);
      search(
        referenceIndex + 1,
        statDifference +
          playerStatDifference(
            referencePlayers[referenceIndex].player,
            memberPlayers[memberIndex].player,
          ),
      );
      usedMembers.delete(memberIndex);
      assignment[referenceIndex] = -1;
    }

    search(referenceIndex + 1, statDifference);
  };

  search(0, 0);
  const finalAssignment = bestAssignment ?? assignment;
  return {
    pairs: finalAssignment.flatMap((memberIndex, referenceIndex) =>
      memberIndex < 0
        ? []
        : [
            {
              reference: referencePlayers[referenceIndex],
              member: memberPlayers[memberIndex],
            },
          ],
    ),
    statDifference: Number.isFinite(bestStatDifference) ? bestStatDifference : 0,
  };
}

function indexedPlayers(snapshot: MatchSnapshot, swapped: boolean): IndexedPlayer[] {
  const redPlayers = swapped ? snapshot.bluePlayers : snapshot.redPlayers;
  const bluePlayers = swapped ? snapshot.redPlayers : snapshot.bluePlayers;
  return [
    ...redPlayers.map((item, index) => ({ player: item, team: 'red' as const, index })),
    ...bluePlayers.map((item, index) => ({
      player: item,
      team: 'blue' as const,
      index: index + 4,
    })),
  ];
}

function maximumMatching(
  referencePlayers: IndexedPlayer[],
  memberPlayers: IndexedPlayer[],
): MatchingResult {
  const red = matchTeam(
    referencePlayers.filter((item) => item.team === 'red'),
    memberPlayers.filter((item) => item.team === 'red'),
  );
  const blue = matchTeam(
    referencePlayers.filter((item) => item.team === 'blue'),
    memberPlayers.filter((item) => item.team === 'blue'),
  );
  return {
    pairs: [...red.pairs, ...blue.pairs].sort(
      (left, right) => left.reference.index - right.reference.index,
    ),
    statDifference: red.statDifference + blue.statDifference,
  };
}

function swapLastKillTeam(
  lastKillTeam: MatchSnapshot['lastKillTeam'],
): MatchSnapshot['lastKillTeam'] {
  if (lastKillTeam === 'red') {
    return 'blue';
  }
  if (lastKillTeam === 'blue') {
    return 'red';
  }
  return '';
}

function orientedState(snapshot: MatchSnapshot, swapped: boolean): OrientedState {
  return {
    redScore: swapped ? snapshot.blueScore : snapshot.redScore,
    blueScore: swapped ? snapshot.redScore : snapshot.blueScore,
    redPickFirst: swapped ? !snapshot.redPickFirst : snapshot.redPickFirst,
    lastKillTeam: swapped ? swapLastKillTeam(snapshot.lastKillTeam) : snapshot.lastKillTeam,
  };
}

function buildDifferences(
  referencePlayers: IndexedPlayer[],
  memberPlayers: IndexedPlayer[],
  matching: MatchingResult,
  referenceState: OrientedState,
  memberState: OrientedState,
): SnapshotDifference[] {
  const differences: SnapshotDifference[] = [];
  const matchedReference = new Set(matching.pairs.map((pair) => pair.reference.index));
  const matchedMembers = new Set(matching.pairs.map((pair) => pair.member.index));

  for (const item of referencePlayers) {
    if (!matchedReference.has(item.index)) {
      differences.push({
        kind: 'roster',
        team: item.team,
        referenceValue: normalizePlayerName(item.player.mainName),
        memberValue: null,
      });
    }
  }
  for (const item of memberPlayers) {
    if (!matchedMembers.has(item.index)) {
      differences.push({
        kind: 'roster',
        team: item.team,
        referenceValue: null,
        memberValue: normalizePlayerName(item.player.mainName),
      });
    }
  }

  for (const pair of matching.pairs) {
    for (const field of STAT_FIELDS) {
      const referenceValue = pair.reference.player[field];
      const memberValue = pair.member.player[field];
      if (referenceValue !== memberValue) {
        differences.push({
          kind: 'stat',
          team: pair.reference.team,
          playerName: normalizePlayerName(pair.reference.player.mainName),
          field,
          referenceValue,
          memberValue,
          delta: memberValue - referenceValue,
        });
      }
    }
  }

  for (const field of SCORE_FIELDS) {
    if (referenceState[field] !== memberState[field]) {
      differences.push({
        kind: 'score',
        field,
        referenceValue: referenceState[field],
        memberValue: memberState[field],
      });
    }
  }
  for (const field of STATE_FIELDS) {
    if (referenceState[field] !== memberState[field]) {
      differences.push({
        kind: 'state',
        field,
        referenceValue: referenceState[field],
        memberValue: memberState[field],
      });
    }
  }
  return differences;
}

function compareWithOrientation(
  reference: MatchSnapshot,
  member: MatchSnapshot,
  swapped: boolean,
  includeDifferences: boolean,
): SnapshotComparison {
  const referencePlayers = indexedPlayers(reference, false);
  const memberPlayers = indexedPlayers(member, swapped);
  const matching = maximumMatching(referencePlayers, memberPlayers);
  const referenceState = orientedState(reference, false);
  const memberState = orientedState(member, swapped);
  const exactStatFields = matching.pairs.reduce(
    (count, pair) =>
      count +
      STAT_FIELDS.filter(
        (field) => pair.reference.player[field] === pair.member.player[field],
      ).length,
    0,
  );
  const exactScores = SCORE_FIELDS.filter(
    (field) => referenceState[field] === memberState[field],
  ).length;
  const exactStates = STATE_FIELDS.filter(
    (field) => referenceState[field] === memberState[field],
  ).length;
  const matchedCount = matching.pairs.length;
  const similarity = Math.round(
    (matchedCount / 8) * 40 +
      (exactStatFields / 32) * 35 +
      (exactScores / 2) * 15 +
      (exactStates / STATE_FIELDS.length) * 10,
  );

  return {
    swapped,
    matchedCount,
    statDifference: matching.statDifference,
    identityMatchPercent: Math.round((matchedCount / 8) * 100),
    similarity,
    differences: includeDifferences
      ? buildDifferences(
          referencePlayers,
          memberPlayers,
          matching,
          referenceState,
          memberState,
        )
      : [],
    orientedState: memberState,
  };
}

function compareSnapshots(
  reference: MatchSnapshot,
  member: MatchSnapshot,
  includeDifferences = true,
): SnapshotComparison {
  const normal = compareWithOrientation(reference, member, false, includeDifferences);
  const swapped = compareWithOrientation(reference, member, true, includeDifferences);
  if (swapped.matchedCount > normal.matchedCount) {
    return swapped;
  }
  if (
    swapped.matchedCount === normal.matchedCount &&
    swapped.statDifference < normal.statDifference
  ) {
    return swapped;
  }
  return normal;
}

function resolveSourceRoot(
  startingRow: ComparisonInputRow,
  rowsByDeviceId: Map<string, ComparisonInputRow>,
): string {
  const path = [startingRow.deviceId];
  let current = startingRow;

  while (current.snapshot.changeSource === 'cloud_sync' && current.snapshot.syncedFrom) {
    const sourceId = current.snapshot.syncedFrom.deviceId;
    const cycleStart = path.indexOf(sourceId);
    if (cycleStart >= 0) {
      return [...path.slice(cycleStart)].sort(compareDeviceIds)[0];
    }

    const source = rowsByDeviceId.get(sourceId);
    if (!source || source.snapshot.clientRevision !== current.snapshot.syncedFrom.revision) {
      return sourceId;
    }
    path.push(sourceId);
    current = source;
  }

  return current.deviceId;
}

function chooseRootRepresentative(
  current: ComparisonInputRow | undefined,
  candidate: ComparisonInputRow,
  root: string,
): ComparisonInputRow {
  if (!current) {
    return candidate;
  }
  const currentIsRoot = current.deviceId === root;
  const candidateIsRoot = candidate.deviceId === root;
  if (currentIsRoot !== candidateIsRoot) {
    return candidateIsRoot ? candidate : current;
  }
  if (candidate.receivedAt !== current.receivedAt) {
    return candidate.receivedAt > current.receivedAt ? candidate : current;
  }
  return compareDeviceIds(candidate.deviceId, current.deviceId) < 0 ? candidate : current;
}

function selectConsensus(
  rows: ComparisonInputRow[],
  sourceRoots: string[],
  nowSec: number,
  compareRows: (left: ComparisonInputRow, right: ComparisonInputRow) => SnapshotComparison,
): ComparisonInputRow | null {
  const representatives = new Map<string, ComparisonInputRow>();
  rows.forEach((row, index) => {
    if (nowSec - row.receivedAt > 120) {
      return;
    }
    const root = sourceRoots[index];
    representatives.set(
      root,
      chooseRootRepresentative(representatives.get(root), row, root),
    );
  });
  const candidates = [...representatives.values()];
  if (candidates.length === 0) {
    return null;
  }

  let best = candidates[0];
  let bestAverage = -1;
  for (const candidate of candidates) {
    const peers = candidates.filter((peer) => peer !== candidate);
    const average =
      peers.length === 0
        ? 100
        : peers.reduce(
            (total, peer) =>
              total + compareRows(candidate, peer).similarity,
            0,
          ) / peers.length;
    if (
      average > bestAverage ||
      (average === bestAverage && candidate.receivedAt > best.receivedAt) ||
      (average === bestAverage &&
        candidate.receivedAt === best.receivedAt &&
        compareDeviceIds(candidate.deviceId, best.deviceId) < 0)
    ) {
      best = candidate;
      bestAverage = average;
    }
  }
  return best;
}

function buildPairwise(
  rows: ComparisonInputRow[],
  compareRows: (left: ComparisonInputRow, right: ComparisonInputRow) => SnapshotComparison,
): PairwiseComparison[] {
  const pairwise: PairwiseComparison[] = [];
  for (let leftIndex = 0; leftIndex < rows.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < rows.length; rightIndex += 1) {
      const compared = compareRows(rows[leftIndex], rows[rightIndex]);
      pairwise.push({
        leftDeviceId: rows[leftIndex].deviceId,
        rightDeviceId: rows[rightIndex].deviceId,
        rightSwapped: compared.swapped,
        identityMatchPercent: compared.identityMatchPercent,
        similarity: compared.similarity,
        differences: compared.differences,
      });
    }
  }
  return pairwise;
}

function buildGroups(
  rows: ComparisonInputRow[],
  nowSec: number,
  compareRows: (left: ComparisonInputRow, right: ComparisonInputRow) => SnapshotComparison,
): SimilarGroup[] {
  const eligible = rows.filter((row) => nowSec - row.receivedAt <= 120);
  const adjacency = new Map<string, Set<string>>(
    eligible.map((row) => [row.deviceId, new Set<string>()]),
  );
  for (let leftIndex = 0; leftIndex < eligible.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < eligible.length; rightIndex += 1) {
      const compared = compareRows(eligible[leftIndex], eligible[rightIndex]);
      if (compared.similarity >= 90 && compared.identityMatchPercent >= 75) {
        adjacency.get(eligible[leftIndex].deviceId)?.add(eligible[rightIndex].deviceId);
        adjacency.get(eligible[rightIndex].deviceId)?.add(eligible[leftIndex].deviceId);
      }
    }
  }

  const components: string[][] = [];
  const visited = new Set<string>();
  const sortedDeviceIds = [...adjacency.keys()].sort(compareDeviceIds);
  for (const deviceId of sortedDeviceIds) {
    if (visited.has(deviceId)) {
      continue;
    }
    const component: string[] = [];
    const pending = [deviceId];
    visited.add(deviceId);
    while (pending.length > 0) {
      const current = pending.pop() as string;
      component.push(current);
      const neighbors = [...(adjacency.get(current) ?? [])].sort(compareDeviceIds);
      for (const neighbor of neighbors) {
        if (!visited.has(neighbor)) {
          visited.add(neighbor);
          pending.push(neighbor);
        }
      }
    }
    if (component.length > 1) {
      components.push(component.sort(compareDeviceIds));
    }
  }

  components.sort((left, right) => compareDeviceIds(left[0], right[0]));
  return components.map((memberDeviceIds, index) => ({
    id: `group-${index + 1}`,
    memberDeviceIds,
  }));
}

export function compareRoomSnapshots(
  rows: ComparisonInputRow[],
  nowSec: number,
  options: RoomComparisonOptions = {},
): RoomComparison {
  const rowIndexes = new Map<ComparisonInputRow, number>(
    rows.map((row, index) => [row, index]),
  );
  const comparisonCache = new Map<string, SnapshotComparison>();
  const compareRows = (
    left: ComparisonInputRow,
    right: ComparisonInputRow,
  ): SnapshotComparison => {
    const leftIndex = rowIndexes.get(left);
    const rightIndex = rowIndexes.get(right);
    if (leftIndex === undefined || rightIndex === undefined) {
      return compareSnapshots(left.snapshot, right.snapshot, false);
    }
    const forward = leftIndex <= rightIndex;
    const key = forward ? `${leftIndex}:${rightIndex}` : `${rightIndex}:${leftIndex}`;
    const cached = comparisonCache.get(key);
    if (cached && forward) {
      return cached;
    }
    if (cached) {
      return compareSnapshots(left.snapshot, right.snapshot, false);
    }
    const compared = compareSnapshots(left.snapshot, right.snapshot, false);
    comparisonCache.set(key, compared);
    return compared;
  };
  const rowsByDeviceId = new Map<string, ComparisonInputRow>();
  for (const row of rows) {
    const current = rowsByDeviceId.get(row.deviceId);
    if (!current || row.receivedAt > current.receivedAt) {
      rowsByDeviceId.set(row.deviceId, row);
    }
  }
  const sourceRoots = rows.map((row) => resolveSourceRoot(row, rowsByDeviceId));
  const consensus = selectConsensus(rows, sourceRoots, nowSec, compareRows);
  const members = rows.map((row, index): ComparisonMember => {
    const compared = consensus
      ? compareSnapshots(consensus.snapshot, row.snapshot)
      : {
          swapped: false,
          identityMatchPercent: 0,
          similarity: 0,
          differences: [],
          orientedState: orientedState(row.snapshot, false),
        };
    return {
      deviceId: row.deviceId,
      broadcasterName: row.broadcasterName,
      deviceSuffix: row.deviceSuffix,
      ...(row.online === undefined ? {} : { online: row.online }),
      clientRevision: row.snapshot.clientRevision,
      changeSource: row.snapshot.changeSource,
      receivedAt: row.receivedAt,
      stale: nowSec - row.receivedAt > 30,
      excludedFromConsensus: nowSec - row.receivedAt > 120,
      sourceRoot: sourceRoots[index],
      swapped: compared.swapped,
      identityMatchPercent: compared.identityMatchPercent,
      similarity: compared.similarity,
      differences: compared.differences,
      ...compared.orientedState,
    };
  });

  const result: RoomComparison = {
    consensusDeviceId: consensus?.deviceId ?? null,
    groups: buildGroups(rows, nowSec, compareRows),
    members,
  };
  if (options.includePairwise) {
    result.pairwise = buildPairwise(
      rows,
      (left, right) => compareSnapshots(left.snapshot, right.snapshot),
    );
  }
  return result;
}
