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
): PlayerEntity {
  return { entityId, names, gameIds };
}

describe('normalized player library', () => {
  test('normalizes and deduplicates identifiers without retaining blank values', () => {
    expect(normalizeIdentifier('  ＡＢＣ  ')).toBe('ＡＢＣ');
    expect(canonicalizeIdentifiers(['A', ' A ', '', 'A', 'B'])).toEqual(['A', 'B']);
  });

  test('reports shared game identifiers owned by different entities', () => {
    const entities = [
      entity('p1', ['甲'], ['same-game']),
      entity('p2', ['乙'], ['same-game']),
      entity('p3', ['丙'], ['other-game']),
    ];

    expect(detectIdentifierConflicts(entities)).toEqual({
      gameIds: [{ identifier: 'same-game', entityIds: ['p1', 'p2'] }],
    });
  });

  test('matches game IDs without using player names as identity evidence', () => {
    const entities = [
      entity('p1', ['甲'], ['game-1']),
      entity('p2', ['game-1'], []),
    ];

    expect(resolvePlayerIdentity(entities, [' game-1 '])).toEqual({
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

    expect(resolvePlayerIdentity(entities, ['game-1'])).toEqual({
      entityId: null,
      matchedBy: 'game',
      candidateEntityIds: ['p1', 'p2'],
      conflict: true,
    });
  });

  test('keeps name-only players but does not match without a game ID', () => {
    const entities = [entity('p1', ['甲'], [])];
    expect(resolvePlayerIdentity(entities, ['甲'])).toEqual({
      entityId: null,
      matchedBy: 'none',
      candidateEntityIds: [],
    });
  });

  test('shared identifiers resolve only within the supplied active roster', () => {
    const entities = [entity('p1', ['A'], ['shared']), entity('p2', ['B'], ['shared'])];
    expect(resolvePlayerIdentity(entities, ['shared'], ['p2'])).toMatchObject({ entityId: 'p2' });
    expect(resolvePlayerIdentity(entities, ['shared'], [])).toMatchObject({ entityId: null, candidateEntityIds: [] });
    expect(resolvePlayerIdentity(entities, ['shared'], ['p1', 'p2'])).toMatchObject({ entityId: null, conflict: true });
  });
});
