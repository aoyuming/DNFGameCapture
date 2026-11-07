param(
    [string]$ReleaseDir = '',
    [string]$OutputPath = '',
    [string]$ClientVersion = '6.0.0'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($ClientVersion -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid client version: $ClientVersion" }
$repoRoot = Split-Path -Parent $root
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $repoRoot 'x64\Release' }
if (-not $OutputPath) {
    $versionCode = $ClientVersion.Replace('.', '')
    $OutputPath = Join-Path $ReleaseDir "update_v$versionCode.zip"
}
$ReleaseDir = (Resolve-Path -LiteralPath $ReleaseDir).Path
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw "Output already exists: $OutputPath" }

$exe = Join-Path $ReleaseDir 'DNFGameCapture.exe'
$exeVersion = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
$expectedFileVersion = "$ClientVersion.0"
if ($exeVersion -ne $expectedFileVersion) { throw "Expected EXE version $expectedFileVersion, got $exeVersion" }
$manifestUrl = 'https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json'
$binaryText = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($exe))
if (-not $binaryText.Contains($manifestUrl)) { throw 'The executable does not contain the production manifest URL' }
$manifest = Invoke-RestMethod -Uri $manifestUrl -TimeoutSec 15
$endpoint = $null
if ($manifest.environment -ne 'production' -or $manifest.protocolVersion -ne 2 -or
    -not [Uri]::TryCreate($manifest.cloudServerUrl, [UriKind]::Absolute, [ref]$endpoint) -or
    $endpoint.Scheme -notin @('http', 'https')) { throw 'Invalid production OSS manifest' }

# Explicit runtime-only allowlist: never include a user's config, licenses,
# SQLite databases, compatibility INIs, match state, logs or encrypted leases.
$files = @('DNFGameCapture.exe', 'WebView2Loader.dll', '7za.exe', 'sprite(击杀大XX).NPK')
$webFiles = @('autocomplete-worker.js', 'index.html', 'main.js', 'style.css',
    'keys.css', 'keys.html', 'keys.js', 'kill.css', 'kill.html', 'kill.js',
    'scene-rules.js', 'scene-rules-ui.js', 'scene-rules-ui.css', 'scene-state-ui.js')
foreach ($name in $webFiles) {
    $files += "web前端\$name"
}
# Validate the fixed-phrase archive offline; no API key or synthesis is used.
& (Join-Path $PSScriptRoot 'generate-kill-voices.ps1') -CheckOnly
$voiceSourceRoot = Join-Path $root 'web前端\voice'
$voiceFiles = @(Get-ChildItem -LiteralPath $voiceSourceRoot -Recurse -Filter '*.wav' -File)
if ($voiceFiles.Count -eq 0) { throw 'Missing bundled voice WAV files' }
foreach ($source in $voiceFiles) {
    $suffix = $source.FullName.Substring($voiceSourceRoot.Length).TrimStart('\')
    $relative = "web前端\voice\$suffix"
    $releaseVoice = Join-Path $ReleaseDir $relative
    if (-not (Test-Path -LiteralPath $releaseVoice -PathType Leaf) -or
        (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $releaseVoice -Algorithm SHA256).Hash) {
        throw "Missing/outdated release voice; copy the existing source WAV (do not regenerate): $relative"
    }
    $files += $relative
}
foreach ($relative in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $ReleaseDir $relative) -PathType Leaf)) {
        throw "Missing runtime file: $relative"
    }
}
foreach ($name in $webFiles) {
    $sourceWeb = Join-Path $root "web前端\$name"
    $releaseWeb = Join-Path $ReleaseDir "web前端\$name"
    if (-not (Test-Path -LiteralPath $sourceWeb -PathType Leaf)) {
        throw "Missing source web file: $name"
    }
    if ((Get-FileHash -LiteralPath $sourceWeb -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $releaseWeb -Algorithm SHA256).Hash) {
        throw "Release web file does not match source: $name"
    }
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::Open($OutputPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in $files) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,
            (Join-Path $ReleaseDir $relative), $relative.Replace('\', '/'),
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
$verify = [IO.Compression.ZipFile]::OpenRead($OutputPath)
try {
    if ($verify.Entries.Count -ne $files.Count) { throw 'Unexpected archive entry count' }
    $verify.Entries | Select-Object FullName, Length
} finally { $verify.Dispose() }
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
Write-Host "Production manifest: $($manifest.cloudServerUrl)"
Write-Host 'Local package only. Verify the production v2 server before publishing the client update.'
