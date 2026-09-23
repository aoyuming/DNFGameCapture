const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(root, 'KillDisplayDlg.h'), 'utf8');
const source = fs.readFileSync(path.join(root, 'KillDisplayDlg.cpp'), 'utf8');
const main = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');

assert.match(header, /CKillDisplayDlg\(const CString& iniPath, CWnd\* pParent/,
    '击杀窗口必须接收配置路径');
assert.match(main, /new CKillDisplayDlg\(m_iniPath,\s*this\)/,
    '主窗口必须把 config.ini 路径传给击杀窗口');
assert.match(source, /L"KillDisplayWindow"/,
    '击杀窗口必须使用独立 INI 节');
for (const key of ['X', 'Y', 'Width', 'Height']) {
    assert.ok(source.includes(`L"${key}"`), `击杀窗口必须持久化 ${key}`);
}
assert.match(header, /OnExitSizeMove/,
    '用户调整击杀窗口后必须立即保存');
assert.match(header, /OnDestroy/,
    '击杀窗口销毁时必须保存尺寸');
assert.match(source, /GetDpiForWindow|GetDpiForWindowFn/,
    '默认和最小尺寸必须根据当前窗口 DPI 缩放');
assert.match(source, /MulDiv\([\s\S]*?96/,
    '持久化宽高必须在 96 DPI 逻辑单位和物理像素之间转换');
assert.match(source, /MonitorFromRect[\s\S]*?rcWork/,
    '恢复窗口时必须限制在显示器工作区');
assert.doesNotMatch(source, /SetWindowPos\(nullptr,\s*120,\s*120,\s*kKillDisplayClientWidth,\s*kKillDisplayClientHeight/,
    '启动时不得再强制使用 900x360 物理像素');

console.log('Kill-display DPI persistence contract tests passed.');
