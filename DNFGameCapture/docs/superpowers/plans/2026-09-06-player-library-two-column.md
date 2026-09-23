# 选手与游戏 ID 双栏管理界面 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将选手身份与游戏 ID 管理页改成双栏管理布局，统一把“绰号”显示为“别名”，并保持现有 C++ 身份组命令和云端 payload 不变。

**Architecture:** 继续使用现有 `playerIdentity` Web 状态和 `cmd_identity_*` 消息。左栏只渲染可搜索的名称行，右栏根据当前焦点渲染身份组详情、共享游戏 ID 摘要和按需编辑器；身份组归并、拆分、删除和保存仍由 C++ 主线程完成。

**Tech Stack:** 原生 HTML/CSS/JavaScript、WebView2 bridge、MFC/C++、PowerShell 静态回归脚本。

---

### Task 1: 添加双栏 DOM 骨架和回归检查

**Files:**
- Modify: `web前端/index.html:479-503`
- Create: `scripts/check-player-identity-two-column.ps1`

- [ ] **Step 1: 写静态失败检查**

创建 PowerShell 检查，读取 `web前端/index.html`、`web前端/main.js`、`web前端/style.css` 和 `DNFGameCaptureDlg.cpp`，断言：

```powershell
function Require-Text($text, $needle, $label) {
    if ($text -notlike "*$needle*") { throw "Missing $label: $needle" }
}
function Forbid-Text($text, $needle, $label) {
    if ($text -like "*$needle*") { throw "Forbidden $label: $needle" }
}

$root = Split-Path -Parent $PSScriptRoot
$html = Get-Content (Join-Path $root 'web前端\index.html') -Raw
$js = Get-Content (Join-Path $root 'web前端\main.js') -Raw
$css = Get-Content (Join-Path $root 'web前端\style.css') -Raw
$cpp = Get-Content (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw

Require-Text $html 'identity-members-pane' 'left member pane'
Require-Text $html 'identity-details-pane' 'right detail pane'
Require-Text $html 'identity-member-list' 'member list host'
Require-Text $html 'identity-detail-pane' 'detail host'
Require-Text $js 'renderIdentityDetailPane' 'detail renderer'
Require-Text $css 'grid-template-columns: minmax(250px, 34%) minmax(0, 1fr)' 'two-column layout'
Forbid-Text ($html + $js + $cpp) '绰号' 'old visible alias wording'
Write-Output 'player identity two-column checks passed'
```

- [ ] **Step 2: 运行检查确认当前代码失败**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-player-identity-two-column.ps1
```

预期：FAIL，指出缺少双栏 DOM 或仍存在“绰号”文案。

- [ ] **Step 3: 替换身份面板骨架**

在 `identity-overlay` 内容中保留现有标题、搜索框、刷新和关闭按钮，将结果区改成：

```html
<div class="identity-workspace">
  <aside class="identity-members-pane">
    <div class="identity-pane-heading">
      <span>选手</span>
      <span id="identity-selected-count">已选择 0 个名称</span>
    </div>
    <div class="identity-member-list" id="identity-member-list"></div>
  </aside>
  <section class="identity-details-pane">
    <div id="identity-detail-pane"></div>
  </section>
</div>
<div class="identity-selection-summary">
  <span class="identity-selection-label">当前已选择</span>
  <span class="identity-selection-names" id="identity-selected-names">暂无选手</span>
</div>
<section class="identity-suggestions" id="identity-suggestions" hidden></section>
```

保留 `identity-search-input`、`identity-refresh` 和 `identity-close` 的现有 id，确保原有事件绑定继续工作。

- [ ] **Step 4: 运行静态检查确认 DOM 断言通过**

运行同一 PowerShell 检查；预期仅剩 JavaScript 渲染函数和旧文案断言失败。

### Task 2: 将身份渲染改为左列表、右详情

**Files:**
- Modify: `web前端/main.js:4802-4945`

- [ ] **Step 1: 保留现有编辑器，增加右栏详情渲染函数**

保留 `renderIdentityIdEditor(group)`，增加以下函数：

```javascript
function renderIdentityNameBadge(name, group) {
    if (!group) return '<span class="identity-name-meta">独立选手</span>';
    return `<span class="identity-name-meta">同一选手 · ${group.names.length} 个名称</span>`;
}

function renderIdentityDetailPane() {
    const detail = document.getElementById('identity-detail-pane');
    if (!detail || !playerIdentityState) return;
    const name = identityFocusedName || '';
    const group = getIdentityGroupForName(name);
    const names = group ? group.names : (name ? [name] : []);
    const ids = group ? group.ids : identityIdsForName(name);
    if (!name) {
        detail.innerHTML = '<div class="identity-empty-state">从左侧选择一个选手查看游戏 ID。</div>';
        return;
    }
    const namesHtml = names.map(item =>
        `<span class="identity-related-name${item === name ? ' is-current' : ''}">${escapeHtml(item)}</span>`
    ).join('');
    const idsHtml = ids.length
        ? ids.slice(0, 4).map(id => `<span class="identity-id-chip" title="${escapeHtml(id)}">${escapeHtml(id)}</span>`).join('')
        : '<span class="identity-empty">暂无游戏ID</span>';
    const expanded = Boolean(group && identityExpandedIdGroups.has(group.groupId));
    const editor = expanded ? renderIdentityIdEditor(group) : '';
    const editButton = group
        ? `<button type="button" class="identity-card-action identity-edit-ids" data-group-id="${escapeHtml(group.groupId)}">${expanded ? '收起编辑' : '编辑游戏ID'}</button>`
        : '';
    const groupActions = group
        ? `<button type="button" class="identity-card-action identity-add-alias" data-group-id="${escapeHtml(group.groupId)}">添加别名</button>${editButton}<button type="button" class="identity-card-action identity-unmerge" data-group-id="${escapeHtml(group.groupId)}">拆散身份组</button>`
        : `<button type="button" class="identity-card-action identity-add-alias" data-source-name="${escapeHtml(name)}">添加别名</button>`;
    detail.innerHTML = `
        <div class="identity-detail-header">
            <div>
                <div class="identity-detail-kicker">当前选手</div>
                <h3>${escapeHtml(name)}</h3>
            </div>
            <span class="identity-detail-count">${group ? `同一选手 · 共 ${group.names.length} 个名称` : '独立选手'}</span>
        </div>
        <div class="identity-detail-section">
            <div class="identity-detail-label">关联名称</div>
            <div class="identity-related-names">${namesHtml}</div>
        </div>
        <div class="identity-detail-section">
            <div class="identity-detail-label">游戏ID <span>${ids.length} 个</span></div>
            <div class="identity-id-list">${idsHtml}</div>
            <div class="identity-detail-actions">${groupActions}</div>
            ${editor}
        </div>`;
}
```

详情模板必须包含当前名称、`同一选手 · 共 N 个名称`、关联名称标签、游戏 ID 数量、摘要、`添加别名`、`编辑游戏ID`、移出/拆散按钮；编辑展开时调用现有 `renderIdentityIdEditor(group)`。

- [ ] **Step 2: 将 `renderPlayerIdentityPanel()` 改成扁平左列表**

使用现有 `playerIdentityState.groups` 和 `entries` 构建名称项：

```javascript
const memberList = document.getElementById('identity-member-list');
const members = [];
playerIdentityState.groups.forEach(group => group.names.forEach(name => members.push({ name, group })));
playerIdentityState.entries.forEach(entry => {
    if (!getIdentityGroupForName(entry.name)) members.push({ name: entry.name, group: null, ids: entry.ids });
});
members.sort((a, b) => Number(b.name === focusedName) - Number(a.name === focusedName));
```

过滤名称或游戏 ID；当前项增加 `identity-focused-member`，输入复选框继续使用 `.identity-name-input` 和 `identitySelectedNames`，点击名称行更新 `identityFocusedName` 后重新渲染。当前项置顶并标红，组内其他名称保持普通状态。

- [ ] **Step 3: 让详情区复用现有事件委托**

将身份操作事件从只监听 `#identity-groups-list` 改为监听新的 `.identity-workspace` 或分别监听 `#identity-member-list`、`#identity-detail-pane`。所有按钮继续发送原消息：

```javascript
cmd_identity_add_alias
cmd_identity_update_ids
cmd_identity_unmerge
cmd_identity_delete_alias
```

左侧复选框仍支持多选归并；“查看并归并”和“忽略”继续使用现有建议区逻辑。

- [ ] **Step 4: 替换全部可见“绰号”文案**

在 `main.js` 中替换为：

```text
删除别名
添加别名
新增别名会从本地游戏ID库删除
输入新的别名：
例如：老王、旋律
归并后的别名不会因为编辑其中一个名称而重新分裂
```

不修改 `AddPlayerIdentityAlias` 等 C++ 函数名和协议字段。

### Task 3: 实现双栏样式和滚动边界

**Files:**
- Modify: `web前端/style.css:2155-2422`

- [ ] **Step 1: 增加双栏工作区样式**

将面板主体设置为固定高度内滚动：

```css
.identity-workspace {
    display: grid;
    grid-template-columns: minmax(250px, 34%) minmax(0, 1fr);
    min-height: 430px;
    max-height: min(620px, calc(100vh - 220px));
    border-top: 1px solid rgba(255,255,255,.08);
    border-bottom: 1px solid rgba(255,255,255,.08);
}
.identity-members-pane,
.identity-details-pane { min-width: 0; min-height: 0; padding: 14px 16px; }
.identity-members-pane { overflow-y: auto; border-right: 1px solid rgba(255,255,255,.08); }
.identity-details-pane { overflow-y: auto; }
.identity-member-list { display: grid; gap: 6px; }
```

右侧默认摘要不展开完整游戏 ID；编辑器内部使用独立滚动，防止长 ID 列表撑高整个窗口。

- [ ] **Step 2: 增加选中、身份组和别名视觉层级**

当前名称使用红色边框和深红背景，关联名称使用低亮度青色标签，游戏 ID 使用中性标签；避免当前所有 8 个关联按钮同时发光。

- [ ] **Step 3: 增加窄窗口单列回退**

在 `max-width: 760px` 下改成单列：左栏限制高度，右栏显示在下方；列表和详情都保持滚动。

- [ ] **Step 4: 运行前端语法和静态检查**

运行：

```powershell
node --check web前端\main.js
node --check web前端\kill.js
powershell -ExecutionPolicy Bypass -File scripts\check-player-identity-two-column.ps1
```

预期：三个命令均成功，检查脚本输出 `player identity two-column checks passed`。

### Task 4: 同步 C++ 可见提示并验证发布文件

**Files:**
- Modify: `DNFGameCaptureDlg.cpp:12550-15591`
- Modify: `scripts/check-player-identity-group.ps1`
- Modify: `x64/Release/web前端/index.html`
- Modify: `x64/Release/web前端/main.js`
- Modify: `x64/Release/web前端/style.css`

- [ ] **Step 1: 替换 C++ 日志和 Toast 中的“绰号”**

仅修改用户可见的错误、日志和 Toast 文案为“别名”，保留函数名、变量名和命令 action 不变。

- [ ] **Step 2: 更新旧身份静态检查**

将 `scripts/check-player-identity-group.ps1` 中的 `删除绰号` 断言改为 `删除别名`，并增加 `添加别名` 断言。

- [ ] **Step 3: 构建 Release x64**

运行：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' '..\DNFGameCapture.slnx' /p:Configuration=Release /p:Platform=x64 /m
```

预期：C++ 编译成功；若正式 Release EXE 被占用，保留构建日志并使用独立临时输出目录验证编译结果，不删除占用文件。

- [ ] **Step 4: 同步并校验 Web 前端**

```powershell
Copy-Item -LiteralPath 'web前端\index.html' -Destination 'x64\Release\web前端\index.html' -Force
Copy-Item -LiteralPath 'web前端\main.js' -Destination 'x64\Release\web前端\main.js' -Force
Copy-Item -LiteralPath 'web前端\style.css' -Destination 'x64\Release\web前端\style.css' -Force

Get-FileHash 'web前端\index.html','x64\Release\web前端\index.html' -Algorithm SHA256
Get-FileHash 'web前端\main.js','x64\Release\web前端\main.js' -Algorithm SHA256
Get-FileHash 'web前端\style.css','x64\Release\web前端\style.css' -Algorithm SHA256
git diff --check
```

预期：每对文件 SHA-256 一致，`git diff --check` 无输出。

- [ ] **Step 5: 运行最终回归检查**

运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-player-identity-group.ps1
powershell -ExecutionPolicy Bypass -File scripts\check-player-identity-two-column.ps1
node --check web前端\main.js
```

检查身份组命令、删除确认、别名继承、按需编辑和双栏 DOM 都存在；不修改云函数、`cloud-match-server` 或比赛快照协议。
