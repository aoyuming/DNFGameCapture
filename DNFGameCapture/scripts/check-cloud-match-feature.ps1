$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)

function Read-RequiredFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $path"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

$header = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.h')
$source = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')

Require-Text $header '#include "CloudMatchClient.h"' 'Main dialog does not include CloudMatchClient.'
Require-Text $header 'CloudMatchClient m_cloudMatchClient' 'Main dialog does not own CloudMatchClient.'
Require-Text $source 'L"CloudMatch"' 'The [CloudMatch] config section is missing.'
foreach ($field in @('ServerUrl', 'DeviceId', 'DeviceToken', 'RoomId',
    'BroadcasterName', 'ClientRevision')) {
    Require-Text $source ('L"' + $field + '"') "CloudMatch config field '$field' is missing."
}
Require-Text $source 'http://127.0.0.1:18880' 'Default CloudMatch server URL is missing.'

$roomOptions = @(
    @{ Id = 'li-yong'; Name = [string]([char]0x674E + [char]0x6C38 + [char]0x623F) },
    @{ Id = 'wen-rou'; Name = [string]([char]0x6E29 + [char]0x67D4 + [char]0x623F) },
    @{ Id = '59'; Name = [string]('59' + [char]0x623F) },
    @{ Id = 'none'; Name = [string]([char]0x4E0D + [char]0x52A0 + [char]0x5165 + [char]0x623F + [char]0x95F4) }
)
foreach ($room in $roomOptions) {
    Require-Text $index ('data-cloud-room-id="' + $room.Id + '"') "Cloud room option '$($room.Id)' is missing from index.html."
    Require-Text $index $room.Name "Cloud room label '$($room.Name)' is missing from index.html."
}

Require-Text $source 'DispatchMessages(' 'CloudMatch messages are not dispatched on the UI timer.'
Require-Text $source 'm_cloudMatchUploadDueTick = ::GetTickCount64() + 400;' 'The 400ms cloud snapshot debounce is missing.'
Require-Text $source 'BuildTeamSyncSnapshotPayloadUnlocked()' 'Cloud snapshots do not reuse the team snapshot builder.'
Require-Text $source 'schemaVersion' 'Cloud snapshot schemaVersion is missing.'
Require-Text $source 'clientRevision' 'Cloud snapshot clientRevision is missing.'
Require-Text $source 'clientTime' 'Cloud snapshot clientTime is missing.'
Require-Text $source 'changeSource' 'Cloud snapshot changeSource is missing.'
Require-Text $source 'syncedFrom' 'Cloud snapshot syncedFrom marker is missing.'

foreach ($command in @(
    'cmd_cloud_room_join',
    'cmd_cloud_room_skip_once',
    'cmd_cloud_room_rename',
    'cmd_cloud_room_leave'
)) {
    Require-Text $source $command "C++ cloud command '$command' is missing."
    Require-Text $main $command "Web cloud command '$command' is missing."
}

Require-Text $index 'id="btn-cloud-match"' 'More menu cloud sync entry is missing.'
Require-Text $index 'id="cloud-room-overlay"' 'Cloud room panel is missing.'
Require-Text $index 'id="cloud-room-first-run"' 'First-run cloud room chooser is missing.'
Require-Text $main 'cloud_room_prompt' 'Web does not handle the first-run room prompt.'
Require-Text $main 'Intl.Segmenter' 'Broadcaster name grapheme validation is missing.'
Require-Text $style '.cloud-room-overlay' 'Cloud room panel styles are missing.'

Require-Text $source 'm_cloudMatchSkipPromptThisRun' 'Skip-once state is missing.'
Require-Text $source 'device_already_registered' 'Lost-token device identity retry is missing.'
Require-Text $source 'UploadSnapshot' 'Cloud snapshot upload path is missing.'
Require-Text $source 'if (m_cloudMatchJoining || m_cloudMatchRegistering ||' 'C++ does not reject concurrent cloud room mutations.'
Require-Text $header 'm_cloudMatchExplicitOcrPayload' 'The explicit OCR snapshot payload marker is missing.'
Require-Text $source 'MarkCloudMatchOcrStateChanged(BuildTeamSyncSnapshotPayloadUnlocked());' 'OCR commits do not capture their exact snapshot payload.'
Require-Text $source 'm_cloudMatchExplicitOcrPayload == currentPayload' 'Cloud change source is not correlated with the exact OCR snapshot.'
Require-Text $source 'source && *source ? source : "manual"' 'Missing cloud change sources must fall back to manual.'
Require-Text $header 'm_cloudMatchPendingChangeSource = "manual"' 'Cloud snapshot source must initialize as manual.'
Require-Text $main 'restoreCloudRoomPromptFromState' 'Web state does not restore the first-run room prompt after reload.'
Require-Text $main 'cloudMatchState.shouldPrompt && !isCloudRoomPanelOpen()' 'Web prompt restoration is not guarded against duplicate display.'
Require-Text $main 'backButton.disabled = busy;' 'The room chooser back button is not locked while joining.'
Require-Text $main 'if (isCloudRoomBusy()) return;' 'Cloud room actions are not guarded while a request is busy.'

foreach ($match in [regex]::Matches($source,
    '(?s)SetMessageCallback\s*\(\s*\[this\]\s*\([^)]*\)\s*\{(?<body>.*?)\}\s*\);')) {
    if ($match.Groups['body'].Value.Contains('ApplyTeamSyncSnapshot')) {
        throw 'CloudMatch callback must never apply a remote team snapshot directly.'
    }
}

$handlerStart = $source.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchMessage(')
$handlerEnd = $source.IndexOf('std::string CDNFGameCaptureDlg::BuildCloudMatchSnapshotPayload(', $handlerStart)
if ($handlerStart -lt 0 -or $handlerEnd -le $handlerStart) {
    throw 'HandleCloudMatchMessage could not be inspected.'
}
$handlerBody = $source.Substring($handlerStart, $handlerEnd - $handlerStart)
if ($handlerBody.Contains('ApplyTeamSyncSnapshot')) {
    throw 'HandleCloudMatchMessage must never apply a remote team snapshot.'
}
Require-Text $handlerBody 'event["room"]' 'Join ACK handler does not inspect the accepted room payload.'
Require-Text $handlerBody 'event["broadcasterName"]' 'Join ACK handler does not inspect the accepted broadcaster name.'
Require-Text $handlerBody 'ackRoomId != m_cloudMatchPendingRoomId' 'Join ACK room correlation is missing.'
Require-Text $handlerBody 'DnfCloudMatchNamesMatchForAck' 'Join ACK broadcaster-name correlation is missing.'

$webStateStart = $source.IndexOf('DnfBuildSharedWebStateJson')
if ($webStateStart -ge 0) {
    $webStateEnd = $source.IndexOf('BuildKillDisplayStatePayload', $webStateStart)
    if ($webStateEnd -lt 0) { $webStateEnd = $source.Length }
    $webState = $source.Substring($webStateStart, $webStateEnd - $webStateStart)
    if ($webState.Contains('DeviceToken') -or $webState.Contains('deviceToken')) {
        throw 'DeviceToken must not be exposed through Web state JSON.'
    }
}

foreach ($webContent in @($index, $main)) {
    if ($webContent.Contains('DeviceToken') -or $webContent.Contains('deviceToken')) {
        throw 'DeviceToken must not be exposed to Web files.'
    }
}

$snapshotStart = $source.IndexOf('BuildCloudMatchSnapshotPayload(')
$snapshotEnd = $source.IndexOf('OnMatchStateChanged(', $snapshotStart)
if ($snapshotStart -lt 0 -or $snapshotEnd -le $snapshotStart) {
    throw 'Cloud snapshot builder could not be inspected for token leakage.'
}
$snapshotBuilder = $source.Substring($snapshotStart, $snapshotEnd - $snapshotStart)
if ($snapshotBuilder.Contains('DeviceToken') -or $snapshotBuilder.Contains('deviceToken')) {
    throw 'DeviceToken must not be included in cloud match snapshots.'
}

foreach ($line in ($source -split "`r?`n")) {
    if (($line.Contains('DeviceToken') -or $line.Contains('deviceToken')) -and
        $line -match 'AppLog|WriteMatchLog|MessageBox') {
        throw 'DeviceToken must not be written to logs or message boxes.'
    }
}

Write-Host 'Cloud match room integration static checks passed.' -ForegroundColor Green
