param([string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-cloud-release-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    if (-not (Test-Path -LiteralPath $VsDevCmd)) { throw 'MSVC developer command file not found.' }
    foreach ($define in @('', '/DDNF_CLOUD_TEST_BUILD=0', '/DDNF_CLOUD_TEST_BUILD=1')) {
        $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /Y- /DUNICODE /D_UNICODE {3} /I"{1}" /Fe:"{2}\release_test.exe" /Fo:"{2}\\" "{1}\CloudReleasePolicy.cpp" "{1}\LicenseLease.cpp" "{1}\scripts\cloud_release_policy_test.cpp"' -f $VsDevCmd, $root, $tempRoot, $define
        & $env:ComSpec /d /s /c $compile
        if ($LASTEXITCODE -ne 0) { throw "Cloud release policy compilation failed ($define)." }
        & (Join-Path $tempRoot 'release_test.exe')
        if ($LASTEXITCODE -ne 0) { throw "Cloud release policy tests failed ($define)." }
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolved)).StartsWith('DNF-cloud-release-')) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
