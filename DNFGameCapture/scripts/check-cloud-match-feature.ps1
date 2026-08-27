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
    'cmd_cloud_room_cancel_join',
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
Require-Text $header 'm_cloudMatchUploadDirty' 'Cloud snapshot dirty state is missing.'
Require-Text $header 'm_cloudMatchUploadInFlight' 'Cloud snapshot in-flight state is missing.'
Require-Text $header 'm_cloudMatchUploadRetryBlocked' 'Cloud snapshot permanent-failure retry gate is missing.'
Require-Text $header 'm_cloudMatchInFlightPayload' 'Cloud snapshot sent payload tracking is missing.'
Require-Text $header 'm_cloudMatchInFlightRevision' 'Cloud snapshot sent revision tracking is missing.'
Require-Text $header 'm_cloudMatchJoinDeadlineTick' 'Cloud room join deadline state is missing.'
Require-Text $header 'bool SaveCloudMatchSettings()' 'Cloud match settings persistence must report failure.'
Require-Text $header 'bool SaveCloudMatchRevision()' 'Cloud snapshot revision needs dedicated persistence.'
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
Require-Text $index 'id="btn-cloud-room-cancel-join"' 'The first-run join flow has no cancel button.'
Require-Text $main 'cmd_cloud_room_cancel_join' 'The Web join cancel command is missing.'

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

$joinStart = $source.IndexOf('void CDNFGameCaptureDlg::BeginCloudRoomJoin(')
$joinEnd = $source.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchMessage(', $joinStart)
if ($joinStart -lt 0 -or $joinEnd -le $joinStart) {
    throw 'BeginCloudRoomJoin could not be inspected.'
}
$joinBody = $source.Substring($joinStart, $joinEnd - $joinStart)
Require-Text $joinBody 'm_cloudMatchClient.Configure(' 'Every cloud room join must rebuild the client generation.'
if ($joinBody.Contains('if (!status.configured)') -or
    $joinBody.Contains('GetStatusSnapshot()')) {
    throw 'BeginCloudRoomJoin must unconditionally reconfigure the client for a new generation.'
}

$cancelStart = $source.IndexOf('void CDNFGameCaptureDlg::CancelCloudRoomJoin(')
$cancelEnd = $source.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchMessage(', $cancelStart)
if ($cancelStart -lt 0 -or $cancelEnd -le $cancelStart) {
    throw 'CancelCloudRoomJoin could not be inspected.'
}
$cancelBody = $source.Substring($cancelStart, $cancelEnd - $cancelStart)
Require-Text $cancelBody 'm_cloudMatchClient.Configure(' 'Canceling a join must invalidate the active client generation.'
Require-Text $cancelBody 'm_cloudMatchJoining = false;' 'Canceling a join must clear joining state.'
Require-Text $cancelBody 'm_cloudMatchRegistering = false;' 'Canceling a join must clear registration state.'

$uploadHandlerStart = $source.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchSnapshotUploadResult(')
$uploadHandlerEnd = $source.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchMessage(', $uploadHandlerStart)
if ($uploadHandlerStart -lt 0 -or $uploadHandlerEnd -le $uploadHandlerStart) {
    throw 'Cloud snapshot upload ACK handler could not be inspected.'
}
$uploadHandler = $source.Substring($uploadHandlerStart, $uploadHandlerEnd - $uploadHandlerStart)
Require-Text $uploadHandler 'acceptedRevision' 'Snapshot success ACK must validate acceptedRevision.'
Require-Text $uploadHandler 'm_cloudMatchInFlightRevision' 'Snapshot ACK must correlate the in-flight revision.'
Require-Text $uploadHandler 'm_cloudMatchPendingPayload == m_cloudMatchInFlightPayload' 'Snapshot ACK must only clear matching dirty content.'
Require-Text $uploadHandler 'm_cloudMatchUploadDirty = false;' 'Snapshot dirty state is not cleared after a matching success ACK.'
Require-Text $uploadHandler 'DnfIsTransientCloudSnapshotFailure' 'Transient snapshot failures are not classified for retry.'
Require-Text $uploadHandler 'm_cloudMatchUploadDueTick' 'Transient snapshot failures are not rescheduled.'
$invalidAckStart = $uploadHandler.IndexOf('if (acceptedRevision == 0')
$invalidAckReturn = $uploadHandler.IndexOf('return;', $invalidAckStart)
if ($invalidAckStart -lt 0 -or $invalidAckReturn -le $invalidAckStart) {
    throw 'Invalid snapshot success ACK handling could not be inspected.'
}
$invalidAckBody = $uploadHandler.Substring($invalidAckStart,
    $invalidAckReturn - $invalidAckStart)
if ($invalidAckBody.Contains('m_cloudMatchUploadDirty = false;')) {
    throw 'An invalid snapshot success ACK must not clear dirty state.'
}
Require-Text $invalidAckBody 'm_cloudMatchUploadRetryBlocked = true;' 'Invalid snapshot success ACKs must block retry without pretending the snapshot was acknowledged.'
$configureOffset = $joinBody.IndexOf('m_cloudMatchClient.Configure(')
$startOffset = $joinBody.IndexOf('m_cloudMatchClient.Start()', $configureOffset)
$joinOffset = $joinBody.IndexOf('m_cloudMatchClient.JoinRoom(', $startOffset)
if ($configureOffset -lt 0 -or $startOffset -le $configureOffset -or
    $joinOffset -le $startOffset) {
    throw 'BeginCloudRoomJoin must Configure, Start, then JoinRoom in that order.'
}

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
Require-Text $snapshotBuilder 'cloud["changeSource"] = "manual";' 'Unknown cloud snapshot sources must fall back to manual.'
if ($snapshotBuilder.Contains('cloud["changeSource"] = "ocr";')) {
    throw 'Cloud snapshot builder must not classify an unknown source as OCR.'
}
Require-Text $snapshotBuilder 'DnfNormalizeCloudSnapshotName' 'Cloud snapshot player names are not normalized and prevalidated.'
Require-Text $snapshotBuilder 'if (aliasUtf8 == mainUtf8)' 'Cloud snapshot aliases equal to the NFC-normalized main name are not rejected explicitly.'
$nameValidatorStart = $source.IndexOf('static bool DnfNormalizeCloudSnapshotName(')
$nameValidatorEnd = $source.IndexOf('static bool DnfIsTransientCloudSnapshotFailure(', $nameValidatorStart)
if ($nameValidatorStart -lt 0 -or $nameValidatorEnd -le $nameValidatorStart) {
    throw 'Cloud snapshot player-name validator could not be inspected.'
}
$nameValidator = $source.Substring($nameValidatorStart,
    $nameValidatorEnd - $nameValidatorStart)
Require-Text $nameValidator 'NormalizeString(NormalizationC' 'Cloud snapshot names are not normalized to NFC.'
Require-Text $nameValidator 'scalarCount > 64' 'Cloud snapshot names are not bounded to 64 Unicode scalars.'
Require-Text $nameValidator 'DnfIsCloudMatchInvisibleCodePoint' 'Cloud snapshot names do not reject control and invisible characters.'

$pollStart = $source.IndexOf('void CDNFGameCaptureDlg::PollCloudMatch(')
$pollEnd = $source.IndexOf('void CDNFGameCaptureDlg::SendCloudRoomPromptIfNeeded(', $pollStart)
if ($pollStart -lt 0 -or $pollEnd -le $pollStart) {
    throw 'PollCloudMatch could not be inspected.'
}
$pollBody = $source.Substring($pollStart, $pollEnd - $pollStart)
Require-Text $pollBody 'm_cloudMatchUploadInFlight' 'PollCloudMatch does not enforce one outstanding snapshot ACK.'
Require-Text $pollBody 'SaveCloudMatchRevision()' 'Snapshot uploads do not use dedicated revision persistence.'
Require-Text $pollBody 'm_cloudMatchJoinDeadlineTick' 'PollCloudMatch does not enforce a room join deadline.'
Require-Text $pollBody 'm_cloudMatchUploadRetryBlocked' 'PollCloudMatch does not honor the permanent-failure retry gate.'
if ($pollBody.Contains('SaveCloudMatchSettings()')) {
    throw 'Ordinary snapshot uploads must not rewrite the full CloudMatch identity settings.'
}
$dataLockOffset = $pollBody.IndexOf('std::lock_guard<std::mutex> dataLock(m_dataMutex)')
$snapshotBuildOffset = $pollBody.IndexOf('BuildTeamSyncSnapshotPayloadUnlocked()', $dataLockOffset)
$sourceLockOffset = $pollBody.IndexOf('std::lock_guard<std::mutex> sourceLock(m_cloudMatchSourceMutex)',
    $snapshotBuildOffset)
if ($dataLockOffset -lt 0 -or $snapshotBuildOffset -le $dataLockOffset -or
    $sourceLockOffset -le $snapshotBuildOffset) {
    throw 'OCR cloud source correlation must hold data then source locks around snapshot construction.'
}

$saveSettingsStart = $source.IndexOf('bool CDNFGameCaptureDlg::SaveCloudMatchSettings(')
$saveSettingsEnd = $source.IndexOf('bool CDNFGameCaptureDlg::SaveCloudMatchRevision(', $saveSettingsStart)
if ($saveSettingsStart -lt 0 -or $saveSettingsEnd -le $saveSettingsStart) {
    throw 'SaveCloudMatchSettings could not be inspected.'
}
$saveSettingsBody = $source.Substring($saveSettingsStart, $saveSettingsEnd - $saveSettingsStart)
Require-Text $saveSettingsBody 'WritePrivateProfileString' 'CloudMatch settings are not written to config.ini.'
Require-Text $saveSettingsBody 'SecureZeroMemory' 'CloudMatch token conversion buffer is not securely erased.'
Require-Text $saveSettingsBody 'return saved;' 'CloudMatch settings persistence does not report aggregate write failures.'
if ($saveSettingsBody.Contains('CA2W(m_cloudMatchDeviceToken')) {
    throw 'CloudMatch token persistence must not use an unerasable CA2W temporary.'
}

Require-Text $main 'let cloudRoomJoinTarget = null;' 'The Web room chooser does not track the requested switch target.'
Require-Text $main 'state.roomId === cloudRoomJoinTarget.roomId' 'The Web room chooser does not verify the joined room before resetting.'
if ($main.Contains('if (state.joined && cloudRoomChoosing)')) {
    throw 'The Web room chooser must not reset merely because the client was already joined before switching rooms.'
}

foreach ($line in ($source -split "`r?`n")) {
    if (($line.Contains('DeviceToken') -or $line.Contains('deviceToken')) -and
        $line -match 'AppLog|WriteMatchLog|MessageBox') {
        throw 'DeviceToken must not be written to logs or message boxes.'
    }
}

Write-Host 'Cloud match room integration static checks passed.' -ForegroundColor Green
