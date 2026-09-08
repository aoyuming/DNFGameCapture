param([string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-library-transition-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    $compileC = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /O2 /MT /TC /Y- /DSQLITE_THREADSAFE=1 /c "{1}\third_party\sqlite\sqlite3.c" /Fo:"{2}\sqlite3.obj"' -f $VsDevCmd, $root, $tempRoot
    & $env:ComSpec /d /s /c $compileC
    if ($LASTEXITCODE -ne 0) { throw 'SQLite compilation failed.' }
    $files = @('PlayerLibraryModel.cpp', 'PlayerLibraryDatabase.cpp', 'PlayerLibraryStore.cpp', 'PlayerIdentityGroupService.cpp', 'scripts/player_library_transition_test.cpp')
    $inputs = ($files | ForEach-Object { '"' + (Join-Path $root $_) + '"' }) -join ' '
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Y- /I"{1}" /Fe:"{2}\transition.exe" /Fo:"{2}\\" {3} "{2}\sqlite3.obj"' -f $VsDevCmd, $root, $tempRoot, $inputs
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Transition test compilation failed.' }
    & (Join-Path $tempRoot 'transition.exe') (Join-Path $tempRoot 'fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Player library transition tests failed.' }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
