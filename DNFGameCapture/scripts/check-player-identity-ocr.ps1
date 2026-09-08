$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$devShell = 'E:\VS2026\Common7\Tools\Launch-VsDevShell.ps1'
$sources = @(
    (Join-Path $root 'PlayerLibraryModel.cpp'),
    (Join-Path $root 'PlayerIdentityGroupService.cpp'),
    (Join-Path $PSScriptRoot 'player_identity_ocr_test.cpp')
)
foreach ($path in @($devShell) + $sources) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}

$tempRoot = (Get-Item -LiteralPath ([System.IO.Path]::GetTempPath())).FullName
$directoryName = 'dnf-player-identity-ocr-' + [Guid]::NewGuid().ToString('N')
$buildDirectory = Join-Path $tempRoot $directoryName
$null = New-Item -ItemType Directory -Path $buildDirectory

try {
    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    Push-Location -LiteralPath $buildDirectory
    try {
        $exe = Join-Path $buildDirectory 'player_identity_ocr_test.exe'
        $compilerArgs = @('/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/O2')
        $compilerArgs += $sources
        $compilerArgs += '/Fe' + $exe
        & cl.exe @compilerArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Player identity OCR test compilation failed (exit $LASTEXITCODE)."
        }
        & $exe
        if ($LASTEXITCODE -ne 0) {
            throw "Player identity OCR tests failed (exit $LASTEXITCODE)."
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    if (Test-Path -LiteralPath $buildDirectory -PathType Container) {
        $resolvedBuild = (Get-Item -LiteralPath $buildDirectory).FullName
        $resolvedParent = (Get-Item -LiteralPath (Split-Path -Parent $resolvedBuild)).FullName
        if ($resolvedParent.TrimEnd([char[]]'\/') -ne $tempRoot.TrimEnd([char[]]'\/') -or
            (Split-Path -Leaf $resolvedBuild) -ne $directoryName) {
            throw "Refusing to clean an unexpected build directory: $resolvedBuild"
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}
