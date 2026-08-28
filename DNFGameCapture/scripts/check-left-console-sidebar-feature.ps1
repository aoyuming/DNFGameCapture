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

function Unicode-Text([int[]]$codes) {
    return -join ($codes | ForEach-Object { [char]$_ })
}

$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')
$dialog = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$webDialog = Read-RequiredFile (Join-Path $root 'WebScoreDlg.cpp')

$layoutMeta = [regex]::Match($index, '<meta name="dnf-web-layout-version" content="([^"]+)">')
$layoutScript = [regex]::Match($main, "const WEB_LAYOUT_VERSION = '([^']+)';")
if (-not $layoutMeta.Success -or -not $layoutScript.Success -or
    $layoutMeta.Groups[1].Value -ne $layoutScript.Groups[1].Value) {
    throw 'The HTML and JavaScript layout generations must match.'
}
Reject-Text $main 'const heightScale =' `
    'The whole Web shell must not shrink vertically when a panel increases document height.'
Require-Text $main 'const fitScale = Math.max(0.5, Math.min(1, widthScale));' `
    'The whole Web shell must fit only to the available width.'

$consolePosition = $index.IndexOf('<aside class="console-sidebar"')
$centerPosition = $index.IndexOf('<div class="left-stack">')
$broadcasterPosition = $index.IndexOf('<aside class="broadcaster-sidebar"')
if ($consolePosition -lt 0 -or $centerPosition -lt 0 -or $broadcasterPosition -lt 0 -or
    $consolePosition -ge $centerPosition -or $centerPosition -ge $broadcasterPosition) {
    throw 'The Web shell must place the console, scoreboard, and broadcaster list in left-to-right order.'
}

$runLogTitle = Unicode-Text @(0x8FD0, 0x884C, 0x65E5, 0x5FD7)
foreach ($needle in @(
    'id="console-count"',
    'id="btn-console-clear"',
    'id="btn-console-close"',
    'id="console-list"',
    ('<span id="console-panel-title">' + $runLogTitle + '</span>')
)) {
    Require-Text $index $needle "The permanent console sidebar is incomplete: $needle"
}
Require-Text $index '<aside class="console-sidebar" id="console-panel"' 'The console sidebar is missing its stable panel id.'
Require-Text $index 'hidden aria-hidden="true"' 'The console sidebar must start hidden.'
Reject-Text $index 'id="btn-console-toggle"' 'The old More-menu console toggle still exists.'

foreach ($needle in @(
    '.console-sidebar,',
    '.broadcaster-sidebar {',
    'flex: 0 0 250px;',
    'width: 250px;',
    'max-height: 462px;',
    '.console-sidebar[hidden]',
    '.console-close {',
    '.console-list {',
    'flex: 1 1 auto;'
)) {
    Require-Text $style $needle "The left console/sidebar sizing is missing: $needle"
}
Reject-Text $style '.console-panel.is-open' 'The old floating console visibility state still exists.'
Require-Text $main 'isConsolePanelOpen' 'The Web client must track the on-demand console state.'
Require-Text $main 'function setConsolePanelOpen' 'The Web client is missing the console open/close handler.'
Require-Text $main 'cmd_set_console_panel_open' 'The Web client does not notify C++ about console width changes.'
Reject-Text $main 'toggleConsolePanel' 'The Web client still exposes the removed floating console toggle.'

Require-Text $webDialog 'constexpr int kCompactClientWidth = 980;' `
    'The Web host compact reference width is missing.'
Require-Text $webDialog 'constexpr int kExpandedClientWidth = 1240;' `
    'The Web host expanded reference width must include the console sidebar.'

foreach ($category in @(
    @(0x4E91, 0x7AEF, 0x8FDE, 0x63A5),
    @(0x4E91, 0x7AEF, 0x6570, 0x636E),
    @(0x4E00, 0x6B21, 0x540C, 0x6B65),
    @(0x5B9E, 0x65F6, 0x540C, 0x6B65),
    @(0x4E3B, 0x64AD, 0x540D, 0x79F0)
)) {
    $needle = '[' + (Unicode-Text $category) + ']'
    Require-Text $dialog $needle "Categorized cloud logging is missing."
}
foreach ($needle in @(
    'cloudRenameRequestPending',
    'showAlert(cloudMatchState.lastError)'
)) {
    Require-Text $main $needle "Cloud rename failures are not surfaced as popups: $needle"
}

Write-Host 'Left console sidebar static checks passed.'
