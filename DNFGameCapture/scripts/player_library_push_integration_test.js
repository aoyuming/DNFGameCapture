const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const cpp = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8').replace(/\r\n/g, '\n');
const header = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.h'), 'utf8');
function section(start, end) {
  const at = cpp.indexOf(start);
  const until = cpp.indexOf(end, at + start.length);
  assert(at >= 0 && until > at, `Missing section ${start}`);
  return cpp.slice(at, until);
}
const manual = section('void CDNFGameCaptureDlg::StartManualAliasDbSync(', 'LRESULT CDNFGameCaptureDlg::OnAliasManualSyncResult(');
const manualResult = section('LRESULT CDNFGameCaptureDlg::OnAliasManualSyncResult(', 'CDNFGameCaptureDlg::~CDNFGameCaptureDlg()');
const automatic = section('void CDNFGameCaptureDlg::StartAliasDbAutoSyncAttempt()', 'LRESULT CDNFGameCaptureDlg::OnAliasDbAutoSyncResult(');
const autoResult = section('LRESULT CDNFGameCaptureDlg::OnAliasDbAutoSyncResult(', 'void CDNFGameCaptureDlg::ResetAliasDbCloudBaseline()');

test('manual and automatic V2 pushes share the canonical acknowledged checkpoint', () => {
  for (const source of [manual, autoResult]) {
    assert.match(source, /DnfPrepareV2PlayerLibraryPayload/);
    assert.match(source, /SubmissionSignature/);
    assert.match(source, /m_playerLibraryPushTracker\.ShouldSkip/);
    assert.match(source, /serverEndpoint[\s\S]*serverSessionToken[\s\S]*serverDeviceId/);
    assert.match(source, /submittedSignature/);
  }
  assert(manual.indexOf('m_playerLibraryPushTracker.ShouldSkip') < manual.indexOf('std::thread('));
  assert(autoResult.indexOf('m_playerLibraryPushTracker.ShouldSkip') < autoResult.indexOf('std::thread('));
  assert.match(manual, /result->v2Scope = v2Scope/);
  assert.match(autoResult, /const auto v2Scope = pending->v2Scope/);
  assert.match(header, /SubmissionTracker m_playerLibraryPushTracker/);
});

test('automatic unchanged pushes still fetch and import the scheduled public library', () => {
  assert.match(automatic, /DnfFetchV2PublicAliasDb\(/);
  assert.match(autoResult, /QueuePlayerLibraryImport\(pending->publicAliasDbJson/);
  assert(autoResult.indexOf('QueuePlayerLibraryImport(pending->publicAliasDbJson') <
    autoResult.indexOf('m_playerLibraryPushTracker.ShouldSkip'));
  assert.match(autoResult, /result->pushAttempted && !dnf::player_library_sync::IsSkipped/);
});

test('automatic V2 push is rebuilt only after pulled cloud identities are committed', () => {
  assert.doesNotMatch(automatic, /DnfSubmitV2PlayerLibrary\(/,
    'the network pull stage must not submit the pre-import snapshot');
  const imported = autoResult.indexOf('QueuePlayerLibraryImport(pending->publicAliasDbJson');
  const rebuilt = autoResult.indexOf('librarySnapshot->v2Entities', imported);
  const submitted = autoResult.indexOf('DnfSubmitV2PlayerLibrary(', rebuilt);
  assert(imported >= 0 && rebuilt > imported && submitted > rebuilt,
    'the committed pull must publish cloud entity IDs before rebuilding and submitting');
  assert.match(autoResult, /pending->libraryPartial[\s\S]*公共库存在归属冲突，已暂停自动投稿/);
});

test('only the acknowledged captured request is saved and V2 never resets the live legacy baseline', () => {
  assert.match(manualResult, /!result->pull && result->ok && result->useServerAuthV2/);
  assert.match(manualResult, /AcknowledgeV2PlayerLibraryPush\(result->submittedSignature, result->pushStatus\)/);
  assert.match(manualResult, /else if \(!result->pull && result->ok && !result->useServerAuthV2\)/);
  assert.match(autoResult, /result->useServerAuthV2 && result->pushAttempted && result->pushOk/);
  assert.match(autoResult, /AcknowledgeV2PlayerLibraryPush\(result->submittedSignature, result->pushStatus\)/);
  assert.match(autoResult, /!result->useServerAuthV2 && !result->localPayloadHash.empty\(\)/);
  assert(autoResult.indexOf('AcknowledgeV2PlayerLibraryPush') < autoResult.indexOf('QueuePlayerLibraryImport'));
});

test('old endpoint or license results cannot checkpoint or apply UI state', () => {
  for (const source of [manualResult, autoResult]) {
    const guard = source.indexOf('result->v2Scope != CurrentV2PlayerLibraryScope()');
    assert(guard >= 0 && guard < source.indexOf('AcknowledgeV2PlayerLibraryPush'));
    assert(guard < source.indexOf('QueuePlayerLibraryImport'));
  }
  const autoScopeGuards = [...autoResult.matchAll(/v2Scope != CurrentV2PlayerLibraryScope\(\)/g)];
  assert.equal(autoScopeGuards.length, 2,
    'V2 scope must be checked again after the asynchronous local import');
  assert(autoScopeGuards[1].index < autoResult.indexOf('librarySnapshot->v2Entities'));
  const invalidate = section('void CDNFGameCaptureDlg::DisableCloudMatchForAuthorization(', 'void CDNFGameCaptureDlg::LoadCloudMatchSettings(');
  assert.match(invalidate, /m_aliasManualSyncGeneration.fetch_add/);
});

test('post-import automatic push rechecks cancellation before reading the committed snapshot', () => {
  const imported = autoResult.indexOf('QueuePlayerLibraryImport(pending->publicAliasDbJson');
  const rebuilt = autoResult.indexOf('librarySnapshot->v2Entities', imported);
  const continuationGuard = autoResult.slice(imported, rebuilt);
  assert.match(continuationGuard, /pending->lifetime != m_aliasAutoSyncLifetime/);
  assert.match(continuationGuard, /!pending->lifetime/);
  assert.match(continuationGuard, /!pending->lifetime->load\(std::memory_order_acquire\)/);
  assert.match(continuationGuard,
    /const auto currentGeneration =[\s\S]*m_aliasAutoSyncGeneration\.load\(std::memory_order_acquire\)/);
  assert.match(continuationGuard,
    /const bool ownsCurrentAttempt =\s*pending->generation == currentGeneration/);
  assert.match(continuationGuard, /!ownsCurrentAttempt/);
  assert.match(continuationGuard, /!m_aliasAutoSyncEnabled/);
  assert.match(continuationGuard, /m_playerLibraryExitPending/);
  assert.match(continuationGuard,
    /if \(ownsCurrentAttempt\) m_aliasAutoSyncInFlight = false/);
});

test('versioned digest persistence is separate from the legacy raw hash', () => {
  const settings = section('void CDNFGameCaptureDlg::LoadAliasDbAutoSyncSettings()', 'bool CDNFGameCaptureDlg::MergePublicAliasDbForAutoSync(');
  assert.match(settings, /LastV2PushSignature/);
  assert.match(settings, /m_playerLibraryPushTracker.Restore/);
  assert.match(settings, /m_playerLibraryPushTracker.SavedSignature/);
  assert.match(settings, /PullBeforePushVersion/);
  assert.match(settings, /syncWorkflowVersion[\s\S]*m_aliasAutoSyncLastSuccessAt = 0/,
    'upgraded clients must run the corrected pull-before-push flow once immediately');
  const save = section('bool CDNFGameCaptureDlg::SaveV2PlayerLibraryPushCheckpoint() const', 'bool CDNFGameCaptureDlg::SaveAliasDbAutoSyncSettings() const');
  assert.doesNotMatch(save, /licenseKey|keyUtf8|serverSessionToken|payload/i);
});

test('V2 acknowledgment decoding exposes skipped server outcomes without fabricated review state', () => {
  assert.match(cpp, /ParseSubmissionStatus\(status, reply\)/);
  assert.match(cpp, /case dnf::player_library_sync::SubmissionStatus::NoChanges:/);
  assert.match(cpp, /case dnf::player_library_sync::SubmissionStatus::AlreadyPending:/);
  const helper = fs.readFileSync(path.join(root, 'PlayerLibraryPushPolicy.cpp'), 'utf8');
  assert.match(helper, /reply\["ok"\]\.is_boolean\(\)/);
  assert.match(helper, /reply\["status"\]\.is_string\(\)/);
  const submit = section('    dnf::player_library_sync::SubmissionStatus& submissionStatus)\n{', 'static bool DnfFetchPublicAliasDb(');
  assert.doesNotMatch(submit, /reply\.value\(/);
  assert.match(submit, /reply\["code"\]\.is_string\(\)/);
});

test('late alias progress is guarded and acknowledged pushes survive pull-apply failures in the display', () => {
  const progress = section('LRESULT CDNFGameCaptureDlg::OnCloudProgress(', 'void CDNFGameCaptureDlg::StartManualAliasDbSync(');
  assert.match(progress, /update->lifetime != m_aliasAutoSyncLifetime/);
  assert.match(progress, /update->generation != currentGeneration/);
  assert.match(progress, /update->v2Scope != CurrentV2PlayerLibraryScope\(\)/);
  assert.match(manual, /false, generation, lifetime, v2Scope/);
  assert.match(automatic, /update->v2Scope = v2Scope/);
  assert.equal((autoResult.match(/showAcknowledgedV2Push\(\);/g) || []).length, 2);
});
