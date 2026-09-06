export type IdentifierKind = 'game' | 'adventure';

export interface PlayerEntity {
  entityId: string;
  names: string[];
  gameIds: string[];
  adventureGroupIds: string[];
}

export interface IdentifierConflict {
  identifier: string;
  entityIds: string[];
}

export interface IdentifierConflicts {
  gameIds: IdentifierConflict[];
  adventureGroupIds: IdentifierConflict[];
}

export type PlayerMatch =
  | {
      entityId: string;
      matchedBy: IdentifierKind;
      candidateEntityIds: string[];
    }
  | {
      entityId: null;
      matchedBy: IdentifierKind | 'none';
      candidateEntityIds: string[];
      conflict?: true;
    };

/** Normalize for storage while preserving the user's display spelling. */
export function normalizeIdentifier(value: string): string {
  return value.normalize('NFC').trim();
}

function identifierKey(value: string): string {
  return normalizeIdentifier(value).toLocaleLowerCase();
}

export function canonicalizeIdentifiers(values: readonly string[]): string[] {
  const result: string[] = [];
  const seen = new Set<string>();
  for (const value of values) {
    const normalized = normalizeIdentifier(value);
    if (!normalized) continue;
    const key = identifierKey(normalized);
    if (seen.has(key)) continue;
    seen.add(key);
    result.push(normalized);
  }
  return result;
}

function conflictMap(
  entities: readonly PlayerEntity[],
  field: 'gameIds' | 'adventureGroupIds',
): IdentifierConflict[] {
  const owners = new Map<string, { display: string; entityIds: Set<string> }>();
  for (const entity of entities) {
    for (const identifier of canonicalizeIdentifiers(entity[field])) {
      const key = identifierKey(identifier);
      const current = owners.get(key) ?? { display: identifier, entityIds: new Set<string>() };
      current.entityIds.add(entity.entityId);
      owners.set(key, current);
    }
  }
  return [...owners.values()]
    .filter((item) => item.entityIds.size > 1)
    .map((item) => ({
      identifier: item.display,
      entityIds: [...item.entityIds].sort(),
    }))
    .sort((left, right) => identifierKey(left.identifier).localeCompare(identifierKey(right.identifier)));
}

export function detectIdentifierConflicts(
  entities: readonly PlayerEntity[],
): IdentifierConflicts {
  return {
    gameIds: conflictMap(entities, 'gameIds'),
    adventureGroupIds: conflictMap(entities, 'adventureGroupIds'),
  };
}

function findCandidates(
  entities: readonly PlayerEntity[],
  identifiers: readonly string[],
  field: 'gameIds' | 'adventureGroupIds',
): string[] {
  const requested = new Set(canonicalizeIdentifiers(identifiers).map(identifierKey));
  if (requested.size === 0) return [];
  return entities
    .filter((entity) => canonicalizeIdentifiers(entity[field]).some((value) => requested.has(identifierKey(value))))
    .map((entity) => entity.entityId)
    .sort();
}

/** Game IDs deliberately win over adventure-group IDs when both match. */
export function resolvePlayerIdentity(
  entities: readonly PlayerEntity[],
  gameIds: readonly string[],
  adventureGroupIds: readonly string[],
): PlayerMatch {
  const gameCandidates = findCandidates(entities, gameIds, 'gameIds');
  if (gameCandidates.length > 0) {
    return gameCandidates.length === 1
      ? { entityId: gameCandidates[0], matchedBy: 'game', candidateEntityIds: gameCandidates }
      : { entityId: null, matchedBy: 'game', candidateEntityIds: gameCandidates, conflict: true };
  }
  const adventureCandidates = findCandidates(entities, adventureGroupIds, 'adventureGroupIds');
  if (adventureCandidates.length === 0) {
    return { entityId: null, matchedBy: 'none', candidateEntityIds: [] };
  }
  return adventureCandidates.length === 1
    ? { entityId: adventureCandidates[0], matchedBy: 'adventure', candidateEntityIds: adventureCandidates }
    : { entityId: null, matchedBy: 'adventure', candidateEntityIds: adventureCandidates, conflict: true };
}
