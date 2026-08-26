$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'scripts\key_mapping_lan_protocol_test.cpp'
$service = Join-Path $root 'KeyMappingLanService.cpp'
$header = Join-Path $root 'KeyMappingLanService.h'
if (-not (Test-Path -LiteralPath $source)) { throw 'Missing LAN protocol test source.' }
foreach ($path in @($service, $header)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing LAN service file: $path" }
}
$content = Get-Content -LiteralPath $source -Raw -Encoding UTF8
$headerContent = Get-Content -LiteralPath $header -Raw -Encoding UTF8
foreach ($needle in @('SetTeamSyncMessageCallback', 'SetTeamSyncSnapshot', 'RequestTeamSync',
    'IsTeamSyncPending', 'SetTeamSyncSubscribed', 'SetTeamSyncClientWriteAllowed',
    'SetTeamSyncAutoSend', 'SetRemoteTeamSyncSnapshot', 'CompleteTeamSyncProposal')) {
    if (-not $headerContent.Contains($needle)) { throw "LAN service team sync API is missing: $needle" }
}
foreach ($needle in @(
    'StartServer(18778',
    'StartClient("127.0.0.1"',
    '0x3FFF',
    'rejected_busy',
    'GetRemoteActiveMask() == 0',
    'SetTeamSyncMessageCallback',
    'SetTeamSyncSnapshot',
    'RequestTeamSync(error)',
    'team_sync_snapshot',
    'snapshot.dump().size() > 4096',
    'large-alias-',
    'payload_too_large',
    'team_sync_push',
    'SetTeamSyncSubscribed(true)',
    'SetTeamSyncSubscribed(false)',
    'teamSyncSubscribed',
    'revision',
    'team_sync_bidirectional_v1',
    'team_sync_write_policy',
    'team_sync_propose',
    'SetTeamSyncClientWriteAllowed(true)',
    'SetTeamSyncAutoSend(true)',
    'SetRemoteTeamSyncSnapshot',
    'CompleteTeamSyncProposal'
)) {
    if (-not $content.Contains($needle)) { throw "Protocol test is missing: $needle" }
}
Write-Host 'Key mapping LAN protocol test source passed static checks.' -ForegroundColor Green
