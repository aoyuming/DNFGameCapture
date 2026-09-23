# Client Ban, Startup Ordering, and Realtime Lock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add full-client bans, guarantee the kill display appears before the main Web window, and make realtime-follow mode visibly read-only.

**Architecture:** A shared server-side ban module is the single policy source for HTTP and Socket authorization. Native startup uses a kill-display readiness message plus timeout fallback. The Web and MFC surfaces derive a read-only state from the existing realtime-follow flag while native command guards remain authoritative.

**Tech Stack:** TypeScript, Express, Socket.IO, SQLite, Vitest, MFC/C++17, WebView2, HTML/CSS/JavaScript, Node contract tests.

---

### Task 1: Server Ban Policy

**Files:**
- Create: `cloud-match-server/src/client-ban.ts`
- Modify: `cloud-match-server/src/db.ts`
- Modify: `cloud-match-server/src/v2-api.ts`
- Modify: `cloud-match-server/src/socket.ts`
- Test: `cloud-match-server/tests/v2-api.test.ts`
- Test: `cloud-match-server/tests/socket-integration.test.ts`

- [ ] Add failing tests for blocked activation, validation, protected APIs, Socket handshake, and automatic expiry.
- [ ] Run focused Vitest tests and confirm `account_banned` expectations fail.
- [ ] Add the `client_bans` schema and shared active-ban lookup/set/clear functions.
- [ ] Enforce the policy in activation, validation, session loading, and Socket middleware.
- [ ] Re-run focused tests and confirm they pass.

### Task 2: Admin Ban Workflow

**Files:**
- Modify: `cloud-match-server/src/admin.ts`
- Modify: `cloud-match-server/src/admin-data.ts`
- Modify: `cloud-match-server/src/broadcaster-admin-page.ts`
- Test: `cloud-match-server/tests/admin.test.ts`

- [ ] Add failing tests for preset/custom/permanent bans, linked authorization IDs, immediate disconnect, session revocation, state display, and unban.
- [ ] Add `/ban` PUT/DELETE endpoints and return normalized ban state.
- [ ] Replace the OCR/disconnect controls with selected-broadcaster ban controls.
- [ ] Run admin tests and confirm they pass.

### Task 3: Native Ban Reaction and Startup Gate

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `DNFGameCaptureDlg.h`
- Modify: `KillDisplayDlg.cpp`
- Modify: `KillDisplayDlg.h`
- Test: `scripts/client-ban-native-contract-test.js`
- Test: `scripts/startup-window-order-test.js`

- [ ] Add failing contract tests for `account_banned` teardown and kill-first startup ordering.
- [ ] Clear the protected lease, stop OCR, and tear down cloud state when banned.
- [ ] Start the kill HTTP/window first and reveal the main Web window only after readiness or timeout.
- [ ] Run both contract tests.

### Task 4: Realtime Read-Only UI

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `DNFGameCaptureDlg.h`
- Test: `scripts/realtime-follow-readonly-test.js`

- [ ] Add a failing contract test for the red banner, monitor label, disabled match controls, blocked history shortcuts, and allowed stop-sync action.
- [ ] Implement the Web read-only state and styling.
- [ ] Expand native action guards and disable MFC match-edit controls.
- [ ] Run the contract test and existing operation-history tests.

### Task 5: Full Verification

**Files:**
- Verify only.

- [ ] Run `npm test` in `cloud-match-server`.
- [ ] Run all new and related Node/PowerShell contract tests.
- [ ] Build `DNFGameCapture.sln` or `DNFGameCapture.vcxproj` in Release x64.
- [ ] Inspect `git diff --check` and the scoped diff without reverting unrelated worktree changes.
