$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webDir = (Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -like 'web*' } | Select-Object -First 1).FullName
$mainJs = Get-Content -Raw (Join-Path $webDir 'main.js')
$dialogCpp = Get-Content -Raw (Join-Path $root 'DNFGameCaptureDlg.cpp')
$dialogHeader = Get-Content -Raw (Join-Path $root 'DNFGameCaptureDlg.h')

$standaloneAction = 'data-source-name="@@DL@@{escapeHtml(name)}">添加别名</button>'
$standaloneActionIndex = $mainJs.IndexOf($standaloneAction.Replace('@@DL@@', '$'))
if ($standaloneActionIndex -lt 0) {
    throw 'Standalone editor button regression.'
}
if ($mainJs.IndexOf("renderIdentityIdChips(group, name, ids)") -lt 0) {
    throw 'Standalone game chip controls must remain available.'
}

if (($mainJs.IndexOf("ids.slice(0, 4") -ge 0) -or ($mainJs.IndexOf("identity-id-more") -ge 0) -or ($mainJs.IndexOf("remainingCount") -ge 0)) {
    throw 'Game ID summary still truncates.'
}

if ($dialogHeader -notmatch 'UpdateStandalonePlayerIdentityIds') {
    throw 'Standalone update declaration is missing.'
}

if ($dialogCpp -notmatch 'UpdateStandalonePlayerIdentityIds') {
    throw 'Standalone update implementation or dispatch is missing.'
}

Write-Output 'Identity standalone editor regression checks passed.'
