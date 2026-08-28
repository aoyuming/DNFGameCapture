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

$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')
$dialog = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$webDialog = Read-RequiredFile (Join-Path $root 'WebScoreDlg.cpp')
$serverSocket = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\socket.ts')
$relations = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\sync-relations.ts')
$schemas = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\schemas.ts')
$cloudFunction = Read-RequiredFile (Join-Path $root ([string]([char]0x4E91 + [char]0x51FD + [char]0x6570 + '\index.js')))

foreach ($id in @(
    'broadcaster-sidebar',
    'broadcaster-online-list',
    'broadcaster-offline-list',
    'cloud-broadcaster-name-input',
    'broadcaster-preview-overlay',
    'btn-broadcaster-sync-once',
    'btn-broadcaster-sync-realtime',
    'btn-broadcaster-stop-realtime'
)) {
    Require-Text $index ('id="' + $id + '"') "Unified broadcaster element '$id' is missing."
}
$previewWarning = [string]([char]0x4EC5 + [char]0x9884 + [char]0x89C8 + [char]0xFF0C +
    [char]0x4E0D + [char]0x4F1A + [char]0x4FEE + [char]0x6539 + [char]0x672C +
    [char]0x5730 + [char]0x6570 + [char]0x636E)
Require-Text $index $previewWarning 'The preview-only warning is missing.'
foreach ($legacyId in @('cloud-room-overlay', 'cloud-sync-overlay', 'btn-cloud-match')) {
    Reject-Text $index ('id="' + $legacyId + '"') "Legacy cloud room UI '$legacyId' is still visible."
}

foreach ($functionName in @(
    'renderBroadcasterSidebar',
    'renderBroadcasterPreview',
    'openBroadcasterPreview',
    'closeBroadcasterPreview',
    'promptForBroadcasterName'
)) {
    Require-Text $main ("function $functionName(") "Web function '$functionName' is missing."
}
foreach ($command in @(
    'cmd_cloud_refresh_broadcasters',
    'cmd_cloud_preview_broadcaster',
    'cmd_cloud_sync_broadcaster',
    'cmd_cloud_realtime_start',
    'cmd_cloud_realtime_stop',
    'cmd_cloud_rename_broadcaster',
    'cmd_cloud_join_unified'
)) {
    Require-Text $main $command "Web unified command '$command' is missing."
    Require-Text $dialog $command "C++ unified command '$command' is missing."
}
Require-Text $main 'cloud_broadcaster_prompt' 'Web does not handle the broadcaster-name prompt.'
$onlineNameTaken = [string]([char]0x88AB + [char]0x5728 + [char]0x7EBF +
    [char]0x4E3B + [char]0x64AD + [char]0x4F7F + [char]0x7528)
Require-Text $main ("lastError.includes('" + $onlineNameTaken + "')") 'Duplicate online broadcaster names do not reopen the name prompt.'
Require-Text $main 'offlineExpiresAt' 'Web does not render the offline retention countdown.'
Require-Text $main 'realtimeRelations' 'Web does not render realtime sync relations.'
Require-Text $main 'syncHistory' 'Web does not render sync history.'
Reject-Text $main "document.getElementById('btn-cloud-room-join')?.addEventListener" 'Legacy room event bindings remain active.'
Reject-Text $main "document.getElementById('btn-cloud-sync-apply')?.addEventListener" 'Legacy room sync event bindings remain active.'

foreach ($selector in @(
    '.broadcaster-sidebar',
    '.broadcaster-card',
    '.broadcaster-preview-overlay',
    '.broadcaster-preview-dialog',
    '.broadcaster-preview-notice'
)) {
    Require-Text $style $selector "Unified broadcaster style '$selector' is missing."
}

Require-Text $dialog '{ "action", "cloud_broadcaster_prompt" }' 'C++ still sends the room chooser prompt.'
Reject-Text $dialog '{ "action", "cloud_room_prompt" }' 'C++ still sends the legacy room prompt.'
Require-Text $dialog 'WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS' 'The professional cloud status control is not hidden.'
Require-Text $webDialog 'constexpr int kReferenceClientWidth = 980;' 'The Web scoreboard reference width is not 980 CSS px.'
Require-Text $dialog 'm_bOcrStartPending.exchange(false)' 'Starting realtime sync does not cancel a pending local OCR start.'
Require-Text $dialog 'm_cloudMatchTemporaryInstance' 'C++ does not track temporary multi-instance cloud identities.'
Require-Text $dialog 'dnf-tmp-' 'C++ does not generate isolated temporary cloud device IDs.'
Require-Text $dialog 'm_cloudMatchTemporaryInstance = GetLastError() == ERROR_ALREADY_EXISTS;' 'C++ does not classify additional local clients as temporary instances.'

Require-Text $relations 'export function listAllSyncHistory(' 'Server cannot expose global 24-hour sync history.'
Require-Text $serverSocket 'history: listAllSyncHistory(db, now())' 'Broadcaster directory does not include sync history.'
Require-Text $serverSocket 'isTemporaryCloudDeviceId' 'Server does not identify temporary multi-instance devices.'
Require-Text $serverSocket 'deleteTemporaryCloudDevice' 'Server does not purge temporary broadcaster data after disconnect.'
Require-Text $schemas 'recentEvents' 'Cloud snapshots do not expose bounded recent recognition data.'
Require-Text $dialog "text.find('\0')" 'Recent recognition upload does not reject embedded NUL characters.'

Require-Text $cloudFunction 'const CLOUD_MATCH_SERVER_URL' 'The authorization function has no configurable cloud match server URL.'
Require-Text $cloudFunction 'cloudServerUrl: CLOUD_MATCH_SERVER_URL' 'Successful authorization does not return cloudServerUrl.'
Require-Text $dialog 'outCloudServerUrl' 'The C++ authorization bridge does not receive cloudServerUrl.'
Require-Text $dialog 'authSuccess->cloudServerUrl' 'Authorization success does not apply cloudServerUrl on the UI thread.'
Require-Text $dialog 'SaveCloudMatchSettings();' 'The authorized cloud server URL is not persisted.'

Write-Host 'Unified broadcaster pool static checks passed.'
