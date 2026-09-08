import { canonicalizeIdentifiers, type PlayerEntity } from './player-library.js';

export const AUTO_GROUP_GAME_THRESHOLD = 5;
export const identityKey = (value: string) => value.normalize('NFC').trim().toLocaleLowerCase();
export type AutomaticIdentityEvidence = ReadonlyMap<string, readonly PlayerEntity[]>;

type Evidence = Set<string>;
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
  const clusters: { ids: string[]; evidence: Evidence[] }[] = [];
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
      clusters.push({ ids: [entity.entityId], evidence });
    }
  }
  const buckets = new Map<string, Set<number>>();
  const keys = clusters.map((cluster, index) => {
    const values = new Set<string>();
    for (const row of cluster.evidence) for (const value of row) values.add(value);
    for (const value of values) {
      const owners = buckets.get(value) ?? new Set<number>(); owners.add(index); buckets.set(value, owners);
    }
    return values;
  });
  const compatible = (left: number, right: number) => clusters[left].evidence.every(a =>
    clusters[right].evidence.every(b => strong(a, b)));
  const claimed = new Set<number>(), result: string[][] = [];
  for (let seed = 0; seed < clusters.length; seed++) {
    if (claimed.has(seed)) continue;
    const members = [seed]; claimed.add(seed);
    const candidates = new Set<number>();
    for (const key of keys[seed]) for (const candidate of buckets.get(key)!) if (!claimed.has(candidate)) candidates.add(candidate);
    for (const candidate of [...candidates].sort((a, b) => a - b)) {
      if (!members.every(member => compatible(candidate, member))) continue;
      members.push(candidate); claimed.add(candidate);
    }
    const ids = members.flatMap(member => clusters[member].ids).sort();
    if (ids.length >= 2) result.push(ids);
  }
  return result;
}
