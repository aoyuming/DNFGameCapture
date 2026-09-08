export type IdentifierKind = 'game';

// Normalized identity unions are bounded by the total library byte limit too.
export const MAX_PLAYER_ENTITY_VALUES = 10_000;

export interface PlayerEntity {
  entityId: string;
  names: string[];
  gameIds: string[];
}

export interface IdentifierConflict {
  identifier: string;
  entityIds: string[];
}

export interface IdentifierConflicts {
  gameIds: IdentifierConflict[];
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
): IdentifierConflict[] {
  const owners = new Map<string, { display: string; entityIds: Set<string> }>();
  for (const entity of entities) {
    for (const identifier of canonicalizeIdentifiers(entity.gameIds)) {
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
    gameIds: conflictMap(entities),
  };
}

function findCandidates(
  entities: readonly PlayerEntity[],
  identifiers: readonly string[],
): string[] {
  const requested = new Set(canonicalizeIdentifiers(identifiers).map(identifierKey));
  if (requested.size === 0) return [];
  return entities
    .filter((entity) => canonicalizeIdentifiers(entity.gameIds).some((value) => requested.has(identifierKey(value))))
    .map((entity) => entity.entityId)
    .sort();
}

export function resolvePlayerIdentity(
  entities: readonly PlayerEntity[],
  gameIds: readonly string[],
  activeEntityIds?: readonly string[],
): PlayerMatch {
  if (activeEntityIds !== undefined) {
    const active = new Set(activeEntityIds);
    entities = entities.filter(entity => active.has(entity.entityId));
  }
  const gameCandidates = findCandidates(entities, gameIds);
  if (gameCandidates.length > 0) {
    return gameCandidates.length === 1
      ? { entityId: gameCandidates[0], matchedBy: 'game', candidateEntityIds: gameCandidates }
      : { entityId: null, matchedBy: 'game', candidateEntityIds: gameCandidates, conflict: true };
  }
  return { entityId: null, matchedBy: 'none', candidateEntityIds: [] };
}
