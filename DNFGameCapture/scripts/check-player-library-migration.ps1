param(
    [Parameter(Mandatory=$true)][string[]]$Sources,
    [string]$VsDevCmd = 'E:\VS2026\Common7\Tools\VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('DNF-library-audit-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    $compileC = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /O2 /MT /TC /Y- /DSQLITE_THREADSAFE=1 /c "{1}\third_party\sqlite\sqlite3.c" /Fo:"{2}\sqlite3.obj"' -f $VsDevCmd, $root, $tempRoot
    & $env:ComSpec /d /s /c $compileC
    if ($LASTEXITCODE -ne 0) { throw 'SQLite compilation failed.' }
    $files = @('PlayerLibraryModel.cpp', 'PlayerLibraryDatabase.cpp', 'PlayerIdentityGroupService.cpp', 'scripts/player_library_migration_audit.cpp')
    $inputs = ($files | ForEach-Object { '"' + (Join-Path $root $_) + '"' }) -join ' '
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Y- /I"{1}" /Fe:"{2}\audit.exe" /Fo:"{2}\\" {3} "{2}\sqlite3.obj"' -f $VsDevCmd, $root, $tempRoot, $inputs
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Migration audit compilation failed.' }
    $index = 0
    foreach ($source in $Sources) {
        $resolvedSource = (Resolve-Path -LiteralPath $source).Path
        $before = (Get-FileHash -LiteralPath $resolvedSource -Algorithm SHA256).Hash
        $metadata = Join-Path (Split-Path -Parent $resolvedSource) 'player_identity_groups.json'
        & (Join-Path $tempRoot 'audit.exe') $resolvedSource (Join-Path $tempRoot ('fixture-' + $index++)) $metadata
        if ($LASTEXITCODE -ne 0) { throw 'Migration audit failed.' }
        if ((Get-FileHash -LiteralPath $resolvedSource -Algorithm SHA256).Hash -ne $before) { throw 'Audit source changed.' }
        Write-Output "Source unchanged: $resolvedSource"
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $allowed = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
}
