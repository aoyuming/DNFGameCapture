$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$headerPath = Join-Path $root 'DNFGameCaptureDlg.h'
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $header 'bool automatic = false, bool preserveLocalFlip = false,' `
    'Team snapshot application has no explicit local-flip preservation option.'
Require-Text $source 'if (!preserveLocalFlip && snapshot.contains("isFlipped")) {' `
    'Remote snapshot application does not preserve the local flip setting.'
$realtimeLabel = -join [char[]](0x5B9E, 0x65F6, 0x540C, 0x6B65)
$realtimeSource = -join [char[]](0x5B9E, 0x65F6)
$realtimeApply = 'true, true, &appliedEpoch, false, L"{0}", L"{1}")' -f `
    $realtimeLabel, $realtimeSource
Require-Text $source $realtimeApply `
    'Cloud realtime snapshots do not opt into local flip preservation.'

$blockedStart = $source.IndexOf('static bool DnfIsCloudRealtimeBlockedWebAction(')
$blockedEnd = $source.IndexOf('static CString DnfCloudMatchErrorText(', $blockedStart)
if ($blockedStart -lt 0 -or $blockedEnd -le $blockedStart) {
    throw 'Unable to inspect the realtime Web action block list.'
}
$blockedBody = $source.Substring($blockedStart, $blockedEnd - $blockedStart)
if ($blockedBody.Contains('cmd_swap')) {
    throw 'The Web flip command must stay available during realtime synchronization.'
}

$flipStart = $source.IndexOf('void CDNFGameCaptureDlg::OnBnClickedFlip()')
$flipEnd = $source.IndexOf('void CDNFGameCaptureDlg::OnBnClickedReset()', $flipStart)
if ($flipStart -lt 0 -or $flipEnd -le $flipStart) {
    throw 'Unable to inspect the native flip handler.'
}
$flipBody = $source.Substring($flipStart, $flipEnd - $flipStart)
if ($flipBody.Contains('RejectLocalMatchEditWhileRealtime()')) {
    throw 'The native flip handler must not be blocked during realtime synchronization.'
}
Require-Text $flipBody 'if (m_cloudRealtimeFollowing) {' `
    'The native flip handler must not advance the match mutation epoch during realtime synchronization.'

$lockStart = $source.IndexOf('void CDNFGameCaptureDlg::ApplyRealtimeEditingLock()')
$lockEnd = $source.IndexOf('bool CDNFGameCaptureDlg::RejectLocalMatchEditWhileRealtime()', $lockStart)
if ($lockStart -lt 0 -or $lockEnd -le $lockStart) {
    throw 'Unable to inspect the realtime editing lock.'
}
$lockBody = $source.Substring($lockStart, $lockEnd - $lockStart)
Require-Text $lockBody 'm_chkFlip.EnableWindow(TRUE)' `
    'The professional-mode flip checkbox must stay enabled during realtime synchronization.'

Write-Host 'Realtime local flip static checks passed.'
