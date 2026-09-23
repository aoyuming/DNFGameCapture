$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $root 'CameraCapture.cpp') -Raw -Encoding UTF8

foreach ($needle in @(
    '!pSample',
    'pSample->ConvertToContiguousBuffer(pBuffer.put())',
    'pBuffer->Lock(&pData',
    'm_pReader->GetCurrentMediaType(',
    '!pCurrentType',
    'requiredBytes > currentLength'
)) {
    if (-not $source.Contains($needle)) {
        throw "Camera capture frame guard is missing: $needle"
    }
}
Write-Host 'Camera capture frame guard checks passed.'
