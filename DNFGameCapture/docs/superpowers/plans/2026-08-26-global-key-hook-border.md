# 按键映射全局响应与窗口边框 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 14 格按键映射改为不受前台窗口限制的全局低级键盘钩子，修复旧空配置，并给 KEY 套壳窗口增加常驻半透明矩形边框。

**Architecture:** 新增独立 `CKeyMappingHook` 类封装 `WH_KEYBOARD_LL` 安装、卸载和键状态，钩子回调只更新原子状态并继续传递事件。`CDNFGameCaptureDlg` 管理功能启用、配置迁移和 14 位 `activeMask`；现有 `keys.html` 仍轮询 `/api/key-mapping-state`，仅在 `shell=1` 时画边框。

**Tech Stack:** MFC/C++17、Win32 `SetWindowsHookExW(WH_KEYBOARD_LL)`、`std::atomic`、WebView2、HTML/CSS/JavaScript、PowerShell 静态回归检查、MSBuild Release x64。

---

## 文件职责

- `KeyMappingHook.h/.cpp`：独立拥有全局低级键盘钩子，记录物理键按下状态，忽略注入事件。
- `DNFGameCaptureDlg.h/.cpp`：管理钩子启停、Web 命令、失败提示、配置迁移和 14 位展示掩码。
- `DNFGameCapture.vcxproj/.filters`：编译并分组新钩子类。
- `web前端/keys.css`：给 KEY 套壳页添加常驻矩形边框及悬浮反馈。
- `web前端/main.js`：处理钩子安装失败的 Web 提示。
- `scripts/check-key-mapping-feature.ps1`：锁定全局钩子、旧配置迁移和常驻边框契约。

### Task 1: 用静态回归检查锁定新行为

**Files:**
- Modify: `scripts/check-key-mapping-feature.ps1`

- [ ] **Step 1: 增加全局钩子、迁移和边框断言**

在读取文件区增加：

```powershell
$hookHeader = Read-RequiredFile 'KeyMappingHook.h'
$hookSource = Read-RequiredFile 'KeyMappingHook.cpp'
```

在 C++ 断言区增加：

```powershell
Require-Text $hookHeader 'class CKeyMappingHook' 'Global key hook class is missing.'
Require-Text $hookSource 'WH_KEYBOARD_LL' 'Low-level global keyboard hook is missing.'
Require-Text $hookSource 'SetWindowsHookExW' 'Global keyboard hook is never installed.'
Require-Text $hookSource 'UnhookWindowsHookEx' 'Global keyboard hook is never removed.'
Require-Text $hookSource 'LLKHF_INJECTED' 'Injected keyboard events are not filtered.'
Require-Text $hookSource 'CallNextHookEx' 'Keyboard events are not forwarded.'
Require-Text $dialogSource 'KEY_MAPPING_LAYOUT_VERSION = 2' 'Key mapping migration version is missing.'
Require-Text $dialogSource 'migrateLegacySlot' 'Legacy empty key slots are not migrated.'
Reject-Text $dialogSource 'GetAsyncKeyState' 'Key mapping still uses foreground polling.'
Reject-Text $dialogSource 'allowPolling = (m_bIsRunning == TRUE)' 'Key mapping is still gated by monitoring state.'
Require-Text $project '<ClCompile Include="KeyMappingHook.cpp" />' 'Global key hook source is not in the project.'
Require-Text $project '<ClInclude Include="KeyMappingHook.h" />' 'Global key hook header is not in the project.'
```

在 CSS 断言区增加：

```powershell
Require-Text $keysCss '.shell-mode::before' 'Key shell border is missing.'
Require-Text $keysCss 'box-shadow: inset' 'Key shell border is not drawn inside the window.'
Require-Text $keysCss '.shell-mode:hover::before' 'Key shell border has no hover feedback.'
```

- [ ] **Step 2: 运行检查并确认为新功能缺失而失败**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: FAIL，首个错误为缺少 `KeyMappingHook.h` 或缺少全局钩子契约。

### Task 2: 新增独立全局键盘钩子

**Files:**
- Create: `KeyMappingHook.h`
- Create: `KeyMappingHook.cpp`
- Modify: `DNFGameCapture.vcxproj`
- Modify: `DNFGameCapture.vcxproj.filters`

- [ ] **Step 1: 定义只负责输入状态的钩子类**

`KeyMappingHook.h` 完整接口：

```cpp
#pragma once

#include <array>
#include <atomic>
#include <Windows.h>

class CKeyMappingHook
{
public:
    CKeyMappingHook();
    ~CKeyMappingHook();

    bool Install(DWORD& errorCode);
    void Uninstall();
    bool IsInstalled() const;
    bool IsKeyDown(UINT vk) const;

private:
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static CKeyMappingHook* s_activeHook;

    void ClearKeyStates();
    void SetKeyState(UINT vk, bool down);

    HHOOK m_hook = nullptr;
    std::array<std::atomic<bool>, 256> m_keyStates;
};
```

- [ ] **Step 2: 实现幂等安装、卸载和轻量回调**

`KeyMappingHook.cpp` 核心实现：

```cpp
#include "pch.h"
#include "KeyMappingHook.h"

CKeyMappingHook* CKeyMappingHook::s_activeHook = nullptr;

CKeyMappingHook::CKeyMappingHook()
{
    ClearKeyStates();
}

CKeyMappingHook::~CKeyMappingHook()
{
    Uninstall();
}

bool CKeyMappingHook::Install(DWORD& errorCode)
{
    errorCode = ERROR_SUCCESS;
    if (m_hook) return true;
    if (s_activeHook && s_activeHook != this) {
        errorCode = ERROR_ALREADY_EXISTS;
        return false;
    }

    ClearKeyStates();
    s_activeHook = this;
    m_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    if (!m_hook) {
        errorCode = GetLastError();
        s_activeHook = nullptr;
        return false;
    }
    return true;
}

void CKeyMappingHook::Uninstall()
{
    HHOOK hook = m_hook;
    m_hook = nullptr;
    if (hook) UnhookWindowsHookEx(hook);
    if (s_activeHook == this) s_activeHook = nullptr;
    ClearKeyStates();
}

bool CKeyMappingHook::IsInstalled() const
{
    return m_hook != nullptr;
}

void CKeyMappingHook::ClearKeyStates()
{
    for (auto& state : m_keyStates) state.store(false, std::memory_order_relaxed);
}

void CKeyMappingHook::SetKeyState(UINT vk, bool down)
{
    if (vk < m_keyStates.size()) {
        m_keyStates[vk].store(down, std::memory_order_relaxed);
    }
}

bool CKeyMappingHook::IsKeyDown(UINT vk) const
{
    if (vk >= m_keyStates.size()) return false;
    if (vk == VK_CONTROL) {
        return m_keyStates[VK_CONTROL].load(std::memory_order_relaxed) ||
            m_keyStates[VK_LCONTROL].load(std::memory_order_relaxed) ||
            m_keyStates[VK_RCONTROL].load(std::memory_order_relaxed);
    }
    if (vk == VK_SHIFT) {
        return m_keyStates[VK_SHIFT].load(std::memory_order_relaxed) ||
            m_keyStates[VK_LSHIFT].load(std::memory_order_relaxed) ||
            m_keyStates[VK_RSHIFT].load(std::memory_order_relaxed);
    }
    if (vk == VK_MENU) {
        return m_keyStates[VK_MENU].load(std::memory_order_relaxed) ||
            m_keyStates[VK_LMENU].load(std::memory_order_relaxed) ||
            m_keyStates[VK_RMENU].load(std::memory_order_relaxed);
    }
    return m_keyStates[vk].load(std::memory_order_relaxed);
}

LRESULT CALLBACK CKeyMappingHook::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_activeHook && lParam) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if ((info->flags & LLKHF_INJECTED) == 0) {
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
            if (down || up) s_activeHook->SetKeyState(info->vkCode, down);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
```

- [ ] **Step 3: 将新文件加入 VS 项目**

`DNFGameCapture.vcxproj` 增加：

```xml
<ClInclude Include="KeyMappingHook.h" />
<ClCompile Include="KeyMappingHook.cpp" />
```

`DNFGameCapture.vcxproj.filters` 将头文件放入 `头文件`，源文件放入 `源文件`。

- [ ] **Step 4: 运行静态检查确认失败点前移到主对话框接线或 CSS**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: FAIL，但不再报缺少钩子类或 VS 项目文件。

### Task 3: 接入钩子生命周期并迁移旧空配置

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `web前端/main.js`

- [ ] **Step 1: 在主对话框声明钩子成员和启停 helper**

`DNFGameCaptureDlg.h` 增加：

```cpp
#include "KeyMappingHook.h"
```

私有方法增加：

```cpp
bool SetKeyMappingEnabled(bool enabled, CString& errorMessage);
```

按键映射成员增加：

```cpp
CKeyMappingHook m_keyMappingHook;
```

- [ ] **Step 2: 实现启用、失败回滚和卸载**

`DNFGameCaptureDlg.cpp` 增加：

```cpp
bool CDNFGameCaptureDlg::SetKeyMappingEnabled(bool enabled, CString& errorMessage)
{
    errorMessage.Empty();
    if (!enabled) {
        m_keyMappingEnabled.store(false);
        m_keyMappingHook.Uninstall();
        m_keyMappingActiveMask.store(0);
        return true;
    }

    DWORD errorCode = ERROR_SUCCESS;
    if (!m_keyMappingHook.Install(errorCode)) {
        m_keyMappingEnabled.store(false);
        m_keyMappingActiveMask.store(0);
        errorMessage.Format(L"全局按键钩子安装失败（错误 %lu）。", errorCode);
        return false;
    }
    m_keyMappingEnabled.store(true);
    return true;
}
```

`LoadKeyMappingSettings()` 后，如果保存状态为启用，调用 `SetKeyMappingEnabled(true, errorMessage)`。失败时写回 `Enabled=0`并写入 C++ 日志。

析构函数开头增加：

```cpp
m_keyMappingHook.Uninstall();
m_keyMappingActiveMask.store(0);
```

- [ ] **Step 3: Web 设置命令改为通过 helper 切换**

`cmd_set_key_mapping_settings` 中用：

```cpp
CString hookError;
const bool requestedEnabled = settings.value("enabled", m_keyMappingEnabled.load());
const bool enabledApplied = SetKeyMappingEnabled(requestedEnabled, hookError);
```

保存槽位后统一调用 `SaveKeyMappingSettings()`。如果 `enabledApplied == false`：

```cpp
AppLog(L"⚠️ [按键映射] " + hookError, RGB(255, 180, 0));
if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"key_mapping_error", hookError);
```

`web前端/main.js` 的通用弹窗分支重新加入：

```javascript
msg.action === 'key_mapping_error'
```

- [ ] **Step 4: 将高亮掩码改为读取钩子状态**

`PollKeyMappingState()` 删除目标窗口和监控状态判断，核心循环改为：

```cpp
if (!m_keyMappingEnabled.load() || !m_keyMappingHook.IsInstalled()) {
    m_keyMappingActiveMask.store(0);
    return;
}

unsigned int mask = 0;
for (int i = 0; i < KEY_MAPPING_SLOT_COUNT; ++i) {
    const UINT vk = keys[i];
    if (vk > 0 && vk <= 0xFE && m_keyMappingHook.IsKeyDown(vk)) {
        mask |= (1u << i);
    }
}
m_keyMappingActiveMask.store(mask);
```

- [ ] **Step 5: 对旧空槽位执行一次性版本迁移**

常量区增加：

```cpp
static constexpr int KEY_MAPPING_LAYOUT_VERSION = 2;
```

`LoadKeyMappingSettings()` 先读取：

```cpp
const int storedLayoutVersion = GetPrivateProfileInt(
    L"KeyMapping", L"LayoutVersion", 0, m_iniPath);
```

每个槽位同时读取 `Vk` 和 `Label`，然后计算：

```cpp
const bool migrateLegacySlot = storedLayoutVersion < KEY_MAPPING_LAYOUT_VERSION &&
    hasStoredVk && _wtoi(vkText) == 0 && CString(labelText).IsEmpty();
```

`!hasStoredVk || migrateLegacySlot` 时使用 `KEY_MAPPING_DEFAULTS[i]`；否则保留已保存键位。锁作用域结束后，当旧版本配置被读取时调用 `SaveKeyMappingSettings()`。

`SaveKeyMappingSettings()` 增加：

```cpp
CString layoutVersion;
layoutVersion.Format(L"%d", KEY_MAPPING_LAYOUT_VERSION);
WritePrivateProfileString(L"KeyMapping", L"LayoutVersion", layoutVersion, m_iniPath);
```

- [ ] **Step 6: 运行静态检查和前端语法检查**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
node --check web前端\main.js
```

Expected: 静态检查只剩 KEY 矩形边框相关断言失败；`main.js` 语法检查通过。

- [ ] **Step 7: 提交 C++ 钩子和迁移实现**

Run from Git root:

```powershell
git add -- DNFGameCapture/KeyMappingHook.h DNFGameCapture/KeyMappingHook.cpp DNFGameCapture/DNFGameCaptureDlg.h DNFGameCapture/DNFGameCaptureDlg.cpp DNFGameCapture/DNFGameCapture.vcxproj DNFGameCapture/DNFGameCapture.vcxproj.filters DNFGameCapture/web前端/main.js DNFGameCapture/scripts/check-key-mapping-feature.ps1
git diff --cached --check
git commit -m "改用全局键盘钩子驱动按键映射"
```

Expected: 提交成功，不包含日志、Release 输出或其它未跟踪文件。

### Task 4: 给 KEY 套壳窗口增加常驻矩形边框

**Files:**
- Modify: `web前端/keys.css`

- [ ] **Step 1: 在套壳模式中用伪元素绘制内边框**

增加：

```css
.shell-mode::before {
    content: "";
    position: absolute;
    inset: 0;
    z-index: 12;
    pointer-events: none;
    box-shadow:
        inset 0 0 0 1px rgba(0, 229, 255, 0.56),
        inset 0 0 12px rgba(0, 229, 255, 0.08);
    transition: box-shadow 120ms ease;
}

.shell-mode:hover::before {
    box-shadow:
        inset 0 0 0 2px rgba(0, 229, 255, 0.92),
        inset 0 0 18px rgba(0, 229, 255, 0.16);
}
```

给 `.shell-controls` 和 `.shell-resize` 设置高于 `12` 的 `z-index`，保证控件视觉上始终位于边框之上。

- [ ] **Step 2: 运行按键映射静态检查**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
```

Expected: PASS，输出 `Key mapping feature static checks passed.`。

- [ ] **Step 3: 提交窗口边框**

Run from Git root:

```powershell
git add -- DNFGameCapture/web前端/keys.css
git diff --cached --check
git commit -m "为按键展示窗口增加定位边框"
```

Expected: 只提交 `keys.css`。

### Task 5: 完整验证、Release 同步和真实按键测试

**Files:**
- Copy to: `..\x64\Release\web前端\main.js`
- Copy to: `..\x64\Release\web前端\keys.css`

- [ ] **Step 1: 运行全部静态验证**

Run:

```powershell
node --check web前端\main.js
node --check web前端\keys.js
node --check web前端\kill.js
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-feature.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-kill-display-feature.ps1
git diff --check
```

Expected: 全部退出码为 `0`；Git 只允许 LF/CRLF 换行提示。

- [ ] **Step 2: 关闭运行中的程序并构建 Release x64**

Run:

```powershell
Get-Process -Name DNFGameCapture -ErrorAction SilentlyContinue | Stop-Process -Force
& "E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe" "..\DNFGameCapture.slnx" /p:Configuration=Release /p:Platform=x64 /m
```

Expected: `0` errors。现有 `CameraCapture.h` 编码警告和 `fallback_printwindow` 未使用标签警告可保留。

- [ ] **Step 3: 同步前端文件并比对 SHA-256**

复制：

```powershell
Copy-Item -LiteralPath "web前端\main.js" -Destination "..\x64\Release\web前端\main.js" -Force
Copy-Item -LiteralPath "web前端\keys.css" -Destination "..\x64\Release\web前端\keys.css" -Force
```

对两组源文件和 Release 文件运行 `Get-FileHash -Algorithm SHA256`。Expected: 每组哈希相同。

- [ ] **Step 4: 验证旧配置迁移和后续显式清空**

在备份配置或临时 Release 目录中创建旧状态：

```ini
[KeyMapping]
Enabled=0

[KeyMappingSlots]
Slot01Vk=0
Slot01Label=
```

首次启动后确认第 1 格迁移为 `Q`且写入 `LayoutVersion=2`。随后在 Web 中清空第 1 格、重启程序，确认第 1 格仍为空。

- [ ] **Step 5: 验证全局响应与矩形边框**

启用按键响应并打开 KEY 窗口，依次让 DNF、主 Web、C++ 窗口和 KEY 窗口位于前台：

```text
Q/W/E/R/T/Y/A/S/D/F/G/H -> 对应格按住时高亮，松开消失
Ctrl/Alt -> 对应修饰键格高亮
同时按键 -> 多格同时高亮
重复绑定 -> 同键对应的多格同时高亮
关闭功能 -> 所有高亮清空，后续按键无响应
```

无按键时确认 KEY 窗口有半透明青色矩形边框，中心保持透明；悬浮时边框、移动、缩放和关闭控件变亮。

- [ ] **Step 6: 检查最终分支状态**

Run from Git root:

```powershell
git status --short --branch
git log -3 --oneline
```

Expected: 实现提交存在，已知无关未跟踪文件保持原样，没有本功能的未提交源码变更。
