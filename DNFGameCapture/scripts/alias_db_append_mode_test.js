const assert = require('node:assert/strict');

const {
    mergeAppendAlias,
    mergeAppendOnlyAliasDb,
    normalizeAppendOnlyAliasDb
} = require('../云函数/alias_append');

function testEmptyValuesCannotDelete() {
    const entries = normalizeAppendOnlyAliasDb({
        Existing: '',
        EmptyArray: [],
        Added: ['A#剑魂', '']
    });

    assert.deepEqual(entries, [
        { mainName: 'Added', aliases: ['A#剑魂'] }
    ], 'append-only input must discard empty/deletion entries');
}

function testPublicPendingAndSubmissionAreUnioned() {
    const result = mergeAppendOnlyAliasDb(
        { Main: ['A#剑魂'], Keep: ['K#法师'] },
        { Main: ['B#鬼泣'], Pending: ['P#奶妈'] },
        { Main: ['C#红眼'], Keep: '', New: ['N#剑魂'] }
    );

    assert.deepEqual(result.players, {
        Main: ['A#剑魂', 'B#鬼泣', 'C#红眼'],
        Keep: ['K#法师'],
        Pending: ['P#奶妈'],
        New: ['N#剑魂']
    }, 'append-only submission must preserve public and pending aliases');
    assert.deepEqual(result.changedMainNames, ['Main', 'New'],
        'only new or extended mains should be reported as changed');
    assert.equal(result.hasDeletion, false,
        'append-only merge must never produce a deletion');
}

function testDuplicateSubmissionIsIdempotent() {
    const result = mergeAppendOnlyAliasDb(
        { Main: ['A#剑魂'] },
        { Main: ['B#鬼泣'] },
        { Main: ['A#剑魂', 'B#鬼泣'] }
    );

    assert.deepEqual(result.players.Main, ['A#剑魂', 'B#鬼泣']);
    assert.deepEqual(result.changedMainNames, [],
        'repeating the same append must be idempotent');
}

function testSameAliasIdDoesNotCreateNakedDuplicate() {
    const aliases = ['A#剑魂'];
    assert.equal(mergeAppendAlias(aliases, 'A'), false,
        'a naked alias must not be appended beside an existing job-qualified ID');
    assert.deepEqual(aliases, ['A#剑魂']);

    const upgraded = ['A'];
    assert.equal(mergeAppendAlias(upgraded, 'A#剑魂'), true,
        'a job-qualified alias may upgrade an existing naked ID');
    assert.deepEqual(upgraded, ['A#剑魂']);
}

testEmptyValuesCannotDelete();
testPublicPendingAndSubmissionAreUnioned();
testDuplicateSubmissionIsIdempotent();
testSameAliasIdDoesNotCreateNakedDuplicate();
console.log('Alias DB append-only tests passed.');
