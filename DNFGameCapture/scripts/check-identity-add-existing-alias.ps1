$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$webRoot = Get-ChildItem -LiteralPath $sourceRoot -Directory |
    Where-Object { $_.Name -like 'web*' } | Select-Object -First 1
if ($null -eq $webRoot) { throw 'Missing web frontend directory.' }
$main = Join-Path $webRoot.FullName 'main.js'
$style = Join-Path $webRoot.FullName 'style.css'

function Require-Text([string]$path, [string]$pattern, [string]$description) {
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -notmatch $pattern) {
        throw "Missing $description in $path"
    }
}

Require-Text $main 'findIdentityEntryByName' 'existing identity name lookup'
Require-Text $main 'confirmExistingIdentityAlias' 'existing identity confirmation flow'
Require-Text $main 'identity-existing-alias-preview' 'existing identity merge preview'
Require-Text $main 'cmd_identity_merge' 'existing identity merge command'
Require-Text $main 'existingGroup\.groupId === sourceGroup\.groupId' 'same-group duplicate guard'
Require-Text $style 'identity-existing-alias-preview' 'existing identity preview styling'

Write-Output 'Existing identity alias static checks passed.'
