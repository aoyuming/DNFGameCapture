param([string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-library-push-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    if (-not (Test-Path -LiteralPath $VsDevCmd)) { throw 'MSVC developer command file not found.' }
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Y- /I"{1}" /Fe:"{2}\push_test.exe" /Fo:"{2}\\" "{1}\PlayerLibraryPushPolicy.cpp" "{1}\scripts\player_library_push_policy_test.cpp"' -f $VsDevCmd, $root, $tempRoot
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Player library push policy compilation failed.' }
    & (Join-Path $tempRoot 'push_test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Player library push policy tests failed.' }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
