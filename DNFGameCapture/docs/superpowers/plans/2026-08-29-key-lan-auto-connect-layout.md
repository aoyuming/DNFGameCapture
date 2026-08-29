# 局域网自动发现、管理员续接与单行布局 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 客户端自动发现并回填服务器，管理员重启后自动续接，握手成功后明确提示，同时修复服务器单行设置遮挡。

**Architecture:** Web 只负责一次性触发发现和选择发现结果；C++ 负责在提权前保存连接参数、持久化一次性续接标记并在新进程消费。连接成功仍以 `KeyMappingLanService` 的 `connected` 状态跃迁为准。服务器 UI 使用专用固定/弹性混合网格。

**Tech Stack:** MFC/C++17、WebView2、原生 HTML/CSS/JavaScript、PowerShell 静态回归脚本。

---

### Task 1: 建立自动连接静态回归

**Files:**
- Create: `scripts/check-key-lan-auto-connect-feature.ps1`
- Modify: `scripts/check-key-mapping-lan-feature.ps1`

- [ ] **Step 1: 编写失败检查**

检查以下实现标记：Web 自动发现辅助函数、打开面板/切换角色触发、首项回填；C++ 的 `PendingClientConnect` 读写和启动恢复函数；握手成功消息；服务器专用单行网格与窄端口列。

- [ ] **Step 2: 运行并确认失败**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-lan-auto-connect-feature.ps1`

Expected: FAIL，指出尚缺少自动发现或待续接逻辑。

### Task 2: 实现 Web 自动发现与回填

**Files:**
- Modify: `web前端/main.js:3501-3565`
- Modify: `web前端/main.js:3754-3765`
- Modify: `web前端/main.js:5558-5591`

- [ ] **Step 1: 增加单次自动发现状态与辅助函数**

增加 `requestKeyLanDiscovery()`、`requestAutomaticKeyLanDiscovery()` 和 `applySelectedKeyLanServer()`；自动触发有面板生命周期锁，手动搜索不受限制。

- [ ] **Step 2: 接入面板打开和角色切换**

打开客户端面板或切换到客户端后搜索一次；关闭面板重置自动搜索锁。

- [ ] **Step 3: 回填发现结果**

优先选择上次地址，否则选择第一台；选中后更新 IP 和端口输入框。

- [ ] **Step 4: 运行前端检查**

Run: `node --check web前端\main.js`

Expected: PASS。

### Task 3: 实现管理员重启续接与成功提示

**Files:**
- Modify: `DNFGameCaptureDlg.h:282-286, 534-541`
- Modify: `DNFGameCaptureDlg.cpp:4934-4943`
- Modify: `DNFGameCaptureDlg.cpp:12163-12208`
- Modify: `DNFGameCaptureDlg.cpp:12390-12400`
- Modify: `DNFGameCaptureDlg.cpp:15487-15565`
- Modify: `DNFGameCaptureDlg.cpp:16671-16734`
- Modify: `web前端/main.js:630-651`

- [ ] **Step 1: 保存连接参数与续接状态**

在管理员判断前保存合法 IP、端口和配对码；仅在用户确认管理员重启时写入 `[KeyMappingLan] PendingClientConnect=1`。

- [ ] **Step 2: 新进程消费待连接标记**

Web 窗口完成创建后调用 `ResumePendingKeyMappingLanClientConnection()`；函数先清除并保存标记，再启用按键钩子并启动客户端，失败时保留连接参数和显示原因。

- [ ] **Step 3: 增加握手成功提示**

客户端 `connected` 从 false 变为 true 时发送 `key_lan_connected`，Web 显示绿色短提示；拒绝和连接失败继续走红色错误提示。

- [ ] **Step 4: 构建 C++**

Run: `& "E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe" "..\DNFGameCapture.slnx" /p:Configuration=Release /p:Platform=x64 /m`

Expected: Build succeeded, 0 errors。

### Task 4: 修复服务器单行网格

**Files:**
- Modify: `web前端/index.html:279-294`
- Modify: `web前端/style.css:3328-3377`

- [ ] **Step 1: 增加服务器专用语义类**

给地址、端口、配对码和状态单元增加独立类名，避免依赖 `first-child` 等脆弱选择器。

- [ ] **Step 2: 设置稳定单行列宽**

使用 `minmax(190px, 1fr) 72px 96px 108px 126px minmax(128px, .8fr)`；端口输入减少水平内边距，常规宽度保持一行。

- [ ] **Step 3: 保留窄视口回退**

现有媒体查询下允许两列换行，并让地址和连接状态横跨整行。

### Task 5: 验证和 Release 同步

**Files:**
- Copy: `web前端/index.html` -> `..\..\..\x64\Release\web前端\index.html`
- Copy: `web前端/style.css` -> `..\..\..\x64\Release\web前端\style.css`
- Copy: `web前端/main.js` -> `..\..\..\x64\Release\web前端\main.js`

- [ ] **Step 1: 运行专项与语法检查**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-lan-auto-connect-feature.ps1`

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-lan-feature.ps1`

Run: `node --check web前端\main.js`

- [ ] **Step 2: 运行差异检查**

Run: `git diff --check`

Expected: 无输出。

- [ ] **Step 3: 构建 Release x64**

Run: `& "E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe" "..\DNFGameCapture.slnx" /p:Configuration=Release /p:Platform=x64 /m`

Expected: 0 errors。

- [ ] **Step 4: 同步并校验前端**

复制三个前端文件到最终 Release 目录，使用 `Get-FileHash -Algorithm SHA256` 确认源文件和目标文件一致。

