$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$webRoot = Get-ChildItem -LiteralPath $root -Directory |
    Where-Object { $_.Name -like 'web*' } | Select-Object -First 1
if ($null -eq $webRoot) { throw 'Missing web frontend directory.' }
$main = Get-Content -LiteralPath (Join-Path $webRoot.FullName 'main.js') -Raw
$style = Get-Content -LiteralPath (Join-Path $webRoot.FullName 'style.css') -Raw

if ($main -notmatch 'identity-suggestion-list') {
    throw 'The identity overlap rows must have a dedicated scroll container.'
}
if ($main -notmatch '(?s)function visibleIdentityOverlaps\(\).*?\.filter\(.*?commonIdCount\s*\|\|\s*0\)\s*>=\s*1') {
    throw 'The identity overlap list must exclude entries with zero shared IDs.'
}
if ($style -notmatch '(?s)\.identity-suggestions\s*\{.*?overflow:\s*hidden') {
    throw 'The identity suggestion shell must clip its fixed-height content.'
}
if ($style -notmatch '(?s)\.identity-suggestion-list\s*\{.*?overflow-y:\s*auto') {
    throw 'The identity suggestion rows must scroll inside their own container.'
}
if ($style -notmatch '--identity-suggestion-list-height:\s*94px') {
    throw 'The identity suggestion list must default to two 44px rows and one 6px gap.'
}
if ($main -notmatch 'visibleRows\s*=\s*Math\.min\(2,\s*rows\.length\)' -or
    $main -notmatch 'rowHeight \* visibleRows \+ gap \* \(visibleRows - 1\)') {
    throw 'Wrapped suggestion rows must be measured with at most two visible rows.'
}
if ($style -notmatch '(?s)\.identity-suggestion-list\s*\{.*?flex:\s*0\s+0\s+var\(--identity-suggestion-list-height\);.*?height:\s*var\(--identity-suggestion-list-height\);.*?max-height:\s*var\(--identity-suggestion-list-height\);') {
    throw 'The identity suggestion row area must use editable height variables.'
}
if ($style -notmatch '(?s)\.identity-suggestion-list\s*\{.*?display:\s*grid;') {
    throw 'The identity suggestion rows must have stable row layout.'
}
if ($style -notmatch '(?s)\.identity-suggestion-row\s*\{.*?min-height:\s*44px;') {
    throw 'The identity suggestion rows must reserve a readable two-row height.'
}
if ($style -notmatch '(?s)\.identity-suggestions\[hidden\]\s*\{\s*display:\s*none;') {
    throw 'An empty identity suggestion shell must leave the layout.'
}
if ($style -notmatch '(?s)\.identity-panel\s*\{.*?height:\s*min\(820px,\s*calc\(100vh\s*-\s*28px\)\);.*?overflow-y:\s*hidden;') {
    throw 'The identity panel must keep a stable height while its sections reflow.'
}
if ($style -notmatch '(?s)\.identity-workspace\s*\{.*?flex:\s*1\s+1\s+auto;.*?min-height:\s*0;.*?max-height:\s*none;') {
    throw 'The identity workspace must receive space released by hidden suggestions.'
}

Write-Output 'Identity overlap scroll check passed.'
