# 实时同步崩溃修复与五天授权租约 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除实时同步心跳放大的 Web 状态数据竞争，并让月卡及以上在成功验证后五天内使用 DPAPI 加密租约。

**Architecture:** 新增独立 `LicenseLease` 策略/存储模块和 `CrashDump` 模块；主窗口只负责调用它们。云端消息处理显式判断消息是否改变可见状态，Web 比赛数据在锁内建立快照后于锁外完成序列化。

**Tech Stack:** C++17、MFC、WinHTTP、Windows DPAPI、DbgHelp、PowerShell 静态回归检查、MSBuild Release x64。

---

### Task 1: 建立失败的回归检查

**Files:**
- Create: `scripts/license_lease_policy_test.cpp`
- Create: `scripts/check-license-lease-policy-test.ps1`
- Create: `scripts/check-realtime-sync-crash-guard.ps1`

- [ ] 写租约策略测试，覆盖 30 天门槛、五天租约、实际到期、永久卡和时间回拨。
- [ ] 写静态检查，要求成功心跳不广播、共享 Web 比赛快照持有 `m_dataMutex`、DPAPI 和 MiniDump 已接入。
- [ ] 运行两个检查，确认当前源码因缺少实现而失败。

### Task 2: 实现租约策略与 DPAPI 存储

**Files:**
- Create: `LicenseLease.h`
- Create: `LicenseLease.cpp`
- Modify: `DNFGameCapture.vcxproj`
- Modify: `DNFGameCapture.vcxproj.filters`

- [ ] 实现 `IsLicenseLeaseEligible`、`GetLicenseLeaseValidUntil` 和 `ValidateLicenseLease`。
- [ ] 使用 `CryptProtectData/CryptUnprotectData` 加密版本化租约，并以 `REG_BINARY` 读写注册表。
- [ ] 运行租约策略测试并确认通过。

### Task 3: 接入五天授权租约

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `DNFGameCaptureDlg.h`

- [ ] 自动验证前尝试读取并校验租约；命中后恢复真实到期时间和授权返回地址。
- [ ] 云函数验证成功后保存符合条件的租约；短卡清除不适用租约。
- [ ] 保证手动换卡始终联网，失败后旧卡仍能重新使用其有效租约。
- [ ] 记录租约命中、失效原因和下次验证时间，不记录卡密或服务器地址。

### Task 4: 修复实时同步广播竞争

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `DNFGameCaptureDlg.h`
- Modify: `scripts/check-realtime-sync-crash-guard.ps1`

- [ ] 成功心跳返回后直接结束处理，不调用 `BroadcastStateToWeb()`。
- [ ] 新增锁内共享比赛快照构建函数，锁外组合完整 Web 状态。
- [ ] Web 最近识别限制为最新 100 条，内部复盘记录保持完整。
- [ ] 运行静态检查和现有云端同步检查。

### Task 5: 加入崩溃转储

**Files:**
- Create: `CrashDump.h`
- Create: `CrashDump.cpp`
- Modify: `DNFGameCapture.cpp`
- Modify: `DNFGameCapture.vcxproj`
- Modify: `DNFGameCapture.vcxproj.filters`

- [ ] 在 `InitInstance()` 开始处安装未处理异常过滤器。
- [ ] 在 `crash-dumps` 目录生成带时间、进程号和异常码的 `.dmp`。
- [ ] 使用 `MiniDumpWriteDump` 写入线程和间接引用内存信息，写失败时不递归崩溃。

### Task 6: 完整验证

**Files:**
- Verify only

- [ ] 运行新增租约策略测试和崩溃保护静态检查。
- [ ] 运行现有授权门禁、云端同步、实时本地翻转和 WebView 桥检查。
- [ ] 运行 `node --check web前端\\main.js`、`git diff --check`。
- [ ] 构建 Release x64，并核对生成的 EXE。
