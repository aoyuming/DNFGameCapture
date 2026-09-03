$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$testSource = Join-Path $PSScriptRoot 'alias_db_auto_sync_policy_test.cpp'
$policySource = Join-Path $root 'AliasDbAutoSyncPolicy.cpp'

foreach ($path in @($testSource, $policySource)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing alias DB auto-sync policy test input: $path"
    }
}

$installRoots = [System.Collections.Generic.List[string]]::new()
$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
foreach ($vswhere in $vswhereCandidates) {
    if ($vswhere -and (Test-Path -LiteralPath $vswhere)) {
        $found = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($LASTEXITCODE -eq 0 -and $found) {
            $installRoots.Add(($found | Select-Object -First 1))
        }
    }
}
foreach ($knownRoot in @(
    'E:\VS2026',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools',
    'C:\Program Files\Microsoft Visual Studio\18\BuildTools',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools',
    'C:\Program Files\Microsoft Visual Studio\2022\Community'
)) {
    if (-not $installRoots.Contains($knownRoot)) { $installRoots.Add($knownRoot) }
}

$devCmd = $null
$compiler = $null
foreach ($installRoot in $installRoots) {
    $candidateDevCmd = Join-Path $installRoot 'Common7\Tools\VsDevCmd.bat'
    $candidateCompiler = Get-ChildItem -LiteralPath (Join-Path $installRoot 'VC\Tools\MSVC') `
        -Filter 'cl.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\cl\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ((Test-Path -LiteralPath $candidateDevCmd) -and $candidateCompiler) {
        $devCmd = $candidateDevCmd
        $compiler = $candidateCompiler.FullName
        break
    }
}
if (-not $devCmd -or -not $compiler) {
    throw 'Visual Studio C++ x64 compiler was not found.'
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('DNFGameCapture-alias-auto-sync-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
$exe = Join-Path $tempRoot 'alias_db_auto_sync_policy_test.exe'

try {
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" /nologo /std:c++17 /EHsc /W4 /utf-8 /Y- /I"{2}" /Fe:"{3}" /Fo:"{4}\\" "{5}" "{6}"' -f `
        $devCmd, $compiler, $root, $exe, $tempRoot, $testSource, $policySource
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw "Alias DB auto-sync policy test compilation failed with exit code $LASTEXITCODE."
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Alias DB auto-sync policy tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host 'Alias DB auto-sync policy test check passed.' -ForegroundColor Green
