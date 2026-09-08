param([string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-player-library-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    if (-not (Test-Path -LiteralPath $VsDevCmd)) { throw 'MSVC developer command file not found.' }
    $sqlite = Join-Path $root 'third_party/sqlite/sqlite3.c'
    $expected = @{
        'sqlite3.c' = 'E80215754AC6CFAEFE272342EFD581B6DBEFDBDC21F96BD83FCB698BEE9D36A5'
        'sqlite3.h' = '2198C66D4C59AE86421590C27FD53309EC14A595AE31C0C7143247444018A0BD'
    }
    foreach ($name in $expected.Keys) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $root ('third_party/sqlite/' + $name)) -Algorithm SHA256).Hash
        if ($actual -ne $expected[$name]) { throw "Vendored SQLite hash mismatch: $name" }
    }
    $compileC = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /O2 /MT /TC /Y- /DSQLITE_THREADSAFE=1 /DSQLITE_DQS=0 /c "{1}" /Fo:"{2}\sqlite3.obj"' -f $VsDevCmd, $sqlite, $tempRoot
    & $env:ComSpec /d /s /c $compileC
    if ($LASTEXITCODE -ne 0) { throw 'SQLite compilation failed.' }
    $sources = @('PlayerLibraryModel.cpp', 'PlayerLibraryDatabase.cpp', 'PlayerLibraryStore.cpp', 'PlayerIdentityGroupService.cpp', 'scripts/player_library_core_test.cpp')
    $inputs = ($sources | ForEach-Object { '"' + (Join-Path $root $_) + '"' }) -join ' '
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Y- /DDNF_PLAYER_LIBRARY_TESTING /I"{1}" /Fe:"{2}\core_test.exe" /Fo:"{2}\\" {3} "{2}\sqlite3.obj"' -f $VsDevCmd, $root, $tempRoot, $inputs
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Player library core compilation failed.' }
    $inspect = 'call "{0}" -arch=x64 -host_arch=x64 >nul && dumpbin /nologo /dependents "{1}\core_test.exe"' -f $VsDevCmd, $tempRoot
    $dependencies = & $env:ComSpec /d /s /c $inspect
    if ($LASTEXITCODE -ne 0) { throw 'Executable dependency inspection failed.' }
    if (($dependencies -join "`n") -match '(?i)\bicu(?:uc|in)?\.dll\b') {
        throw 'Player library must not hard-import an ICU DLL.'
    }
    Write-Host 'Dependency check passed: no hard ICU DLL import.'
    & (Join-Path $tempRoot 'core_test.exe') (Join-Path $tempRoot 'fixtures')
    if ($LASTEXITCODE -ne 0) { throw 'Player library core tests failed.' }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
