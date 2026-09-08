$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cpp = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw -Encoding UTF8
$webScore = Get-Content -LiteralPath (Join-Path $root 'WebScoreDlg.cpp') -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $cpp '[startup-timing]' 'startup timing log marker is missing'
Require-Text $cpp 'LoadDeathXCalibrationFromIni' 'calibration load must be timed'
Require-Text $cpp 'LoadConfigFromFile' 'config load must be timed'
Require-Text $cpp 'LoadAliasDB' 'alias database load must be timed'
Require-Text $cpp 'SyncDataToTree' 'tree synchronization must be timed'
Require-Text $cpp 'RefreshDisplay' 'display refresh must be timed'
Require-Text $cpp 'WriteScoreToFile' 'score file write must be timed'
Require-Text $cpp 'LoadPlayerIdentityGroups' 'identity group load must be timed'
Require-Text $cpp 'UpdateAndRefreshRecentList' 'recent player rebuild must be timed'
Require-Text $cpp 'LoadAliasCloudDeleteBaseline' 'cloud baseline load must be timed'
Require-Text $cpp 'ResetAliasDbCloudBaseline' 'cloud baseline reset must be timed'
Require-Text $cpp '[startup-timing] DnfBuildSharedWebStateJson' 'initial web state build must be timed'
Require-Text $cpp 'if (!m_cloudMatchWebReady) return;' 'startup must not build web state before page_ready'
Require-Text $webScore 'm_webViewInitStartedAt' 'WebView2 environment startup must be timed'
Require-Text $webScore 'm_webViewControllerStartedAt' 'WebView2 controller startup must be timed separately'

Write-Host 'startup load timing static check passed.'
