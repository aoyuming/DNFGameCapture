const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web前端', 'index.html'), 'utf8');
const main = fs.readFileSync(path.join(root, 'web前端', 'main.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'web前端', 'style.css'), 'utf8');

const matchRowStart = html.indexOf('class="control-row control-row-match"');
const auxRowStart = html.indexOf('class="control-row control-row-aux"');
assert.ok(matchRowStart >= 0 && auxRowStart > matchRowStart,
    '主操作行和辅助操作行应存在且顺序正确');
const matchRow = html.slice(matchRowStart, auxRowStart);
assert.match(matchRow, /id="btn-operation-history"/,
    '操作历史按钮必须位于主窗口第一行');

assert.match(html, /id="operation-history-overlay"/,
    '操作历史宽对话框遮罩必须存在');
assert.match(html, /id="operation-history-dialog"/,
    '操作历史宽对话框主体必须存在');
assert.match(html, /id="operation-history-list"/,
    '操作历史左侧步骤列表必须存在');
assert.match(html, /关键操作|操作历史面板/,
    '操作历史面板应明确展示关键操作');

assert.match(main, /isOperationHistoryOpen/,
    '前端必须维护操作历史对话框打开状态');
assert.match(main, /setOperationHistoryOpen\(/,
    '前端必须提供操作历史对话框开关');
assert.match(main, /btn-operation-history/,
    '前端必须绑定操作历史按钮');
assert.match(main, /operation-history-list/,
    '前端必须渲染操作历史步骤列表');
assert.match(main, /operation-history-item-detail/,
    '前端必须渲染操作关键变动范围');
assert.match(main, /data-history-id=/,
    '每条历史记录必须携带稳定的原生条目 ID');
assert.match(main, /cmd_match_history_restore/,
    '前端必须发送按条目直接回溯命令');
assert.match(main, /operation-history-list[\s\S]*?addEventListener\(['"]click['"][\s\S]*?restoreMatchHistoryEntry/,
    '历史列表必须支持单击条目直接回溯');
assert.match(main, /operation-history-list[\s\S]*?addEventListener\(['"]keydown['"][\s\S]*?Enter[\s\S]*?restoreMatchHistoryEntry/,
    '历史条目必须支持键盘 Enter 或空格回溯');

assert.match(css, /\.operation-history-dialog[\s\S]*?width:\s*min\(430px,\s*calc\(100vw\s*-\s*36px\)\)/,
    '操作历史面板应使用接近最近识别的窄侧栏宽度');
assert.match(css, /\.operation-history-dialog[\s\S]*?max-height:\s*calc\(100vh\s*-\s*36px\)/,
    '操作历史面板应限制在窗口高度内');
assert.match(css, /\.operation-history-dialog[\s\S]*?position:\s*fixed/,
    '操作历史应固定在窗口右侧');
assert.match(css, /\.operation-history-dialog[\s\S]*?right:\s*18px/,
    '操作历史应从右侧显示');
assert.match(css, /\.operation-history-dialog[\s\S]*?background:\s*rgba\([^\n]+0\.9/,
    '操作历史面板应使用半透明背景');
assert.match(css, /\.operation-history-dialog[\s\S]*?backdrop-filter:\s*blur/,
    '操作历史面板应启用半透明模糊效果');
assert.match(css, /\.operation-history-layout[\s\S]*?display:\s*block/,
    '操作历史面板应使用单列关键记录布局');
assert.match(css, /\.operation-history-item\s*\{[\s\S]*?cursor:\s*pointer/,
    '操作历史条目应明确显示可点击');
assert.match(css, /\.operation-history-item:focus-visible[\s\S]*?outline:/,
    '键盘选中历史条目时必须有可见焦点');

console.log('Operation-history Web contract tests passed.');
