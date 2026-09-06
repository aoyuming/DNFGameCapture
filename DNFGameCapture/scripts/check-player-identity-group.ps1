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
    $text = Get-Content -LiteralPath $path -Raw
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
Require-Text $serviceHeader 'AUTO_GROUP_SHARED_ID_THRESHOLD\s*=\s*4' 'strong-overlap auto-group threshold'
Require-Text $serviceHeader 'AUTO_GROUP_POLICY_VERSION\s*=\s*2' 'auto-group policy version'
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
Require-Text $main '删除别名' 'Web identity alias deletion label'
Require-Text $main '添加别名' 'Web identity alias add label'
Require-Text $main 'identity-id-editor' 'inline identity ID editor'
Require-Text $main 'identity-id-remove' 'inline identity ID remove action'
Require-Text $main 'identityExpandedIdGroups' 'collapsed identity ID editor state'
Require-Text $main 'identity-edit-ids' 'identity ID edit toggle'
Require-Text $main '编辑游戏ID' 'identity ID edit toggle label'

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
Require-Text $main 'Number\(Boolean\(right\.group\)\)\s*-\s*Number\(Boolean\(left\.group\)\)' 'grouped players before standalone players'
Require-Text $main 'groupOrder' 'stable identity group ordering'
Require-Text $main 'memberOrder' 'stable name ordering inside an identity group'
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
