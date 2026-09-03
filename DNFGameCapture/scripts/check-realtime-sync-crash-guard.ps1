$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dialogPath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$leasePath = Join-Path $root 'LicenseLease.cpp'
$crashPath = Join-Path $root 'CrashDump.cpp'
$appPath = Join-Path $root 'DNFGameCapture.cpp'

foreach ($path in @($dialogPath, $leasePath, $crashPath, $appPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing crash guard source file: $path"
    }
}

$dialog = Get-Content -LiteralPath $dialogPath -Raw -Encoding UTF8
$lease = Get-Content -LiteralPath $leasePath -Raw -Encoding UTF8
$crash = Get-Content -LiteralPath $crashPath -Raw -Encoding UTF8
$app = Get-Content -LiteralPath $appPath -Raw -Encoding UTF8

$heartbeatGuard = 'if (type == "realtime_heartbeat_result" && ok)'
if (-not $dialog.Contains($heartbeatGuard)) {
    throw 'Successful realtime heartbeat must have an explicit no-broadcast fast path.'
}
$heartbeatStart = $dialog.IndexOf($heartbeatGuard)
$heartbeatBody = $dialog.Substring($heartbeatStart,
    [Math]::Min(400, $dialog.Length - $heartbeatStart))
if (-not $heartbeatBody.Contains('return;')) {
    throw 'Successful realtime heartbeat must return before full Web state broadcast.'
}

$snapshotStart = $dialog.IndexOf('json CDNFGameCaptureDlg::BuildSharedWebMatchSnapshotJson()')
$stateStart = $dialog.IndexOf('json CDNFGameCaptureDlg::DnfBuildSharedWebStateJson()')
if ($snapshotStart -lt 0 -or $stateStart -le $snapshotStart) {
    throw 'Shared Web match snapshot helper is missing.'
}
$snapshotBody = $dialog.Substring($snapshotStart, $stateStart - $snapshotStart)
foreach ($needle in @('std::lock_guard<std::mutex> dataLock(m_dataMutex)',
        'BuildTeamSyncSnapshotPayloadUnlocked()', 'recentCount < 100')) {
    if (-not $snapshotBody.Contains($needle)) {
        throw "Shared Web match snapshot safety contract is missing: $needle"
    }
}

foreach ($needle in @('CryptProtectData(', 'CryptUnprotectData(', 'REG_BINARY',
        'DNF_LICENSE_LEASE_REG_VALUE')) {
    if (-not $lease.Contains($needle)) {
        throw "DPAPI license lease contract is missing: $needle"
    }
}
foreach ($needle in @('MiniDumpWriteDump(', 'crash-dumps')) {
    if (-not $crash.Contains($needle)) {
        throw "Crash dump contract is missing: $needle"
    }
}
foreach ($forbidden in @('MiniDumpWithIndirectlyReferencedMemory',
        'MiniDumpScanMemory')) {
    if ($crash.Contains($forbidden)) {
        throw "Crash dump captures unnecessary process memory: $forbidden"
    }
}
foreach ($needle in @('const BOOL dumpWritten =', 'if (!dumpWritten)',
        '::DeleteFileW(dumpPath);')) {
    if (-not $crash.Contains($needle)) {
        throw "Crash dump write failure handling is missing: $needle"
    }
}
if (-not $app.Contains('DnfInstallCrashDumpHandler();')) {
    throw 'Crash dump handler must be installed before application initialization.'
}

Write-Host 'Realtime sync crash guard check passed.' -ForegroundColor Green
