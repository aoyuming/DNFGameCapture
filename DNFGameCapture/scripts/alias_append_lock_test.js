'use strict';

const assert = require('node:assert/strict');
const { withAliasAppendLock } = require('../云函数/alias_append');

async function testAppendRequestsAreSerialized() {
    const events = [];
    const work = (name, delay) => withAliasAppendLock(async () => {
        events.push(`${name}:start`);
        await new Promise(resolve => setTimeout(resolve, delay));
        events.push(`${name}:end`);
    });

    await Promise.all([work('first', 15), work('second', 0)]);
    assert.deepEqual(events, [
        'first:start', 'first:end', 'second:start', 'second:end'
    ], 'append requests must not overlap their read/merge/write transaction');
}

testAppendRequestsAreSerialized()
    .then(() => console.log('Alias append lock tests passed.'))
    .catch(error => {
        console.error(error);
        process.exitCode = 1;
    });
