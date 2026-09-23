const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const dialog = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.h'), 'utf8');
const kill = fs.readFileSync(path.join(root, 'KillDisplayDlg.cpp'), 'utf8');
const webScore = fs.readFileSync(path.join(root, 'WebScoreDlg.cpp'), 'utf8');
const webScoreHeader = fs.readFileSync(path.join(root, 'WebScoreDlg.h'), 'utf8');

assert.match(header, /WM_KILL_DISPLAY_READY/,
    '击杀展示窗口必须能通知主窗口页面已就绪');
assert.match(dialog, /DnfStartKillDisplayHttpServer[\s\S]*?OpenKillDisplayWindow\(\)[\s\S]*?m_pWebDlg\s*=\s*new CWebScoreDlg/,
    '启动时必须先启动击杀服务并显示击杀窗口，再创建主 Web 窗口');
assert.match(dialog, /m_pWebDlg->ShowWindow\(SW_HIDE\)/,
    '主 Web 窗口创建后必须先保持隐藏');
assert.match(kill, /add_NavigationCompleted[\s\S]*?WM_KILL_DISPLAY_READY/,
    '击杀 WebView 导航完成后必须通知主窗口');
assert.match(dialog, /WM_KILL_DISPLAY_READY[\s\S]*?RevealMainWebWindow/,
    '主窗口必须处理击杀展示就绪消息');
assert.match(dialog, /kStartupMainWebRevealTimeoutTimerId[\s\S]*?RevealMainWebWindow/,
    '击杀展示初始化异常时必须有有限超时兜底');
assert.match(webScoreHeader, /afx_msg void OnShowWindow\(BOOL bShow, UINT nStatus\)/,
    '主 Web 窗口必须在显示状态变化时同步 WebView2 控制器');
assert.match(webScore, /ON_WM_SHOWWINDOW\(\)/,
    '主 Web 窗口必须监听 WM_SHOWWINDOW');
assert.match(webScore,
    /OnShowWindow\(BOOL bShow, UINT nStatus\)[\s\S]*?put_IsVisible\(bShow \? TRUE : FALSE\)/,
    '主 Web 窗口重新显示时必须显式恢复 WebView2 合成画面');
assert.match(webScore,
    /m_webviewController\s*=\s*controller;[\s\S]*?put_IsVisible\(\s*::IsWindowVisible\(m_hWnd\) \? TRUE : FALSE\)/,
    'WebView2 控制器晚于窗口显示创建时也必须继承宿主窗口可见状态');

console.log('Startup window ordering contract tests passed.');
