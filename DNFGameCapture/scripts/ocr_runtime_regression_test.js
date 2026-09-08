const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../DNFGameCaptureDlg.cpp'), 'utf8');
function body(start, end) {
  const offset = source.indexOf(start);
  assert.ok(offset >= 0, start);
  return source.slice(offset, source.indexOf(end, offset + start.length));
}
assert.ok(!/m_sceneScanner|MaybeScanScene|DrawSceneIdentity|cmd_scene_diagnostics|adventureGroupIds|HasUniqueAdventureId/.test(source),
  'Removed scene/adventure recognition must have no native entry point or snapshot fields');
const supervisor = body('void CDNFGameCaptureDlg::OcrSupervisorLoop()', 'bool CDNFGameCaptureDlg::RefreshOcrExePathFromRunningProcess');
assert.match(supervisor, /health\.Observe/, 'Recovery must use confirmed health transitions');
const reset = body('void CDNFGameCaptureDlg::QueuePlayerIdentityCommand(', 'void CDNFGameCaptureDlg::QueuePlayerLibraryImport');
assert.match(reset, /if \(reset && result\.ok\)[\s\S]*ClearActivePlayersAfterLibraryReset\(\)/);
assert.doesNotMatch(reset, /CaptureResetMatchLibraryBaseline\(\)/, 'Reset must not retain fielded names');
assert.match(reset, /if \(m_ocrTaskCount\)/, 'Reset must reject unfinished scoring work');
const start = body('void CDNFGameCaptureDlg::OnBnClickedStart()', 'LRESULT CDNFGameCaptureDlg::OnOcrServiceFail');
assert.match(start, /if \(m_playerLibraryResetInFlight\)/, 'Monitoring cannot restart during reset');
assert.match(start, /if \(m_players\[i\]\.aliases\.empty\(\)\)/, 'Every active player needs game IDs before monitoring');
const save = body('bool CDNFGameCaptureDlg::SaveConfigToFile()', 'void CDNFGameCaptureDlg::LoadConfigFromFile()');
assert.match(save, /if \(m_playerLibraryResetInFlight\) return true;/, 'UI cannot rewrite the roster during reset');
const clear = body('void CDNFGameCaptureDlg::ClearActivePlayersAfterLibraryReset()', 'bool CDNFGameCaptureDlg::QueuePlayerLibrarySave');
assert.match(clear, /event\.readOnly = true/, 'History from cleared slots must not score replacement players');
assert.match(reset, /SetOcrStartupPendingUI\(false\)/, 'Reset must release the disabled native start button');
const ocr = body('OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(', '// 【函数 2】');
assert.match(ocr, /AcquireForeground\(\)[\s\S]*monitoringGeneration != m_ocrMonitoringGeneration/, 'Queued OCR must recheck the monitoring generation');
assert.match(ocr, /UpdateIdentityPanelCache\(nAreaIndex, ocrText, monitoringGeneration\)/,
  'Late OCR responses must carry their original monitoring generation to cache commit');
const ready = body('LRESULT CDNFGameCaptureDlg::OnOcrStartResult(', 'void CDNFGameCaptureDlg::BeginOcrServiceRecovery(');
assert.match(ready, /if \(!m_bIsRunning\) OnBnClickedStart\(\)/,
  'Async readiness must rerun the current roster and authorization guards, not bypass them');
const taskStart = body('void CDNFGameCaptureDlg::StartOcrMatchingTask(', 'void CDNFGameCaptureDlg::EndOcrMatchingTask(');
assert.match(taskStart, /DoRetryMatchingTask\(triggerSide, monitoringGeneration\)/);
const retry = body('void CDNFGameCaptureDlg::DoRetryMatchingTask(', 'void CDNFGameCaptureDlg::OnBnClickedStart()');
assert.match(retry, /MatchIdentityPanel\(side\)/, 'Game-ID temporal fusion must remain connected');
assert.match(retry, /std::lock_guard<std::mutex> dataLock\(m_dataMutex\);\s*if \(cancelled\(\)\) return;/,
  'Scoring must recheck cancellation while holding the roster lock');
const identity = fs.readFileSync(path.join(__dirname, '../DNFGameCaptureDlg_IdentityPatch.cpp'), 'utf8');
assert.match(identity, /identityLock\(m_identityMutex\);\s*if \(monitoringGeneration != m_ocrMonitoringGeneration.load\(\)\) return;/,
  'Cache commits must validate cancellation under the identity lock');
assert.match(identity, /m_identityMatcher\.MatchPanel\(side, candidates, now, dbg\)/);
assert.match(identity, /m_identityMatcher\.NotifyKillConfirmed/);
assert.ok(!/sceneScanner|ResolveAdventure|SceneAdventure/.test(identity), 'No removed fallback may affect scoring');
for (const name of ['DNFGameCaptureDlg.h', 'DNFGameCapture.vcxproj', 'DNFGameCapture.vcxproj.filters']) {
  assert.ok(!/SceneIdentity/.test(fs.readFileSync(path.join(__dirname, '..', name), 'utf8')),
    `Removed scanner must not be linked by ${name}`);
}
for (const name of ['SceneIdentityScanner.cpp', 'SceneIdentityScanner.h', 'SceneIdentityPolicy.cpp', 'SceneIdentityPolicy.h']) {
  assert.equal(fs.existsSync(path.join(__dirname, '..', name)), false, `Remove unused module ${name}`);
}
const review = body('bool CDNFGameCaptureDlg::ToggleReviewEvent(', 'void CDNFGameCaptureDlg::DoRetryMatchingTask(');
assert.match(review, /if \(ev\.readOnly\) return false;/, 'Native review commands must reject cleared-roster history');
const web = fs.readFileSync(path.join(__dirname, '../web前端/main.js'), 'utf8');
assert.match(web, /ev\.readOnly \|\| \(!ev\.undone && !ev\.statsApplied\)/, 'Cleared-roster undo buttons must be disabled');
console.log('OCR runtime / monitoring-only / full reset integration checks passed.');
