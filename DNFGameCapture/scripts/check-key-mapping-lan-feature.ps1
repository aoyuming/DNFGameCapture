$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)

function Read-RequiredFile([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $relativePath"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Read-WebFile([string]$name) {
    $path = Join-Path $webRoot $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required web file: $name"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if (-not $content.Contains($needle)) {
        throw $message
    }
}

function Reject-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.Contains($needle)) {
        throw $message
    }
}

$dialogHeader = Read-RequiredFile 'DNFGameCaptureDlg.h'
$dialogSource = Read-RequiredFile 'DNFGameCaptureDlg.cpp'
$lanHeader = Read-RequiredFile 'KeyMappingLanService.h'
$lanSource = Read-RequiredFile 'KeyMappingLanService.cpp'
$index = Read-WebFile 'index.html'
$main = Read-WebFile 'main.js'
$style = Read-WebFile 'style.css'
$project = Read-RequiredFile 'DNFGameCapture.vcxproj'
$filters = Read-RequiredFile 'DNFGameCapture.vcxproj.filters'

Require-Text $lanHeader 'class KeyMappingLanService' 'Independent key mapping LAN service class is missing.'
Require-Text $lanHeader 'enum class KeyMappingLanRole' 'LAN roles are not strongly typed.'
Require-Text $lanHeader 'standalone' 'Standalone role is missing.'
Require-Text $lanHeader 'server' 'Server role is missing.'
Require-Text $lanHeader 'client' 'Client role is missing.'
Require-Text $lanHeader 'std::atomic<unsigned int>' 'LAN active mask is not atomic.'
Require-Text $lanHeader 'GetStatusSnapshot' 'Thread-safe LAN status snapshot is missing.'
Require-Text $lanSource 'SOCK_STREAM' 'TCP state channel is missing.'
Require-Text $lanSource 'SOCK_DGRAM' 'UDP discovery channel is missing.'
Require-Text $lanSource 'TCP_NODELAY' 'TCP_NODELAY is not enabled.'
Require-Text $lanSource 'htonl' 'TCP frames are not length-prefixed in network byte order.'
Require-Text $lanSource 'ntohl' 'TCP frame lengths are not decoded from network byte order.'
Require-Text $lanSource '65536' 'The 64 KB team-state snapshot payload limit is missing.'
Require-Text $lanSource '0x3FFF' 'The 14-bit active mask is not validated.'
Require-Text $lanSource '250' 'The 250 ms full-state heartbeat is missing.'
Require-Text $lanSource '1000' 'The one-second server heartbeat timeout is missing.'
Require-Text $lanSource '2000' 'The two-second client reconnect interval is missing.'
foreach ($type in @('hello', 'accepted', 'rejected', 'state', 'ping', 'pong',
    'team_sync_request', 'team_sync_snapshot', 'team_sync_error',
    'team_sync_subscribe', 'team_sync_push', 'team_sync_write_policy',
    'team_sync_propose', 'team_sync_propose_result')) {
    Require-Text $lanSource ('"' + $type + '"') "LAN protocol message '$type' is missing."
}
Require-Text $lanHeader 'SetTeamSyncSubscribed' 'Client team-state subscription API is missing.'
Require-Text $lanHeader 'teamSyncPushSupported' 'Push capability status is missing.'
Require-Text $lanHeader 'teamSyncSubscribed' 'Subscription status is missing.'
Require-Text $lanSource 'team_sync_push_v1' 'Push capability is not advertised or detected.'
Require-Text $lanSource 'team_sync_bidirectional_v1' 'Bidirectional team sync capability is missing.'
Require-Text $lanHeader 'SetTeamSyncClientWriteAllowed' 'Server write permission API is missing.'
Require-Text $lanHeader 'SetTeamSyncAutoSend' 'Client automatic upload API is missing.'
Require-Text $lanHeader 'SetRemoteTeamSyncSnapshot' 'Remote snapshot echo suppression API is missing.'
Require-Text $lanSource 'kTeamSyncDebounceMs' 'Server snapshot push debounce is missing.'
Require-Text $lanSource 'revision' 'Team-state push revision is missing.'

Require-Text $dialogHeader '#include "KeyMappingLanService.h"' 'Main dialog does not include the LAN service.'
Require-Text $dialogHeader 'm_keyMappingLocalMask' 'Local key mask is not separated from the effective mask.'
Require-Text $dialogHeader 'm_keyMappingLanService' 'Main dialog does not own the LAN service.'
Require-Text $dialogSource 'KeyMappingLan' 'LAN settings are not persisted in config.ini.'
Require-Text $dialogSource 'IsRunningAsAdmin()' 'Admin privilege is not checked before local capture.'
Require-Text $dialogSource 'RelaunchAsAdmin()' 'Admin relaunch path is missing.'
Require-Text $dialogSource 'key_mapping_admin_required' 'Web admin restart prompt is missing.'
Require-Text $dialogSource 'm_keyMappingLanRole == KeyMappingLanRole::server' 'Server effective-mask behavior is missing.'
Require-Text $dialogSource 'OpenKeyDisplayWindow();' 'Server does not open the KEY display after pairing.'

foreach ($command in @(
    'cmd_set_key_lan_role',
    'cmd_start_key_lan_server',
    'cmd_stop_key_lan_server',
    'cmd_discover_key_lan_servers',
    'cmd_connect_key_lan',
    'cmd_disconnect_key_lan',
    'cmd_regenerate_key_pair_code',
    'cmd_restart_as_admin',
    'cmd_request_team_sync',
    'cmd_apply_team_sync',
    'cmd_cancel_team_sync',
    'cmd_undo_team_sync',
    'cmd_set_team_sync_auto_receive',
    'cmd_set_team_sync_allow_client_write',
    'cmd_set_team_sync_auto_send'
)) {
    Require-Text $dialogSource $command "C++ command '$command' is missing."
    Require-Text $main $command "Web command '$command' is missing."
}

Require-Text $index 'id="key-lan-role"' 'LAN role selector is missing.'
Require-Text $index 'id="key-lan-server-panel"' 'Server controls are missing.'
Require-Text $index 'id="key-lan-client-panel"' 'Client controls are missing.'
Require-Text $index 'id="btn-key-lan-discover"' 'Server discovery control is missing.'
Require-Text $index 'id="key-lan-server-address"' 'Manual server address input is missing.'
Require-Text $index 'id="key-lan-pair-code"' 'Pair code input is missing.'
Require-Text $index 'id="btn-key-team-sync"' 'Manual team-state sync control is missing.'
Require-Text $index 'id="btn-key-team-undo-sync"' 'Team-state sync undo control is missing.'
Require-Text $index 'id="btn-key-lan"' 'Independent LAN sync menu entry is missing.'
Require-Text $index 'id="key-lan-overlay"' 'Independent LAN sync dialog is missing.'
Require-Text $index 'id="team-sync-auto-receive"' 'Automatic team-state receive control is missing.'
Require-Text $index 'id="team-sync-allow-client-write"' 'Server client-write permission is missing.'
Require-Text $index 'id="team-sync-auto-send"' 'Client automatic upload control is missing.'
$mappingStart = $index.IndexOf('id="key-mapping-overlay"')
$lanStart = $index.IndexOf('id="key-lan-overlay"')
if ($mappingStart -lt 0 -or $lanStart -lt 0 -or $lanStart -le $mappingStart) {
    throw 'Key mapping and LAN sync dialogs are not ordered as independent overlays.'
}
$mappingMarkup = $index.Substring($mappingStart, $lanStart - $mappingStart)
Reject-Text $mappingMarkup 'id="key-lan-role"' 'LAN connection controls still live inside the local key mapping dialog.'
Require-Text $main 'key_mapping_admin_required' 'Web does not handle the admin restart prompt.'
Require-Text $main "okText: '" 'Team-state sync diff confirmation is missing.'
Require-Text $main 'cmd_apply_team_sync' 'Team-state sync confirmation command is missing.'
Require-Text $dialogSource 'BuildTeamSyncSnapshotPayload' 'C++ team-state snapshot builder is missing.'
Require-Text $dialogSource 'ApplyTeamSyncSnapshot' 'C++ team-state apply path is missing.'
Require-Text $dialogSource 'm_teamSyncBackupSnapshot' 'C++ team-state undo backup is missing.'
Require-Text $dialogHeader 'm_teamSyncLocalBaselineSnapshot' 'C++ local baseline snapshot is missing.'
Require-Text $dialogHeader 'm_teamSyncAppliedSnapshot' 'C++ applied snapshot guard is missing.'
Require-Text $dialogHeader 'm_teamSyncEventBoundaryId' 'C++ recent-event sync boundary is missing.'
Require-Text $dialogHeader 'AddReviewEventUnlocked' 'Recent-event locked/unlocked helpers are missing.'
Require-Text $dialogHeader 'BuildTeamSyncSnapshotPayloadUnlocked' 'C++ locked snapshot builder split is missing.'
Require-Text $dialogSource 'SetTeamSyncSnapshot' 'The UI thread does not refresh the LAN snapshot cache.'
Require-Text $dialogSource 'localBaseline' 'The validated local baseline is not sent to Web.'
Require-Text $dialogSource 'currentSnapshot != m_teamSyncLocalBaselineSnapshot' 'Snapshot baseline comparison is not atomic with apply.'
Require-Text $dialogSource 'currentSnapshot != m_teamSyncAppliedSnapshot' 'Undo can overwrite match changes made after sync.'
Require-Text $dialogSource 'm_keyMappingLanWasConnected && !status.connected' 'Disconnect does not invalidate the pending team-state preview.'
Require-Text $dialogSource 'applyLanStatus.connected' 'Apply does not recheck the client connection.'
Require-Text $dialogSource '!snapshot.contains("lastKillerTeam")' 'lastKillerTeam is not a required snapshot field.'
Require-Text $main 'pushStateToServer();' 'Web does not flush current edits before applying a snapshot.'
Require-Text $main 'cmd_cancel_team_sync' 'Web cannot cancel a pending team-state preview.'
if ($main -match '(?s)showConfirm\(buildTeamSyncDiffHtml.*?if \(ok\) \{\s*pushStateToServer\(\)') {
    throw 'Web confirmation must not overwrite newer C++ OCR state before baseline validation.'
}
foreach ($field in @('outputSeatLabelToKillFile', 'lastKillerTeam', 'currentStreak')) {
    Require-Text $main $field "Team-state diff preview is missing field: $field"
}
Require-Text $style '.key-lan-panel' 'LAN panel styles are missing.'
Require-Text $style '.key-team-sync-panel' 'Team-state sync panel styles are missing.'
Require-Text $style '.key-lan-overlay' 'Independent LAN dialog styles are missing.'
Require-Text $dialogSource 'AutoReceiveTeamSync' 'Automatic team-state receive preference is not persisted.'
Require-Text $dialogSource 'AllowClientTeamSyncWrite' 'Server client-write permission is not persisted.'
Require-Text $dialogSource 'AutoSendTeamSync' 'Client automatic upload preference is not persisted.'
Require-Text $dialogHeader 'm_teamSyncAutoReceive' 'Automatic receive state is missing from the main dialog.'
Require-Text $dialogHeader 'm_teamSyncAllowClientWrite' 'Server client-write permission state is missing.'
Require-Text $dialogHeader 'm_teamSyncAutoSend' 'Client automatic upload state is missing.'
Require-Text $dialogSource 'team_sync_push' 'C++ does not apply pushed team-state snapshots.'

Require-Text $project '<ClCompile Include="KeyMappingLanService.cpp" />' 'LAN service source is not in the project.'
Require-Text $project '<ClInclude Include="KeyMappingLanService.h" />' 'LAN service header is not in the project.'
Require-Text $filters 'KeyMappingLanService.cpp' 'LAN service source is not in project filters.'
Require-Text $filters 'KeyMappingLanService.h' 'LAN service header is not in project filters.'

Require-Text $dialogSource '127.0.0.1' 'The local display service loopback binding marker is missing.'
Reject-Text $dialogSource 'INADDR_ANY; // 18777' 'The 18777 display service must remain loopback-only.'

Write-Host 'Key mapping LAN static checks passed.' -ForegroundColor Green
