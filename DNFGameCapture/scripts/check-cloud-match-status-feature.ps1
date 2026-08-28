$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)
$displayHeaderPath = Join-Path $root 'CloudMatchStatusDisplay.h'
$displaySourcePath = Join-Path $root 'CloudMatchStatusDisplay.cpp'
$dialogSourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$resourceScriptPath = Join-Path $root 'DNFGameCapture.rc'
$webDialogSourcePath = Join-Path $root 'WebScoreDlg.cpp'
$webMainPath = Join-Path $webRoot 'main.js'
$webHtmlPath = Join-Path $webRoot 'index.html'
$webCssPath = Join-Path $webRoot 'style.css'
$testPath = Join-Path $PSScriptRoot 'cloud_match_status_test.cpp'
$webTestPath = Join-Path $PSScriptRoot 'cloud_match_status_web_test.js'

foreach ($path in @($displayHeaderPath, $displaySourcePath, $dialogSourcePath,
    $resourceScriptPath, $webDialogSourcePath, $webMainPath, $webHtmlPath,
    $webCssPath, $testPath, $webTestPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing status feature file: $path" }
}

$displayHeader = Get-Content -LiteralPath $displayHeaderPath -Raw -Encoding UTF8
$dialogSource = Get-Content -LiteralPath $dialogSourcePath -Raw -Encoding UTF8
$resourceScript = Get-Content -LiteralPath $resourceScriptPath -Raw -Encoding UTF8
$webDialogSource = Get-Content -LiteralPath $webDialogSourcePath -Raw -Encoding UTF8
$webMain = Get-Content -LiteralPath $webMainPath -Raw -Encoding UTF8
$webHtml = Get-Content -LiteralPath $webHtmlPath -Raw -Encoding UTF8
$webCss = Get-Content -LiteralPath $webCssPath -Raw -Encoding UTF8

foreach ($needle in @(
    'enum class CloudMatchDisplayState',
    'struct CloudMatchDisplayContext',
    'BuildCloudMatchDisplayStatus(',
    'CloudMatchDisplayStateName('
)) {
    if (-not $displayHeader.Contains($needle)) {
        throw "Cloud display contract is missing: $needle"
    }
}

$statusCreateStart = $dialogSource.IndexOf('if (!m_cloudMatchStatus.Create(')
$statusCreateEnd = $dialogSource.IndexOf('RefreshCloudMatchStatusDisplay(', $statusCreateStart)
if ($statusCreateStart -lt 0 -or $statusCreateEnd -le $statusCreateStart) {
    throw 'Unable to inspect the native cloud status control.'
}
$statusCreateBlock = $dialogSource.Substring($statusCreateStart,
    $statusCreateEnd - $statusCreateStart)
if ($statusCreateBlock.Contains('WS_VISIBLE')) {
    throw 'The professional window must not display the legacy cloud room status.'
}
if ($resourceScript.Contains('IDC_STATIC_CLOUD_ROOM_STATUS')) {
    throw 'The professional dialog resource still displays the legacy cloud room status.'
}

foreach ($needle in @(
    'id="broadcaster-sidebar"',
    'id="broadcaster-cloud-status"',
    'id="broadcaster-online-list"',
    'id="broadcaster-offline-list"'
)) {
    if (-not $webHtml.Contains($needle)) { throw "Broadcaster status markup is missing: $needle" }
}
foreach ($legacy in @('id="cloud-room-status"', 'id="cloud-room-overlay"',
    'id="cloud-sync-overlay"')) {
    if ($webHtml.Contains($legacy)) { throw "Legacy room status UI remains visible: $legacy" }
}
foreach ($needle in @(
    'function renderBroadcasterSidebar()',
    "document.getElementById('broadcaster-cloud-status')",
    'renderBroadcasterSidebar();',
    'cloudOfflineRemaining('
)) {
    if (-not $webMain.Contains($needle)) { throw "Broadcaster status wiring is missing: $needle" }
}
foreach ($needle in @(
    '.broadcaster-sidebar {',
    '.broadcaster-sidebar-subtitle[data-state="online"]',
    '.broadcaster-sidebar-subtitle[data-state="working"]',
    '.broadcaster-presence-dot',
    '.broadcaster-card.offline',
    'overflow-y: auto'
)) {
    if (-not $webCss.Contains($needle)) { throw "Broadcaster status style is missing: $needle" }
}
if (-not $webDialogSource.Contains('constexpr int kReferenceClientWidth = 980;')) {
    throw 'The Web window does not reserve 980 CSS px for the broadcaster sidebar.'
}

& node --check $webMainPath
if ($LASTEXITCODE -ne 0) { throw 'Web JavaScript syntax check failed.' }
& node $webTestPath
if ($LASTEXITCODE -ne 0) { throw 'Unified broadcaster Web state tests failed.' }

$installRoots = @(
    'E:\VS2026',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools'
)
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
    ('DNFGameCapture-cloud-status-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
$exe = Join-Path $tempRoot 'cloud_match_status_test.exe'
try {
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" /nologo /utf-8 /std:c++17 /EHsc /W4 /Y- /I"{2}" /Fe:"{3}" /Fo:"{4}\\" "{5}" "{6}"' -f `
        $devCmd, $compiler, $root, $exe, $tempRoot, $testPath, $displaySourcePath
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw "Cloud status test compilation failed with exit code $LASTEXITCODE."
    }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Cloud status tests failed with exit code $LASTEXITCODE." }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host 'Unified broadcaster status checks passed.' -ForegroundColor Green
