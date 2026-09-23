# 按键映射窗口简化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将按键映射改成固定 `2x7` 默认键位与手动绑定，并只向用户提供可被直播伴侣捕获的透明窗口。

**Architecture:** C++ 保留 14 格键盘轮询、持久化和独立 WebView2 套壳窗口，删除捕获画面区域、OCR 线程和相关消息。主 Web 只负责启用、窗口显隐、手动绑定、样式与恢复默认；透明页面作为套壳窗口内部渲染层，不再向用户提供网址入口。

**Tech Stack:** MFC/C++17、WebView2、Win32 `GetAsyncKeyState`、HTML/CSS/JavaScript、PowerShell 静态回归检查、MSBuild Release x64。

---

## 文件职责

- `scripts/check-key-mapping-feature.ps1`：锁定 14 格默认键位、窗口入口和旧框选/OCR/网址逻辑已删除。
- `DNFGameCaptureDlg.h/.cpp`：保存 14 格配置、读取按键状态、同步主 Web、管理展示窗口。
- `KeyDisplayDlg.h/.cpp`：提供可移动、缩放、隐藏和持久化位置的透明命名窗口。
- `web前端/index.html`：提供简化后的按键映射设置面板。
- `web前端/main.js`：管理 14 格默认配置、手动绑定、样式和恢复默认。
- `web前端/style.css`：设置面板固定 `2x7` 布局。
- `web前端/keys.html/.css/.js`：窗口内部的 `2x7` 透明高亮层。

### Task 1: 用静态回归检查锁定简化契约

**Files:**
- Modify: `scripts/check-key-mapping-feature.ps1`

- [ ] **Step 1: 将检查改为要求 14 格和默认键位**

在脚本中增加以下断言，并增加 `Reject-Text` 检查旧功能：

```powershell
Require-Text $main 'const KEY_MAPPING_SLOT_COUNT = 14' 'Web key mapping is not fixed to 14 slots.'
Require-Text $main "['Q', 'W', 'E', 'R', 'T', 'Y', 'Ctrl', 'A', 'S', 'D', 'F', 'G', 'H', 'Alt']" 'Default key layout is missing.'
Require-Text $keysJs 'const SLOT_COUNT = 14' 'Key display is not fixed to 14 slots.'
Require-Text $keysCss 'repeat(7, minmax(0, 1fr))' 'Key display is not using seven columns.'
Require-Text $dialogHeader 'KEY_MAPPING_SLOT_COUNT = 14' 'C++ key mapping is not fixed to 14 slots.'
Reject-Text $index 'btn-key-region-calibrate' 'Manual region calibration button still exists.'
Reject-Text $index 'btn-key-read' 'Key OCR button still exists.'
Reject-Text $index 'btn-key-obs-copy' 'Key display URL button still exists.'
Reject-Text $dialogSource 'BeginKeyMappingOcr' 'Key OCR implementation still exists.'
Reject-Text $dialogSource 'EnterKeyRegionCalibrationMode' 'Key region calibration still exists.'
Reject-Text $dialogSource 'cmd_copy_key_obs_url' 'Key display URL command still exists.'
```

- [ ] **Step 2: 运行检查并确认因旧实现仍存在而失败**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: FAIL，首个错误为 14 格常量或默认键位缺失。

### Task 2: 收缩 C++ 状态为 14 格固定映射

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] **Step 1: 定义 14 格常量与默认键位**

在 `DNFGameCaptureDlg.h` 的 `KeyMappingSlot` 前增加 C++17 内联常量：

```cpp
inline constexpr int KEY_MAPPING_SLOT_COUNT = 14;
```

在 `DNFGameCaptureDlg.cpp` 的按键映射常量区增加默认表：

```cpp
struct DnfDefaultKeyMappingSlot {
    UINT vk;
    const wchar_t* label;
};

static constexpr DnfDefaultKeyMappingSlot KEY_MAPPING_DEFAULTS[KEY_MAPPING_SLOT_COUNT] = {
    { 'Q', L"Q" }, { 'W', L"W" }, { 'E', L"E" }, { 'R', L"R" },
    { 'T', L"T" }, { 'Y', L"Y" }, { VK_CONTROL, L"Ctrl" },
    { 'A', L"A" }, { 'S', L"S" }, { 'D', L"D" }, { 'F', L"F" },
    { 'G', L"G" }, { 'H', L"H" }, { VK_MENU, L"Alt" }
};
```

将 `m_keyMappingSlots[16]` 改为：

```cpp
KeyMappingSlot m_keyMappingSlots[KEY_MAPPING_SLOT_COUNT];
```

源文件数组和循环统一使用头文件中的 `KEY_MAPPING_SLOT_COUNT`，避免散落数字和类私有成员可见性问题。

- [ ] **Step 2: 删除框选和 OCR 状态、方法及消息**

从头文件删除：

```cpp
WM_KEY_MAPPING_OCR_RESULT
OnKeyMappingOcrResult(...)
EnterKeyRegionCalibrationMode()
ExitKeyRegionCalibrationMode(bool)
DrawKeyRegionCalibration(CDC&)
GetKeyRegionClientRect() const
BeginKeyMappingOcr()
```

同时删除 `m_keyMappingOcrPending`、`m_keyMappingOcrThread`、所有 `m_keyMappingRegion*`、`m_bKeyRegionCalibrationMode`、拖框和快照成员。源文件删除 OCR 图像辅助函数、OCR 结果结构、消息映射、鼠标框选分支、绘制网格、源变化标记和析构线程等待。

- [ ] **Step 3: 删除旧 Web 命令和网址状态**

从 `OnWebCmdReceived` 删除：

```cpp
cmd_begin_key_region_calibration
cmd_read_key_mapping
cmd_copy_key_obs_url
```

删除 `KEY_DISPLAY_OBS_URL_W/UTF8` 和 `GetKeyDisplayObsUrl()`。`BuildKeyMappingSettingsJson()` 不再输出 `obsUrl`、`ocrPending`、`region`，只输出：

```cpp
settings["enabled"] = m_keyMappingEnabled.load();
settings["windowVisible"] = IsKeyDisplayWindowVisible();
settings["httpReady"] = m_bKillDisplayHttpReady;
settings["slots"] = json::array();
```

- [ ] **Step 4: 读取配置时为缺失项应用默认键位**

读取每格 `Vk` 前先用 `GetPrivateProfileString` 判断键是否存在。不存在时使用 `KEY_MAPPING_DEFAULTS[i]`；存在且值为 `0` 时保留用户主动清空状态：

```cpp
wchar_t vkText[32] = {};
GetPrivateProfileString(L"KeyMappingSlots", key, L"", vkText, 32, m_iniPath);
if (vkText[0] == L'\0') {
    m_keyMappingSlots[i].vk = KEY_MAPPING_DEFAULTS[i].vk;
    m_keyMappingSlots[i].label = KEY_MAPPING_DEFAULTS[i].label;
} else {
    m_keyMappingSlots[i].vk = static_cast<UINT>(_wtoi(vkText));
}
```

若 `Label` 不存在但 `Vk` 存在，则保留空标签或通过默认键位匹配补齐；不要把用户明确保存的 `Vk=0` 恢复成默认。

- [ ] **Step 5: 将状态与轮询改为 14 格、2x7**

`BuildKeyMappingStatePayload()` 输出：

```cpp
state["rows"] = 2;
state["columns"] = 7;
```

设置解析、状态序列化、保存和 `PollKeyMappingState()` 的数组及循环均使用 14 格。`activeMask` 继续使用 `unsigned int`，14 位足够且重复绑定行为不变。

- [ ] **Step 6: 运行静态检查确认 C++ 旧逻辑已清除但前端仍会失败**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: FAIL，原因转为 Web 仍是 16 格、按钮仍存在或透明页仍是 8 列。

### Task 3: 简化主 Web 设置为默认或手动配置

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`

- [ ] **Step 1: 删除框选、OCR 和网址控件**

工具栏仅保留启用开关、窗口开关和恢复默认：

```html
<label class="key-mapping-switch">
    <input type="checkbox" id="key-mapping-enabled">
    <span>启用按键响应</span>
</label>
<button class="key-mapping-action" id="btn-key-display-toggle">打开响应窗口</button>
<button class="key-mapping-action accent" id="btn-key-reset-defaults">恢复默认键位</button>
```

删除 `key-ocr-actions`，将网格说明改为“14 个技能格键位”。副标题默认显示“默认 QWERTY+Ctrl / ASDFGH+Alt”。

- [ ] **Step 2: 将前端默认配置改为 14 格**

在 `main.js` 增加：

```javascript
const KEY_MAPPING_SLOT_COUNT = 14;
const KEY_MAPPING_DEFAULT_LABELS = ['Q', 'W', 'E', 'R', 'T', 'Y', 'Ctrl', 'A', 'S', 'D', 'F', 'G', 'H', 'Alt'];
const KEY_MAPPING_DEFAULT_VKS = [81, 87, 69, 82, 84, 89, 17, 65, 83, 68, 70, 71, 72, 18];
```

`createDefaultKeyMappingSettings()` 用这两个数组生成 14 格，并移除 `obsUrl`、`ocrPending`、`region`。`normalizeKeyMappingSettings()` 固定只合并前 14 格。

- [ ] **Step 3: 删除旧交互并增加恢复默认**

删除 `keyMappingOcrCandidates`、OCR 结果处理、框选/读取/复制网址监听器和相关渲染状态。增加：

```javascript
function resetKeyMappingDefaults() {
    keyMappingSettings.slots.forEach((slot, index) => {
        slot.vk = KEY_MAPPING_DEFAULT_VKS[index];
        slot.label = KEY_MAPPING_DEFAULT_LABELS[index];
    });
    keyMappingCaptureSlot = -1;
    renderKeyMappingPanel();
    sendKeyMappingSettings(true);
}
```

绑定 `btn-key-reset-defaults`。此函数只恢复 `vk/label`，不修改每格 `color/opacity`。

- [ ] **Step 4: 将设置网格改为七列**

在 `style.css` 中将设置网格改为：

```css
.key-mapping-grid {
    grid-template-columns: repeat(7, minmax(0, 1fr));
}
```

窄窗口媒体查询继续允许两行完整显示，不引入水平滚动条。

- [ ] **Step 5: 运行前端语法检查**

Run:

```powershell
node --check web前端\main.js
```

Expected: PASS，无输出。

### Task 4: 将透明响应层改为 2x7 窗口内部页面

**Files:**
- Modify: `web前端/keys.js`
- Modify: `web前端/keys.css`
- Verify: `web前端/keys.html`
- Verify: `KeyDisplayDlg.cpp`

- [ ] **Step 1: 改成 14 格和七列**

`keys.js`：

```javascript
const SLOT_COUNT = 14;
```

`keys.css`：

```css
.key-grid {
    grid-template-columns: repeat(7, minmax(0, 1fr));
    grid-template-rows: repeat(2, minmax(0, 1fr));
}
```

- [ ] **Step 2: 保留仅套壳窗口可见的控制按钮**

确认 `?shell=1` 才显示移动、关闭和缩放控件，普通页面不新增 OBS 文案或网址入口。`CKeyDisplayDlg` 继续导航内部 `keys.html?shell=1`，标题保持：

```cpp
L"DNF Key Display - DNF按键映射窗口"
```

窗口保持 `WS_POPUP | WS_EX_APPWINDOW | WS_EX_LAYERED`，不增加 `WS_THICKFRAME` 和置顶标志。

- [ ] **Step 3: 运行透明页语法与静态检查**

Run:

```powershell
node --check web前端\keys.js
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: 两项均 PASS，静态脚本输出 `Key mapping feature static checks passed.`。

### Task 5: 构建、同步和运行验证

**Files:**
- Copy to: `..\x64\Release\web前端\index.html`
- Copy to: `..\x64\Release\web前端\style.css`
- Copy to: `..\x64\Release\web前端\main.js`
- Copy to: `..\x64\Release\web前端\keys.html`
- Copy to: `..\x64\Release\web前端\keys.css`
- Copy to: `..\x64\Release\web前端\keys.js`

- [ ] **Step 1: 运行完整静态验证**

Run:

```powershell
node --check web前端\main.js
node --check web前端\keys.js
node --check web前端\kill.js
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-kill-display-feature.ps1
git diff --check
```

Expected: 全部退出码为 0；仅允许 Git 的 LF/CRLF 提示。

- [ ] **Step 2: 关闭占用 Release 文件的程序并构建**

Run:

```powershell
Get-Process -Name DNFGameCapture -ErrorAction SilentlyContinue | Stop-Process -Force
& "E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe" "..\DNFGameCapture.slnx" /p:Configuration=Release /p:Platform=x64 /m
```

若终端同时存在 `Path` 与 `PATH`，使用已验证的 `ProcessStartInfo` 环境去重方式启动 MSBuild。Expected: `0` errors。

- [ ] **Step 3: 复制六个前端文件并进行 SHA-256 对比**

只复制 `index.html/style.css/main.js/keys.html/keys.css/keys.js` 到 Release。逐文件运行 `Get-FileHash -Algorithm SHA256`，Expected: 源文件与 Release 文件全部一致。

- [ ] **Step 4: 启动程序并验证窗口状态接口**

启动 Release 程序，确认：

```text
更多 > 工具 > 按键映射
默认第一行 Q W E R T Y Ctrl
默认第二行 A S D F G H Alt
没有框选、读取键位、复制 OBS 网址按钮
```

打开响应窗口，直播伴侣按窗口标题 `DNF Key Display - DNF按键映射窗口` 查找。按下默认键位时对应格显示，松开立即透明；窗口可移动、缩放和关闭。

- [ ] **Step 5: 提交实现**

仅暂存本计划涉及的源码、前端和静态检查文件：

```powershell
git add -- DNFGameCapture/DNFGameCaptureDlg.cpp DNFGameCapture/DNFGameCaptureDlg.h DNFGameCapture/KeyDisplayDlg.cpp DNFGameCapture/KeyDisplayDlg.h DNFGameCapture/DNFGameCapture.vcxproj DNFGameCapture/DNFGameCapture.vcxproj.filters DNFGameCapture/web前端/index.html DNFGameCapture/web前端/main.js DNFGameCapture/web前端/style.css DNFGameCapture/web前端/keys.html DNFGameCapture/web前端/keys.css DNFGameCapture/web前端/keys.js DNFGameCapture/scripts/check-key-mapping-feature.ps1
git commit -m "简化按键映射并提供直播伴侣窗口"
```

提交前再次运行 `git diff --cached --check`，不得包含无关未跟踪文件。
