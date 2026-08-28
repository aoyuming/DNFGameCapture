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

function Get-CppFunctionBody([string]$content, [string]$signature) {
    $start = $content.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "C++ function is missing: $signature" }
    $parameterOpen = $content.IndexOf('(', $start)
    if ($parameterOpen -lt 0) { throw "C++ function parameters are missing: $signature" }
    $parameterDepth = 0
    $parameterClose = -1
    $inParameterString = $false
    $inParameterChar = $false
    for ($i = $parameterOpen; $i -lt $content.Length; ++$i) {
        $ch = $content[$i]
        if ($inParameterString -or $inParameterChar) {
            if ($ch -eq [char]0x5C) { ++$i; continue }
            if ($inParameterString -and $ch -eq '"') { $inParameterString = $false }
            elseif ($inParameterChar -and $ch -eq "'") { $inParameterChar = $false }
            continue
        }
        if ($ch -eq '"') { $inParameterString = $true; continue }
        if ($ch -eq "'") { $inParameterChar = $true; continue }
        if ($ch -eq '(') { ++$parameterDepth; continue }
        if ($ch -eq ')') {
            --$parameterDepth
            if ($parameterDepth -eq 0) { $parameterClose = $i; break }
        }
    }
    if ($parameterClose -lt 0) { throw "Unbalanced C++ parameters: $signature" }
    $open = $content.IndexOf('{', $parameterClose)
    if ($open -lt 0) { throw "C++ function body is missing: $signature" }
    $depth = 0
    $inString = $false
    $inChar = $false
    $inLineComment = $false
    $inBlockComment = $false
    for ($i = $open; $i -lt $content.Length; ++$i) {
        $ch = $content[$i]
        $next = if ($i + 1 -lt $content.Length) { $content[$i + 1] } else { [char]0 }
        if ($inLineComment) {
            if ($ch -eq "`n") { $inLineComment = $false }
            continue
        }
        if ($inBlockComment) {
            if ($ch -eq '*' -and $next -eq '/') { $inBlockComment = $false; ++$i }
            continue
        }
        if ($inString -or $inChar) {
            if ($ch -eq [char]0x5C) { ++$i; continue }
            if ($inString -and $ch -eq '"') { $inString = $false }
            elseif ($inChar -and $ch -eq "'") { $inChar = $false }
            continue
        }
        if ($ch -eq '/' -and $next -eq '/') { $inLineComment = $true; ++$i; continue }
        if ($ch -eq '/' -and $next -eq '*') { $inBlockComment = $true; ++$i; continue }
        if ($ch -eq '"') { $inString = $true; continue }
        if ($ch -eq "'") { $inChar = $true; continue }
        if ($ch -eq '{') { ++$depth; continue }
        if ($ch -eq '}') {
            --$depth
            if ($depth -eq 0) { return $content.Substring($start, $i - $start + 1) }
        }
    }
    throw "Unbalanced C++ function body: $signature"
}

$header = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.h')
$source = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')
$clientHeader = Read-RequiredFile (Join-Path $root 'CloudMatchClient.h')
$clientSource = Read-RequiredFile (Join-Path $root 'CloudMatchClient.cpp')
$clientTest = Read-RequiredFile (Join-Path $root 'scripts\cloud_match_client_test.cpp')

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
Require-Text $index 'id="cloud-room-rename-input" type="text" maxlength="512"' 'Cloud room rename input must allow the server 512-byte boundary.'
Require-Text $index 'id="cloud-room-name-input" type="text" maxlength="512"' 'Cloud room join input must allow the server 512-byte boundary.'
Require-Text $main 'cloud_room_prompt' 'Web does not handle the first-run room prompt.'
Require-Text $main 'Intl.Segmenter' 'Broadcaster name grapheme validation is missing.'
Require-Text $style '.cloud-room-overlay' 'Cloud room panel styles are missing.'
Require-Text $source 'trimmed.GetLength() > 512' 'C++ broadcaster input must not reject the server 512-byte boundary by an older UTF-16 limit.'
Require-Text $source 'required > 512' 'C++ broadcaster normalization must align with the server 512-byte boundary.'

Require-Text $source 'm_cloudMatchSkipPromptThisRun' 'Skip-once state is missing.'
Require-Text $source 'device_already_registered' 'Lost-token device identity retry is missing.'
Require-Text $source 'UploadSnapshot' 'Cloud snapshot upload path is missing.'
Require-Text $header 'm_cloudMatchUploadDirty' 'Cloud snapshot dirty state is missing.'
Require-Text $header 'm_cloudMatchUploadInFlight' 'Cloud snapshot in-flight state is missing.'
Require-Text $header 'm_cloudMatchUploadRetryBlocked' 'Cloud snapshot permanent-failure retry gate is missing.'
Require-Text $header 'm_cloudMatchInFlightPayload' 'Cloud snapshot sent payload tracking is missing.'
Require-Text $header 'm_cloudMatchInFlightRevision' 'Cloud snapshot sent revision tracking is missing.'
Require-Text $header 'm_cloudMatchJoinDeadlineTick' 'Cloud room join deadline state is missing.'
Require-Text $header 'm_cloudMatchRoomConfirmed' 'Confirmed cloud room state is missing.'
Require-Text $header 'm_cloudMatchRestoring' 'Cloud room restore state is missing.'
Require-Text $header 'm_cloudMatchLeaveDeadlineTick' 'Cloud room leave deadline state is missing.'
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

$handlerBody = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::HandleCloudMatchMessage('
if ($handlerBody.Contains('ApplyTeamSyncSnapshot')) {
    throw 'HandleCloudMatchMessage must never apply a remote team snapshot.'
}
Require-Text $handlerBody 'event["room"]' 'Join ACK handler does not inspect the accepted room payload.'
Require-Text $handlerBody 'event["broadcasterName"]' 'Join ACK handler does not inspect the accepted broadcaster name.'
Require-Text $handlerBody 'ackRoomId != m_cloudMatchPendingRoomId' 'Join ACK room correlation is missing.'
Require-Text $handlerBody 'DnfCloudMatchNamesMatchForAck' 'Join ACK broadcaster-name correlation is missing.'

$joinBody = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::BeginCloudRoomJoin('
Require-Text $joinBody 'm_cloudMatchClient.Configure(' 'Every cloud room join must rebuild the client generation.'
if ($joinBody.Contains('if (!status.configured)') -or
    $joinBody.Contains('GetStatusSnapshot()')) {
    throw 'BeginCloudRoomJoin must unconditionally reconfigure the client for a new generation.'
}

$cancelBody = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::CancelCloudRoomJoin('
Require-Text $cancelBody 'm_cloudMatchClient.Configure(' 'Canceling a join must invalidate the active client generation.'
Require-Text $cancelBody 'BeginCloudRoomRestore(' 'Canceling a room switch must formally restore the saved room.'

$restoreBody = Get-CppFunctionBody $source 'bool CDNFGameCaptureDlg::BeginCloudRoomRestore('
Require-Text $restoreBody 'm_cloudMatchRestoring = true;' 'Cloud room restore does not enter restoring state.'
Require-Text $restoreBody 'm_cloudMatchRoomConfirmed = false;' 'Cloud room restore must invalidate room confirmation.'
Require-Text $restoreBody 'm_cloudMatchClient.Configure(' 'Cloud room restore must rebuild the client generation.'
Require-Text $restoreBody 'm_cloudMatchClient.Start()' 'Cloud room restore must start the client.'
Require-Text $restoreBody 'm_cloudMatchClient.JoinRoom(' 'Cloud room restore must formally join the saved room.'

$uploadHandler = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::HandleCloudMatchSnapshotUploadResult('
Require-Text $uploadHandler 'event.find("clientRevision")' 'Snapshot results are not correlated by local clientRevision.'
Require-Text $uploadHandler 'acceptedRevision' 'Snapshot success ACK must validate acceptedRevision.'
Require-Text $uploadHandler 'm_cloudMatchInFlightRevision' 'Snapshot ACK must correlate the in-flight revision.'
Require-Text $uploadHandler 'm_cloudMatchPendingPayload == m_cloudMatchInFlightPayload' 'Snapshot ACK must only clear matching dirty content.'
Require-Text $uploadHandler 'm_cloudMatchUploadDirty = false;' 'Snapshot dirty state is not cleared after a matching success ACK.'
Require-Text $uploadHandler 'DnfIsTransientCloudSnapshotFailure' 'Transient snapshot failures are not classified for retry.'
Require-Text $uploadHandler 'm_cloudMatchUploadDueTick' 'Transient snapshot failures are not rescheduled.'
Require-Text $uploadHandler 'DnfClampCloudMatchRetryAfterMs' 'Snapshot retry delay is not safely bounded.'
Require-Text $source 'DnfReadCloudMatchUnsigned(event, "retryAfterMs"' 'Snapshot rate limits do not preserve the server retry delay.'
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
$revisionGuard = $uploadHandler.IndexOf('eventRevision != m_cloudMatchInFlightRevision')
$firstDueMutation = $uploadHandler.IndexOf('m_cloudMatchUploadDueTick')
$firstErrorMutation = $uploadHandler.IndexOf('m_cloudMatchLastError')
if ($revisionGuard -lt 0 -or ($firstDueMutation -ge 0 -and $revisionGuard -gt $firstDueMutation) -or
    ($firstErrorMutation -ge 0 -and $revisionGuard -gt $firstErrorMutation)) {
    throw 'Old snapshot results must be rejected before changing debounce or error state.'
}
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

$snapshotBuilder = Get-CppFunctionBody $source 'std::string CDNFGameCaptureDlg::BuildCloudMatchSnapshotPayload('
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

$transientStart = $source.IndexOf('static bool DnfIsTransientCloudSnapshotFailure(')
$transientEnd = $source.IndexOf('static bool DnfIsFatalCloudJoinError(', $transientStart)
if ($transientStart -lt 0 -or $transientEnd -le $transientStart) {
    throw 'Transient cloud snapshot classifier could not be inspected.'
}
$transientBody = $source.Substring($transientStart, $transientEnd - $transientStart)
Require-Text $transientBody 'code == "rate_limited"' 'Server mutation rate limits must be retried as transient failures.'

$pollBody = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::PollCloudMatch('
Require-Text $pollBody 'm_cloudMatchUploadInFlight' 'PollCloudMatch does not enforce one outstanding snapshot ACK.'
Require-Text $pollBody 'SaveCloudMatchRevision()' 'Snapshot uploads do not use dedicated revision persistence.'
Require-Text $pollBody 'm_cloudMatchJoinDeadlineTick' 'PollCloudMatch does not enforce a room join deadline.'
Require-Text $pollBody 'm_cloudMatchUploadRetryBlocked' 'PollCloudMatch does not honor the permanent-failure retry gate.'
Require-Text $pollBody 'latestAlreadyQueued' 'PollCloudMatch does not permit a newer debounced snapshot while an older ACK is pending.'
Require-Text $pollBody '!m_cloudMatchRoomConfirmed' 'Snapshot upload is not gated on confirmed room membership.'
if ($pollBody.Contains('m_cloudMatchUploadInFlight ||')) {
    throw 'An older snapshot ACK must not block enqueueing a newer debounced snapshot.'
}
if ($pollBody.Contains('SaveCloudMatchSettings()')) {
    throw 'Ordinary snapshot uploads must not rewrite the full CloudMatch identity settings.'
}
Require-Text $pollBody 'm_cloudMatchLeaveDeadlineTick' 'PollCloudMatch does not enforce a cloud room leave deadline.'
$leaveTimeoutStart = $pollBody.IndexOf('if (m_cloudMatchLeaving && m_cloudMatchLeaveDeadlineTick')
$uploadTimeoutStart = $pollBody.IndexOf('if (m_cloudMatchUploadInFlight &&', $leaveTimeoutStart)
if ($leaveTimeoutStart -lt 0 -or $uploadTimeoutStart -le $leaveTimeoutStart) {
    throw 'Cloud room leave timeout block could not be inspected.'
}
$leaveTimeout = $pollBody.Substring($leaveTimeoutStart,
    $uploadTimeoutStart - $leaveTimeoutStart)
Require-Text $leaveTimeout 'm_cloudMatchLeaving = false;' 'A timed-out leave must clear leaving state.'
Require-Text $leaveTimeout 'm_cloudMatchRoomConfirmed = false;' 'A timed-out leave must mark room membership unknown.'
if ($leaveTimeout.Contains('m_cloudMatchRoomId.clear()')) {
    throw 'A timed-out leave must preserve local room identity for retry.'
}
$dataLockOffset = $pollBody.IndexOf('std::lock_guard<std::mutex> dataLock(m_dataMutex)')
$snapshotBuildOffset = $pollBody.IndexOf('BuildTeamSyncSnapshotPayloadUnlocked()', $dataLockOffset)
$sourceLockOffset = $pollBody.IndexOf('std::lock_guard<std::mutex> sourceLock(m_cloudMatchSourceMutex)',
    $snapshotBuildOffset)
if ($dataLockOffset -lt 0 -or $snapshotBuildOffset -le $dataLockOffset -or
    $sourceLockOffset -le $snapshotBuildOffset) {
    throw 'OCR cloud source correlation must hold data then source locks around snapshot construction.'
}

$saveSettingsBody = Get-CppFunctionBody $source 'bool CDNFGameCaptureDlg::SaveCloudMatchSettingsForRoomIdentity('
Require-Text $saveSettingsBody 'WritePrivateProfileString' 'CloudMatch settings are not written to config.ini.'
Require-Text $saveSettingsBody 'SecureZeroMemory' 'CloudMatch token conversion buffer is not securely erased.'
Require-Text $saveSettingsBody 'return saved;' 'CloudMatch settings persistence does not report aggregate write failures.'
if ($saveSettingsBody.Contains('CA2W(m_cloudMatchDeviceToken')) {
    throw 'CloudMatch token persistence must not use an unerasable CA2W temporary.'
}

Require-Text $main 'let cloudRoomJoinTarget = null;' 'The Web room chooser does not track the requested switch target.'
Require-Text $main 'state.roomId === cloudRoomJoinTarget.roomId' 'The Web room chooser does not verify the joined room before resetting.'
Require-Text $main 'restoring:' 'Web cloud state does not normalize restoring state.'
Require-Text $main 'roomConfirmed:' 'Web cloud state does not normalize room confirmation.'
if ($main.Contains('if (state.joined && cloudRoomChoosing)')) {
    throw 'The Web room chooser must not reset merely because the client was already joined before switching rooms.'
}
$cancelJoinStart = $main.IndexOf("document.getElementById('btn-cloud-room-cancel-join')?.addEventListener")
$renameStart = $main.IndexOf("document.getElementById('btn-cloud-room-rename')?.addEventListener", $cancelJoinStart)
if ($cancelJoinStart -lt 0 -or $renameStart -le $cancelJoinStart) {
    throw 'Cloud room cancel handler could not be inspected.'
}
$cancelJoinHandler = $main.Substring($cancelJoinStart, $renameStart - $cancelJoinStart)
Require-Text $cancelJoinHandler "cloudSelectedRoomId = '';" 'Canceling a first room join must return Web UI to room selection.'

Require-Text $clientSource 'ExtractSnapshotClientRevision(snapshotJson)' 'CloudMatchClient does not extract clientRevision from legal snapshots.'
Require-Text $clientSource 'std::uint64_t clientRevision = 0;' 'CloudMatchClient pending snapshot/ACK state does not retain clientRevision.'
$clientEncodeFailure = Get-CppFunctionBody $clientSource 'void NotifySnapshotUploadFailure('
Require-Text $clientEncodeFailure '{ "clientRevision", clientRevision }' 'Snapshot encoding failures omit clientRevision.'
$clientCanceled = Get-CppFunctionBody $clientSource 'void NotifySnapshotCanceled('
Require-Text $clientCanceled '{ "clientRevision", snapshot.clientRevision }' 'Canceled snapshots omit clientRevision.'
$clientAckHandler = Get-CppFunctionBody $clientSource 'void HandleAck('
Require-Text $clientAckHandler 'normalized["clientRevision"] = pending.clientRevision;' 'Snapshot ACK results omit clientRevision.'
$clientAckFailure = Get-CppFunctionBody $clientSource 'void NotifyPendingAckFailure('
Require-Text $clientAckFailure 'normalized["clientRevision"] = pending.clientRevision;' 'Shared pending-ACK failures omit clientRevision.'
Require-Text $clientAckFailure 'normalized["requestId"] = pending.requestId;' 'Shared pending-ACK failures omit requestId.'
$clientAckTimeout = Get-CppFunctionBody $clientSource 'void ExpireAcks('
Require-Text $clientAckTimeout 'NotifyPendingAckFailure(activeConfig.generation, pending, "timeout")' 'Snapshot ACK timeout does not use the shared revision-preserving settlement path.'
$clientConnectionLoss = Get-CppFunctionBody $clientSource 'void FailPendingAcks('
Require-Text $clientConnectionLoss 'NotifyPendingAckFailure(activeConfig.generation, entry.second, code)' 'Snapshot connection-loss does not use the shared revision-preserving settlement path.'
Require-Text $clientHeader 'CompleteLatestSnapshotAckForTesting' 'CloudMatchClient ACK correlation lacks an executable test seam.'
Require-Text $clientHeader 'CompleteLatestSnapshotAckWithPayloadForTesting' 'CloudMatchClient malformed ACK handling lacks an executable test seam.'
$notifyJson = Get-CppFunctionBody $clientSource 'void NotifyJson('
Require-Text $notifyJson 'clientRevision' 'Oversized protected snapshot results lose clientRevision.'
Require-Text $notifyJson 'requestId' 'Oversized protected request results lose requestId.'
Require-Text $clientHeader 'ExpireLatestSnapshotAckForTesting' 'Client tests do not exercise the production ACK expiry path.'
Require-Text $clientHeader 'FailLatestSnapshotAckForTesting' 'Client tests do not exercise the production connection-loss path.'
Require-Text $clientSource 'TakeLatestSnapshotForSend(activeConfig.generation)' 'Production snapshot sending does not use the shared latest-snapshot transfer path.'
Require-Text $clientSource 'TakeLatestSnapshotForSend(generation)' 'Snapshot tests do not use the production latest-snapshot transfer path.'
Require-Text $clientTest 'ExpireLatestSnapshotAckForTesting()' 'Executable tests do not run the production ACK expiry path.'
Require-Text $clientTest 'FailLatestSnapshotAckForTesting("connection_lost")' 'Executable tests do not run the production connection-loss path.'
Require-Text $clientTest 'CompleteLatestSnapshotAckForTesting(true, 808, {}, 140000)' 'Executable tests do not cover oversized snapshot result fallback.'
Require-Text $clientTest 'CompleteLatestSnapshotAckWithPayloadForTesting' 'Executable tests do not cover malformed ACK ok types.'

$webCommandHandler = Get-CppFunctionBody $source 'LRESULT CDNFGameCaptureDlg::OnWebCmdReceived('
$leaveCommandStart = $webCommandHandler.IndexOf('else if (action == "cmd_cloud_room_leave")')
$nextCommandStart = $webCommandHandler.IndexOf('else if (action == "cmd_set_appearance_panel_open")', $leaveCommandStart)
if ($leaveCommandStart -lt 0 -or $nextCommandStart -le $leaveCommandStart) {
    throw 'Cloud room leave command branch could not be inspected.'
}
$leaveCommand = $webCommandHandler.Substring($leaveCommandStart,
    $nextCommandStart - $leaveCommandStart)
Require-Text $leaveCommand 'm_cloudMatchClient.Configure(' 'Unknown room leave does not rebuild the client generation.'
Require-Text $leaveCommand 'm_cloudMatchClient.Start()' 'Unknown room leave does not start the cloud client.'
Require-Text $leaveCommand 'm_cloudMatchClient.LeaveRoom()' 'Unknown room leave does not send a formal leave request.'
Require-Text $leaveCommand 'm_cloudMatchLeaving = true;' 'Unknown room leave does not enter leaving state.'
Require-Text $leaveCommand 'm_cloudMatchLeaveDeadlineTick' 'Cloud room leave command does not start a host deadline.'
if ($leaveCommand.Contains('m_cloudMatchRoomId.clear()')) {
    throw 'Cloud room leave command must not clear local identity before a successful ACK.'
}

$leaveResultStart = $handlerBody.IndexOf('else if (type == "room_leave_result")')
$snapshotResultStart = $handlerBody.IndexOf('else if (type == "snapshot_upload_result")', $leaveResultStart)
if ($leaveResultStart -lt 0 -or $snapshotResultStart -le $leaveResultStart) {
    throw 'Cloud room leave result branch could not be inspected.'
}
$leaveResult = $handlerBody.Substring($leaveResultStart,
    $snapshotResultStart - $leaveResultStart)
Require-Text $leaveResult 'm_cloudMatchLeaveDeadlineTick = 0;' 'Cloud room leave result does not settle its host deadline.'
Require-Text $leaveResult 'SaveCloudMatchSettingsForRoomIdentity({}, CString{})' 'Successful server leave must persist an empty identity before clearing the in-memory room.'
$leaveSuccess = $leaveResult.IndexOf('if (ok)')
$saveEmpty = $leaveResult.IndexOf('SaveCloudMatchSettingsForRoomIdentity({}, CString{})', $leaveSuccess)
$saveFailure = $leaveResult.IndexOf('if (!SaveCloudMatchSettingsForRoomIdentity', $leaveSuccess)
$leaveClear = $leaveResult.IndexOf('m_cloudMatchRoomId.clear()', $saveFailure)
$leaveFailure = $leaveResult.LastIndexOf('else {')
if ($leaveSuccess -lt 0 -or $saveEmpty -le $leaveSuccess -or
    $saveFailure -le $leaveSuccess -or $leaveClear -le $saveFailure -or
    $leaveFailure -le $leaveClear) {
    throw 'Cloud room identity must only be cleared after empty identity persistence succeeds.'
}
$saveFailureEnd = $leaveResult.IndexOf('else {', $saveFailure)
$saveFailureBody = $leaveResult.Substring($saveFailure,
    $saveFailureEnd - $saveFailure)
Require-Text $saveFailureBody 'm_cloudMatchRoomConfirmed = false;' 'Failed leave persistence must keep room membership unconfirmed.'
$leavePersistenceErrorPrefix = [string]([char]0x670D + [char]0x52A1 +
    [char]0x7AEF + [char]0x5DF2 + [char]0x9000 + [char]0x51FA +
    [char]0x4F46 + [char]0x672C + [char]0x5730 + [char]0x914D +
    [char]0x7F6E + [char]0x4FDD + [char]0x5B58 + [char]0x5931 +
    [char]0x8D25)
Require-Text $saveFailureBody $leavePersistenceErrorPrefix 'Failed leave persistence must explain the split server/local state.'
if ($saveFailureBody.Contains('m_cloudMatchRoomId.clear()') -or
    $saveFailureBody.Contains('m_cloudMatchBroadcasterName.Empty()')) {
    throw 'Failed leave persistence must retain the old room identity for an idempotent retry.'
}
$leaveFailureBody = $leaveResult.Substring($leaveFailure)
Require-Text $leaveFailureBody 'm_cloudMatchRoomConfirmed = false;' 'Failed cloud room leave must mark membership unknown.'
if ($leaveFailureBody.Contains('m_cloudMatchRoomId.clear()')) {
    throw 'Failed cloud room leave must preserve local identity for retry.'
}
$saveIdentityBody = $saveSettingsBody
Require-Text $saveIdentityBody 'CA2W(roomIdOverride.c_str(), CP_UTF8)' 'Cloud identity persistence does not write the requested room identity.'
Require-Text $saveIdentityBody 'writeSetting(L"BroadcasterName", broadcasterNameOverride)' 'Cloud identity persistence does not write the requested broadcaster identity.'
$startSavedBody = Get-CppFunctionBody $source 'void CDNFGameCaptureDlg::StartSavedCloudMatchSession('
Require-Text $startSavedBody '!DnfIsCloudMatchRoomId(m_cloudMatchRoomId)' 'Restart must not join after the empty room identity was saved.'
$renameBranchStart = $handlerBody.IndexOf('else if (type == "room_rename_result")')
$leaveBranchStart = $handlerBody.IndexOf('else if (type == "room_leave_result")', $renameBranchStart)
if ($renameBranchStart -lt 0 -or $leaveBranchStart -le $renameBranchStart) {
    throw 'Cloud room rename result branch could not be inspected.'
}
$renameBranch = $handlerBody.Substring($renameBranchStart, $leaveBranchStart - $renameBranchStart)
Require-Text $renameBranch 'm_cloudMatchRenaming = false;' 'Malformed rename ACKs can leave rename permanently busy.'
Require-Text $renameBranch 'invalid_response' 'Malformed rename ACKs do not report invalid_response.'

foreach ($line in ($source -split "`r?`n")) {
    if (($line.Contains('DeviceToken') -or $line.Contains('deviceToken')) -and
        $line -match 'AppLog|WriteMatchLog|MessageBox') {
        throw 'DeviceToken must not be written to logs or message boxes.'
    }
}

Write-Host 'Cloud match room integration static checks passed.' -ForegroundColor Green
