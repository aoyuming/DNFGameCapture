$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$appSource = Get-Content -LiteralPath (Join-Path $root 'DNFGameCapture.cpp') -Raw -Encoding UTF8
$dialogSource = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw -Encoding UTF8

foreach ($needle in @(
    'CreateMutexW',
    'Global\\DNFGameCapture_SingleInstance',
    'ERROR_ALREADY_EXISTS',
    'DnfReleaseSingleInstanceMutex'
)) {
    if (-not $appSource.Contains($needle)) {
        throw "Single-instance startup contract is missing: $needle"
    }
}

if ($appSource.IndexOf('if (::GetLastError() == ERROR_ALREADY_EXISTS)') -lt 0 -and
    $appSource.IndexOf('if (GetLastError() == ERROR_ALREADY_EXISTS)') -lt 0) {
    throw 'A second process must be rejected before the main dialog is created.'
}
if ($dialogSource.Contains('m_cloudMatchTemporaryInstance = GetLastError()')) {
    throw 'Duplicate processes must not be downgraded to temporary cloud instances.'
}
if ($dialogSource.Contains('m_hSingleInstanceMutex = CreateMutex')) {
    throw 'The dialog must not own a second copy of the single-instance mutex.'
}
if (-not $dialogSource.Contains('DnfReleaseSingleInstanceMutex()')) {
    throw 'Administrator relaunch must release the application mutex first.'
}

Write-Host 'Single-instance feature static checks passed.' -ForegroundColor Green
