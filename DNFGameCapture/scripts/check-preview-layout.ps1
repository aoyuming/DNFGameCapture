$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& 'E:/VS2026/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$build = Join-Path ([System.IO.Path]::GetTempPath()) ('dnf-preview-test-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $build
Push-Location -LiteralPath $build
try {
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /WX (Join-Path $PSScriptRoot 'preview_layout_test.cpp') '/Fe:preview-test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Preview layout compilation failed' }
    & ./preview-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Preview layout tests failed' }
} finally { Pop-Location }
