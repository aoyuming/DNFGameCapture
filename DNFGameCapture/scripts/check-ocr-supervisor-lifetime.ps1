$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root "DNFGameCaptureDlg.h"
$sourcePath = Join-Path $root "DNFGameCaptureDlg.cpp"

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
    "StartOcrMatchingTask",
    "StopOcrMatchingTasks",
    "WaitForOcrMatchingTasks",
    "RegisterOcrSupervisorRequest",
    "ReleaseOcrSupervisorRequest",
    "CancelOcrSupervisorRequest",
    "m_ocrTaskCv",
    "m_ocrTaskCount",
    "m_bOcrTaskStop",
    "m_hOcrSupervisorRequest"
)) {
    Require-Text $header $needle "OCR lifetime declaration is missing: $needle"
}

foreach ($needle in @(
    "RestartOcrProcessForRecovery",
    "m_hOcrTrackedProcess",
    "m_ocrTrackedProcessId",
    "m_ocrProcessNotReadySince"
)) {
    Require-Text $header $needle "OCR process recovery declaration is missing: $needle"
}

foreach ($needle in @(
    "void CDNFGameCaptureDlg::StartOcrMatchingTask(int triggerSide)",
    "void CDNFGameCaptureDlg::StopOcrMatchingTasks()",
    "void CDNFGameCaptureDlg::WaitForOcrMatchingTasks()",
    "bool CDNFGameCaptureDlg::RegisterOcrSupervisorRequest(HINTERNET hRequest)",
    "bool CDNFGameCaptureDlg::ReleaseOcrSupervisorRequest(HINTERNET hRequest)",
    "void CDNFGameCaptureDlg::CancelOcrSupervisorRequest()"
)) {
    Require-Text $source $needle "OCR lifetime implementation is missing: $needle"
}

foreach ($needle in @(
    "void CDNFGameCaptureDlg::RestartOcrProcessForRecovery()",
    "WaitForSingleObject",
    "GetExitCodeProcess",
    "TerminateProcess",
    "staleProcessTimeoutMs",
    "EnsureOcrRunning(forceRestart)"
)) {
    Require-Text $source $needle "OCR process recovery implementation is missing: $needle"
}

$triggerStart = $source.IndexOf("auto fireActiveXTrigger")
$triggerEnd = $source.IndexOf("if (g_pendingActiveDeathSide >= 0)", $triggerStart)
if ($triggerStart -lt 0 -or $triggerEnd -le $triggerStart) {
    throw "Unable to inspect the automatic OCR task launch."
}
$triggerBody = $source.Substring($triggerStart, $triggerEnd - $triggerStart)
if ($triggerBody.Contains("std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask")) {
    throw "Automatic OCR matching must use the tracked task launcher."
}
Require-Text $triggerBody "StartOcrMatchingTask(deadSide)" "Automatic OCR matching launcher is missing."

$manualStart = $source.IndexOf("void CDNFGameCaptureDlg::ManualTriggerKill(int killSide)")
$manualEnd = $source.IndexOf("LRESULT CDNFGameCaptureDlg::OnWGCInitDone", $manualStart)
if ($manualStart -lt 0 -or $manualEnd -le $manualStart) {
    throw "Unable to inspect the manual OCR task launch."
}
$manualBody = $source.Substring($manualStart, $manualEnd - $manualStart)
if ($manualBody.Contains("std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask")) {
    throw "Manual OCR matching must use the tracked task launcher."
}
Require-Text $manualBody "StartOcrMatchingTask(killSide)" "Manual OCR matching launcher is missing."

$destructorStart = $source.IndexOf("CDNFGameCaptureDlg::~CDNFGameCaptureDlg()")
$destructorEnd = $source.IndexOf("void CDNFGameCaptureDlg::InitTrayIcon()", $destructorStart)
if ($destructorStart -lt 0 -or $destructorEnd -le $destructorStart) {
    throw "Unable to inspect the dialog destructor."
}
$destructorBody = $source.Substring($destructorStart, $destructorEnd - $destructorStart)
Require-Text $destructorBody "StopOcrMatchingTasks();" "Destructor must wait for OCR matching tasks."
$waitIndex = $destructorBody.IndexOf("StopOcrMatchingTasks();")
$closeIndex = $destructorBody.IndexOf("WinHttpCloseHandle(m_hHttpConnect)")
if ($closeIndex -lt 0 -or $waitIndex -gt $closeIndex) {
    throw "OCR matching tasks must stop before shared WinHTTP handles close."
}

$exitStart = $source.IndexOf("void CDNFGameCaptureDlg::DoRealExit()")
$exitEnd = $source.IndexOf("bool CDNFGameCaptureDlg::RejectLocalMatchEditWhileRealtime()", $exitStart)
if ($exitStart -lt 0 -or $exitEnd -le $exitStart) {
    throw "Unable to inspect the real-exit path."
}
$exitBody = $source.Substring($exitStart, $exitEnd - $exitStart)
Require-Text $exitBody "StopOcrMatchingTasks();" "Real-exit path must stop OCR matching tasks."

$stopStart = $source.IndexOf("void CDNFGameCaptureDlg::StopOcrSupervisor()")
$stopEnd = $source.IndexOf("void CDNFGameCaptureDlg::RequestOcrSupervisorWork()", $stopStart)
if ($stopStart -lt 0 -or $stopEnd -le $stopStart) {
    throw "Unable to inspect supervisor shutdown."
}
$stopBody = $source.Substring($stopStart, $stopEnd - $stopStart)
Require-Text $stopBody "CancelOcrSupervisorRequest();" "Supervisor shutdown must cancel the active HTTP request."

$ensureStart = $source.IndexOf("bool CDNFGameCaptureDlg::EnsureOcrRunning(bool forceRestart)")
$ensureEnd = $source.IndexOf("void CDNFGameCaptureDlg::FilterLivePlatformPrefixes", $ensureStart)
if ($ensureStart -lt 0 -or $ensureEnd -le $ensureStart) {
    throw "Unable to inspect OCR launch policy."
}
$ensureBody = $source.Substring($ensureStart, $ensureEnd - $ensureStart)
Require-Text $ensureBody "if (!processRunning)" "OCR launch must be guarded by process absence."
if ($ensureBody.Contains("forceRestart || !processRunning || now - m_lastLaunchOcrTime")) {
    throw "An existing Umi-OCR process must not trigger a duplicate launch by elapsed time alone."
}

Write-Host "OCR supervisor lifetime static checks passed." -ForegroundColor Green
