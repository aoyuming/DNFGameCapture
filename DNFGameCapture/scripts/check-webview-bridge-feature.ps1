$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webDialogPath = Join-Path $root 'WebScoreDlg.cpp'
$dialogPath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$webMainPath = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF + '\main.js')

foreach ($path in @($webDialogPath, $dialogPath, $webMainPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing WebView bridge file: $path"
    }
}

$webDialog = Get-Content -LiteralPath $webDialogPath -Raw -Encoding UTF8
$dialog = Get-Content -LiteralPath $dialogPath -Raw -Encoding UTF8
$webMain = Get-Content -LiteralPath $webMainPath -Raw -Encoding UTF8

if ($webDialog -notmatch '(?:const\s+)?HRESULT\s+hr\s*=\s*m_webview->PostWebMessageAsJson') {
    throw 'SendStateToWeb must capture PostWebMessageAsJson HRESULT.'
}
if ($webDialog -notmatch 'FAILED\(hr\)') {
    throw 'SendStateToWeb must log failed WebView2 message delivery.'
}
if ($dialog -notmatch 'web_bridge_ack') {
    throw 'C++ must send a WebView bridge acknowledgement after page_ready.'
}
if ($dialog -notmatch 'web_bridge_received') {
    throw 'C++ must receive the frontend WebView bridge diagnostic.'
}
if ($dialog -notmatch 'BroadcastStateToWeb' -or $dialog -notmatch 'errorLog\.Format') {
    throw 'C++ must log shared-state serialization failures instead of swallowing them.'
}
if ($dialog -notmatch 'payloadLog\.Format' -or $dialog -notmatch 'payload\.size\(\)') {
    throw 'C++ must log the serialized shared-state byte size.'
}
if ($dialog -notmatch 'DnfJsonUtf8\(\s*m_cloudMatchSyncUndoBackup\.empty\(\)') {
    throw 'Cloud sync undoReason must be converted to UTF-8 before entering nlohmann JSON.'
}
if ($webMain -notmatch 'web_bridge_ack') {
    throw 'Web frontend must handle the WebView bridge acknowledgement.'
}
if ($webMain -notmatch 'web_bridge_received') {
    throw 'Web frontend must report the first received C++ message.'
}
if ($webMain -notmatch 'bridgeAckReceived') {
    throw 'Web frontend must track bridge acknowledgement separately from sync_state.'
}

Write-Output 'WebView bridge checks passed.'
