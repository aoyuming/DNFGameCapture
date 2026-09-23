const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const client = fs.readFileSync(path.join(root, 'CloudMatchClient.cpp'), 'utf8');
const protocol = fs.readFileSync(path.join(root, 'CloudMatchProtocol.cpp'), 'utf8');
const socket = fs.readFileSync(path.join(root, 'cloud-match-server', 'src', 'socket.ts'), 'utf8');
const admin = fs.readFileSync(path.join(root, 'cloud-match-server', 'src', 'admin.ts'), 'utf8');

assert.match(source, /account_banned/,
    '客户端必须识别服务器封号错误码');
assert.match(source, /account_banned[\s\S]*?DnfClearProtectedLicenseLease\(\)/,
    '封号后必须清除本机加密授权租约');
assert.match(source, /account_banned[\s\S]*?DisableCloudMatchForAuthorization/,
    '封号后必须立即关闭云端连接');
assert.match(source, /account_banned[\s\S]*?OnBnClickedStart\(\)/,
    '封号后必须停止正在运行的 OCR');
assert.match(source, /leaseEndpointRefresh\s*&&\s*!authFailure->accountBanned/,
    '租约地址刷新发现封号时不得继续保留离线授权');
assert.match(source, /accountBanned[\s\S]*?attempt\s*=\s*maxAttempts/,
    '授权接口明确返回封号后不得继续自动重试');
assert.match(source, /struct DnfCloudAuthFailure[\s\S]*?bannedUntil/,
    '授权失败回调必须保留封禁截止时间');
assert.match(source, /reply\.find\("bannedUntil"\)/,
    '客户端必须解析授权接口的 bannedUntil');
assert.match(source, /ShowClientBanNotice\([\s\S]*?ShowCenteredMsgBox/,
    '客户端必须以弹窗显示封禁期限');
assert.match(source, /永久封禁/,
    '永久封禁必须有明确文案');
assert.match(source, /封禁至[\s\S]*?FormatTimeStamp/,
    '限时封禁必须显示本地日期时间');
assert.match(source, /type\s*==\s*"cloud_error"[\s\S]*?bannedUntil[\s\S]*?ShowClientBanNotice/,
    '实时连接收到封号时也必须弹出期限');
assert.match(client, /NotifyCloudError\([\s\S]*?bannedUntil/,
    'Socket 客户端必须把封禁期限透传给主窗口');
assert.match(protocol, /bannedUntil/,
    'Socket.IO 连接错误解析必须保留封禁期限');
assert.match(socket, /createSocketError\('account_banned',[\s\S]*?bannedUntil:\s*ban\.expiresAt/,
    '服务器 Socket 拒绝连接时必须返回封禁截止时间');
assert.match(admin, /disconnectDevice\(deviceId,[\s\S]*?account_banned[\s\S]*?bannedUntil:\s*ban\.expiresAt/,
    '后台封禁在线客户端时必须在断开前下发封禁期限');

console.log('Native client-ban contract tests passed.');
