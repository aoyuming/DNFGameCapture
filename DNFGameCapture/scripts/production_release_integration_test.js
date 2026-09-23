const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');
const source = read('DNFGameCaptureDlg.cpp');
const body = (start, end) => source.slice(source.indexOf(start), source.indexOf(end, source.indexOf(start) + start.length));

test('5.4.0 release metadata and client package stay aligned', () => {
  assert.match(read('DNFGameCaptureDlg.h'), /CURRENT_VERSION L"5\.4\.0"/);
  const resource = fs.readFileSync(path.join(root, 'DNFGameCapture.rc'), 'utf16le');
  assert.match(resource, /FILEVERSION 5,4,0,0/);
  assert.match(resource, /PRODUCTVERSION 5,4,0,0/);
  assert.match(resource, /VALUE "FileVersion", "5\.4\.0\.0"/);
  assert.match(resource, /VALUE "ProductVersion", "5\.4\.0\.0"/);
  assert.match(resource, /5\.4\.0",IDC_STATIC/);
  const packageClient = read('scripts/package-production-client.ps1');
  assert.match(packageClient, /deployment-packages\\update_v540\.zip/);
  assert.match(packageClient, /Expected EXE version 5\.4\.0\.0/);
  assert.match(packageClient, /Get-FileHash[\s\S]*Release web file does not match source/);
  assert.match(read('scripts/DNFGameCapture-5.0.2.iss'), /AppVersion "5\.4\.0"/);
});

test('5.2.2 release wiring uses compiled environment, not legacy opt-in', () => {
  const settings = body('void CDNFGameCaptureDlg::LoadCloudMatchSettings()', 'bool CDNFGameCaptureDlg::SaveCloudMatchSettings()');
  assert.match(settings, /m_cloudServerAuthV2 = true;/);
  assert.match(settings, /dnf::cloud_release::CurrentManifestUrl\(\)/);
  assert.match(settings, /dnf::cloud_release::CanUseCachedEndpoint\(/);
  assert.match(read('DNFGameCapture.vcxproj'), /DNF_CLOUD_TEST_BUILD=1/);
});

test('upgrade source is recorded before cloud settings can change', () => {
  const settings = body('void CDNFGameCaptureDlg::LoadCloudMatchSettings()', 'bool CDNFGameCaptureDlg::SaveCloudMatchSettings()');
  assert.match(settings, /L"Upgrade520", L"PriorPlayerLibrary"/);
  assert.ok(settings.indexOf('L"Upgrade520", L"PriorPlayerLibrary"') < settings.indexOf('SaveCloudMatchSettings();'));
  const save = body('bool CDNFGameCaptureDlg::SaveCloudMatchSettingsForRoomIdentity(', 'bool CDNFGameCaptureDlg::SaveCloudMatchRevision()');
  assert.match(save, /if \(!m_cloudReleaseSettingsReady\) return false;/);
  const library = body('void CDNFGameCaptureDlg::StartPlayerLibrary()', 'void CDNFGameCaptureDlg::PublishPlayerLibrary(');
  assert.match(library, /options\.priorDatabasePath/);
  assert.match(library, /dnf::cloud_release::CurrentEnvironment\(\)/);
  assert.match(library, /m_priorPlayerLibrary == L"test"/);
});

test('leases are scoped before activation and after online validation', () => {
  const activation = body('bool CDNFGameCaptureDlg::TryActivateFromLicenseLease', 'bool CDNFGameCaptureDlg::BeginLicenseLeaseEndpointRefresh');
  assert.ok(activation.indexOf('LeaseMatchesRelease(lease)') > 0);
  assert.ok(activation.indexOf('LeaseMatchesRelease(lease)') < activation.indexOf('m_bIsAuthValid = true;'));
  const refresh = body('bool CDNFGameCaptureDlg::BeginLicenseLeaseEndpointRefresh', 'bool CDNFGameCaptureDlg::BeginLicenseCloudCheck');
  assert.match(refresh, /LeaseMatchesRelease\(lease\)/);
  const success = body('LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime', 'bool CDNFGameCaptureDlg::VerifyKey');
  assert.match(success, /refreshedLease\.environment = dnf::cloud_release::CurrentEnvironment\(\)/);
  assert.match(success, /refreshedLease\.endpointManifestUrl = dnf::cloud_release::CurrentManifestUrl\(\)/);
});

test('old server and network failures are not mislabeled as invalid permanent cards', () => {
  assert.match(source, /status == 404 \|\| status == 405/);
  const fail = body('LRESULT CDNFGameCaptureDlg::OnCloudAuthFail', 'LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime');
  assert.doesNotMatch(fail, /卡密无效或已过期/);
  assert.match(fail, /!authFailure->serviceFailure/);
  assert.match(source, /dnf::cloud_release::ValidateManifest\(/);
});

test('automatic registration restores the saved broadcaster name before joining', () => {
  const start = body('void CDNFGameCaptureDlg::StartSavedCloudMatchSession()', 'void CDNFGameCaptureDlg::BeginCloudDeviceRegistration()');
  assert.match(start, /m_cloudMatchPendingBroadcasterName = m_cloudMatchBroadcasterName;/);
  assert.match(start, /m_cloudMatchPendingRoomId = m_cloudMatchRoomId;/);
  assert.ok(start.indexOf('m_cloudMatchPendingBroadcasterName =') < start.indexOf('BeginCloudDeviceRegistration();'));
});

test('interrupted settings migration cannot mistake a legacy production token for a test token', () => {
  const settings = body('void CDNFGameCaptureDlg::LoadCloudMatchSettings()', 'bool CDNFGameCaptureDlg::SaveCloudMatchSettings()');
  assert.match(settings, /const bool environmentChanged = previousEnvironment\.IsEmpty\(\) \?\s*\(production \? m_priorPlayerLibrary == L"test" : !previousTest\)/);
});

test('5.2.2 release notes keep deployment manual and record measured package evidence', () => {
  const notes = read('docs/release-5.2.2.md');
  for (const marker of ['源头预防', '部分冲突', '名称确认', '持久重定向', 'server-first',
    '不自动部署', '不上传 OSS', '不发布更新清单', '不执行真实冲突批次',
    'update_v522\\.zip', 'dnf-cloud-match-server-production-5\\.2\\.2\\.zip',
    '364/364', '91/91', '5\\.2\\.2\\.0', '43 allowlisted entries', '14 allowlisted runtime entries']) {
    assert.match(notes, new RegExp(marker));
  }
  assert.doesNotMatch(notes, /_TBD\b/, 'final release evidence must not retain placeholders');
  const hashes = [...notes.matchAll(/SHA256:\s*([A-Fa-f0-9]{64})\b/g)].map(match => match[1]);
  assert.equal(hashes.length, 3, 'client ZIP, server ZIP and client EXE hashes must be recorded');
  assert.equal(new Set(hashes).size, 3, 'each recorded artifact must have its own measured hash');
});
