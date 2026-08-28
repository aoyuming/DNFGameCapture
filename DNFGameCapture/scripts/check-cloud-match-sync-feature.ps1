$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'CloudMatchSync.h'
$sourcePath = Join-Path $root 'CloudMatchSync.cpp'
$dialogHeaderPath = Join-Path $root 'DNFGameCaptureDlg.h'
$dialogSourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$testPath = Join-Path $PSScriptRoot 'cloud_match_sync_test.cpp'
$themeTestPath = Join-Path $PSScriptRoot 'cloud_match_theme_test.js'
$webRoot = Get-ChildItem -LiteralPath $root -Directory | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'main.js')) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'index.html')) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'style.css'))
} | Select-Object -First 1
if (-not $webRoot) {
    throw 'Unable to locate the Web scoreboard directory.'
}
$webMainPath = Join-Path $webRoot.FullName 'main.js'
$webHtmlPath = Join-Path $webRoot.FullName 'index.html'
$webCssPath = Join-Path $webRoot.FullName 'style.css'

foreach ($path in @($headerPath, $sourcePath, $dialogHeaderPath, $dialogSourcePath,
    $testPath, $themeTestPath, $webMainPath, $webHtmlPath, $webCssPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing cloud match sync feature file: $path"
    }
}

$dialogHeader = Get-Content -LiteralPath $dialogHeaderPath -Raw -Encoding UTF8
$dialogSource = Get-Content -LiteralPath $dialogSourcePath -Raw -Encoding UTF8
$webMain = Get-Content -LiteralPath $webMainPath -Raw -Encoding UTF8
$webHtml = Get-Content -LiteralPath $webHtmlPath -Raw -Encoding UTF8
$webCss = Get-Content -LiteralPath $webCssPath -Raw -Encoding UTF8

foreach ($needle in @(
    'cmd_cloud_sync_open', 'cmd_cloud_sync_close', 'cmd_cloud_sync_refresh',
    'cmd_cloud_sync_select', 'cmd_cloud_sync_apply', 'cmd_cloud_sync_cancel_preview',
    'cmd_cloud_sync_undo', 'RequestComparison(', 'RequestSnapshot(',
    'DnfConvertCloudMatchSnapshot(', 'ValidateTeamSyncSnapshot(',
    'ApplyTeamSyncSnapshot(', 'RefreshAfterTeamSyncApply()',
    'm_cloudMatchPendingChangeSource = "cloud_sync"', 'syncedFrom',
    'm_cloudMatchSyncGeneration', 'm_cloudMatchSyncUndoBackup',
    'm_cloudMatchSyncUndoAppliedHash'
)) {
    if (-not $dialogSource.Contains($needle) -and -not $dialogHeader.Contains($needle)) {
        throw "Cloud match host safety contract is missing: $needle"
    }
}

foreach ($needle in @(
    'm_matchMutationEpoch', 'm_cloudMatchSyncUndoPostApplyEpoch',
    'm_cloudMatchSyncUndoRoomId', 'm_cloudMatchSyncUndoConnectionGeneration',
    'DnfResolveCloudMatchRelativeSwap(', 'DnfIsCloudMatchPreviewCurrent(',
    'DnfCanCloudMatchUndo(', 'm_cloudMatchSyncComparisonRoomRevision',
    'm_cloudMatchSyncPreviewRequestId', 'CancelRequest(',
    'm_cloudMatchSyncComparisonToken', 'm_cloudMatchSyncComparisonGeneratedAt',
    'm_cloudMatchSyncComparisonTotalMembers',
    'm_cloudMatchSyncComparisonBoundedMembers'
)) {
    if (-not $dialogSource.Contains($needle) -and -not $dialogHeader.Contains($needle)) {
        throw "Cloud match review safety contract is missing: $needle"
    }
}
if (-not $dialogSource.Contains('event["comparisonToken"].get<std::string>()')) {
    throw 'Cloud comparison pagination must validate and retain the server token.'
}
if (-not $dialogSource.Contains('m_cloudMatchSyncComparisonCursor, 64,') -or
    -not $dialogSource.Contains('m_cloudMatchSyncComparisonToken')) {
    throw 'Cloud comparison continuation requests must carry cursor and token.'
}
if (-not $dialogSource.Contains(
        'std::to_wstring(m_cloudMatchSyncComparisonTotalMembers)') -or
    -not $dialogSource.Contains(
        'std::to_wstring(m_cloudMatchSyncComparisonBoundedMembers)')) {
    throw 'Cloud comparison UI must disclose total and bounded member counts.'
}
if (-not $dialogHeader.Contains('bool RefreshAfterTeamSyncApply()')) {
    throw 'Cloud apply persistence aggregation must return a result.'
}
if (-not $dialogSource.Contains('if (!RefreshAfterTeamSyncApply())')) {
    throw 'Cloud apply must not report success after persistence failure.'
}

function Assert-DialogBlockContains {
    param(
        [Parameter(Mandatory = $true)][string]$Start,
        [Parameter(Mandatory = $true)][string]$End,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $startIndex = $dialogSource.IndexOf($Start)
    $endIndex = $dialogSource.IndexOf($End, $startIndex + $Start.Length)
    if ($startIndex -lt 0 -or $endIndex -le $startIndex) {
        throw "Unable to locate mutation block: $Start"
    }
    $body = $dialogSource.Substring($startIndex, $endIndex - $startIndex)
    if (-not $body.Contains($Needle)) {
        throw $Message
    }
}

foreach ($block in @(
    @('bool CDNFGameCaptureDlg::ToggleReviewEvent(',
      'void CDNFGameCaptureDlg::DoRetryMatchingTask(',
      'MarkMatchMutation();', 'Review undo/restore must advance the mutation epoch.'),
    @('void CDNFGameCaptureDlg::OnBnClickedFlip()',
      'void CDNFGameCaptureDlg::OnBnClickedReset()',
      'MarkMatchMutation();', 'Manual team flipping must advance the mutation epoch.'),
    @('void CDNFGameCaptureDlg::OnBnClickedQuickAdd()',
      'LRESULT CDNFGameCaptureDlg::OnWebCmdReceived(',
      'MarkMatchMutation();', 'Quick player entry must advance the mutation epoch.'),
    @('void CDNFGameCaptureDlg::OnRClickTree(',
      'CString CDNFGameCaptureDlg::CheckFieldConflict(',
      'if (matchStateMutationCommand) MarkMatchMutation();',
      'Native tree mutations must advance the mutation epoch per command.'),
    @('void CDNFGameCaptureDlg::OnEndLabelEdit(',
      'void CDNFGameCaptureDlg::OnCustomDrawTree(',
      'MarkMatchMutation();', 'Native score/player edits must advance the mutation epoch.'),
    @('else if (action == "cmd_delete_alias")',
      'else if (action == "cmd_undo_event")',
      'MarkMatchMutation();', 'Deleting an active alias must advance the mutation epoch.'),
    @('else if (action == "cmd_set_output_seat_label")',
      'else if (action == "cmd_set_red_pick_mode")',
      'MarkMatchMutation();', 'TXT seat-order changes must advance the mutation epoch.'),
    @('else if (action == "cmd_set_red_pick_mode")',
      'else if (action == "cmd_set_scoreboard_text_styles")',
      'MarkMatchMutation();', 'Pick-order changes must advance the mutation epoch.')
)) {
    Assert-DialogBlockContains -Start $block[0] -End $block[1] `
        -Needle $block[2] -Message $block[3]
}

$handlerStart = $dialogSource.IndexOf('void CDNFGameCaptureDlg::HandleCloudMatchMessage(')
$handlerEnd = $dialogSource.IndexOf('std::string CDNFGameCaptureDlg::BuildCloudMatchSnapshotPayload(', $handlerStart)
if ($handlerStart -lt 0 -or $handlerEnd -le $handlerStart) {
    throw 'Unable to locate cloud match message handler.'
}
$handlerBody = $dialogSource.Substring($handlerStart, $handlerEnd - $handlerStart)
if ($handlerBody.Contains('ApplyTeamSyncSnapshot(')) {
    throw 'Network result handling must never apply a remote match snapshot.'
}
$applyCommand = $dialogSource.IndexOf('action == "cmd_cloud_sync_apply"')
if ($applyCommand -lt 0) {
    throw 'Explicit cloud sync apply command is missing.'
}
$selectCommand = $dialogSource.IndexOf('action == "cmd_cloud_sync_select"')
$selectCommandBody = $dialogSource.Substring($selectCommand, $applyCommand - $selectCommand)
if ($selectCommandBody.Contains('excludedFromConsensus')) {
    throw 'Consensus exclusion must not block an explicit manual snapshot preview.'
}
if ($dialogSource.Contains('event.value("consensusDeviceId", std::string())')) {
    throw 'Nullable consensusDeviceId must be read without a throwing typed value conversion.'
}
$snapshotHandlerStart = $dialogSource.IndexOf(
    'void CDNFGameCaptureDlg::HandleCloudMatchSnapshotResult(')
$snapshotHandlerEnd = $dialogSource.IndexOf(
    'void CDNFGameCaptureDlg::QueueCloudMatchSyncedUpload(', $snapshotHandlerStart)
$snapshotHandler = $dialogSource.Substring($snapshotHandlerStart,
    $snapshotHandlerEnd - $snapshotHandlerStart)
foreach ($unsafeRead in @(
    'event.value("broadcasterName", std::string())',
    'event.value("receivedAt", 0ll)'
)) {
    if ($snapshotHandler.Contains($unsafeRead)) {
        throw "Snapshot preview must use validated summary metadata: $unsafeRead"
    }
}
foreach ($needle in @(
    'const std::string confirmedDeviceId = j.value("deviceId", std::string())',
    'const std::uint64_t confirmedRevision = j.value("clientRevision", 0ull)',
    'const std::uint64_t confirmedGeneration = j.value("generation", 0ull)'
)) {
    if (-not $dialogSource.Contains($needle)) {
        throw "Cloud apply confirmation correlation is missing: $needle"
    }
}
if ($dialogSource.Contains(
    'm_cloudMatchSyncUndoApplied = m_teamSyncAppliedSnapshot')) {
    throw 'Cloud undo must fingerprint the actual post-apply state after shared normalization.'
}
if (-not $dialogSource.Contains(
    'json::parse(BuildTeamSyncSnapshotPayload(), nullptr, false)')) {
    throw 'Cloud undo must capture the actual post-apply TeamSync snapshot.'
}

foreach ($needle in @(
    'btn-cloud-sync', 'cloud-sync-panel', 'cloud-sync-member-list',
    'cloud-sync-difference-groups', 'btn-cloud-sync-apply', 'btn-cloud-sync-undo',
    'cmd_cloud_sync_apply', 'cmd_cloud_sync_cancel_preview', 'cmd_cloud_sync_undo'
)) {
    if (-not $webMain.Contains($needle) -and -not $webHtml.Contains($needle)) {
        throw "Cloud match Web UI contract is missing: $needle"
    }
}
if (-not $webCss.Contains('overflow-y: auto')) {
    throw 'Cloud match panel must remain scrollable in short windows.'
}
foreach ($theme in @('dark-esports', 'frost-broadcast', 'black-gold')) {
    if (-not $webCss.Contains("html[data-theme=`"$theme`"]")) {
        throw "Cloud match panel is missing explicit theme variables for $theme."
    }
}
if (-not $webMain.Contains("event.target?.id === 'cloud-sync-overlay'")) {
    throw 'Cloud sync backdrop click must close only when the overlay itself is clicked.'
}
& node $themeTestPath
if ($LASTEXITCODE -ne 0) {
    throw 'Cloud match theme runtime tests failed.'
}
$memberHandlerStart = $webMain.IndexOf("document.getElementById('cloud-sync-member-list')")
$applyHandlerStart = $webMain.IndexOf("document.getElementById('btn-cloud-sync-apply')", $memberHandlerStart)
if ($memberHandlerStart -lt 0 -or $applyHandlerStart -le $memberHandlerStart) {
    throw 'Unable to locate cloud member selection and apply handlers.'
}
$memberHandler = $webMain.Substring($memberHandlerStart,
    $applyHandlerStart - $memberHandlerStart)
if ($memberHandler.Contains('cmd_cloud_sync_apply')) {
    throw 'Selecting a cloud member must not send the apply command.'
}
$renderMembersStart = $webMain.IndexOf('function renderCloudSyncMembers(')
$renderMembersEnd = $webMain.IndexOf('function renderCloudSyncGroups(', $renderMembersStart)
$renderMembers = $webMain.Substring($renderMembersStart,
    $renderMembersEnd - $renderMembersStart)
if ($renderMembers.Contains('excludedFromConsensus === true || panel.busy')) {
    throw 'Consensus exclusion must remain a warning badge, not disable manual preview.'
}
foreach ($needle in @(
    'deviceId: String(preview.deviceId || '''')',
    'clientRevision: revision',
    'generation: Number(preview.generation || 0)'
)) {
    if (-not $webMain.Contains($needle)) {
        throw "Web apply confirmation correlation is missing: $needle"
    }
}

$installRoots = [System.Collections.Generic.List[string]]::new()
foreach ($knownRoot in @(
    'E:\VS2026',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools'
)) {
    $installRoots.Add($knownRoot)
}
$devCmd = $null
$compiler = $null
foreach ($installRoot in $installRoots) {
    $candidateDevCmd = Join-Path $installRoot 'Common7\Tools\VsDevCmd.bat'
    $candidateCompiler = Get-ChildItem -LiteralPath (Join-Path $installRoot 'VC\Tools\MSVC') `
        -Filter 'cl.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\cl\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ((Test-Path -LiteralPath $candidateDevCmd) -and $candidateCompiler) {
        $devCmd = $candidateDevCmd
        $compiler = $candidateCompiler.FullName
        break
    }
}
if (-not $devCmd -or -not $compiler) {
    throw 'Visual Studio C++ x64 compiler was not found for the cloud sync harness.'
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('DNFGameCapture-cloud-match-sync-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
$exe = Join-Path $tempRoot 'cloud_match_sync_test.exe'
try {
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" /nologo /std:c++17 /EHsc /W4 /Y- /I"{2}" /Fe:"{3}" /Fo:"{4}\\" "{5}" "{6}"' -f `
        $devCmd, $compiler, $root, $exe, $tempRoot, $testPath, $sourcePath
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw "Cloud match sync test compilation failed with exit code $LASTEXITCODE."
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Cloud match sync tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host 'Cloud match sync static and runtime checks passed.' -ForegroundColor Green
