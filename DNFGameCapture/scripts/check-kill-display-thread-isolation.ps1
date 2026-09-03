$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$cppPath = Join-Path $root "DNFGameCaptureDlg.cpp"
$headerPath = Join-Path $root "DNFGameCaptureDlg.h"

if (-not (Test-Path -LiteralPath $cppPath) -or -not (Test-Path -LiteralPath $headerPath)) {
    Write-Error "Could not locate DNFGameCaptureDlg sources."
    exit 1
}

$cpp = Get-Content -LiteralPath $cppPath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$failures = New-Object System.Collections.Generic.List[string]

if ($cpp -notmatch '(?s)json\s+CDNFGameCaptureDlg::DnfBuildKillDisplayStateJson\(\).*?\n\}') {
    $failures.Add("Missing dedicated kill-display state builder.")
}

$payloadMatch = [regex]::Match(
    $cpp,
    '(?s)std::string\s+CDNFGameCaptureDlg::BuildKillDisplayStatePayload\(\)\s*\{(?<body>.*?)\n\}'
)
if (-not $payloadMatch.Success) {
    $failures.Add("Could not inspect BuildKillDisplayStatePayload().")
}
else {
    $body = $payloadMatch.Groups['body'].Value
    if ($body.Contains('DnfBuildSharedWebStateJson')) {
        $failures.Add("HTTP state endpoint must not build the full main-Web state.")
    }
    if (-not $body.Contains('DnfBuildKillDisplayStateJson')) {
        $failures.Add("HTTP state endpoint must use the dedicated kill-display snapshot.")
    }
}

$killBuilderMatch = [regex]::Match(
    $cpp,
    '(?s)json\s+CDNFGameCaptureDlg::DnfBuildKillDisplayStateJson\(\)\s*\{(?<body>.*?)\n\}'
)
if ($killBuilderMatch.Success) {
    $body = $killBuilderMatch.Groups['body'].Value
    foreach ($forbidden in @(
        'GetSelectedTargetWindowLabel',
        'IsWindowVisible',
        'IsKillDisplayWindowVisible',
        'm_cmbTargetWindow',
        'fullAliasDB',
        'recentEvents',
        'backupLiveSources',
        'scoreboardTextStyles'
    )) {
        if ($body.Contains($forbidden)) {
            $failures.Add("Kill-display HTTP snapshot contains forbidden UI/full-state access: $forbidden")
        }
    }
}

if ($header -notmatch 'json\s+DnfBuildKillDisplayStateJson\(\);') {
    $failures.Add("Missing DnfBuildKillDisplayStateJson declaration.")
}

if ($cpp -notmatch 'static\s+std::once_flag\s+s_fontCacheOnce;') {
    $failures.Add("Installed font cache must use std::once_flag for concurrent Web/HTTP access.")
}
if ($cpp -notmatch 'std::call_once\(s_fontCacheOnce') {
    $failures.Add("Installed font cache must initialize through std::call_once.")
}

$httpStartIndex = $cpp.IndexOf(
    'm_bKillDisplayHttpReady = DnfStartKillDisplayHttpServer(',
    [System.StringComparison]::Ordinal)
$matchLoadIndex = $cpp.IndexOf('LoadAliasDB();',
    [System.StringComparison]::Ordinal)
if ($httpStartIndex -lt 0 -or $matchLoadIndex -lt 0 -or
    $httpStartIndex -lt $matchLoadIndex) {
    $failures.Add("Kill-display HTTP must start only after startup match data is fully loaded.")
}

$flipHandlerMatch = [regex]::Match(
    $cpp,
    '(?s)void\s+CDNFGameCaptureDlg::OnBnClickedFlip\(\)\s*\{(?<body>.*?)\n\}'
)
if (-not $flipHandlerMatch.Success -or
    $flipHandlerMatch.Groups['body'].Value -notmatch '(?s)lock_guard<std::mutex>.*?m_bFlipSides\s*=') {
    $failures.Add("Flip state writes must be protected by m_dataMutex.")
}

$redPickHandlerMatch = [regex]::Match(
    $cpp,
    '(?s)else if \(action == "cmd_set_red_pick_mode"\)\s*\{(?<body>.*?)\n\s*\}'
)
if (-not $redPickHandlerMatch.Success -or
    $redPickHandlerMatch.Groups['body'].Value -notmatch '(?s)lock_guard<std::mutex>.*?m_bRedPickFirst\s*=') {
    $failures.Add("Red-pick state writes must be protected by m_dataMutex.")
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host "FAIL: $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Kill display thread-isolation checks passed." -ForegroundColor Green
