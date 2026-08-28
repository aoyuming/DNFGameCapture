# Reverse Sync Guard, Preview Typography, and Admin Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent active realtime followers from being synchronized back, make broadcaster previews use their available space, and add a localhost-only server administration console.

**Architecture:** Realtime direction is enforced consistently by a shared relation query on the server, a C++ guard before local mutation, and a Web presentation guard. The admin console is a second Express server bound only to `127.0.0.1:18881`; it shares the SQLite connection and a narrow socket-control interface with the public server while serving embedded HTML/CSS/JS and CSRF-protected JSON mutations.

**Tech Stack:** MFC/C++17, vanilla HTML/CSS/JavaScript, Node.js 22, TypeScript, Express, Socket.IO, SQLite/better-sqlite3, Vitest, Supertest.

---

### Task 1: Lock the reverse realtime rule with tests

**Files:**
- Modify: `cloud-match-server/tests/sync-relations.test.ts`
- Modify: `cloud-match-server/tests/socket-integration.test.ts`
- Modify: `scripts/check-unified-broadcaster-pool-feature.ps1`

- [ ] Add a relation-store test proving `isReverseRealtimeSyncBlocked(db, viewer, target, now)` returns true only when `target` currently follows `viewer`.
- [ ] Run the focused relation test and verify it fails because the query does not exist.
- [ ] Add a socket integration test where A follows B and B is rejected when starting realtime sync toward A with `reverse_sync_conflict`.
- [ ] Run the focused socket test and verify it fails because reverse starts are currently accepted.
- [ ] Extend the static check to require the Web helper, C++ helper/guards, and Chinese error mapping; run it and verify it fails.

### Task 2: Implement reverse-sync enforcement

**Files:**
- Modify: `cloud-match-server/src/sync-relations.ts`
- Modify: `cloud-match-server/src/socket.ts`
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `web前端/main.js`

- [ ] Implement the store query and reject `sync:realtime:start` before changing any existing relation.
- [ ] Add `CDNFGameCaptureDlg::IsCloudReverseSyncBlocked()` using `m_cloudRealtimeRelations`.
- [ ] Guard both `cmd_cloud_sync_broadcaster` and `cmd_cloud_realtime_start` before applying local data or stopping OCR.
- [ ] Map `reverse_sync_conflict` to a clear Chinese message in `DnfCloudMatchErrorText`.
- [ ] Add a Web helper using the same relation direction, disable both actions, and show a prominent preview notice while blocked.
- [ ] Re-run focused server and static tests until green.

### Task 3: Enlarge preview typography

**Files:**
- Modify: `web前端/style.css`
- Modify: `web前端/index.html`
- Modify: `scripts/check-broadcaster-preview-layout-feature.ps1`

- [ ] Extend the static style assertions for 17px primary names, 12px aliases, 11px stats, and the enlarged preview sections.
- [ ] Run the check and verify it fails on the existing smaller typography.
- [ ] Increase title, subtitle, metrics, team titles, player row height, recent recognition, relation text, and action button sizes while preserving alias ellipsis and hover titles.
- [ ] Bump the Web layout/cache version and re-run the layout check.

### Task 4: Define and test the localhost admin API

**Files:**
- Create: `cloud-match-server/tests/admin.test.ts`
- Create: `cloud-match-server/src/admin.ts`
- Create: `cloud-match-server/src/admin-data.ts`
- Modify: `cloud-match-server/src/socket.ts`

- [ ] Write Supertest coverage for security headers, CSRF rejection, broadcaster/search data, realtime relations/history, forced stop, forced disconnect, selected offline deletion, clearing offline/test data, and expired-data cleanup.
- [ ] Run `npm test -- tests/admin.test.ts` and verify it fails because the admin module is absent.
- [ ] Export a narrow socket controller for active device IDs, disconnect, relation stop, and directory notifications.
- [ ] Implement read models that expose only broadcaster/match/sync metadata and never tokens, authorization data, screenshots, appearance, or key mappings.
- [ ] Implement transactional cleanup that keeps permanent device identity rows while deleting lobby membership, snapshots, audit rows, sync history, and realtime relations.
- [ ] Implement CSRF-protected mutation routes and security/no-cache headers, then run the admin tests until green.

### Task 5: Build the embedded admin page and listener

**Files:**
- Create: `cloud-match-server/src/admin-page.ts`
- Modify: `cloud-match-server/src/admin.ts`
- Modify: `cloud-match-server/src/app.ts`
- Modify: `cloud-match-server/src/config.ts`
- Modify: `cloud-match-server/src/server.ts`

- [ ] Serve `/admin`, `/admin/app.js`, and `/admin/style.css` from TypeScript strings so deployment has no static-copy dependency.
- [ ] Render a quiet operations dashboard with searchable broadcaster rows, match preview, live relations, sync history, 3-second refresh, and guarded dangerous actions.
- [ ] Add `ADMIN_HOST`/`ADMIN_PORT` with production defaults `127.0.0.1`/`18881` and start/stop the second listener with the public app.
- [ ] Add tests proving the listener host defaults to loopback and the page contains no direct score-edit controls.

### Task 6: Deployment and complete verification

**Files:**
- Modify: `cloud-match-server/deploy/server.env`
- Modify: `cloud-match-server/deploy/install.sh`
- Modify: `cloud-match-server/README-部署.md`
- Modify: `scripts/check-unified-broadcaster-pool-feature.ps1`

- [ ] Document the SSH tunnel `ssh -L 18881:127.0.0.1:18881 root@47.109.149.111` and local URL `http://127.0.0.1:18881/admin`.
- [ ] Verify the installer never opens firewall port 18881 and health-checks the public server as before.
- [ ] Run `npm test`, `npm run typecheck:test`, and `npm run build` in `cloud-match-server`.
- [ ] Run `node --check web前端/main.js`, PowerShell feature checks, and `git diff --check`.
- [ ] Build Release x64 with clean `TEMP`, `TMP`, and `Path` environment values.
- [ ] Copy changed Web files to the main `x64/Release/web前端`, copy the rebuilt executable, and compare SHA-256 hashes.
- [ ] Package the updated Ubuntu server ZIP without deploying it.

**Commit note:** Do not commit or push these changes until the user explicitly asks.
