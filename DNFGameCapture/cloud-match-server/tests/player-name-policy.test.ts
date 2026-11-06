import { describe, expect, test } from 'vitest';
import { applyPlayerNamePolicy, type PlayerNamePolicy } from '../src/player-name-policy.js';

const row = (entityId: string, names: string[], gameIds: string[] = []) => ({ entityId, names, gameIds });

describe('player name policy', () => {
  test('removes numeric-prefix names without dropping clean aliases or their IDs', () => {
    const input = [
      row('mixed', ['90老王', '老王', '王搞'], ['game-1']),
      row('noise', ['91新主播'], ['game-2']),
    ];

    expect(applyPlayerNamePolicy(input, 'remove_power_prefixes')).toEqual([
      row('mixed', ['老王', '王搞'], ['game-1']),
    ]);
  });

  test('strips numeric prefixes and merges variants with the clean entity for designated cards', () => {
    const input = [
      row('clean', ['老王', '王搞'], ['game-1']),
      row('power-90', ['90老王'], ['game-2']),
      row('power-91', ['91 老王'], ['game-3']),
      row('other', ['新主播'], ['game-4']),
    ];

    expect(applyPlayerNamePolicy(input, 'merge_power_prefixes')).toEqual([
      row('clean', ['老王', '王搞'], ['game-1', 'game-2', 'game-3']),
      row('other', ['新主播'], ['game-4']),
    ]);
  });

  test('preserves the input unchanged when an older server has no policy', () => {
    const input = [row('power', ['90老王'], ['game-1'])];
    expect(applyPlayerNamePolicy(input, 'none' as PlayerNamePolicy)).toEqual(input);
  });
});
