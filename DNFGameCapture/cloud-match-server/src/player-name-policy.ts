import { identityKey } from './library-identity-policy.js';
import type { PlayerEntity } from './player-library.js';

export type PlayerNamePolicy = 'none' | 'remove_power_prefixes' | 'merge_power_prefixes';

export interface PlayerNamePolicyProjection {
  entities: PlayerEntity[];
  entityIdMap: Map<string, string | null>;
}

export function cleanPowerPrefixName(name: string): string | null {
  const match = /^\s*[0-9０-９]+\s*/u.exec(name);
  if (!match) return null;
  const clean = name.slice(match[0].length).trim();
  return clean || null;
}

export function applyPlayerNamePolicy(
  entities: readonly PlayerEntity[],
  policy: PlayerNamePolicy,
): PlayerEntity[] {
  return projectPlayerNamePolicy(entities, policy).entities;
}

export function projectPlayerNamePolicy(
  entities: readonly PlayerEntity[],
  policy: PlayerNamePolicy,
): PlayerNamePolicyProjection {
  if (policy === 'none') return {
    entities: entities.map(entity => ({ ...entity, names: [...entity.names], gameIds: [...entity.gameIds] })),
    entityIdMap: new Map(entities.map(entity => [entity.entityId, entity.entityId])),
  };

  if (policy === 'remove_power_prefixes') {
    const projected = entities.flatMap(entity => {
      const names = entity.names.filter(name => cleanPowerPrefixName(name) === null);
      return names.length ? [{ ...entity, names, gameIds: [...entity.gameIds] }] : [];
    });
    const included = new Set(projected.map(entity => entity.entityId));
    return {
      entities: projected,
      entityIdMap: new Map(entities.map(entity => [entity.entityId, included.has(entity.entityId) ? entity.entityId : null])),
    };
  }

  const parents = new Map(entities.map(entity => [entity.entityId, entity.entityId]));
  const find = (id: string): string => {
    const parent = parents.get(id);
    if (parent === undefined || parent === id) return id;
    const root = find(parent);
    parents.set(id, root);
    return root;
  };
  const ownerByCleanName = new Map<string, string>();
  for (const entity of entities) for (const name of entity.names) {
    if (cleanPowerPrefixName(name) === null) ownerByCleanName.set(identityKey(name), entity.entityId);
  }
  const ownerBySuffix = new Map(ownerByCleanName);
  for (const entity of entities) for (const name of entity.names) {
    const clean = cleanPowerPrefixName(name);
    if (clean === null) continue;
    const key = identityKey(clean);
    const target = ownerBySuffix.get(key);
    if (!target) {
      ownerBySuffix.set(key, entity.entityId);
      continue;
    }
    parents.set(find(entity.entityId), find(target));
  }

  const merged = new Map<string, PlayerEntity>();
  for (const entity of entities) {
    const root = find(entity.entityId);
    let target = merged.get(root);
    if (!target) {
      target = { entityId: root, names: [], gameIds: [] };
      merged.set(root, target);
    }
    for (const name of entity.names) {
      const clean = cleanPowerPrefixName(name);
      const value = clean ?? name;
      if (!target.names.some(existing => identityKey(existing) === identityKey(value))) target.names.push(value);
    }
    for (const gameId of entity.gameIds) {
      if (!target.gameIds.some(existing => identityKey(existing) === identityKey(gameId))) target.gameIds.push(gameId);
    }
  }
  const projected = [...merged.values()];
  return {
    entities: projected,
    entityIdMap: new Map(entities.map(entity => [entity.entityId, find(entity.entityId)])),
  };
}
