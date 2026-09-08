param(
    [string]$ReleaseDate = '20260907',
    [ValidatePattern('^[a-z0-9-]+$')][string]$PackageLabel = 'admin-hub'
)
$ErrorActionPreference = 'Stop'
function Assert-TestPackageEnvironment([string]$EnvironmentText) {
    $lines = $EnvironmentText -split '\r?\n'
    $passwords = @($lines | Where-Object { $_ -match '^\s*(?:export\s+)?ADMIN_PASSWORD\s*=' })
    $ports = @($lines | Where-Object { $_ -match '^\s*(?:export\s+)?PORT\s*=' })
    if ($passwords.Count -ne 1 -or $passwords[0] -cne 'ADMIN_PASSWORD=Aym724794' -or
        $ports.Count -ne 1 -or $ports[0] -cne 'PORT=28880') {
        throw 'Test environment must contain exactly the designated default password and test port, not private deployment credentials.'
    }
}
if ($ReleaseDate -notmatch '^\d{8}$') { throw 'ReleaseDate must be YYYYMMDD.' }
$root = Split-Path -Parent $PSScriptRoot
$server = Join-Path $root 'cloud-match-server'
$output = Join-Path $root 'deployment-packages'
$name = "dnf-cloud-match-server-test-$PackageLabel-$ReleaseDate"
$archive = Join-Path $output "$name.zip"
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$stage = Join-Path $tempRoot ('dnf-test-package-' + [Guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'dnf-cloud-match-server'
try {
    Push-Location -LiteralPath $server
    try {
        & npm run build
        if ($LASTEXITCODE -ne 0) { throw 'Server build failed.' }
    } finally { Pop-Location }
    foreach ($required in @('library-admin.js','library-admin-data.js','library-admin-page.js','library-submission-reconcile.js',
        'library-store.js','library-identity-policy.js','license-admin-page.js','license-admin.js','license-store.js','license-vault.js','broadcaster-admin-page.js')) {
        if (-not (Test-Path -LiteralPath (Join-Path $server "dist\$required"))) { throw "Missing runtime: $required" }
    }
    New-Item -ItemType Directory -Path (Join-Path $payload 'deploy') -Force | Out-Null
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $server 'dist') -Destination $payload -Recurse
    foreach ($file in @('package.json','package-lock.json')) {
        Copy-Item -LiteralPath (Join-Path $server $file) -Destination $payload
    }
    $readme = if ($PackageLabel -eq 'legacy-permanent-license') { 'README-legacy-permanent-license.md' }
        elseif ($PackageLabel -eq 'library-auto-refresh') { 'README-library-auto-refresh.md' }
        elseif ($PackageLabel -eq 'game-id-only') { 'README-game-id-only.md' }
        elseif ($PackageLabel -eq 'license-management') { 'README-license-management.md' }
        elseif ($PackageLabel -eq 'shared-identifiers') { 'README-shared-identifiers.md' }
        elseif ($PackageLabel -eq 'partial-review') { 'README-library-partial-review.md' } else { 'README-test-identity-admin.md' }
    Copy-Item -LiteralPath (Join-Path $server $readme) -Destination (Join-Path $payload 'README.md')
    foreach ($file in @('install-test.sh','test-server.env','dnf-cloud-match-test.service')) {
        $destination = Join-Path $payload "deploy\$file"
        Copy-Item -LiteralPath (Join-Path $server "deploy\$file") -Destination $destination
        $text = [IO.File]::ReadAllText($destination).Replace("`r`n", "`n")
        [IO.File]::WriteAllText($destination, $text, [Text.UTF8Encoding]::new($false))
    }
    $environment = Get-Content -LiteralPath (Join-Path $payload 'deploy\test-server.env') -Raw
    Assert-TestPackageEnvironment $environment
    $files = Get-ChildItem -LiteralPath $payload -File -Recurse
    if ($files | Where-Object { $_.Extension -in @('.sqlite','.db','.exe','.pdb') -or $_.Name -match 'token|secret' }) {
        throw 'Unexpected data or credential file in runtime payload.'
    }
    $manifest = foreach ($file in ($files | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($payload.Length + 1).Replace('\','/')
        "$( (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $relative"
    }
    [IO.File]::WriteAllText((Join-Path $payload 'SHA256SUMS.txt'), ($manifest -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archiveStream = [IO.File]::Open($archive, [IO.FileMode]::Create)
    $writer = [IO.Compression.ZipArchive]::new($archiveStream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $payload -File -Recurse | Sort-Object FullName)) {
            $entryName = 'dnf-cloud-match-server/' + $file.FullName.Substring($payload.Length + 1).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($writer, $file.FullName, $entryName,
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $writer.Dispose(); $archiveStream.Dispose() }
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if ($entry.Name -eq '') { continue }
            if ($entry.FullName.Contains('\')) { throw 'ZIP paths must use forward slashes for Ubuntu.' }
            $relative = $entry.FullName.Replace('/','\')
            $original = Join-Path $stage $relative
            if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "Unexpected ZIP entry: $relative" }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($actual -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash) { throw "ZIP checksum mismatch: $relative" }
        }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $output "$name.sha256.txt"), "$hash  $name.zip`n", [Text.UTF8Encoding]::new($false))
    [PSCustomObject]@{ Archive=$archive; SHA256=$hash; Bytes=(Get-Item -LiteralPath $archive).Length; Files=$files.Count + 1 }
} finally {
    $resolved = [IO.Path]::GetFullPath($stage)
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolved) -notmatch '^dnf-test-package-[0-9a-f]{32}$') { throw 'Unsafe temporary cleanup path.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
