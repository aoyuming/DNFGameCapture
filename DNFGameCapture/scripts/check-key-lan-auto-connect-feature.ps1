$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)

function Read-RequiredFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $path"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if (-not $content.Contains($needle)) {
        throw $message
    }
}

$dialogHeader = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.h')
$dialogSource = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$index = Read-RequiredFile (Join-Path $webRoot 'index.html')
$main = Read-RequiredFile (Join-Path $webRoot 'main.js')
$style = Read-RequiredFile (Join-Path $webRoot 'style.css')

Require-Text $main 'function requestAutomaticKeyLanDiscovery()' `
    'Opening the client panel does not have a guarded automatic discovery helper.'
Require-Text $main 'function applySelectedKeyLanServer' `
    'Discovered server selection is not shared with automatic address fill.'
Require-Text $main 'requestAutomaticKeyLanDiscovery();' `
    'The client panel never triggers automatic server discovery.'
Require-Text $main 'selectedServer.address' `
    'The selected discovered server does not fill the server address.'

Require-Text $dialogHeader 'ResumePendingKeyMappingLanClientConnection' `
    'The elevated process has no pending client connection resume path.'
Require-Text $dialogHeader 'm_keyMappingLanPendingClientConnect' `
    'The one-shot pending client connection state is missing.'
Require-Text $dialogSource 'L"PendingClientConnect"' `
    'Pending client connection state is not persisted.'
Require-Text $dialogSource 'ResumePendingKeyMappingLanClientConnection();' `
    'Startup does not consume the pending client connection.'
Require-Text $dialogSource 'L"key_lan_connected"' `
    'A successful LAN handshake does not notify the Web UI.'
Require-Text $main "msg.action === 'key_lan_connected'" `
    'The Web UI does not handle the successful LAN connection notice.'

$connectBranch = [regex]::Match($dialogSource,
    '(?s)else if \(action == "cmd_connect_key_lan"\).*?else if \(action == "cmd_disconnect_key_lan"\)')
if (-not $connectBranch.Success) {
    throw 'The LAN client connection command branch was not found.'
}
$savePosition = $connectBranch.Value.IndexOf('m_keyMappingLanServerAddress = address;')
$adminPosition = $connectBranch.Value.IndexOf('if (!IsRunningAsAdmin())')
if ($savePosition -lt 0 -or $adminPosition -lt 0 -or $savePosition -gt $adminPosition) {
    throw 'Server address and pair code must be saved before the administrator check.'
}

Require-Text $index 'key-lan-mode-panel server-panel' `
    'The server controls do not have a dedicated single-row layout class.'
Require-Text $style '.key-lan-mode-panel.server-panel' `
    'The server controls do not have a dedicated grid.'
Require-Text $style '72px' `
    'The server port column is not constrained to a narrow fixed width.'

Write-Host 'Key LAN auto-connect and layout checks passed.' -ForegroundColor Green
