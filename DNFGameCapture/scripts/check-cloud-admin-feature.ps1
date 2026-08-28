$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Read-RequiredFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing required file: $path" }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

function Reject-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        throw $message
    }
}

$admin = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\admin.ts')
$page = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\admin-page.ts')
$app = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\app.ts')
$config = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\config.ts')
$server = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\server.ts')
$socket = Read-RequiredFile (Join-Path $root 'cloud-match-server\src\socket.ts')
$env = Read-RequiredFile (Join-Path $root 'cloud-match-server\deploy\server.env')
$install = Read-RequiredFile (Join-Path $root 'cloud-match-server\deploy\install.sh')
$deployReadmeName = 'README-' + [char]0x90E8 + [char]0x7F72 + '.md'
$readme = Read-RequiredFile (Join-Path $root ('cloud-match-server\' + $deployReadmeName))

foreach ($needle in @(
    "app.get('/admin'",
    "app.get('/admin/api/state'",
    "x-dnf-admin-csrf",
    "X-Frame-Options",
    "Content-Security-Policy",
    "deleteBroadcasterLobbyData",
    "clearOfflineAndTemporaryBroadcasterData"
)) {
    Require-Text $admin $needle "Admin route/security behavior is missing: $needle"
}

foreach ($needle in @(
    'setInterval(refresh, 3000)',
    'id="relation-list"',
    'id="disconnect-button"',
    'id="cleanup-offline"',
    "classList.add('bidirectional')",
    '.empty-state[hidden]'
)) {
    Require-Text $page $needle "Admin page behavior is missing: $needle"
}
Reject-Text $page 'score-mutation' 'The admin page must not expose score mutation controls.'

foreach ($needle in @(
    'adminExpressApp',
    'adminHttpServer',
    'createCloudMatchAdminApp'
)) {
    Require-Text $app $needle "Cloud app does not own the localhost admin server: $needle"
}
foreach ($needle in @(
    'getActiveDeviceIds(): ReadonlySet<string>',
    'disconnectDevice(deviceId: string): boolean',
    'stopRealtimeViewer(viewerDeviceId: string): boolean',
    'notifyDirectoryChanged(reason: string): void'
)) {
    Require-Text $socket $needle "Socket admin controller is incomplete: $needle"
}

Require-Text $config "value === '0.0.0.0' || value === '::1'" 'Admin host does not support authenticated public binding.'
Require-Text $config "adminPort: readPort('ADMIN_PORT', 18881)" 'Admin port is not 18881 by default.'
Require-Text $config "adminPassword: process.env.ADMIN_PASSWORD?.trim() ?? ''" 'Admin password is not loaded from the environment.'
Require-Text $server 'app.adminHttpServer.listen' 'The admin listener is not started.'
Require-Text $server 'ADMIN_PASSWORD is required' 'Production startup does not reject a missing admin password.'
Require-Text $env 'ADMIN_HOST=0.0.0.0' 'Deployment does not bind authenticated admin publicly.'
Require-Text $env 'ADMIN_PORT=18881' 'Deployment does not configure the admin port.'
Require-Text $env 'ADMIN_PASSWORD=' 'Deployment does not declare the admin password.'
Require-Text $admin "WWW-Authenticate" 'Admin routes do not challenge unauthenticated browsers.'
Require-Text $admin "requireAdminAuthentication" 'Admin routes are not protected by authentication middleware.'
Require-Text $install 'ADMIN_PASSWORD="$(od -An -N16 -tx1 /dev/urandom' 'The installer does not generate a random admin password.'
Require-Text $install 'chmod 0600 "${ENV_FILE}"' 'The admin password file permissions are not restricted.'
Require-Text $install 'ufw allow 18881/tcp' 'The installer does not open the authenticated admin port in UFW.'
Require-Text $readme 'http://47.109.149.111:18881/admin' 'Public admin URL is missing.'
if (($readme.Split('0.0.0.0/0').Count - 1) -lt 2) {
    throw 'The deployment guide does not warn against a globally open admin rule.'
}

Write-Host 'Cloud admin static checks passed.'
