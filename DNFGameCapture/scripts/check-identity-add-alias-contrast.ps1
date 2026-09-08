$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webDir = (Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -like 'web*' } | Select-Object -First 1).FullName
$style = Get-Content -Raw (Join-Path $webDir 'style.css')

if ($style.IndexOf('.identity-add-alias') -lt 0) {
    throw 'Add-alias button needs a dedicated emphasis style.'
}

if ($style -notmatch '(?s)\.identity-add-alias\s*\{[^}]*background\s*:\s*[^;]+;[^}]*color\s*:\s*[^;]+;') {
    throw 'Add-alias button emphasis style must define a visible background and text color.'
}

Write-Output 'Identity add-alias contrast checks passed.'
