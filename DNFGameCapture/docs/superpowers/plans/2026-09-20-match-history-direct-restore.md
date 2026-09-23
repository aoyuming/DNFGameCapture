# Match History Direct Restore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct local history labels and let the user click any history row to restore the match to the state immediately after that operation.

**Architecture:** Native C++ remains the owner of history snapshots and cursor movement. The Web panel sends only an entry ID; native code resolves the ID, applies that entry's `afterSnapshot`, moves the cursor to `index + 1`, persists the restored state, and broadcasts the updated history. Later entries remain available through redo.

**Tech Stack:** MFC/C++17, nlohmann/json, WebView2 bridge, vanilla JavaScript/CSS, Node contract tests, MSBuild.

---

### Task 1: Lock the expected native behavior with a failing contract test

**Files:**
- Modify: `scripts/match-history-native-contract-test.js`
- Test: `scripts/match-history-native-contract-test.js`

- [ ] Add assertions requiring `RestoreMatchHistoryToEntry(std::uint64_t, CString&)`, `cmd_match_history_restore`, application of the selected entry's `afterSnapshot`, and cursor assignment to `index + 1`.
- [ ] Add assertions that local fallback history uses `本地状态更新/本机`, while reset and clear retain `手动重置战绩/手动清空选手`.
- [ ] Run `node scripts/match-history-native-contract-test.js` and confirm it fails because direct restore is absent.

### Task 2: Implement native direct restore

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] Declare `bool RestoreMatchHistoryToEntry(std::uint64_t entryId, CString& errorMessage);` beside undo/redo.
- [ ] In the implementation, call `ObserveMatchHistoryState()`, find the entry by stable ID, return a readable error when missing, no-op successfully when already current, apply `afterSnapshot`, persist via `RefreshAfterTeamSyncApply()`, set `m_matchHistoryCursor = index + 1`, and refresh `m_matchHistoryObservedSnapshot`.
- [ ] Handle `cmd_match_history_restore` by validating the JSON ID, invoking the helper, logging success, showing bridge errors, and broadcasting state.
- [ ] Run the native contract test and confirm it passes.

### Task 3: Make every history row an accessible one-click restore target

**Files:**
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Modify: `scripts/operation-history-web-test.js`

- [ ] Add failing Web assertions for `data-history-id`, `cmd_match_history_restore`, delegated click handling, Enter/Space keyboard handling, and pointer/focus styling.
- [ ] Render rows with `role="button"`, `tabindex="0"`, stable `data-history-id`, and a tooltip explaining that clicking restores to that step.
- [ ] Add `restoreMatchHistoryEntry(id)` to post `{ action: 'cmd_match_history_restore', id }`.
- [ ] Use one delegated list handler for click and keyboard activation; ignore the current row to avoid redundant writes.
- [ ] Style rows as actionable and give keyboard focus a visible outline.
- [ ] Run `node scripts/operation-history-web-test.js` and `node --check web前端/main.js`.

### Task 4: Build, visually verify, and package

**Files:**
- Build output: `x64/Release/DNFGameCapture.exe`
- Package output: `deployment-packages/update_v530_match-history-direct-restore-20260920.zip`

- [ ] Run all history, sync, player-library, and release contract tests plus `git diff --check`.
- [ ] Build `Release|x64` with the installed full Visual Studio MSBuild.
- [ ] Verify the generated EXE contains `手动重置战绩`, `手动清空选手`, and no `云端状态变化` string.
- [ ] Open the local Web preview and verify clickable/focus history-row styling without overlap.
- [ ] Stage only the runtime allowlist and produce a fresh 5.3.0 update ZIP and SHA-256 file.

The currently running outer-path EXE is an older build and must not be overwritten while running. Deliver the new package and clearly identify the stale process path.
