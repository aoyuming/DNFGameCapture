import { canonicalizeIdentifiers, type PlayerEntity } from './player-library.js';

export const AUTO_GROUP_GAME_THRESHOLD = 5;
export const identityKey = (value: string) => value.normalize('NFC').trim().toLocaleLowerCase();
export type AutomaticIdentityEvidence = ReadonlyMap<string, readonly PlayerEntity[]>;

type Evidence = Set<string>;
const compareStrings = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;
const evidenceOf = (row: PlayerEntity): Evidence => new Set(canonicalizeIdentifiers(row.gameIds).map(identityKey));
function overlaps(left: Set<string>, right: Set<string>, threshold: number) {
  if (left.size < threshold || right.size < threshold) return false;
  if (left.size > right.size) [left, right] = [right, left];
  let count = 0;
  for (const key of left) if (right.has(key) && ++count >= threshold) return true;
  return false;
}
const strong = (a: Evidence, b: Evidence) => overlaps(a, b, AUTO_GROUP_GAME_THRESHOLD);

/** Byte-bounded library input; posting lists prune pairs, and no popular-ID bucket is dropped. */
export function automaticIdentityGroups(entities: readonly PlayerEntity[], original: AutomaticIdentityEvidence = new Map()): string[][] {
  const clusters: { ids: string[]; evidence: Evidence[]; probe: Evidence }[] = [];
  const signatures = new Map<string, number>();
  for (const entity of [...entities].sort((a, b) => a.entityId < b.entityId ? -1 : a.entityId > b.entityId ? 1 : 0)) {
    const live = evidenceOf(entity);
    const unique = new Map<string, Evidence>();
    for (const row of original.get(entity.entityId) ?? [entity]) {
      const value = evidenceOf(row);
      // Retained proof prevents union amplification; removed IDs are no longer proof.
      for (const key of value) if (!live.has(key)) value.delete(key);
      const signature = JSON.stringify([...value].sort());
      unique.set(signature, value);
    }
    const evidence = [...unique.values()];
    if (!evidence.length || evidence.some(row => row.size < AUTO_GROUP_GAME_THRESHOLD)) continue;
    // Only single-set signatures can be compressed without hiding incompatible constituents.
    const signature = unique.size === 1 ? unique.keys().next().value! : undefined;
    const found = signature === undefined ? undefined : signatures.get(signature);
    if (found !== undefined) clusters[found].ids.push(entity.entityId);
    else {
      if (signature !== undefined) signatures.set(signature, clusters.length);
      const probe = [...unique].sort(([left], [right]) => compareStrings(left, right))[0][1];
      clusters.push({ ids: [entity.entityId], evidence, probe });
    }
  }

  const probeKeyFrequency = new Map<string, number>();
  for (const cluster of clusters) for (const key of cluster.probe) {
    probeKeyFrequency.set(key, (probeKeyFrequency.get(key) ?? 0) + 1);
  }
  const prefixes = clusters.map(cluster => [...cluster.probe]
    .sort((left, right) => probeKeyFrequency.get(left)! - probeKeyFrequency.get(right)! || compareStrings(left, right))
    .slice(0, cluster.probe.size - AUTO_GROUP_GAME_THRESHOLD + 1));
  const buckets = new Map<string, Set<number>>();
  for (const [index, prefix] of prefixes.entries()) {
    for (const value of prefix) {
      const owners = buckets.get(value) ?? new Set<number>(); owners.add(index); buckets.set(value, owners);
    }
  }
  const compatible = (left: number, right: number) => clusters[left].evidence.every(a =>
    clusters[right].evidence.every(b => strong(a, b)));
  const claimed = new Set<number>(), result: string[][] = [];
  for (let seed = 0; seed < clusters.length; seed++) {
    if (claimed.has(seed)) continue;
    const members = [seed]; claimed.add(seed);
    const candidates = new Set<number>();
    // Five shared probe keys imply an intersection between these globally ordered prefixes.
    for (const key of prefixes[seed]) for (const candidate of buckets.get(key)!) if (!claimed.has(candidate)) candidates.add(candidate);
    for (const candidate of [...candidates].sort((a, b) => a - b)) {
      if (!members.every(member => compatible(candidate, member))) continue;
      members.push(candidate); claimed.add(candidate);
    }
    const ids = members.flatMap(member => clusters[member].ids).sort();
    if (ids.length >= 2) result.push(ids);
  }
  return result;
}
