$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'DNFGameCaptureDlg.h'
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'

if (-not (Test-Path -LiteralPath $headerPath)) { throw "Missing file: $headerPath" }
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Missing file: $sourcePath" }

$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

function Require-Text([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

foreach ($needle in @(
    'StartOcrSupervisor',
    'StopOcrSupervisor',
    'RequestOcrSupervisorWork',
    'OcrSupervisorLoop',
    'WarmupOcrEngine',
    'm_ocrSupervisorThread',
    'm_ocrSupervisorCv',
    'm_bOcrSupervisorStop',
    'm_bOcrServiceReady',
    'm_bOcrEngineReady',
    'm_bStartAfterOcrReady'
)) {
    Require-Text $header $needle "OCR supervisor declaration is missing: $needle"
}

foreach ($needle in @(
    'void CDNFGameCaptureDlg::StartOcrSupervisor()',
    'void CDNFGameCaptureDlg::StopOcrSupervisor()',
    'void CDNFGameCaptureDlg::RequestOcrSupervisorWork()',
    'void CDNFGameCaptureDlg::OcrSupervisorLoop()',
    'bool CDNFGameCaptureDlg::WarmupOcrEngine()',
    'sei.lpDirectory',
    'L"/api/ocr"',
    'SEE_MASK_NOCLOSEPROCESS',
    'm_hOcrTrackedProcess = sei.hProcess',
    'CloseTrackedOcrProcess();',
    '::CloseHandle(m_hOcrTrackedProcess)',
    'StartOcrSupervisor();',
    'StopOcrSupervisor();'
)) {
    Require-Text $source $needle "OCR supervisor implementation is missing: $needle"
}

$bootstrapStart = $source.IndexOf('void CDNFGameCaptureDlg::BeginOcrServiceBootstrap()')
$bootstrapEnd = $source.IndexOf('LRESULT CDNFGameCaptureDlg::OnOcrStartResult', $bootstrapStart)
if ($bootstrapStart -lt 0 -or $bootstrapEnd -le $bootstrapStart) {
    throw 'Unable to inspect the OCR bootstrap function.'
}
$bootstrapBody = $source.Substring($bootstrapStart, $bootstrapEnd - $bootstrapStart)
if ($bootstrapBody.Contains('std::thread')) {
    throw 'OCR bootstrap must not create an unmanaged detached thread.'
}

$recoveryStart = $source.IndexOf('void CDNFGameCaptureDlg::BeginOcrServiceRecovery')
$recoveryEnd = $source.IndexOf('LRESULT CDNFGameCaptureDlg::OnOcrRecoverResult', $recoveryStart)
if ($recoveryStart -lt 0 -or $recoveryEnd -le $recoveryStart) {
    throw 'Unable to inspect the OCR recovery function.'
}
$recoveryBody = $source.Substring($recoveryStart, $recoveryEnd - $recoveryStart)
if ($recoveryBody.Contains('std::thread')) {
    throw 'OCR recovery must not create an unmanaged detached thread.'
}

$timerStart = $source.IndexOf('else if (nID == 7)')
$timerEnd = $source.IndexOf('// ============================================================================', $timerStart + 20)
if ($timerStart -lt 0 -or $timerEnd -le $timerStart) {
    throw 'Unable to inspect the OCR watchdog timer block.'
}
$timerBody = $source.Substring($timerStart, $timerEnd - $timerStart)
Require-Text $timerBody 'RequestOcrSupervisorWork' 'Timer 7 does not wake the OCR supervisor.'
$requestIndex = $timerBody.IndexOf('RequestOcrSupervisorWork', [System.StringComparison]::Ordinal)
$legacyGateIndex = $timerBody.IndexOf('if (m_bIsRunning && !m_bOcrStartPending', [System.StringComparison]::Ordinal)
if ($legacyGateIndex -ge 0 -and $legacyGateIndex -lt $requestIndex) {
    throw 'Timer 7 still gates the OCR watchdog behind m_bIsRunning.'
}

$startMethodStart = $source.IndexOf('void CDNFGameCaptureDlg::OnBnClickedStart()', [System.StringComparison]::Ordinal)
$startMethodEnd = $source.IndexOf('LRESULT CDNFGameCaptureDlg::OnOcrServiceFail', $startMethodStart, [System.StringComparison]::Ordinal)
if ($startMethodStart -lt 0 -or $startMethodEnd -le $startMethodStart) {
    throw 'Unable to inspect the start button path.'
}
$startMethodBody = $source.Substring($startMethodStart, $startMethodEnd - $startMethodStart)
if ($startMethodBody.Contains('ProbeOcrServiceReady()')) {
    throw 'The start button still performs synchronous OCR probing.'
}
Require-Text $startMethodBody 'm_bOcrEngineReady.load' 'The start button does not use atomic OCR readiness.'

Write-Host 'OCR background supervisor static checks passed.' -ForegroundColor Green
