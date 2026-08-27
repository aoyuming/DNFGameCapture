$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'CloudMatchClient.h'
$sourcePath = Join-Path $root 'CloudMatchClient.cpp'
$projectPath = Join-Path $root 'DNFGameCapture.vcxproj'
$filtersPath = Join-Path $root 'DNFGameCapture.vcxproj.filters'

foreach ($path in @($headerPath, $sourcePath, $projectPath, $filtersPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing cloud match client feature file: $path"
    }
}

$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$project = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
$filters = Get-Content -LiteralPath $filtersPath -Raw -Encoding UTF8

foreach ($needle in @(
    'struct CloudMatchStatusSnapshot',
    'bool configured = false',
    'bool connecting = false',
    'bool connected = false',
    'bool reconnecting = false',
    'std::uint64_t roomRevision = 0',
    'bool Configure(',
    'bool Start()',
    'void Stop()',
    'bool RegisterDevice(',
    'bool JoinRoom(',
    'bool Rename(',
    'bool LeaveRoom()',
    'bool UploadSnapshot(',
    'bool RequestComparison(',
    'bool RequestSnapshot(',
    'CloudMatchStatusSnapshot GetStatusSnapshot() const',
    'void SetMessageCallback(',
    'CloudMatchClient(const CloudMatchClient&) = delete',
    'operator=(const CloudMatchClient&) = delete'
)) {
    if (-not $header.Contains($needle)) {
        throw "CloudMatchClient public contract is missing: $needle"
    }
}

if ($source.Contains('.detach()')) {
    throw 'CloudMatchClient must never detach a thread.'
}

foreach ($needle in @(
    'kMaxCommandQueueSize = 64',
    'kMaxPendingAcks = 64',
    'kAckTimeout = std::chrono::seconds(8)',
    'kNetworkTimeoutMs = 3000',
    'kReceivePollTimeoutMs = 200',
    'kReconnectDelaysSeconds{ 1, 2, 5, 10, 20 }',
    'std::thread worker',
    'std::condition_variable condition',
    'commands.size() >= kMaxCommandQueueSize',
    'pendingAcks.size() >= kMaxPendingAcks',
    'latestSnapshot',
    'configGeneration',
    'nextAckId',
    'queue_full',
    'uploadSnapshot,',
    'CommandKind::uploadSnapshot,',
    'room->value("displayName", current.roomName)',
    'bool SendRememberedJoinIfNeeded(',
    'if (!SendRememberedJoinIfNeeded(activeConfig, connection)) return false;',
    '/api/devices/register',
    '/socket.io/?EIO=4&transport=websocket',
    'room:join',
    'room:rename',
    'room:leave',
    'snapshot:upload',
    'room:comparison',
    'snapshot:get',
    'room:changed',
    'room:presence',
    'room_join_result',
    'room_rename_result',
    'room_leave_result',
    'snapshot_upload_result',
    'room_comparison_result',
    'snapshot_result',
    'cloud_error'
)) {
    if (-not $source.Contains($needle)) {
        throw "CloudMatchClient bounded worker contract is missing: $needle"
    }
}

if ($source.Contains('room->value("name", current.roomName)')) {
    throw 'Room status must use the server RoomDto displayName field.'
}
if ($source -match '(?s)"snapshot:upload".{0,300}CommandKind::requestComparison') {
    throw 'Snapshot upload ACKs must not be tagged as comparison commands.'
}

foreach ($needle in @(
    'WinHttpCrackUrl',
    'WinHttpOpen(',
    'WinHttpConnect(',
    'WinHttpOpenRequest(',
    'WinHttpSendRequest(',
    'WinHttpReceiveResponse(',
    'WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET',
    'WinHttpWebSocketCompleteUpgrade(',
    'WinHttpWebSocketSend(',
    'WinHttpWebSocketReceive(',
    'WinHttpWebSocketClose(',
    'WinHttpSetTimeouts(',
    'ERROR_WINHTTP_TIMEOUT',
    'WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE',
    'WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE',
    'WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE',
    'WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE',
    'WinHttpCloseHandle('
)) {
    if (-not $source.Contains($needle)) {
        throw "CloudMatchClient WinHTTP contract is missing: $needle"
    }
}

foreach ($needle in @(
    '<ClInclude Include="CloudMatchProtocol.h" />',
    '<ClInclude Include="CloudMatchClient.h" />',
    '<ClCompile Include="CloudMatchProtocol.cpp">',
    '<ClCompile Include="CloudMatchClient.cpp" />',
    'winhttp.lib'
)) {
    if (-not $project.Contains($needle)) {
        throw "Cloud match project reference is missing: $needle"
    }
}

foreach ($needle in @(
    '<ClInclude Include="CloudMatchProtocol.h">',
    '<ClInclude Include="CloudMatchClient.h">',
    '<ClCompile Include="CloudMatchProtocol.cpp">',
    '<ClCompile Include="CloudMatchClient.cpp">'
)) {
    if (-not $filters.Contains($needle)) {
        throw "Cloud match project filter reference is missing: $needle"
    }
}

Write-Host 'Cloud match client static checks passed.' -ForegroundColor Green
