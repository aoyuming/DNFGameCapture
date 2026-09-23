# OCR 后台预热与自动恢复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不阻塞主界面的前提下后台预热 Umi-OCR，并在其被关闭后自动恢复。

**Architecture:** 在 `CDNFGameCaptureDlg` 中增加一个可停止的监督线程和条件变量，统一负责 OCR 进程启动、服务探测和最小请求预热。UI 只读取原子就绪状态并通过窗口消息继续开始监控；退出时先停止并等待监督线程，再关闭 WinHTTP 和 OCR 子进程。

**Tech Stack:** MFC/Win32 C++17、`std::thread`、`std::condition_variable`、WinHTTP、现有 Umi-OCR HTTP API。

---

### Task 1: Add a regression contract for the OCR supervisor

**Files:**
- Create: `scripts/check-ocr-background-supervisor-feature.ps1`
- Test: the new PowerShell script itself

- [ ] **Step 1: Write the failing static test**

Create a PowerShell check that loads `DNFGameCaptureDlg.h/.cpp` and requires the supervisor methods, atomic readiness flags, explicit launch directory, warmup endpoint, and lifecycle stop call. It must also reject the old detached bootstrap/recovery thread pattern.

- [ ] **Step 2: Run the test and verify it fails for the missing feature**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-ocr-background-supervisor-feature.ps1
```

Expected: non-zero exit with a missing supervisor contract, before production code is changed.

### Task 2: Add supervisor state and lifecycle declarations

**Files:**
- Modify: `DNFGameCaptureDlg.h`

- [ ] **Step 1: Add declarations and state**

Add declarations for `StartOcrSupervisor`, `StopOcrSupervisor`, `RequestOcrSupervisorWork`, `OcrSupervisorLoop`, and `WarmupOcrEngine`. Add a joinable thread, condition variable, stop/wake flags, service/engine readiness atomics, and a pending-start request flag. Keep the existing public OCR method signatures for compatibility.

- [ ] **Step 2: Run the static test**

Run the same PowerShell check and confirm it still fails on the not-yet-implemented definitions rather than on the declarations.

### Task 3: Implement deterministic background launch and warmup

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] **Step 1: Implement the minimal warmup request**

Add a WinHTTP helper that posts a valid 1-pixel PNG JSON body to `/api/ocr`, checks the response, and logs elapsed time. It must run only from the supervisor thread and use the existing HTTP session/connection.

- [ ] **Step 2: Make automatic launch use the executable directory**

Update `EnsureOcrRunning` so `SHELLEXECUTEINFO::lpDirectory` is the parent directory of `m_ocrExePath`, close the optional process handle immediately, and retain the existing no-focus behavior. Keep all waits off the UI thread.

- [ ] **Step 3: Implement the supervisor loop**

Start with an immediate probe, launch when the process/service is absent, wait with bounded backoff, warm the engine once, publish atomic readiness, and wake again after the health interval. When stop is requested, leave the loop without launching or touching UI controls.

- [ ] **Step 4: Run the static test**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-ocr-background-supervisor-feature.ps1
```

Expected: PASS for launch context, warmup, and supervisor implementation.

### Task 4: Integrate start, recovery, and shutdown paths

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] **Step 1: Start the supervisor after dialog initialization**

Call `StartOcrSupervisor()` only after the dialog HWND, OCR path, HTTP handles, and Web dialog have been initialized.

- [ ] **Step 2: Replace click-time waiting with a supervisor request**

Change the start path to use the atomic engine-ready state. If not ready, set the existing pending UI and ask the supervisor to complete startup; preserve the existing success message path for starting monitoring.

- [ ] **Step 3: Route recovery requests to the supervisor**

Remove detached OCR recovery/bootstrap work from the hot path. Recovery requests only invalidate readiness and wake the supervisor. Timer 7 must continue health checking even when monitoring is stopped so a manually closed Umi-OCR is relaunched.

- [ ] **Step 4: Stop the supervisor before resource destruction**

Call `StopOcrSupervisor()` at the beginning of the destructor and true-exit path, before closing WinHTTP handles or destroying the dialog. Cancel pending start requests and leave existing score data untouched.

- [ ] **Step 5: Run the static test**

Run the OCR supervisor check and confirm it passes, including no `m_bIsRunning` gate around the watchdog and no detached thread in the bootstrap/recovery methods.

### Task 5: Verify compilation and regression behavior

**Files:**
- Modify: none unless verification finds a compile issue

- [ ] **Step 1: Run source checks**

```powershell
node --check web前端\main.js
node --check web前端\kill.js
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-ocr-background-supervisor-feature.ps1
git diff --check
```

- [ ] **Step 2: Build Release x64**

If `DNFGameCapture.exe` is running, terminate that process first, then run the approved VS MSBuild Release x64 command. Confirm the compiler and linker exit successfully.

- [ ] **Step 3: Inspect the final diff**

Confirm only OCR supervisor source/header and its focused regression script/docs changed; do not stage or revert unrelated user changes.
