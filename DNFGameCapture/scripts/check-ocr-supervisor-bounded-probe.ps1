$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Missing file: $sourcePath" }

$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

function Require-Text([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

foreach ($needle in @(
    'static bool DnfOcrHttpRequest',
    'WSAStartup(MAKEWORD(2, 2)',
    'WSACleanup()',
    'ioctlsocket',
    'select(0',
    'SO_ERROR',
    'Connection: close',
    'DnfOcrHttpRequest("GET", "/",',
    'DnfOcrHttpRequest("POST", "/api/ocr",'
)) {
    Require-Text $source $needle "Bounded OCR request contract is missing: $needle"
}

$probeStart = $source.IndexOf('bool CDNFGameCaptureDlg::ProbeOcrServiceReady()')
$probeEnd = $source.IndexOf('bool CDNFGameCaptureDlg::RefreshOcrExePathFromRunningProcess', $probeStart)
if ($probeStart -lt 0 -or $probeEnd -le $probeStart) {
    throw 'Unable to inspect the OCR readiness probe.'
}
$probeBody = $source.Substring($probeStart, $probeEnd - $probeStart)
if ($probeBody.Contains('WinHttpReceiveResponse')) {
    throw 'OCR readiness probe must not use an unbounded synchronous WinHTTP response wait.'
}
Require-Text $probeBody 'DnfOcrHttpRequest("GET", "/",' 'OCR readiness probe is not using the bounded request helper.'

$warmupStart = $source.IndexOf('bool CDNFGameCaptureDlg::WarmupOcrEngine()')
$warmupEnd = $source.IndexOf('bool CDNFGameCaptureDlg::IsTrackedOcrProcessAlive()', $warmupStart)
if ($warmupStart -lt 0 -or $warmupEnd -le $warmupStart) {
    throw 'Unable to inspect the OCR warmup request.'
}
$warmupBody = $source.Substring($warmupStart, $warmupEnd - $warmupStart)
if ($warmupBody.Contains('WinHttpReceiveResponse')) {
    throw 'OCR warmup must not use an unbounded synchronous WinHTTP response wait.'
}
Require-Text $warmupBody 'DnfOcrHttpRequest("POST", "/api/ocr",' 'OCR warmup is not using the bounded request helper.'

$ensureStart = $source.IndexOf('bool CDNFGameCaptureDlg::EnsureOcrRunning(bool forceRestart)')
$ensureEnd = $source.IndexOf('void CDNFGameCaptureDlg::FilterLivePlatformPrefixes', $ensureStart)
if ($ensureStart -lt 0 -or $ensureEnd -le $ensureStart) {
    throw 'Unable to inspect the OCR startup wait.'
}
$ensureBody = $source.Substring($ensureStart, $ensureEnd - $ensureStart)
Require-Text $ensureBody 'startupWaitDeadline' 'OCR startup wait has no total deadline.'
Require-Text $ensureBody 'processObserved' 'OCR startup wait does not detect a process disappearing mid-start.'
if ($ensureBody.Contains('for (int i = 0; i < 30; ++i)')) {
    throw 'OCR startup must not multiply a per-probe timeout by a fixed 30-iteration wait.'
}

Write-Host 'OCR supervisor bounded probe static checks passed.' -ForegroundColor Green
