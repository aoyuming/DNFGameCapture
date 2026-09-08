$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $env:TEMP ('dnf-ocr-health-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $build
& 'E:/VS2026/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Push-Location -LiteralPath $build
try {
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /WX (Join-Path $PSScriptRoot 'ocr_service_health_test.cpp') /Fe:ocr_service_health_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'OCR health tests did not compile' }
    & (Join-Path $build 'ocr_service_health_test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'OCR health tests failed' }
} finally { Pop-Location }
