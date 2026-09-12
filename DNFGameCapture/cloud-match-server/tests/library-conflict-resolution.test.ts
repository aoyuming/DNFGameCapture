import { describe, expect, test } from 'vitest';
import {
  buildConflictResolutionGroups,
  type ResolutionSubmission,
} from '../src/library-conflict-resolution.js';
import type { PlayerEntity } from '../src/player-library.js';

const entity = (entityId: string, names: string[], gameIds: string[] = []): PlayerEntity =>
  ({ entityId, names, gameIds });

const submission = (
  id: number,
  revision: string,
  ...entities: PlayerEntity[]
): ResolutionSubmission => ({
  id,
  submissionRevision: revision.repeat(64),
  entities,
});

describe('library conflict resolution grouping', () => {
  test('aggregates normalized-name sources across submissions around one public target', () => {
    const groups = buildConflictResolutionGroups({
      revision: 7,
      publicEntities: [entity('public-a', ['丁真'], ['public-game'])],
      submissions: [
        submission(2, 'b', entity('local-b', ['丁真'], ['game-b'])),
        submission(1, 'a', entity('local-a', [' 丁真 '], ['game-a'])),
      ],
      conflicts: [
        { kind: 'names', value: '丁真', entityIds: ['local-b', 'public-a', 'local-a'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'unique_name_target',
      conflictNames: ['丁真'],
      publicEntityIds: ['public-a'],
      suggestedTargetEntityId: 'public-a',
      sources: [
        {
          entityId: 'local-a',
          names: [' 丁真 '],
          gameIds: ['game-a'],
          submissionId: 1,
          submissionRevision: 'a'.repeat(64),
        },
        {
          entityId: 'local-b',
          names: ['丁真'],
          gameIds: ['game-b'],
          submissionId: 2,
          submissionRevision: 'b'.repeat(64),
        },
      ],
    });
    expect(groups[0].token).toMatch(/^[a-f0-9]{64}$/u);
  });

  test('classifies a connected component with multiple live public targets as a public merge', () => {
    const groups = buildConflictResolutionGroups({
      revision: 3,
      publicEntities: [entity('public-b', ['B']), entity('public-a', ['A'])],
      submissions: [submission(3, 'c', entity('local-c', ['A', 'B']))],
      conflicts: [
        { kind: 'names', value: 'B', entityIds: ['local-c', 'public-b'] },
        { kind: 'names', value: 'A', entityIds: ['local-c', 'public-a'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'public_merge',
      conflictNames: ['A', 'B'],
      publicEntityIds: ['public-a', 'public-b'],
    });
    expect(groups[0]).not.toHaveProperty('suggestedTargetEntityId');
  });

  test('classifies a component without a live public target as ambiguous', () => {
    const groups = buildConflictResolutionGroups({
      revision: 0,
      publicEntities: [],
      submissions: [
        submission(4, 'd', entity('local-d', ['Same'])),
        submission(5, 'e', entity('local-e', ['Same'])),
      ],
      conflicts: [
        { kind: 'names', value: 'Same', entityIds: ['local-e', 'local-d'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'ambiguous',
      conflictNames: ['Same'],
      publicEntityIds: [],
    });
    expect(groups[0]).not.toHaveProperty('suggestedTargetEntityId');
  });

  test('keeps disconnected conflict components separate', () => {
    const groups = buildConflictResolutionGroups({
      revision: 11,
      publicEntities: [entity('public-b', ['B']), entity('public-a', ['A'])],
      submissions: [
        submission(2, 'b', entity('local-b', ['B'])),
        submission(1, 'a', entity('local-a', ['A'])),
      ],
      conflicts: [
        { kind: 'names', value: 'B', entityIds: ['public-b', 'local-b'] },
        { kind: 'names', value: 'A', entityIds: ['public-a', 'local-a'] },
      ],
    });

    expect(groups).toHaveLength(2);
    expect(groups.map(group => group.conflictNames).sort()).toEqual([['A'], ['B']]);
    expect(groups.every(group => group.kind === 'unique_name_target')).toBe(true);
  });

  test('orders group contents and final groups deterministically without mutating input', () => {
    const publicA = entity('public-a', ['A']);
    const publicB = entity('public-b', ['B']);
    const localA = entity('local-a', ['A'], ['game-a']);
    const localB = entity('local-b', ['B'], ['game-b']);
    const forward = {
      revision: 9,
      publicEntities: [publicB, publicA],
      submissions: [submission(2, 'b', localB), submission(1, 'a', localA)],
      conflicts: [
        { kind: 'names' as const, value: 'B', entityIds: ['local-b', 'public-b'] },
        { kind: 'names' as const, value: 'A', entityIds: ['local-a', 'public-a'] },
      ],
    };
    const snapshot = structuredClone(forward);
    const reverse = {
      revision: 9,
      publicEntities: [publicA, publicB],
      submissions: [submission(1, 'a', localA), submission(2, 'b', localB)],
      conflicts: [
        { kind: 'names' as const, value: 'A', entityIds: ['public-a', 'local-a'] },
        { kind: 'names' as const, value: 'B', entityIds: ['public-b', 'local-b'] },
      ],
    };

    const first = buildConflictResolutionGroups(forward);
    const second = buildConflictResolutionGroups(reverse);

    expect(first).toEqual(second);
    expect(first.map(group => group.token)).toEqual(first.map(group => group.token).sort());
    expect(forward).toEqual(snapshot);
  });

  test('changes the token when the library or any involved submission revision changes', () => {
    const makeToken = (revision: number, firstRevision: string, secondRevision: string) =>
      buildConflictResolutionGroups({
        revision,
        publicEntities: [entity('public-a', ['A'])],
        submissions: [
          submission(1, firstRevision, entity('local-a', ['A'])),
          submission(2, secondRevision, entity('local-b', ['A'])),
        ],
        conflicts: [
          { kind: 'names', value: 'A', entityIds: ['public-a', 'local-a', 'local-b'] },
        ],
      })[0].token;

    expect(new Set([
      makeToken(7, 'a', 'b'),
      makeToken(8, 'a', 'b'),
      makeToken(7, 'c', 'b'),
      makeToken(7, 'a', 'd'),
    ]).size).toBe(4);
  });

  test('signs every source ID, public ID, and authoritative conflict name', () => {
    const makeToken = (publicId: string, sourceId: string, conflictName: string) =>
      buildConflictResolutionGroups({
        revision: 1,
        publicEntities: [entity(publicId, ['browser-name'])],
        submissions: [submission(1, 'a', entity(sourceId, ['browser-name']))],
        conflicts: [
          { kind: 'names', value: conflictName, entityIds: [publicId, sourceId] },
        ],
      })[0].token;

    expect(new Set([
      makeToken('public-a', 'local-a', 'A'),
      makeToken('public-b', 'local-a', 'A'),
      makeToken('public-a', 'local-b', 'A'),
      makeToken('public-a', 'local-a', 'B'),
    ]).size).toBe(4);
  });

  test('does not infer unique-name eligibility from a game-ID-only conflict', () => {
    const groups = buildConflictResolutionGroups({
      revision: 1,
      publicEntities: [entity('public-a', ['Same browser name'], ['shared-game'])],
      submissions: [
        submission(1, 'a', entity('local-a', ['Same browser name'], ['shared-game'])),
      ],
      conflicts: [
        { kind: 'gameIds', value: 'shared-game', entityIds: ['public-a', 'local-a'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'ambiguous',
      conflictNames: [],
      publicEntityIds: ['public-a'],
    });
    expect(groups[0]).not.toHaveProperty('suggestedTargetEntityId');
  });

  test('rejects a unique target when one source is reachable only through a game-ID edge', () => {
    const groups = buildConflictResolutionGroups({
      revision: 1,
      publicEntities: [entity('public-a', ['Name'])],
      submissions: [
        submission(1, 'a',
          entity('local-name', ['Name']),
          entity('local-game', ['Other'], ['shared-game'])),
      ],
      conflicts: [
        { kind: 'names', value: 'Name', entityIds: ['public-a', 'local-name'] },
        { kind: 'gameIds', value: 'shared-game', entityIds: ['local-name', 'local-game'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'ambiguous',
      publicEntityIds: ['public-a'],
      sources: [
        { entityId: 'local-game' },
        { entityId: 'local-name' },
      ],
    });
    expect(groups[0]).not.toHaveProperty('suggestedTargetEntityId');
  });

  test('allows redundant game-ID evidence between members already connected by name', () => {
    const groups = buildConflictResolutionGroups({
      revision: 1,
      publicEntities: [entity('public-a', ['Name'], ['shared-game'])],
      submissions: [
        submission(1, 'a', entity('local-a', ['Name'], ['shared-game'])),
      ],
      conflicts: [
        { kind: 'names', value: 'Name', entityIds: ['public-a', 'local-a'] },
        { kind: 'gameIds', value: 'shared-game', entityIds: ['public-a', 'local-a'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'unique_name_target',
      suggestedTargetEntityId: 'public-a',
    });
  });

  test('does not treat a same-ID pending source as a distinct unique-target association', () => {
    const groups = buildConflictResolutionGroups({
      revision: 1,
      publicEntities: [entity('public-a', ['A'])],
      submissions: [submission(1, 'a', entity('public-a', ['A']))],
      conflicts: [
        { kind: 'names', value: 'A', entityIds: ['orphan-id', 'public-a'] },
      ],
    });

    expect(groups).toHaveLength(1);
    expect(groups[0]).toMatchObject({
      kind: 'ambiguous',
      publicEntityIds: ['public-a'],
      sources: [{ entityId: 'public-a' }],
    });
    expect(groups[0]).not.toHaveProperty('suggestedTargetEntityId');
  });
});
