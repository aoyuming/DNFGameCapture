$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$displayHeaderPath = Join-Path $root 'CloudMatchStatusDisplay.h'
$displaySourcePath = Join-Path $root 'CloudMatchStatusDisplay.cpp'
$dialogHeaderPath = Join-Path $root 'DNFGameCaptureDlg.h'
$dialogSourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$webDialogSourcePath = Join-Path $root 'WebScoreDlg.cpp'
$killDialogSourcePath = Join-Path $root 'KillDisplayDlg.cpp'
$keyDialogSourcePath = Join-Path $root 'KeyDisplayDlg.cpp'
$resourceHeaderPath = Join-Path $root 'Resource.h'
$resourceScriptPath = Join-Path $root 'DNFGameCapture.rc'
$projectPath = Join-Path $root 'DNFGameCapture.vcxproj'
$filtersPath = Join-Path $root 'DNFGameCapture.vcxproj.filters'
$testPath = Join-Path $PSScriptRoot 'cloud_match_status_test.cpp'
$webTestPath = Join-Path $PSScriptRoot 'cloud_match_status_web_test.js'
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)
$webMainPath = Join-Path $webRoot 'main.js'
$webHtmlPath = Join-Path $webRoot 'index.html'
$webCssPath = Join-Path $webRoot 'style.css'

foreach ($path in @($displayHeaderPath, $displaySourcePath, $dialogHeaderPath,
    $dialogSourcePath, $webDialogSourcePath, $killDialogSourcePath,
    $keyDialogSourcePath, $resourceHeaderPath, $resourceScriptPath,
    $projectPath, $filtersPath, $testPath, $webTestPath, $webMainPath,
    $webHtmlPath, $webCssPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing cloud match status feature file: $path"
    }
}

$displayHeader = Get-Content -LiteralPath $displayHeaderPath -Raw -Encoding UTF8
$displaySource = Get-Content -LiteralPath $displaySourcePath -Raw -Encoding UTF8
$dialogHeader = Get-Content -LiteralPath $dialogHeaderPath -Raw -Encoding UTF8
$dialogSource = Get-Content -LiteralPath $dialogSourcePath -Raw -Encoding UTF8
$webDialogSource = Get-Content -LiteralPath $webDialogSourcePath -Raw -Encoding UTF8
$killDialogSource = Get-Content -LiteralPath $killDialogSourcePath -Raw -Encoding UTF8
$keyDialogSource = Get-Content -LiteralPath $keyDialogSourcePath -Raw -Encoding UTF8
$resourceHeader = Get-Content -LiteralPath $resourceHeaderPath -Raw -Encoding UTF8
$resourceScript = Get-Content -LiteralPath $resourceScriptPath -Raw -Encoding UTF8
$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
$filters = Get-Content -LiteralPath $filtersPath -Raw -Encoding UTF8
$webMain = Get-Content -LiteralPath $webMainPath -Raw -Encoding UTF8
$webHtml = Get-Content -LiteralPath $webHtmlPath -Raw -Encoding UTF8
$webCss = Get-Content -LiteralPath $webCssPath -Raw -Encoding UTF8

foreach ($needle in @(
    'enum class CloudMatchDisplayState',
    'struct CloudMatchDisplayContext',
    'struct CloudMatchDisplayStatus',
    'BuildCloudMatchDisplayStatus(',
    'CloudMatchDisplayStateName('
)) {
    if (-not $displayHeader.Contains($needle)) {
        throw "Cloud match display contract is missing: $needle"
    }
}
foreach ($needle in @(
    '#include "CloudMatchStatusDisplay.h"',
    'void RefreshCloudMatchStatusDisplay(',
    'CStatic         m_cloudMatchStatus',
    'm_cloudMatchClient.GetStatusSnapshot()',
    'cloudStatus.roomName',
    'cloudStatus.broadcasterName',
    'cloudMatch["displayState"]',
    'cloudMatch["displayText"]',
    'IDC_STATIC_CLOUD_ROOM_STATUS',
    'm_cloudMatchStatus.SubclassWindow(',
    'RGB(35, 143, 85)',
    'RGB(188, 135, 0)',
    'RGB(196, 48, 62)',
    'RGB(112, 119, 128)'
)) {
    if (-not $dialogSource.Contains($needle) -and
        -not $dialogHeader.Contains($needle)) {
        throw "Cloud match native status wiring is missing: $needle"
    }
}
if ($dialogSource.Contains('m_cloudMatchStatus.Create(') -or
    $dialogSource.Contains('ID_STATIC_CLOUD_MATCH_STATUS')) {
    throw 'Cloud match native status must bind the resource control without a dynamic CStatic ID.'
}

$statusIdMatches = [regex]::Matches($resourceHeader,
    '(?m)^\s*#define\s+IDC_STATIC_CLOUD_ROOM_STATUS\s+(\d+)\s*$')
if ($statusIdMatches.Count -ne 1) {
    throw 'Resource.h must define exactly one IDC_STATIC_CLOUD_ROOM_STATUS.'
}
$statusControlId = [int]$statusIdMatches[0].Groups[1].Value
$cropIdMatch = [regex]::Match($dialogSource,
    'ID_CHK_AUTO_CROP_BLACK_BARS\s*=\s*(\d+)')
if (-not $cropIdMatch.Success -or
    $statusControlId -eq [int]$cropIdMatch.Groups[1].Value) {
    throw 'Cloud room status and black-bar recrop controls must use distinct IDs.'
}
$sameValueDefinitions = [regex]::Matches($resourceHeader,
    ('(?m)^\s*#define\s+\S+\s+' + $statusControlId + '\s*$'))
if ($sameValueDefinitions.Count -ne 1) {
    throw 'IDC_STATIC_CLOUD_ROOM_STATUS must have a unique numeric resource value.'
}
if ([regex]::Matches($resourceScript,
        '\bIDC_STATIC_CLOUD_ROOM_STATUS\b').Count -ne 1) {
    throw 'The professional dialog resource must contain exactly one cloud status control.'
}
$professionalDialogStart = $resourceScript.IndexOf(
    'IDD_DNFGAMECAPTURE_DIALOG DIALOGEX')
$professionalDialogEnd = $resourceScript.IndexOf("`nEND", $professionalDialogStart)
if ($professionalDialogStart -lt 0 -or
    $professionalDialogEnd -le $professionalDialogStart) {
    throw 'Unable to locate the professional dialog resource block.'
}
$professionalDialog = $resourceScript.Substring($professionalDialogStart,
    $professionalDialogEnd - $professionalDialogStart)
foreach ($needle in @('IDC_STATIC_CLOUD_ROOM_STATUS', 'SS_ENDELLIPSIS')) {
    if (-not $professionalDialog.Contains($needle)) {
        throw "Professional dialog cloud status resource is missing: $needle"
    }
}

$pollStart = $dialogSource.IndexOf('void CDNFGameCaptureDlg::PollCloudMatch()')
$pollEnd = $dialogSource.IndexOf('void CDNFGameCaptureDlg::SendCloudRoomPromptIfNeeded()', $pollStart)
if ($pollStart -lt 0 -or $pollEnd -le $pollStart) {
    throw 'Unable to locate PollCloudMatch for UI-thread status verification.'
}
$pollBody = $dialogSource.Substring($pollStart, $pollEnd - $pollStart)
if (-not $pollBody.Contains('m_cloudMatchClient.DispatchMessages(32);') -or
    -not $pollBody.Contains('RefreshCloudMatchStatusDisplay(cloudStatus);')) {
    throw 'Cloud status must refresh after queued messages are dispatched on the UI timer.'
}

$jsonStart = $dialogSource.IndexOf('json CDNFGameCaptureDlg::DnfBuildSharedWebStateJson()')
$jsonEnd = $dialogSource.IndexOf('std::string CDNFGameCaptureDlg::BuildKillDisplayStatePayload()', $jsonStart)
$jsonBody = $dialogSource.Substring($jsonStart, $jsonEnd - $jsonStart)
foreach ($secret in @('m_cloudMatchDeviceToken', 'deviceToken', 'cloudToken')) {
    if ($jsonBody.Contains($secret)) {
        throw "Cloud status JSON must not expose credentials: $secret"
    }
}

foreach ($needle in @(
    'id="cloud-room-status"',
    'class="cloud-room-status"'
)) {
    if (-not $webHtml.Contains($needle)) {
        throw "Cloud status label markup is missing: $needle"
    }
}
$leftStackIndex = $webHtml.IndexOf('<div class="left-stack">')
$statusLabelIndex = $webHtml.IndexOf('id="cloud-room-status"')
$mainContainerIndex = $webHtml.IndexOf('id="main-container"')
$teamsIndex = $webHtml.IndexOf('id="teams-wrap"')
if ($leftStackIndex -lt 0 -or $statusLabelIndex -le $leftStackIndex -or
    $statusLabelIndex -ge $mainContainerIndex -or
    $statusLabelIndex -ge $teamsIndex) {
    throw 'Cloud status label must live at the top of left-stack before teams.'
}
foreach ($needle in @(
    'function renderCloudRoomStatus()',
    'renderCloudRoomStatus();',
    "getElementById('cloud-room-status')?.addEventListener('click', openCloudSyncPanel)"
)) {
    if (-not $webMain.Contains($needle)) {
        throw "Cloud status Web wiring is missing: $needle"
    }
}
$openPanelStart = $webMain.IndexOf('function openCloudSyncPanel()')
$openPanelEnd = $webMain.IndexOf('function closeCloudSyncPanel()', $openPanelStart)
if ($openPanelStart -lt 0 -or $openPanelEnd -le $openPanelStart) {
    throw 'Unable to locate the existing cloud sync panel opener.'
}
$openPanelBody = $webMain.Substring($openPanelStart,
    $openPanelEnd - $openPanelStart)
if (-not $openPanelBody.Contains('setMoreControlsOpen(false);')) {
    throw 'Cloud status click path must retain more-menu close behavior.'
}
if (-not $webMain.Contains('status.title = state.displayText;')) {
    throw 'Cloud status tooltip must retain the complete display text.'
}
foreach ($needle in @(
    '.cloud-room-status {',
    'text-overflow: ellipsis',
    'overflow: hidden',
    '.cloud-room-status[data-state="online"]',
    '.cloud-room-status[data-state="reconnecting"]',
    '.cloud-room-status[data-state="offline"]',
    '.cloud-room-status[data-state="not-joined"]',
    'html[data-theme="dark-esports"] .cloud-room-status',
    'html[data-theme="frost-broadcast"] .cloud-room-status',
    'html[data-theme="black-gold"] .cloud-room-status',
    '.more-controls-menu {',
    'overflow-y: auto'
)) {
    if (-not $webCss.Contains($needle)) {
        throw "Cloud status responsive/theme contract is missing: $needle"
    }
}
if ($webCss.Contains('.cloud-room-status[data-state="working"]') -or
    -not $webMain.Contains("new Set(['online', 'reconnecting', 'offline', 'not-joined'])")) {
    throw 'Cloud status Web contract must use reconnecting, not working.'
}

function Assert-NoWindowTitleMutation {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$WindowName,
        [string]$ExpectedExistingCall = ''
    )
    $memberCalls = [regex]::Matches($Content,
        '(?m)^\s*(?:this->)?SetWindowTextW?\s*\(')
    $expectedCount = if ($ExpectedExistingCall) { 1 } else { 0 }
    if ($memberCalls.Count -ne $expectedCount -or
        ($ExpectedExistingCall -and -not $Content.Contains($ExpectedExistingCall))) {
        throw "Task 8 must preserve the existing $WindowName window title contract."
    }
    if ([regex]::IsMatch($Content,
            '::SetWindowTextW?\s*\(\s*(?:m_hWnd|GetSafeHwnd\(\)|this->GetSafeHwnd\(\))')) {
        throw "Task 8 must not change the $WindowName window title through its HWND."
    }
}
Assert-NoWindowTitleMutation -Content $dialogSource -WindowName 'main'
Assert-NoWindowTitleMutation -Content $webDialogSource -WindowName 'Web' `
    -ExpectedExistingCall 'SetWindowText(title);'
Assert-NoWindowTitleMutation -Content $killDialogSource -WindowName 'KILL' `
    -ExpectedExistingCall 'SetWindowText(kKillDisplayWindowTitle);'
Assert-NoWindowTitleMutation -Content $keyDialogSource -WindowName 'KEY' `
    -ExpectedExistingCall 'SetWindowText(kWindowTitle);'
foreach ($pointer in @('m_pWebDlg', 'm_pKillDisplayDlg', 'm_pKeyDisplayDlg')) {
    if ([regex]::IsMatch($dialogSource,
            ([regex]::Escape($pointer) + '->SetWindowTextW?\s*\('))) {
        throw "Task 8 must not change a child window title through $pointer."
    }
}

foreach ($needle in @(
    '<ClInclude Include="CloudMatchStatusDisplay.h" />',
    '<ClCompile Include="CloudMatchStatusDisplay.cpp">',
    '<PrecompiledHeader>NotUsing</PrecompiledHeader>'
)) {
    if (-not $project.Contains($needle)) {
        throw "Cloud status project reference is missing: $needle"
    }
}
foreach ($needle in @(
    '<ClInclude Include="CloudMatchStatusDisplay.h">',
    '<ClCompile Include="CloudMatchStatusDisplay.cpp">'
)) {
    if (-not $filters.Contains($needle)) {
        throw "Cloud status project filter reference is missing: $needle"
    }
}

& node --check $webMainPath
if ($LASTEXITCODE -ne 0) {
    throw 'Cloud status Web JavaScript syntax check failed.'
}
& node $webTestPath
if ($LASTEXITCODE -ne 0) {
    throw 'Cloud status Web runtime tests failed.'
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
    throw 'Visual Studio C++ x64 compiler was not found for the status harness.'
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('DNFGameCapture-cloud-match-status-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
$exe = Join-Path $tempRoot 'cloud_match_status_test.exe'
try {
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" /nologo /utf-8 /std:c++17 /EHsc /W4 /Y- /I"{2}" /Fe:"{3}" /Fo:"{4}\\" "{5}" "{6}"' -f `
        $devCmd, $compiler, $root, $exe, $tempRoot, $testPath, $displaySourcePath
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw "Cloud match status test compilation failed with exit code $LASTEXITCODE."
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Cloud match status tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host 'Cloud match status static and runtime checks passed.' -ForegroundColor Green
