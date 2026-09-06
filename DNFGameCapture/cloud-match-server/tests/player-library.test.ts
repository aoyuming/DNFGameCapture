import { describe, expect, test } from 'vitest';

import {
  canonicalizeIdentifiers,
  detectIdentifierConflicts,
  normalizeIdentifier,
  resolvePlayerIdentity,
  type PlayerEntity,
} from '../src/player-library.js';

function entity(
  entityId: string,
  names: string[],
  gameIds: string[],
  adventureGroupIds: string[] = [],
): PlayerEntity {
  return { entityId, names, gameIds, adventureGroupIds };
}

describe('normalized player library', () => {
  test('normalizes and deduplicates identifiers without retaining blank values', () => {
    expect(normalizeIdentifier('  ＡＢＣ  ')).toBe('ＡＢＣ');
    expect(canonicalizeIdentifiers(['A', ' A ', '', 'A', 'B'])).toEqual(['A', 'B']);
  });

  test('reports duplicate game or adventure identifiers owned by different entities', () => {
    const entities = [
      entity('p1', ['甲'], ['same-game'], ['same-adventure']),
      entity('p2', ['乙'], ['same-game'], ['other-adventure']),
      entity('p3', ['丙'], ['other-game'], ['same-adventure']),
    ];

    expect(detectIdentifierConflicts(entities)).toEqual({
      gameIds: [{ identifier: 'same-game', entityIds: ['p1', 'p2'] }],
      adventureGroupIds: [{ identifier: 'same-adventure', entityIds: ['p1', 'p3'] }],
    });
  });

  test('matches game IDs first even when an adventure ID points at another entity', () => {
    const entities = [
      entity('p1', ['甲'], ['game-1'], []),
      entity('p2', ['乙'], [], ['adventure-1']),
    ];

    expect(resolvePlayerIdentity(entities, [' game-1 '], ['adventure-1'])).toEqual({
      entityId: 'p1',
      matchedBy: 'game',
      candidateEntityIds: ['p1'],
    });
  });

  test('returns a conflict instead of guessing when one identifier has multiple owners', () => {
    const entities = [
      entity('p1', ['甲'], ['game-1']),
      entity('p2', ['乙'], ['game-1']),
    ];

    expect(resolvePlayerIdentity(entities, ['game-1'], [])).toEqual({
      entityId: null,
      matchedBy: 'game',
      candidateEntityIds: ['p1', 'p2'],
      conflict: true,
    });
  });

  test('falls back to adventure group IDs only when no game ID matches', () => {
    const entities = [entity('p1', ['甲'], [], ['adventure-1'])];
    expect(resolvePlayerIdentity(entities, ['unknown'], ['adventure-1'])).toEqual({
      entityId: 'p1',
      matchedBy: 'adventure',
      candidateEntityIds: ['p1'],
    });
  });
});
