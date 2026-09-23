const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web前端', 'index.html'), 'utf8');
const main = fs.readFileSync(path.join(root, 'web前端', 'main.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'web前端', 'style.css'), 'utf8');
const native = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');

assert.match(html, /id="realtime-readonly-banner"/,
    '主界面必须提供实时同步红色禁止提示');
assert.match(main, /function applyRealtimeReadOnlyState\(/,
    'Web 必须集中应用实时同步只读状态');
for (const selector of [
    '.team-score-input', '.name-input', '.stat-kill', '.stat-death', '.stat-ak',
    '#btn-swap', '#btn-monitor', '#btn-reset', '#btn-clear-teams',
    '#btn-match-history-undo', '#btn-match-history-redo', '.operation-history-item'
]) {
    assert.ok(main.includes(selector), `实时同步只读范围缺少 ${selector}`);
}
assert.match(main, /实时同步中/,
    '运行按钮必须在跟随期间显示“实时同步中”');
assert.match(main, /cloudMatchState\?\.realtimeFollowing[\s\S]*?cmd_match_history_undo/,
    '历史撤销快捷键必须在实时同步时停止发送');
assert.match(main, /cmd_cloud_realtime_stop/,
    '实时同步只读状态必须保留停止同步入口');
assert.match(main, /function clearAllTeamsData\(\)\s*{\s*if \(cloudMatchState\?\.realtimeFollowing === true\)/,
    '清空场上数据必须在入口处拦截实时同步状态');
assert.match(main, /getElementById\('btn-swap'\)[\s\S]*?realtimeFollowing[\s\S]*?cmd_swap/,
    '交换按钮必须在发送命令前拦截实时同步状态');
assert.match(main, /getElementById\('btn-monitor'\)[\s\S]*?realtimeFollowing[\s\S]*?cmd_monitor/,
    '运行按钮必须在发送命令前拦截实时同步状态');
assert.match(css, /\.realtime-readonly-banner[\s\S]*?#[0-9a-fA-F]{6}|\.realtime-readonly-banner[\s\S]*?rgb/,
    '禁止提示必须有明确的红色视觉样式');
const readonlyInputRule = css.match(
    /body\.realtime-readonly \.team-score-input:disabled,\s*body\.realtime-readonly \.name-input:disabled,\s*body\.realtime-readonly \.stat-item input:disabled\s*\{([\s\S]*?)\}/
)?.[1] ?? '';
assert.ok(readonlyInputRule, '必须存在实时同步编辑框的只读样式');
assert.match(readonlyInputRule, /background:\s*transparent/,
    '实时同步禁用的编辑框必须使用透明背景');
assert.match(readonlyInputRule, /border-color:\s*transparent/,
    '实时同步禁用的编辑框不得显示边框');
assert.match(readonlyInputRule, /opacity:\s*1\s*;/,
    '实时同步禁用的编辑框必须保持正常状态的不透明度');
assert.match(readonlyInputRule, /-webkit-text-fill-color:\s*currentColor/,
    '实时同步禁用的编辑框必须沿用正常状态文字颜色');
assert.doesNotMatch(readonlyInputRule, /(?:background|border-color):\s*(?:rgba?\(|#[0-9a-fA-F])/,
    '实时同步禁用的编辑框不得铺底色或可见边框');
assert.doesNotMatch(readonlyInputRule, /(?:^|\n)\s*color:\s*(?:rgba?\(|#[0-9a-fA-F])/,
    '实时同步禁用的编辑框不得覆盖正常状态文字颜色');
for (const action of [
    'update_state', 'cmd_swap', 'cmd_reset_stats', 'cmd_match_history_undo',
    'cmd_match_history_redo', 'cmd_match_history_restore'
]) {
    assert.ok(native.includes(`action == "${action}"`), `原生实时同步拦截缺少 ${action}`);
}
assert.match(native, /ApplyRealtimeEditingLock\(/,
    '专业模式控件必须同步进入只读状态');
for (const control of [
    'm_chkFlip', 'm_btnStart', 'm_btnApply', 'm_btnReset',
    'm_editQuickAdd', 'm_btnQuickAdd', 'm_cmbTeamSelect', 'm_treePlayers'
]) {
    assert.match(native, new RegExp(`ApplyRealtimeEditingLock\\([\\s\\S]*?${control}\\.EnableWindow`),
        `专业模式实时同步锁定缺少 ${control}`);
}
assert.match(native, /ApplyRealtimeEditingLock\([\s\S]*?实时同步中/,
    '专业模式运行按钮必须显示“实时同步中”');
assert.match(native, /void CDNFGameCaptureDlg::BroadcastStateToWeb\(\)\s*{\s*ApplyRealtimeEditingLock\(\);/,
    '每次广播状态前必须同步专业模式控件锁定状态');

console.log('Realtime-follow read-only contract tests passed.');
