$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)

function Read-RequiredFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing required file: $path" }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

function Reject-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        throw $message
    }
}

$header = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.h')
$source = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$syncSource = Read-RequiredFile (Join-Path $root 'CloudMatchSync.cpp')
$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')
$socket = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\socket.ts')
$unified = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\unified.ts')
$relations = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\sync-relations.ts')
$cloudFunctionPath = Join-Path $root ([string]([char]0x4E91 + [char]0x51FD + [char]0x6570 + '\index.js'))
$cloudFunction = Read-RequiredFile $cloudFunctionPath

Require-Text $header '#include "CloudMatchClient.h"' 'Main dialog does not include CloudMatchClient.'
Require-Text $header 'CloudMatchClient m_cloudMatchClient' 'Main dialog does not own CloudMatchClient.'
Require-Text $source 'L"CloudMatch"' 'The [CloudMatch] config section is missing.'
foreach ($field in @('ServerUrl', 'DeviceId', 'DeviceToken', 'RoomId',
    'BroadcasterName', 'ClientRevision')) {
    Require-Text $source ('L"' + $field + '"') "CloudMatch config field '$field' is missing."
}
Require-Text $source 'http://47.109.149.111:18880' 'Public fallback server URL is missing.'
Require-Text $cloudFunction 'cloudServerUrl: CLOUD_MATCH_SERVER_URL' 'Authorization does not supply cloudServerUrl.'
Require-Text $source 'authSuccess->cloudServerUrl' 'C++ does not apply the authorized server URL.'

foreach ($id in @(
    'broadcaster-sidebar', 'broadcaster-online-list', 'broadcaster-offline-list',
    'cloud-broadcaster-name-input', 'broadcaster-preview-overlay',
    'btn-broadcaster-sync-once', 'btn-broadcaster-sync-realtime',
    'btn-broadcaster-stop-realtime'
)) {
    Require-Text $index ('id="' + $id + '"') "Unified broadcaster UI '$id' is missing."
}
foreach ($legacyId in @(
    'btn-cloud-match', 'cloud-room-status', 'cloud-room-overlay',
    'cloud-room-server-input', 'cloud-sync-overlay'
)) {
    Reject-Text $index ('id="' + $legacyId + '"') "Legacy cloud room UI '$legacyId' is still visible."
}
foreach ($binding in @(
    "document.getElementById('btn-cloud-room-join')?.addEventListener",
    "document.getElementById('btn-cloud-sync-apply')?.addEventListener",
    "document.getElementById('cloud-room-server-input')?.addEventListener"
)) {
    Reject-Text $main $binding "Legacy cloud room binding remains active: $binding"
}
Require-Text $style '.broadcaster-sidebar' 'Broadcaster sidebar styles are missing.'
Require-Text $style '.broadcaster-preview-dialog' 'Broadcaster preview styles are missing.'
Require-Text $main 'renderBroadcasterSidebar();' 'Unified broadcaster state is not rendered.'
Require-Text $main 'promptForBroadcasterName' 'First-run broadcaster-name prompt is missing.'

foreach ($command in @(
    'cmd_cloud_join_unified', 'cmd_cloud_refresh_broadcasters',
    'cmd_cloud_preview_broadcaster', 'cmd_cloud_sync_broadcaster',
    'cmd_cloud_realtime_start', 'cmd_cloud_realtime_stop',
    'cmd_cloud_rename_broadcaster'
)) {
    Require-Text $source $command "C++ unified command '$command' is missing."
    Require-Text $main $command "Web unified command '$command' is missing."
}
Require-Text $source 'JoinUnifiedPool(' 'The client never joins the unified pool.'
Require-Text $source '{ "action", "cloud_broadcaster_prompt" }' 'The first-run prompt still uses the room chooser.'
Require-Text $source 'm_cloudMatchUploadDueTick = ::GetTickCount64() + 400;' 'Cloud snapshot debounce is missing.'
Require-Text $source 'DispatchMessages(32)' 'Cloud messages are not dispatched on the UI timer.'
Require-Text $source 'BuildTeamSyncSnapshotPayloadUnlocked()' 'Cloud snapshots do not reuse the team snapshot.'
Require-Text $source 'm_cloudMatchUploadInFlight' 'Cloud snapshot ACK serialization is missing.'
Require-Text $source 'm_cloudMatchUploadRetryBlocked' 'Permanent snapshot failures are not gated.'
Require-Text $source 'm_cloudMatchPendingChangeSource = "cloud_sync"' 'Synchronized snapshots are not marked cloud_sync.'

Require-Text $source 'DnfIsCloudRealtimeBlockedWebAction' 'Realtime following does not block local Web mutations.'
Require-Text $source 'if (m_bIsRunning) OnBnClickedStart();' 'Starting realtime sync does not stop local OCR.'
Require-Text $source 'm_cloudRealtimeFollowing = true;' 'Realtime following is not locked before the first snapshot arrives.'
Require-Text $source 'realtime_start_result' 'Realtime start failures cannot release the local edit lock.'
Require-Text $header 'm_cloudRealtimeHeartbeatDueTick' 'Realtime sync has no heartbeat schedule.'
Require-Text $source 'm_cloudMatchClient.HeartbeatRealtimeSync(' 'Realtime sync never sends heartbeats.'
Require-Text $source 'if (disconnectedNow && m_cloudRealtimeFollowing)' 'Cloud disconnect does not release the realtime edit lock.'
Require-Text $source 'm_teamSyncLocalBaselineSnapshot = json::parse(' 'Unified preview does not establish a local one-time-sync baseline.'
Require-Text $source 'localAliasesByMainName' 'One-time sync does not merge aliases from matching local main names.'
Require-Text $source 'm_teamSyncAppliedSnapshot = std::move(appliedSnapshot);' 'Undo safety does not track the actual merged result.'

Require-Text $socket 'ALL_BROADCASTERS_ROOM_ID' 'Server unified pool is missing.'
Require-Text $unified 'BROADCASTER_RETENTION_SECONDS' 'Offline broadcaster retention is missing.'
Require-Text $relations 'SYNC_RETENTION_SECONDS' 'Sync-history retention is missing.'
Require-Text $socket "'broadcasters:list'" 'Broadcaster directory event is missing.'
Require-Text $socket "'sync:realtime:start'" 'Realtime start event is missing.'
Require-Text $socket "'sync:record'" 'Successful sync record event is missing.'

foreach ($webContent in @($index, $main)) {
    foreach ($secret in @('DeviceToken', 'deviceToken')) {
        Reject-Text $webContent $secret 'DeviceToken must not be exposed to Web files.'
    }
}
$webStateStart = $source.IndexOf('json CDNFGameCaptureDlg::DnfBuildSharedWebStateJson()')
$webStateEnd = $source.IndexOf('std::string CDNFGameCaptureDlg::BuildKillDisplayStatePayload()', $webStateStart)
if ($webStateStart -lt 0 -or $webStateEnd -le $webStateStart) {
    throw 'Unable to inspect shared Web state.'
}
$webState = $source.Substring($webStateStart, $webStateEnd - $webStateStart)
Reject-Text $webState 'deviceToken' 'Cloud credentials are exposed through shared Web state.'

$snapshotStart = $source.IndexOf('std::string CDNFGameCaptureDlg::BuildCloudMatchSnapshotPayload(')
$snapshotEnd = $source.IndexOf('void CDNFGameCaptureDlg::OnMatchStateChanged(', $snapshotStart)
if ($snapshotStart -lt 0 -or $snapshotEnd -le $snapshotStart) {
    throw 'Unable to inspect cloud snapshot builder.'
}
$snapshotBuilder = $source.Substring($snapshotStart, $snapshotEnd - $snapshotStart)
foreach ($secret in @('deviceToken', 'authorizationCode', 'screenshot', 'imagePath')) {
    Reject-Text $snapshotBuilder $secret "Cloud snapshot contains forbidden data: $secret"
}
foreach ($field in @('schemaVersion', 'clientRevision', 'changeSource', 'recentEvents')) {
    Require-Text $snapshotBuilder $field "Cloud snapshot field '$field' is missing."
}
Require-Text $snapshotBuilder "text.find('\0')" 'Recent recognition text does not reject embedded NUL.'
Require-Text $syncSource 'ValidateRecentEvents' 'Downloaded recent recognition data is not validated.'

foreach ($match in [regex]::Matches($source,
    '(?s)SetMessageCallback\s*\(\s*\[this\]\s*\([^)]*\)\s*\{(?<body>.*?)\}\s*\);')) {
    if ($match.Groups['body'].Value.Contains('ApplyTeamSyncSnapshot')) {
        throw 'CloudMatch worker callbacks must not mutate match data directly.'
    }
}

foreach ($line in ($source -split "`r?`n")) {
    if (($line.Contains('DeviceToken') -or $line.Contains('deviceToken')) -and
        $line -match 'AppLog|WriteMatchLog|MessageBox') {
        throw 'DeviceToken must not be written to logs or message boxes.'
    }
}

Write-Host 'Unified cloud match integration static checks passed.' -ForegroundColor Green
