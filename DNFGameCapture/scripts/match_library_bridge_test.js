const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const cpp = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const web = fs.readFileSync(path.join(root, 'web前端/main.js'), 'utf8');
function section(start, end) {
  const at = cpp.indexOf(start);
  assert(at >= 0, `Missing ${start}`);
  const until = cpp.indexOf(end, at + start.length);
  assert(until > at, `Missing ${end}`);
  return cpp.slice(at, until);
}

test('legacy conversion preserves name-only players and shared game owners without retired keys', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'dnf-library-converter-'));
  try {
    const input = path.join(dir, 'input.ini');
    const output = path.join(dir, 'output.json');
    fs.writeFileSync(input, 'Alpha=(Shared#Job)(OnlyA)\nBeta=(Shared#Job)(OnlyB)\nNameOnly=\nadventureGroupIds=\n');
    execFileSync(process.execPath, [path.join(root, 'scripts/convert-legacy-alias-db-to-v2.js'), input, output]);
    const { entities } = JSON.parse(fs.readFileSync(output, 'utf8'));
    assert.equal(entities.length, 4);
    for (const entity of entities) {
      assert.deepEqual(Object.keys(entity).sort(), ['entityId', 'gameIds', 'names']);
      assert.equal(entity.names.length, 1);
    }
    assert.deepEqual(entities.find(e => e.names[0] === 'NameOnly').gameIds, []);
    assert.deepEqual(entities.find(e => e.names[0] === 'adventureGroupIds').gameIds, []);
    assert.deepEqual(entities.find(e => e.names[0] === 'Alpha').gameIds, ['Shared#Job', 'OnlyA']);
    assert.deepEqual(entities.find(e => e.names[0] === 'Beta').gameIds, ['Shared#Job', 'OnlyB']);
  } finally {
    assert(path.resolve(dir).startsWith(path.resolve(os.tmpdir()) + path.sep));
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('start guard requires game aliases for each active player', () => {
  const guard = section('CString missingAliasPlayers;', 'if (!missingAliasPlayers.IsEmpty() || !shortAliasPlayers.IsEmpty())');
  assert.match(guard, /for \(int i = 0; i < 8; \+\+i\)/);
  assert.match(guard, /m_players\[i\]\.name\.IsEmpty\(\)/);
  assert.match(guard, /m_players\[i\]\.aliases\.empty\(\)/);
  assert.match(guard, /DnfIsLegacyShortAliasWithoutMeta\(a\.name\)/);
  assert.doesNotMatch(guard, /adventure|library->Lookup|activeOwners/i);
});
test('public pulls explicitly opt into partial ownership imports and report skipped names', () => {
  const queue = section('void CDNFGameCaptureDlg::QueuePlayerLibraryImport(', 'void CDNFGameCaptureDlg::LoadAliasDB(');
  assert.match(queue, /root\["skipOwnershipConflicts"\] = true/);
  assert.match(queue, /result\.importReport\.skipped/);
  const manual = section('LRESULT CDNFGameCaptureDlg::OnAliasManualSyncResult(', 'void CDNFGameCaptureDlg::');
  const automatic = section('LRESULT CDNFGameCaptureDlg::OnAliasDbAutoSyncResult(', 'void CDNFGameCaptureDlg::ResetAliasDbCloudBaseline');
  for (const callback of [manual, automatic]) {
    assert.match(callback, /DnfPlayerLibraryImportSummary\(committed\)/);
    assert.match(callback, /if \(committed\.ok && committed\.importReport\.skipped\.empty\(\)\) \{[\s\S]*?if \(!wasDirty\) ResetAliasDbCloudBaseline\(\);\s*\}/);
  }
  assert.match(automatic, /const bool cycleReady = !result->libraryPartial/);
  assert.match(automatic, /if \(cycleReady\) \{[\s\S]*?m_aliasAutoSyncLastSuccessAt =/);
  const match = section('void CDNFGameCaptureDlg::PumpSyncedPlayerLibrary()', 'void CDNFGameCaptureDlg::QueuePlayerIdentityCommand(');
  assert.doesNotMatch(match, /skipOwnershipConflicts/);
});
test('one-shot and realtime acceptance enqueue additive library imports', () => {
  assert.match(section('else if (action == "cmd_cloud_sync_broadcaster")', 'else if (action == "cmd_cloud_realtime_start")'), /QueueSyncedPlayerLibrary\(snapshot,/);
  assert.match(section('void CDNFGameCaptureDlg::HandleUnifiedCloudSnapshot(', 'void CDNFGameCaptureDlg::'), /QueueSyncedPlayerLibrary\(teamSnapshot,/);
});
test('sync imports use worker queue and coalesce identical evidence without replacing the library', () => {
  const queue = section('bool CDNFGameCaptureDlg::QueueSyncedPlayerLibrary(', 'void CDNFGameCaptureDlg::QueuePlayerIdentityCommand(');
  assert.match(queue, /DnfBuildSyncedPlayerLibrary/);
  assert.match(queue, /m_playerLibraryStore->ImportV2\(/);
  assert.match(queue, /m_matchLibraryRequestId/);
  assert.doesNotMatch(queue, /SubmitLegacy|SaveAliasDB\(|\.get\(\)|\.join\(/);
});
test('association opening can repair a missing active name and keeps errors readable', () => {
  const open = section('else if (action == "cmd_set_identity_panel_open")', 'else if (action ==');
  assert.match(open, /FindName/);
  assert.match(open, /QueueSyncedPlayerLibrary/);
  assert.match(web, /action: 'cmd_set_identity_panel_open', open: true, name: identityFocusedName/);
});
test('cloud match uploads contain game aliases and no retired identity evidence', () => {
  const upload = section('std::string CDNFGameCaptureDlg::BuildCloudMatchSnapshotPayload(', 'void CDNFGameCaptureDlg::');
  assert.match(upload, /"aliases"/);
  assert.doesNotMatch(upload, /adventure/i);
});

test('accepted pending writes drain on graceful exit and failures retain transient evidence', () => {
  const pump = section('void CDNFGameCaptureDlg::PumpSyncedPlayerLibrary()', 'void CDNFGameCaptureDlg::QueuePlayerIdentityCommand(');
  assert.doesNotMatch(pump, /m_playerLibraryExitPending/);
  assert.match(pump, /m_matchLibraryQueue\.Complete\(result\.ok\)/);
  assert.match(pump, /if \(result\.ok\) \{\s*std::lock_guard[\s\S]*?m_matchLibraryLatestEvidence = json::object\(\)/);
  const poll = section('void CDNFGameCaptureDlg::PollPlayerLibrary()', 'bool CDNFGameCaptureDlg::QueuePlayerLibrarySave(');
  assert.match(poll, /m_playerLibraryRequests\.empty\(\) && !m_matchLibraryQueue\.HasPending\(\)/);
  assert.match(poll, /result\.requestId != m_matchLibraryRequestId && ApplyPlayerLibraryDelta/);
});

test('sync origin check and payload capture share the data lock', () => {
  const queue = section('void CDNFGameCaptureDlg::QueueCloudMatchSyncedUpload(', 'void CDNFGameCaptureDlg::RequestUnifiedCloudDirectory()');
  const lock = queue.indexOf('lock(m_dataMutex)');
  const guard = queue.indexOf('if (expectedEpoch');
  const build = queue.indexOf('BuildTeamSyncSnapshotPayloadUnlocked()');
  assert(lock >= 0 && guard > lock && build > guard);
  assert.doesNotMatch(queue, /BuildTeamSyncSnapshotPayload\(\)/);
  const apply = section('bool CDNFGameCaptureDlg::ApplyTeamSyncSnapshot(', 'bool CDNFGameCaptureDlg::RefreshAfterTeamSyncApply()');
  assert.match(apply, /MarkMatchMutation\(resolvedHistoryLabel, resolvedHistorySource\);\s*if \(appliedEpoch\) \*appliedEpoch = m_matchMutationEpoch\.load/);
  const once = section('else if (action == "cmd_cloud_sync_broadcaster")', 'else if (action == "cmd_cloud_realtime_start")');
  assert.match(once, /QueueCloudMatchSyncedUpload\(targetDeviceId, revision, &appliedEpoch\)/);
  assert.match(once, /QueueSyncedPlayerLibrary\(snapshot, targetDeviceId, revision, true, appliedEpoch\)/);
  assert.match(section('bool CDNFGameCaptureDlg::QueueSyncedPlayerLibrary(', 'void CDNFGameCaptureDlg::PumpSyncedPlayerLibrary()'), /IsOutstanding\(payload\) \? payload : json::object\(\)/);
});
