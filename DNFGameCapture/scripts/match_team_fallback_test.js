const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const source = fs.readFileSync(path.join(__dirname, '..', 'DNFGameCaptureDlg.cpp'), 'utf8');
const fusionStart = source.indexOf('// Use temporal game-ID fusion for unresolved sides after direct matching.');
const processMatchStart = source.indexOf('auto processMatch =');
const acceptCollectedStart = source.indexOf('auto acceptCollectedFirstRound =', processMatchStart);
const processMatch = source.slice(processMatchStart, acceptCollectedStart);

test('each OCR frame contributes scores for every eligible player before cross-frame ranking', () => {
  assert.notEqual(processMatchStart, -1);
  assert.notEqual(acceptCollectedStart, -1);
  const scoreBlockStart = processMatch.indexOf('int maxScore = -2');
  const playerLoopStart = processMatch.indexOf('for (int p = 0; p < 8; p++)', scoreBlockStart);
  const playerLoopEnd = processMatch.indexOf('m_dataMutex.unlock();', playerLoopStart);
  const collectScore = processMatch.indexOf('frameCandidates.push_back', playerLoopStart);
  const mergeScores = processMatch.indexOf('for (const auto& candidate : frameCandidates)', playerLoopEnd);

  assert.ok(playerLoopStart >= 0 && playerLoopEnd > playerLoopStart);
  assert.ok(collectScore > playerLoopStart && collectScore < playerLoopEnd,
    'keep each player score while iterating the roster, not only the frame winner');
  assert.ok(mergeScores > playerLoopEnd,
    'merge every player score after releasing the roster lock');
  assert.match(processMatch.slice(mergeScores), /mergeFirstRoundCandidate\(isKiller,/);
});

test('whole-roster scoring remains a last fallback after team-scoped attempts', () => {
  const fallbackIndex = source.indexOf('[全场兜底-最高分]');
  assert.notEqual(fallbackIndex, -1);
  assert.ok(fallbackIndex > fusionStart, 'whole-roster fallback must follow team-scoped fusion');
});

test('aggressive matching scores every team-scoped frame before deciding', () => {
  const retryStart = source.indexOf('// 二轮增强评分也先收集全部帧');
  const retryEnd = source.indexOf('// Use temporal game-ID fusion for unresolved sides after direct matching.', retryStart);
  assert.ok(retryStart >= 0 && retryEnd > retryStart);
  const retryBlock = source.slice(retryStart, retryEnd);
  assert.match(retryBlock, /true,\s*frame\.frameIdx,\s*true\)/);
  assert.doesNotMatch(retryBlock, /if\s*\(\s*(killer|dead)Resolved\s*\)\s*break/);
});
