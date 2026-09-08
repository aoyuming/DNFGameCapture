$ErrorActionPreference = 'Stop'

function Require-Text([string]$text, [string]$needle, [string]$label) {
    if ($text -notlike "*$needle*") { throw "Missing $($label): $needle" }
}

function Forbid-Text([string]$text, [string]$needle, [string]$label) {
    if ($text -like "*$needle*") { throw "Forbidden $($label): $needle" }
}

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -like 'web*' } | Select-Object -First 1
if (-not $webRoot) { throw 'Cannot locate the web frontend directory.' }
$html = Get-Content (Join-Path $webRoot.FullName 'index.html') -Raw
$js = Get-Content (Join-Path $webRoot.FullName 'main.js') -Raw
$css = Get-Content (Join-Path $webRoot.FullName 'style.css') -Raw
$cpp = Get-Content (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw

Require-Text $html 'identity-members-pane' 'left member pane'
Require-Text $html 'identity-details-pane' 'right detail pane'
Require-Text $html 'identity-member-list' 'member list host'
Require-Text $html 'identity-detail-pane' 'detail host'
Require-Text $js 'renderIdentityDetailPane' 'detail renderer'
Require-Text $css 'grid-template-columns: minmax(250px, 34%) minmax(0, 1fr)' 'two-column layout'
Forbid-Text ($html + $js + $cpp) '绰号' 'old visible alias wording'

Write-Output 'player identity two-column checks passed'
