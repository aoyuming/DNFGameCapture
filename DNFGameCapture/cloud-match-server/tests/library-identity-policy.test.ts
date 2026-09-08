import { expect, test } from 'vitest';
import { automaticIdentityGroups } from '../src/library-identity-policy.js';
import type { PlayerEntity } from '../src/player-library.js';

const row = (entityId: string, gameIds: string[]): PlayerEntity =>
  ({ entityId, names: [entityId], gameIds });

test('automatic grouping requires five distinct overlapping game IDs', () => {
  const four = ['a', 'b', 'c', 'd'];
  expect(automaticIdentityGroups([row('a', four), row('b', [...four, 'A'])])).toEqual([]);
  expect(automaticIdentityGroups([row('a', [...four, 'e']), row('b', [...four, 'e', 'other'])])).toEqual([['a', 'b']]);
  expect(automaticIdentityGroups([row('a', []), row('b', [])])).toEqual([]);
  expect(automaticIdentityGroups([row('a', ['one']), row('b', ['one'])])).toEqual([]);
});

test('pairwise evidence avoids transitive or order-dependent over-merges', () => {
  const first = ['1', '2', '3', '4', '5'], last = ['6', '7', '8', '9', '10'];
  const rows = [row('a', first), row('b', [...first, ...last]), row('c', last)];
  expect(automaticIdentityGroups(rows)).toEqual([['a', 'b']]);
  expect(automaticIdentityGroups([...rows].reverse())).toEqual([['a', 'b']]);
});

test('large exact qualifying sets bypass pair expansion and weak popular IDs do not merge', () => {
  const rows = Array.from({ length: 1200 }, (_, i) => row(`p-${i}`, ['1', '2', '3', '4', '5']));
  expect(automaticIdentityGroups(rows)[0]).toHaveLength(1200);
  expect(automaticIdentityGroups(rows.map(r => ({ ...r, gameIds: ['shared', r.entityId] })))).toEqual([]);
});

test('popular shared IDs still group 257 nonidentical qualifying sets', () => {
  const rows = Array.from({ length: 257 }, (_, i) => row(`p-${i}`, ['1', '2', '3', '4', '5', `unique-${i}`]));
  expect(automaticIdentityGroups(rows)).toHaveLength(1);
  expect(automaticIdentityGroups(rows)[0]).toHaveLength(257);
});

test('saved original constituents prevent union-generated evidence after reimport', () => {
  const a = row('a', ['1', '2', '3', '4', '5']);
  const c = row('c', ['6', '7', '8', '9', '10']);
  const b = row('b', [...a.gameIds, ...c.gameIds]);
  const union = row('a', b.gameIds);
  expect(automaticIdentityGroups([union, c], new Map([['a', [a, b]]]))).toEqual([]);
});

test('removed live identifiers cannot keep contributing stale automatic evidence', () => {
  const ids = ['1', '2', '3', '4', '5'];
  const a = row('a', ids), b = row('b', ids), candidate = row('c', ids);
  const original = new Map([['a', [a, b]]]);
  expect(automaticIdentityGroups([row('a', ['new']), candidate], original)).toEqual([]);
});
