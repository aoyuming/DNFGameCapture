param(
    [string]$OutputDirectory,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$server = Join-Path $root 'cloud-match-server'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'deployment-packages' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$name = 'dnf-cloud-match-server-production-5.2.0'
$archive = Join-Path $OutputDirectory "$name.zip"
$sidecar = Join-Path $OutputDirectory "$name.sha256.txt"
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$stage = Join-Path $tempRoot ('dnf-production-package-' + [Guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'dnf-cloud-match-server'
$utf8 = [Text.UTF8Encoding]::new($false)

function Write-PackageText([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text.Replace("`r`n", "`n"), $utf8)
}
try {
    if (-not $ValidateOnly -and ((Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath $sidecar))) {
        throw 'Production archive already exists. Review it and choose another output directory; existing releases are never overwritten.'
    }
    New-Item -ItemType Directory -Path (Join-Path $payload 'deploy') -Force | Out-Null
    # Compile to a clean private directory; never pick up stale or unrelated files from shared dist.
    Push-Location -LiteralPath $server
    try {
        & node (Join-Path $server 'node_modules/typescript/bin/tsc') -p tsconfig.json --outDir (Join-Path $payload 'dist')
        if ($LASTEXITCODE -ne 0) { throw 'Clean production TypeScript build failed.' }
    } finally { Pop-Location }
    foreach ($required in @('server.js', 'v2-api.js', 'library-store.js', 'license-store.js', 'license-vault.js',
        'migrations/remove-adventure-identifiers.js')) {
        if (-not (Test-Path -LiteralPath (Join-Path $payload "dist/$required") -PathType Leaf)) { throw "Missing runtime: $required" }
    }
    foreach ($file in @('package.json', 'package-lock.json')) {
        Copy-Item -LiteralPath (Join-Path $server $file) -Destination $payload
    }
    $documents = @{ 'README-production-5.2.0.md' = 'README.md'; 'README-game-id-only.md' = 'README-game-id-only.md' }
    foreach ($file in $documents.Keys) {
        Write-PackageText (Join-Path $payload $documents[$file]) ([IO.File]::ReadAllText((Join-Path $server $file)))
    }
    $deploymentFiles = @('install.sh', 'preflight.sh', 'production-env.cjs', 'server.env', 'dnf-cloud-match.service')
    foreach ($file in $deploymentFiles) {
        Write-PackageText (Join-Path $payload "deploy/$file") ([IO.File]::ReadAllText((Join-Path $server "deploy/$file")))
    }
    $environment = [IO.File]::ReadAllText((Join-Path $payload 'deploy/server.env'))
    $expected = @(
        'NODE_ENV=production', 'HOST=0.0.0.0', 'PORT=18880', 'PUBLIC_URL=http://47.109.149.111:18880',
        'ADMIN_HOST=127.0.0.1', 'ADMIN_PORT=18881', 'ADMIN_PASSWORD=',
        'DATABASE_PATH=/var/lib/dnf-cloud-match/cloud-match.sqlite', 'ALLOW_LEGACY_PERMANENT_KEYS=true'
    )
    $actual = @($environment -split '\r?\n' | Where-Object { $_ -ne '' })
    if ($actual.Count -ne $expected.Count -or (Compare-Object ($expected | Sort-Object) ($actual | Sort-Object) -CaseSensitive)) {
        throw 'Production environment template differs from the credential-free production allowlist.'
    }
    $release = [ordered]@{
        clientVersion = '5.2.0'
        environment = 'production'
        cloudServerUrl = 'http://47.109.149.111:18880'
        protocolVersion = 2
        manifestUrl = 'https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json'
        serverFirstRequired = $true
        unauthenticatedLibraryStatus = 401
    }
    Write-PackageText (Join-Path $payload 'release.json') (($release | ConvertTo-Json) + "`n")
    $files = @(Get-ChildItem -LiteralPath $payload -File -Recurse)
    $allowedRoot = @('package.json', 'package-lock.json', 'README.md', 'README-game-id-only.md', 'release.json')
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($payload.Length + 1).Replace('\', '/')
        $allowed = $relative -in $allowedRoot -or
            ($relative.StartsWith('dist/') -and $file.Extension -eq '.js') -or
            ($relative.StartsWith('deploy/') -and $relative.Substring(7) -in $deploymentFiles)
        if (-not $allowed -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Unexpected data, credential or linked file: $relative"
        }
    }
    $manifest = foreach ($file in ($files | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($payload.Length + 1).Replace('\', '/')
        "$( (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $relative"
    }
    Write-PackageText (Join-Path $payload 'SHA256SUMS.txt') (($manifest -join "`n") + "`n")
    if ($ValidateOnly) {
        [PSCustomObject]@{ Validated = $true; Files = $files.Count + 1; ServerFirstRequired = $true; ArchiveCreated = $false }
        return
    }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stagedArchive = Join-Path $stage "$name.zip"
    $stream = [IO.File]::Open($stagedArchive, [IO.FileMode]::CreateNew)
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $payload -File -Recurse | Sort-Object FullName)) {
            $entryName = 'dnf-cloud-match-server/' + $file.FullName.Substring($payload.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entryName,
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose(); $stream.Dispose() }
    $zip = [IO.Compression.ZipFile]::OpenRead($stagedArchive)
    try {
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.Contains('\')) { throw 'ZIP must use forward slash paths.' }
            $original = Join-Path $stage $entry.FullName
            if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "Unexpected archive entry: $($entry.FullName)" }
            $entryStream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $actualHash = [BitConverter]::ToString($sha.ComputeHash($entryStream)).Replace('-', '') }
            finally { $entryStream.Dispose(); $sha.Dispose() }
            if ($actualHash -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash) { throw 'Archive checksum mismatch.' }
        }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash -LiteralPath $stagedArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $stagedSidecar = Join-Path $stage "$name.sha256.txt"
    Write-PackageText $stagedSidecar "$hash  $name.zip`n"
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Move-Item -LiteralPath $stagedArchive -Destination $archive
    Move-Item -LiteralPath $stagedSidecar -Destination $sidecar
    [PSCustomObject]@{ Archive = $archive; SHA256 = $hash; Bytes = (Get-Item -LiteralPath $archive).Length; Files = $files.Count + 1 }
} finally {
    $resolved = [IO.Path]::GetFullPath($stage)
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolved) -notmatch '^dnf-production-package-[0-9a-f]{32}$') { throw 'Unsafe temporary cleanup path.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
