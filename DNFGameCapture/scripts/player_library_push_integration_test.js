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
  for (const source of [manual, automatic]) {
    assert.match(source, /DnfPrepareV2PlayerLibraryPayload/);
    assert.match(source, /SubmissionSignature/);
    assert.match(source, /m_playerLibraryPushTracker\.ShouldSkip/);
    assert.match(source, /serverEndpoint, serverSessionToken, serverDeviceId/);
    assert.match(source, /submittedSignature/);
    assert.match(source, /result->v2Scope = v2Scope/);
  }
  assert(manual.indexOf('m_playerLibraryPushTracker.ShouldSkip') < manual.indexOf('std::thread('));
  assert.match(automatic, /useServerAuthV2 \? v2Unchanged : payloadHash == previousPushHash/);
  assert.match(header, /SubmissionTracker m_playerLibraryPushTracker/);
});

test('automatic unchanged pushes still fetch and import the scheduled public library', () => {
  assert(automatic.indexOf('DnfFetchV2PublicAliasDb(') < automatic.indexOf('useServerAuthV2 ? v2Unchanged'));
  assert.match(autoResult, /QueuePlayerLibraryImport\(pending->publicAliasDbJson/);
  assert.match(autoResult, /result->pushAttempted && !dnf::player_library_sync::IsSkipped/);
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
  const invalidate = section('void CDNFGameCaptureDlg::DisableCloudMatchForAuthorization(', 'void CDNFGameCaptureDlg::LoadCloudMatchSettings(');
  assert.match(invalidate, /m_aliasManualSyncGeneration.fetch_add/);
});

test('versioned digest persistence is separate from the legacy raw hash', () => {
  const settings = section('void CDNFGameCaptureDlg::LoadAliasDbAutoSyncSettings()', 'bool CDNFGameCaptureDlg::MergePublicAliasDbForAutoSync(');
  assert.match(settings, /LastV2PushSignature/);
  assert.match(settings, /m_playerLibraryPushTracker.Restore/);
  assert.match(settings, /m_playerLibraryPushTracker.SavedSignature/);
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
