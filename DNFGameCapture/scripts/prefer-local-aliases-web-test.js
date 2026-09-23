const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web前端', 'index.html'), 'utf8');
const main = fs.readFileSync(path.join(root, 'web前端', 'main.js'), 'utf8');
const css = fs.readFileSync(path.join(root, 'web前端', 'style.css'), 'utf8');
const cpp = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');

const auxStart = html.indexOf('class="control-row control-row-aux"');
const moreStart = html.indexOf('id="more-controls-menu"');
const aux = html.slice(auxStart, moreStart);
const more = html.slice(moreStart);

assert.ok(aux.includes('id="prefer-local-aliases-toggle"'),
    '本地优先开关应位于主辅助控制行');
assert.ok(!aux.includes('id="output-seat-label-toggle"'),
    '选人顺序不应继续占用主辅助控制行');
assert.ok(more.includes('id="output-seat-label-toggle"'),
    '选人顺序应移动到更多菜单');
assert.match(aux, /id="prefer-local-aliases-toggle"[\s\S]*?<\/label>/,
    '本地优先开关的 HTML 结构不完整');
assert.doesNotMatch(
    html.match(/id="prefer-local-aliases-toggle"[\s\S]{0,180}/)?.[0] || '',
    /checked/,
    '本地优先开关默认不能勾选');
assert.match(css, /\.prefer-local-aliases-toggle[\s\S]*?overflow: hidden;/,
    '主行本地优先文字必须限制溢出，避免撑大窗口');
assert.match(css, /\.prefer-local-aliases-toggle span[\s\S]*?text-overflow: ellipsis;/,
    '窄窗口下本地优先文字应使用省略显示');

assert.match(main, /let preferLocalAliases = false;/,
    'Web 状态默认值必须关闭本地优先');
assert.match(main, /preferLocalAliases\s*=\s*!\!state\.preferLocalAliases/,
    'Web 必须接收 C++ 广播的本地优先状态');
assert.match(main, /cmd_set_prefer_local_aliases/,
    'Web 必须发送本地优先设置动作');

assert.match(cpp, /m_bPreferLocalAliasesOnSync/,
    'C++ 必须保存本地优先状态');
assert.match(cpp, /PreferLocalAliases/,
    '本地优先状态必须写入配置');
assert.match(cpp, /data\["preferLocalAliases"\]/,
    'C++ Web 状态必须广播本地优先状态');
assert.match(cpp, /action == "cmd_set_prefer_local_aliases"/,
    'C++ 必须处理本地优先设置动作');

const applyStart = cpp.indexOf('bool CDNFGameCaptureDlg::ApplyTeamSyncSnapshot(');
const applyBody = cpp.slice(applyStart);
assert.match(applyBody, /m_bPreferLocalAliasesOnSync/,
    '同步应用逻辑必须读取本地优先开关');
assert.match(applyBody, /const bool mergeLocalAliasesFirst = m_bPreferLocalAliasesOnSync/,
    '同步逻辑必须计算本地别名优先模式');
assert.match(applyBody, /if \(mergeLocalAliasesFirst \|\| \(createBackup && !automatic\)\)/,
    '开启本地优先时必须在自动同步中建立本地别名表');
assert.match(applyBody, /ResolvePreferredLocalName/,
    '同步应用逻辑必须把远端名称解析为本地场上已选的关联名称');
assert.match(applyBody, /parsed\[i\]\.name\s*=\s*preferredLocalName/,
    '开启本地优先后必须实际替换场上显示名称');

console.log('Prefer-local-aliases Web contract tests passed.');
