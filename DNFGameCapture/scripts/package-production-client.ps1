param(
    [string]$ReleaseDir = '',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ReleaseDir) { $ReleaseDir = Join-Path (Split-Path -Parent $root) 'x64\Release' }
if (-not $OutputPath) { $OutputPath = Join-Path $root 'deployment-packages\update_v521.zip' }
$ReleaseDir = (Resolve-Path -LiteralPath $ReleaseDir).Path
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw "Output already exists: $OutputPath" }

$exe = Join-Path $ReleaseDir 'DNFGameCapture.exe'
$version = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if ($version -ne '5.2.1.0') { throw "Expected EXE version 5.2.1.0, got $version" }
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
    'keys.css', 'keys.html', 'keys.js', 'kill.css', 'kill.html', 'kill.js')
foreach ($name in $webFiles) {
    $files += "web前端\$name"
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
