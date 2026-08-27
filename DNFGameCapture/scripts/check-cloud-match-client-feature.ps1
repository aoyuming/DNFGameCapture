$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'CloudMatchClient.h'
$sourcePath = Join-Path $root 'CloudMatchClient.cpp'
$protocolSourcePath = Join-Path $root 'CloudMatchProtocol.cpp'
$clientTestPath = Join-Path $PSScriptRoot 'cloud_match_client_test.cpp'
$projectPath = Join-Path $root 'DNFGameCapture.vcxproj'
$filtersPath = Join-Path $root 'DNFGameCapture.vcxproj.filters'

foreach ($path in @($headerPath, $sourcePath, $protocolSourcePath, $clientTestPath,
    $projectPath, $filtersPath)) {
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
    'std::size_t DispatchMessages(std::size_t maxCount = 32)',
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
    'kMaxInboundMessageQueueSize = 128',
    'kAckTimeout = std::chrono::seconds(8)',
    'kNetworkTimeoutMs = 3000',
    'kReceivePollTimeoutMs = 200',
    'kReconnectDelaysSeconds{ 1, 2, 5, 10, 20 }',
    'std::thread worker',
    'std::condition_variable condition',
    'std::atomic<bool> stopRequested',
    'std::atomic<HINTERNET> activeCancelableHandle',
    'std::deque<InboundMessage> inboundMessages',
    'activeCancelableHandle.exchange(nullptr)',
    'activeCancelableHandle.compare_exchange_strong',
    'PublishCancelableHandle(',
    'ReleaseCancelableHandle(',
    'ActiveHandleScope requestScope',
    'ActiveHandleScope socketScope',
    'handleOwner_.Release()',
    'ERROR_WINHTTP_OPERATION_CANCELLED',
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

foreach ($needle in @(
    'EnqueueInboundMessage(',
    'DispatchMessages(std::size_t maxCount)',
    'IsCoalescibleNotification(',
    'queue_overflow',
    'ClearCommandsLocked(',
    'ClearLatestSnapshotLocked(',
    'ClearPendingAcksLocked(',
    'ClearDesiredRoomLocked(',
    'ClearInboundMessagesLocked('
)) {
    if (-not $source.Contains($needle)) {
        throw "CloudMatchClient hardening contract is missing: $needle"
    }
}

if ($source -notmatch '(?s)DispatchMessages\(std::size_t maxCount\).*?copiedCallback.*?copiedCallback\(') {
    throw 'DispatchMessages must copy and invoke the callback on its caller.'
}
if ($source -match '(?s)void NotifyJson\(.*?copiedCallback\(') {
    throw 'The worker-side notification path must never invoke user callbacks.'
}
if ($source -notmatch '(?s)bool Configure\(.*?configGeneration\.fetch_add.*?CancelActiveOperation\(.*?ClearCommandsLocked\(.*?ClearLatestSnapshotLocked\(.*?ClearPendingAcksLocked\(.*?ClearDesiredRoomLocked\(') {
    throw 'Configure must advance generation, cancel active I/O, and clear all old work.'
}
if ($source -notmatch 'PublishCancelableHandle\(HINTERNET handle,\s*std::uint64_t generation\)') {
    throw 'Cancelable handles must be published against an explicit configuration generation.'
}
if ($source -notmatch '(?s)PublishCancelableHandle\(HINTERNET handle,\s*std::uint64_t generation\).*?configGeneration\.load.*?generation.*?compare_exchange_strong.*?configGeneration\.load.*?generation') {
    throw 'PublishCancelableHandle must reject stale generations before and after publication.'
}
if ($source -notmatch '(?s)bool SendText\(.*?std::uint64_t generation.*?ShouldAbort\(generation\).*?WinHttpWebSocketSend.*?ShouldAbort\(generation\)') {
    throw 'Every WebSocket send must check generation before and after the blocking call.'
}
if (-not $source.Contains('SecureZeroMemory(') -or
    $source.Contains("std::fill(value.begin(), value.end(), '\0')")) {
    throw 'Sensitive strings must use a guaranteed SecureZeroMemory wipe.'
}
foreach ($needle in @(
    '~Config()',
    'Config(Config&& other) noexcept',
    'Config& operator=(Config&& other) noexcept',
    'SecureClear(requestBody)',
    'SecureClear(responseBody)',
    'SecureClear(connectPacket)',
    'SecureClear(config.deviceToken)'
)) {
    if (-not $source.Contains($needle)) {
        throw "CloudMatchClient token hygiene contract is missing: $needle"
    }
}
if (-not $source.Contains('SnapshotUploadEncodeResult::payloadTooLarge') -or
    -not $source.Contains('"payload_too_large"') -or
    -not $source.Contains('"snapshot_upload_result"')) {
    throw 'Oversized snapshot envelopes must emit one normalized payload_too_large result.'
}
if (-not $source.Contains('SnapshotUploadEncodeResult::invalidPayload') -or
    -not $source.Contains('"invalid_payload"')) {
    throw 'Invalid snapshot encoding must emit one normalized invalid_payload result.'
}
if ($source -notmatch '(?s)encoded == cloud_match::SnapshotUploadEncodeResult::payloadTooLarge.*?NotifySnapshotUploadFailure.*?return SnapshotSendResult::rejected') {
    throw 'Server-limit snapshot rejection must be consumed without reconnect or requeue.'
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

if ($source -notmatch '(?s)void Stop\(\).*?stopRequested\.store\(true.*?CancelActiveOperation\(\)') {
    throw 'Stop must atomically take and close the active blocking WinHTTP handle.'
}
if ($source -notmatch '(?s)PublishCancelableHandle\(HINTERNET handle,.*?stopRequested\.load') {
    throw 'Cancelable WinHTTP handles must not be published after Stop is requested.'
}
if ($source -notmatch '(?s)ERROR_WINHTTP_OPERATION_CANCELLED.*?ShouldStop') {
    throw 'A Stop-triggered WinHTTP cancellation must be handled as normal shutdown.'
}
if ($source -notmatch '(?s)ActiveHandleScope requestScope.*?WinHttpSendRequest') {
    throw 'REST requests must be published for cancellation before blocking send/receive calls.'
}
if ($source -notmatch '(?s)ActiveHandleScope socketScope.*?WinHttpWebSocketReceive') {
    throw 'The WebSocket must be published for cancellation before a blocking receive.'
}
if ($source -notmatch '(?s)if \(stopping\).*?condition\.wait\(lock.*?!stopping') {
    throw 'Concurrent Stop calls must wait for the in-progress join instead of racing it.'
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
    'C:\Program Files\Microsoft Visual Studio\2022\Community',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise'
)) {
    if (-not $installRoots.Contains($knownRoot)) {
        $installRoots.Add($knownRoot)
    }
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
    throw 'Visual Studio C++ x64 compiler was not found for the client harness.'
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('DNFGameCapture-cloud-match-client-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $tempRoot
$exe = Join-Path $tempRoot 'cloud_match_client_test.exe'

try {
    $compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && "{1}" /nologo /std:c++17 /EHsc /W4 /Y- /DCLOUD_MATCH_CLIENT_STANDALONE /DCLOUD_MATCH_PROTOCOL_STANDALONE /I"{2}" /Fe:"{3}" /Fo:"{4}\\" "{5}" "{6}" "{7}" winhttp.lib' -f `
        $devCmd, $compiler, $root, $exe, $tempRoot, $clientTestPath, $sourcePath, `
        $protocolSourcePath
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw "Cloud match client test compilation failed with exit code $LASTEXITCODE."
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Cloud match client tests failed with exit code $LASTEXITCODE."
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host 'Cloud match client static and runtime checks passed.' -ForegroundColor Green
