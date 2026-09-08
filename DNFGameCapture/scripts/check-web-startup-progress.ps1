$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webDir = (Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -like 'web*' } | Select-Object -First 1).FullName
$header = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.h') -Raw
$cpp = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw
$html = Get-Content -LiteralPath (Join-Path $webDir 'index.html') -Raw
$js = Get-Content -LiteralPath (Join-Path $webDir 'main.js') -Raw
$css = Get-Content -LiteralPath (Join-Path $webDir 'style.css') -Raw

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Assert-Contains $header 'WM_STARTUP_STAGE' 'missing startup stage message'
Assert-Contains $header 'OnStartupStage' 'missing startup stage handler'
Assert-Contains $header 'WM_ALIAS_MANUAL_SYNC_RESULT' 'missing manual alias sync message'
Assert-Contains $cpp 'PostMessage(WM_STARTUP_STAGE' 'heavy initialization is still in constructor'
Assert-Contains $cpp 'StartManualAliasDbSync' 'manual alias sync is not dispatched to background work'
Assert-Contains $cpp 'DnfFetchPublicAliasDb' 'manual alias pull has no network worker helper'
if ($html -match 'cloud-progress-bar' -or $js -match 'startup_progress|cloud_progress|renderCloudProgressBar|updateStartupProgress|updateCloudProgress' -or $css -match '\.cloud-progress-bar') {
    throw 'The visible startup/cloud progress bar must be removed.'
}
if ($header -match 'SetProgressPanelVisible|m_progressPanel' -or $cpp -match 'SetProgressPanelVisible|m_progressPanel|kProgressExtraClientHeight') {
    throw 'Progress-bar-specific native window sizing must be removed.'
}

Write-Output 'web startup progress removal static check passed'
