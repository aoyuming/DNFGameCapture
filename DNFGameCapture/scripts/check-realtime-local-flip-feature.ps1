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

Require-Text $header 'bool automatic = false, bool preserveLocalFlip = false);' `
    'Team snapshot application has no explicit local-flip preservation option.'
Require-Text $source 'if (!preserveLocalFlip && snapshot.contains("isFlipped")) {' `
    'Remote snapshot application does not preserve the local flip setting.'
Require-Text $source 'ApplyTeamSyncSnapshot(teamSnapshot, false, applyError, true, true)' `
    'Cloud realtime snapshots do not opt into local flip preservation.'

$blockedStart = $source.IndexOf('static bool DnfIsCloudRealtimeBlockedWebAction(')
$blockedEnd = $source.IndexOf('static CString DnfCloudMatchErrorText(', $blockedStart)
if ($blockedStart -lt 0 -or $blockedEnd -le $blockedStart) {
    throw 'Unable to inspect the realtime Web action block list.'
}
$blockedBody = $source.Substring($blockedStart, $blockedEnd - $blockedStart)
if ($blockedBody.Contains('cmd_swap')) {
    throw 'The Web flip command is still blocked during realtime synchronization.'
}

$flipStart = $source.IndexOf('void CDNFGameCaptureDlg::OnBnClickedFlip()')
$flipEnd = $source.IndexOf('void CDNFGameCaptureDlg::OnBnClickedReset()', $flipStart)
if ($flipStart -lt 0 -or $flipEnd -le $flipStart) {
    throw 'Unable to inspect the native flip handler.'
}
$flipBody = $source.Substring($flipStart, $flipEnd - $flipStart)
if ($flipBody.Contains('RejectLocalMatchEditWhileRealtime()')) {
    throw 'The native flip handler is still blocked during realtime synchronization.'
}

Write-Host 'Realtime local flip static checks passed.'
