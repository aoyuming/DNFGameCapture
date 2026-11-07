param(
    [string]$ClientVersion = '6.0.0'
)

$ErrorActionPreference = 'Stop'
if ($ClientVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid client version: $ClientVersion"
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $projectRoot
$releaseDir = Join-Path $repoRoot 'x64\Release'
if (-not (Test-Path -LiteralPath $releaseDir -PathType Container)) {
    throw "Release package directory not found: $releaseDir"
}

$versionCode = $ClientVersion.Replace('.', '')
$packagePath = Join-Path $releaseDir "update_v$versionCode.zip"

$buildId = [Guid]::NewGuid().ToString('N')
$stagedPackagePath = "$packagePath.$buildId.tmp"
$buildRoot = Join-Path $env:LOCALAPPDATA "DNFGameCapture\builds\release-$ClientVersion-$buildId"
$binDir = Join-Path $buildRoot 'bin'
$objDir = Join-Path $buildRoot 'obj'
New-Item -ItemType Directory -Path $binDir, $objDir | Out-Null

if (-not (Get-Command MSBuild.exe -ErrorAction SilentlyContinue)) {
    $devShell = 'E:\VS2026\Common7\Tools\Launch-VsDevShell.ps1'
    if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) {
        throw "MSBuild is unavailable and Visual Studio developer shell was not found: $devShell"
    }
    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$msbuild = (Get-Command MSBuild.exe -ErrorAction Stop).Source
$project = Join-Path $projectRoot 'DNFGameCapture.vcxproj'
$buildArgs = @(
    $project,
    '/t:Build',
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:OutDir=$binDir\",
    "/p:IntDir=$objDir\",
    '/p:BuildProjectReferences=false',
    '/m'
)
& $msbuild @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Release x64 build failed with exit code $LASTEXITCODE." }

foreach ($name in @('7za.exe', 'sprite(击杀大XX).NPK')) {
    $source = Join-Path $releaseDir $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required runtime asset is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $binDir $name)
}

$webSource = Join-Path $projectRoot 'web前端'
$webDestination = Join-Path $binDir 'web前端'
$webFiles = @(
    'autocomplete-worker.js', 'index.html', 'main.js', 'style.css',
    'keys.css', 'keys.html', 'keys.js', 'kill.css', 'kill.html', 'kill.js',
    'scene-rules.js', 'scene-rules-ui.js', 'scene-rules-ui.css', 'scene-state-ui.js'
)
foreach ($name in $webFiles) {
    $source = Join-Path $webSource $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required web runtime file is missing: $source"
    }
    New-Item -ItemType Directory -Path $webDestination -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $webDestination $name)
}

$voiceSource = Join-Path $webSource 'voice'
$voiceFiles = @(Get-ChildItem -LiteralPath $voiceSource -Recurse -Filter '*.wav' -File)
if ($voiceFiles.Count -eq 0) { throw "Bundled voice files are missing: $voiceSource" }
foreach ($file in $voiceFiles) {
    $relative = $file.FullName.Substring($voiceSource.Length).TrimStart('\')
    $destination = Join-Path $webDestination ("voice\$relative")
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}

$packageScript = Join-Path $PSScriptRoot 'package-production-client.ps1'
try {
    & $packageScript -ReleaseDir $binDir -OutputPath $stagedPackagePath -ClientVersion $ClientVersion
    if (-not (Test-Path -LiteralPath $stagedPackagePath -PathType Leaf)) {
        throw "Package was not created: $stagedPackagePath"
    }
    Move-Item -LiteralPath $stagedPackagePath -Destination $packagePath -Force
} finally {
    if (Test-Path -LiteralPath $stagedPackagePath -PathType Leaf) {
        Remove-Item -LiteralPath $stagedPackagePath -Force
    }
}

Write-Host "Build output: $binDir"
Write-Host "Client package: $packagePath"
Get-FileHash -LiteralPath $packagePath -Algorithm SHA256
