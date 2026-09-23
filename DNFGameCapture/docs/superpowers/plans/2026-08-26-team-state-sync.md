# 局域网手动比赛状态同步 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有按键映射 LAN 连接上增加服务器到客户端的手动比赛状态快照同步，覆盖比分、队伍和 8 人战绩，并提供差异预览、确认覆盖和本次撤销。

**Architecture:** `KeyMappingLanService` 增加独立的 `team_sync_request/team_sync_snapshot/team_sync_error` 消息，不改变按键 `state` 心跳。服务器网络线程通过快照提供器读取当前状态，客户端收到快照后投递 Windows 消息到主线程；主线程负责校验、备份、应用、保存和广播。Web 只负责发起请求、显示差异和确认/撤销，不直接伪造比赛数据。

**Tech Stack:** Visual C++/MFC, Winsock TCP framing, nlohmann::json, WebView2 HTML/CSS/JavaScript, PowerShell static checks.

---

### Task 1: Extend the protocol test first

**Files:**
- Modify: `scripts/key_mapping_lan_protocol_test.cpp`

- [ ] **Step 1: Add a failing snapshot request test**

Register a server snapshot provider returning a JSON object with `redScore`, `blueScore`, and eight player entries. Register a client message callback, call `client.RequestTeamSync(error)`, wait for a `team_sync_snapshot` callback, then assert the returned object contains the expected score. Also assert a second request while one is pending is rejected.

- [ ] **Step 2: Run the protocol test check before implementation**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-lan-protocol-test.ps1
```

Expected: FAIL because `SetTeamSyncCallbacks` and `RequestTeamSync` do not exist yet.

### Task 2: Add an asynchronous LAN team-sync channel

**Files:**
- Modify: `KeyMappingLanService.h`
- Modify: `KeyMappingLanService.cpp`

- [ ] **Step 1: Add callback and request APIs**

Add:

```cpp
using TeamSyncSnapshotProvider = std::function<std::string()>;
using TeamSyncMessageCallback = std::function<void(const std::string&)>;

void SetTeamSyncCallbacks(TeamSyncSnapshotProvider provider,
    TeamSyncMessageCallback callback);
bool RequestTeamSync(std::string& error);
bool IsTeamSyncPending() const;
```

Store callbacks under a dedicated mutex. Store a monotonically increasing request id and a pending flag atomically. `RequestTeamSync` must require client role, a running/connected service, and reject a second pending request without changing data.

- [ ] **Step 2: Send manual requests from the client loop**

In `ClientThreadMain`, after pairing and before normal heartbeat polling, send at most one frame:

```json
{"type":"team_sync_request","version":1,"requestId":N}
```

Only send it when `RequestTeamSync` set the pending flag. Do not send it from `hello`, `state`, `ping`, reconnect, or connection-established paths.

- [ ] **Step 3: Handle server requests and client responses**

On the server, when paired and receiving `team_sync_request`, call the snapshot provider, parse its JSON, and send:

```json
{"type":"team_sync_snapshot","version":1,"requestId":N,"snapshot":{...}}
```

If the provider is empty or invalid, send `team_sync_error` with a short reason. On the client, accept only the matching pending request id, clear pending, and invoke the message callback with the complete JSON frame. Unknown team-sync message types must not terminate an otherwise valid key-sync connection.

- [ ] **Step 4: Add timeout and disconnect cleanup**

If a pending request has no response within 4 seconds, clear it and callback with `team_sync_error` reason `timeout`. Clear pending on `StopNetwork`, socket loss, rejected pairing, and role changes. Keep the existing 4 KB payload limit and protocol version checks.

- [ ] **Step 5: Run the protocol test**

Run the existing protocol test build/check command used by the repository and confirm the new request/response assertions pass while the 14-bit key-mask assertions still pass.

### Task 3: Marshal snapshots into C++ and apply them safely

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] **Step 1: Add state and message declarations**

Add `WM_KEY_MAPPING_TEAM_SYNC`, a message handler, a snapshot builder, and an apply helper. Keep two in-memory JSON values: `m_teamSyncPendingSnapshot` and `m_teamSyncBackupSnapshot`; add a boolean for backup availability. Clear them when the LAN role changes or the network is stopped.

- [ ] **Step 2: Register service callbacks in the dialog**

The snapshot provider must lock `m_dataMutex` and return only the approved match fields. The service message callback must allocate a `CString`, post it to the dialog window, and never touch MFC controls or `m_players` directly from the network thread. Clear both callbacks before shutdown.

- [ ] **Step 3: Build and validate the snapshot**

Build a version `1` object containing `redScore`, `blueScore`, `redPickMode`, `isFlipped`, `outputSeatLabelToKillFile`, `lastKillerTeam`, and eight players with `team`, `name`, `aliases`, `kills`, `deaths`, `akCount`, and `currentStreak`. Strip CR/LF from text and reject malformed arrays, negative counters, oversized text, invalid team/index combinations, or a snapshot version other than `1`.

- [ ] **Step 4: Add manual request, apply, and undo Web commands**

Handle these commands in `OnWebCmdReceived`:

```text
cmd_request_team_sync
cmd_apply_team_sync
cmd_undo_team_sync
```

`cmd_request_team_sync` is valid only for a connected client. `cmd_apply_team_sync` uses the stored pending snapshot instead of trusting a snapshot sent back from JavaScript. Before applying, save the current snapshot as the undo backup. Apply scores, team/player fields, streaks, pick mode, flip flag, and TXT output switch; then call `SaveAliasDB(false)`, `SaveConfigToFile()`, `WriteScoreToFile()`, `RefreshDisplay()`, and `BroadcastStateToWeb()`.

`cmd_undo_team_sync` applies the backup through the same validation path without creating a second backup, then clears the undo flag. All failures leave the current local state untouched.

- [ ] **Step 5: Broadcast LAN sync status**

Extend `BuildKeyMappingSettingsJson()` with `teamSyncPending`, `teamSyncCanUndo`, and `teamSyncSupported`. Send `team_sync_snapshot` to Web only after the main-thread handler has validated and stored it. Send `team_sync_error` for timeout, disconnect, unsupported peer, invalid data, or apply failure.

### Task 4: Add Web preview, confirmation, and undo controls

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`

- [ ] **Step 1: Add client-only controls**

Under the existing client LAN panel add a status line, `获取服务器比赛状态` button, and hidden `撤销本次同步` button. The server panel explains that the client must request manually; it gets no automatic push control.

- [ ] **Step 2: Render sync state without changing local inputs**

Normalize `teamSyncPending`, `teamSyncCanUndo`, and `teamSyncSupported`. Disable the request button while pending or disconnected. Do not call `applyStateFromServer` for a snapshot; store it separately until confirmation.

- [ ] **Step 3: Show a readable diff confirmation**

When `team_sync_snapshot` arrives, compare it against current DOM scores and eight rows. Render changed scores, names, aliases, kills, deaths, AK, pick mode, and flip state using escaped text. The existing confirmation modal uses `确认覆盖本地` and `取消`; confirmation sends only `cmd_apply_team_sync`.

- [ ] **Step 4: Add undo and error handling**

Handle `team_sync_error` with an alert and restore the button state. The undo button sends `cmd_undo_team_sync` after confirmation. Normal `sync_state` updates re-render controls but never auto-apply a pending snapshot.

- [ ] **Step 5: Add focused styles and cache version**

Style the sync block as a compact warning-safe panel with clear pending/connected states. Increment the main Web cache query so old JavaScript cannot hide the new commands.

### Task 5: Regression checks and packaging

**Files:**
- Modify: `scripts/check-key-mapping-lan-feature.ps1`
- Modify: `scripts/check-key-mapping-lan-protocol-test.ps1`

- [ ] **Step 1: Add static contract assertions**

Require the three Web commands, three protocol message names, snapshot builder/apply/undo symbols, client sync controls, diff confirmation text, and status fields. Keep assertions for existing key-mask protocol and loopback-only display service.

- [ ] **Step 2: Run checks**

```powershell
node --check web前端\main.js
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-lan-feature.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-key-mapping-lan-protocol-test.ps1
git diff --check
```

- [ ] **Step 3: Build Release x64**

```powershell
MSBuild ..\DNFGameCapture.slnx /p:Configuration=Release /p:Platform=x64 /m
```

Confirm the C++ project compiles with the new service callbacks and message handler.

- [ ] **Step 4: Sync Web assets to Release**

Copy `index.html`, `style.css`, and `main.js` from `web前端` to `x64\Release\web前端`, then compare SHA-256 hashes. Do not copy unrelated user files.

- [ ] **Step 5: Manual acceptance**

With two instances paired: verify ordinary key heartbeats do not change scores; client request shows a diff; cancel leaves local data unchanged; confirm copies scores/roster/stats; undo restores the prior state; disconnect and timeout never modify local state.
