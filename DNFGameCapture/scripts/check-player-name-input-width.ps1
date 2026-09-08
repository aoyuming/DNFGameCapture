$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$webRoot = Get-ChildItem -LiteralPath $root -Directory |
    Where-Object { $_.Name -like 'web*' } | Select-Object -First 1
if ($null -eq $webRoot) { throw 'Missing web frontend directory.' }
$style = Get-Content -LiteralPath (Join-Path $webRoot.FullName 'style.css') -Raw

if ($style -notmatch '(?s)\.name-input\s*\{.*?flex:\s*1\s+1\s+90px;.*?min-width:\s*90px;') {
    throw 'The player name input must reserve at least 90px for names such as 90白羽.'
}

Write-Output 'Player name input width check passed.'
