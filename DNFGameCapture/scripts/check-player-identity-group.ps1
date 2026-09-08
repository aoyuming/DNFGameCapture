param([string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat')
$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $sourceRoot 'DNFGameCaptureDlg.cpp'
$header = Join-Path $sourceRoot 'DNFGameCaptureDlg.h'
$resources = Join-Path $sourceRoot 'DNFGameCapture.rc'
$serviceHeader = Join-Path $sourceRoot 'PlayerIdentityGroupService.h'
$serviceCpp = Join-Path $sourceRoot 'PlayerIdentityGroupService.cpp'
$project = Join-Path $sourceRoot 'DNFGameCapture.vcxproj'
$filters = Join-Path $sourceRoot 'DNFGameCapture.vcxproj.filters'
$webRoot = Get-ChildItem -LiteralPath $sourceRoot -Directory |
    Where-Object { $_.Name -like 'web*' } | Select-Object -First 1
if ($null -eq $webRoot) { throw 'Missing web frontend directory.' }
$index = Join-Path $webRoot.FullName 'index.html'
$main = Join-Path $webRoot.FullName 'main.js'
$style = Join-Path $webRoot.FullName 'style.css'

function Require-File([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing file: $path"
    }
}

function Require-Text([string]$path, [string]$pattern, [string]$description) {
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) {
        throw "Missing $description in $path"
    }
}

foreach ($path in @($cpp, $header, $resources, $serviceHeader, $serviceCpp, $project, $filters, $index, $main, $style)) {
    Require-File $path
}

Require-Text $project 'PlayerIdentityGroupService\.h' 'identity service header project entry'
Require-Text $project 'PlayerIdentityGroupService\.cpp' 'identity service source project entry'
Require-Text $filters 'PlayerIdentityGroupService\.h' 'identity service header filter entry'
Require-Text $filters 'PlayerIdentityGroupService\.cpp' 'identity service source filter entry'

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-player-identity-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    if (-not (Test-Path -LiteralPath $VsDevCmd)) { throw 'MSVC developer command file not found.' }
    $test = Join-Path $PSScriptRoot 'player_identity_group_test.cpp'
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Y- /Fe:"{1}\identity_test.exe" /Fo:"{1}\\" "{2}" "{3}"' -f $VsDevCmd, $tempRoot, $serviceCpp, $test
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Player identity compilation failed.' }
    & (Join-Path $tempRoot 'identity_test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Player identity tests failed.' }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}

Require-Text $serviceHeader 'AUTO_GROUP_SHARED_ID_THRESHOLD\s*=\s*5' 'strong-overlap auto-group threshold'
Require-Text $serviceHeader 'AUTO_GROUP_POLICY_VERSION\s*=\s*5' 'auto-group policy version'
if ((Get-Content -LiteralPath $serviceHeader -Raw) -match '(?i)adventure') { throw 'Retired identifier API remains in the identity service.' }
Require-Text $serviceCpp 'AUTO_GROUP_SHARED_ID_THRESHOLD' 'strong-overlap auto-group logic'
Require-Text $serviceCpp 'ComputeAliasEntriesFingerprint' 'identity entry fingerprint helper'
Require-Text $cpp 'autoGroupPolicyVersion' 'auto-group policy persistence'
Require-Text $cpp 'needsAutoGroupPolicyMigration' 'legacy auto-group ignore migration'
Require-Text $cpp 'm_playerIdentityStateCacheValid' 'cached identity state'
Require-Text (Join-Path $sourceRoot 'scripts\player_identity_group_test.cpp') 'TestStrongOverlapAutoGroups' 'strong-overlap identity test'

foreach ($command in @(
        'cmd_identity_refresh',
        'cmd_identity_merge',
        'cmd_identity_add_alias',
        'cmd_identity_update_ids',
        'cmd_identity_unmerge',
        'cmd_identity_delete_alias',
        'cmd_identity_ignore_overlap')) {
    Require-Text $cpp ([regex]::Escape($command)) "C++ command $command"
    Require-Text $main ([regex]::Escape($command)) "Web command $command"
}

Require-Text $cpp 'DeletePlayerIdentityAlias' 'identity alias deletion handler'
Require-Text $main 'identity-delete-alias' 'Web identity alias deletion action'
Require-Text $main '\u5220\u9664\u522b\u540d' 'Web identity alias deletion label'
Require-Text $main '\u6dfb\u52a0\u522b\u540d' 'Web identity alias add label'
Require-Text $main 'identity-id-editor' 'inline identity ID editor'
Require-Text $main 'identity-id-remove' 'inline identity ID remove action'
Require-Text $main 'identityExpandedIdGroups' 'collapsed identity ID editor state'
Require-Text $main 'identity-edit-ids' 'identity ID edit toggle'
Require-Text $main 'identity-game-id-edit' 'inline game ID edit action'

foreach ($symbol in @(
        'LoadPlayerIdentityGroups',
        'SavePlayerIdentityGroups',
        'BuildPlayerIdentityStateJson',
        'RefreshActivePlayerAliasLists',
        'player_identity_groups\.json')) {
    Require-Text $cpp $symbol "identity persistence symbol $symbol"
}

foreach ($element in @('btn-identity-groups', 'identity-overlay', 'identity-member-list', 'identity-detail-pane')) {
    Require-Text $index ([regex]::Escape($element)) "Web element $element"
}
Require-Text $index 'identity-selection-summary' 'fixed identity selection summary'
Require-Text $index 'identity-selected-names' 'selected identity names element'
Require-Text $main 'playerIdentityState' 'Web identity state'
Require-Text $main 'getIdentityGroupForName' 'Web group lookup'
Require-Text $main 'identity-suggestion-ignore' 'ignore overlap suggestion action'
Require-Text $main 'identityFocusedName' 'focused identity name state'
Require-Text $main 'identity-focused-member' 'focused identity member styling hook'
Require-Text $main 'scrollIntoView' 'focused identity auto-scroll'
Require-Text $main 'renderIdentitySelectionSummary' 'live identity selection summary'
Require-Text $main 'members\.push\(\{ name, group, ids: group\.ids' 'stable grouped-player ordering in cached snapshot'
Require-Text $main 'for \(const entry of standalone\) \{ members\.push\(entry\)' 'standalone players appended after groups'
$memberSort = [regex]::Match($main, 'visibleMembers\.sort\([\s\S]*?\);')
if ($memberSort.Success -and $memberSort.Value -match 'right\.name\s*===\s*focusedName') {
    throw 'Clicking a player must not promote it to the first row.'
}
Require-Text $cpp 'm_playerIdentityAutoSplitFingerprints\.find' 'overlap ignore fingerprint filtering'
Require-Text $style 'identity-link-button' 'identity link button styling'
Require-Text $style 'identity-selection-summary' 'identity selection summary styling'
Require-Text $style 'background: rgba\(180, 24, 48, 0\.22\)' 'red focused identity background'
Require-Text $style 'border-color: rgba\(255, 80, 110, 0\.72\)' 'red focused identity border'
Require-Text $header '#define CURRENT_VERSION L"5\.1\.0"' 'application version 5.1.0'
Require-Text $resources 'FILEVERSION 5,1,0,0' 'resource file version 5.1.0'

$payloadStart = $text = Get-Content -LiteralPath $cpp -Raw
$payloadMatch = [regex]::Match(
    $payloadStart,
    'std::string CDNFGameCaptureDlg::BuildAliasDbJsonPayload[\s\S]*?std::string CDNFGameCaptureDlg::BuildAliasDbAppendPayload')
if (-not $payloadMatch.Success) {
    throw 'Unable to isolate the flat alias payload builders.'
}
if ($payloadMatch.Value -match 'player_identity_groups|playerIdentity|beforeMerge|autoSplitFingerprints') {
    throw 'Identity-group metadata leaked into the cloud alias payload builders.'
}

Write-Output 'Player identity group static checks passed.'
