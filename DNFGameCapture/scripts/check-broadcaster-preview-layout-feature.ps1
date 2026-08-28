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
$webDialogHeader = Read-RequiredFile (Join-Path $root 'WebScoreDlg.h')
$webDialogSource = Read-RequiredFile (Join-Path $root 'WebScoreDlg.cpp')
$dialogSource = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')

foreach ($id in @('broadcaster-list', 'broadcaster-list-count')) {
    Require-Text $index ('id="' + $id + '"') "Merged broadcaster list element '$id' is missing."
}
$mergedTitle = [string]([char]0x4E3B + [char]0x64AD + [char]0x5217 + [char]0x8868)
Require-Text $index ('<div class="broadcaster-sidebar-title">' + $mergedTitle + '</div>') 'The merged sidebar title is outdated.'
foreach ($legacyId in @('broadcaster-online-list', 'broadcaster-offline-list')) {
    Reject-Text $index ('id="' + $legacyId + '"') "Split broadcaster list '$legacyId' still exists."
}

foreach ($needle in @(
    'function cloudOfflineElapsed(',
    'function sortBroadcasterDirectory(',
    'setInterval(() => renderBroadcasterSidebar(), 30 * 1000);',
    "action: 'cmd_set_broadcaster_preview_open', open: true",
    "action: 'cmd_set_broadcaster_preview_open', open: false",
    'main.title =',
    'aliases.title ='
)) {
    Require-Text $main $needle "Broadcaster preview/list behavior is missing: $needle"
}

foreach ($needle in @(
    '.broadcaster-list-section.unified',
    '.broadcaster-card.offline',
    '.broadcaster-preview-player-identity strong',
    '.broadcaster-preview-player-identity span'
)) {
    Require-Text $style $needle "Broadcaster preview/list style is missing: $needle"
}

foreach ($needle in @(
    '.broadcaster-preview-title { font-size: 20px;',
    'font-size: 12px;',
    'min-height: 56px;',
    '.broadcaster-preview-player-identity strong { font-size: 17px;',
    'font: 700 11px Consolas, monospace;',
    'font: 800 12px "Microsoft YaHei", sans-serif;'
)) {
    Require-Text $style $needle "Broadcaster preview typography is not enlarged: $needle"
}
Require-Text $index '20260829-console-on-demand' 'The broadcaster preview cache version was not updated.'
Require-Text $main "const WEB_LAYOUT_VERSION = '20260829-console-on-demand';" 'The Web bridge layout version does not match the preview assets.'

Require-Text $webDialogHeader 'SetBroadcasterPreviewExpanded(bool expanded)' 'The Web host has no preview expansion API.'
Require-Text $webDialogHeader 'm_broadcasterPreviewExpanded' 'The Web host does not track preview expansion independently.'
Require-Text $webDialogSource 'kBroadcasterPreviewClientHeight = 760' 'The preview client height is not defined.'
Require-Text $webDialogSource 'ApplyExpandedWindowSize()' 'The Web host does not centralize expanded window sizing.'
Require-Text $dialogSource 'cmd_set_broadcaster_preview_open' 'The C++ bridge does not receive preview visibility changes.'
Require-Text $dialogSource 'SetBroadcasterPreviewExpanded' 'The C++ bridge does not resize for broadcaster preview.'

Write-Host 'Broadcaster preview layout static checks passed.'
