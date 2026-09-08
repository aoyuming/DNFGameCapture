$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Missing client source file: $sourcePath"
}

$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

$wrongBinding = 'const std::string serverDeviceId = m_cloudMatchDeviceId;'
if ($source.Contains($wrongBinding)) {
    throw 'V2 player-library requests must not authenticate with the Socket.IO broadcaster device ID.'
}

$correctBinding = 'const std::string serverDeviceId = hwidUtf8;'
$correctCount = ([regex]::Matches($source, [regex]::Escape($correctBinding))).Count
if ($correctCount -ne 2) {
    throw "Expected both manual and automatic V2 player-library paths to use hwidUtf8; found $correctCount."
}

Write-Host 'V2 player-library authentication identity checks passed.' -ForegroundColor Green
