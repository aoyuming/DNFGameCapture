# Match Library Merge And Admin Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development for independent implementation and review. Preserve the existing dirty workspace and do not commit unrelated work.

**Goal:** Persist broadcaster-synced identities, apply the approved shared-game-ID >= 3 OR shared-adventure-ID >= 2 rule, and reuse the previous three-column library admin interface against the isolated test server.

**Architecture:** SQLite worker remains the local library owner. Match sync submits additive immutable identity batches, never a replacement of the full local library. Server snapshots carry optional adventureGroupIds; missing fields are treated as absent evidence. Admin UI reuses the existing layout but all operations go through authenticated/CSRF-protected SQLite APIs, never OSS.

**Tech Stack:** C++17/MFC/WebView2/static SQLite, TypeScript/Express/Socket.IO/better-sqlite3, existing HTML/CSS/JS.

## Task 1: Local Identity Policy And Persistence

Files: PlayerIdentityGroupService.h/.cpp, PlayerLibraryModel.h/.cpp, PlayerLibraryDatabase.h/.cpp, scripts/player_identity_group_test.cpp, scripts/player_library_core_test.cpp.

- [x] Add failing boundary tests for 2/3 common game IDs and 1/2 common adventure IDs; duplicates never increase evidence.
- [x] Implement indexed dual-kind evidence and persist shared identity unions after additive import. Preserve original sets for splitting, ignore/split exceptions, names, and remote identity provenance. Do not infer identity from one or two identical game IDs alone.
- [x] Verify ignored pairs stay separate, chain overlaps do not accidentally collapse unrelated names, reimport is idempotent, known remote IDs remain importable after local merge, and failure rolls back.
- [x] Run core and identity service tests.
- [x] Final provenance regressions: retain first-import cloud IDs, prefer live owners over split history, and self-import large normalized unions without truncation. Eight added native regressions passed.

## Task 2: Match Sync Integration

Files: DNFGameCaptureDlg.cpp/.h, CloudMatchProtocol.h, relevant protocol tests, cloud-match-server/src/schemas.ts and snapshot tests.

- [x] Add failing protocol and bridge checks for optional adventure IDs and legacy snapshots.
- [x] Include bounded adventureGroupIds in test-server snapshots; round-trip through validation, conversion and preview without changing score/stat semantics. Keep production payloads unchanged.
- [x] After applying sync, enqueue an additive ImportV2 identity batch on the SQLite worker, including named players with no game IDs. Refresh association state only from committed snapshots; surface busy/errors in Chinese.
- [x] Keep imports independent of preview/cancel/undo and preserve local display orientation. Coalesce repeated realtime identity batches and avoid library/snapshot echo loops.
- [x] Verify one-shot/realtime call chains, busy queue, reload and empty-ID players with automated tests. Live two-client Windows testing remains for the test environment.

## Task 3: Server Library Administration

Reference: 秘钥后台管理/web-admin/index.html, style.css, app.js. Do not change or deploy old OSS administration.
Files: cloud-match-server/src/admin.ts, v2-api.ts, new library-admin UI/API modules and tests.

- [x] Add API tests for list/detail/search, guarded public entity CRUD, import, pending review/batch rejection and approval, stale revisions, invalid payloads, and auth/CSRF.
- [x] Serve /admin/library using existing admin authentication. Reuse the screenshot's stats/topbar/three columns/bottom actions, with names, aliases, game IDs and adventure IDs.
- [x] Preserve strict public ownership validation; show conflicts for explicit review. Only actual supported operations get active controls; no pretend delete submissions or OSS calls. Both old/new admin review paths share guards and an atomic publication/status transaction.
- [x] Preserve editing/selection during refresh, fit narrow viewports, use confirmation before destructive operations and meaningful busy/error states.
- [x] Run server and real-browser tests with fixtures, including keyboard/scroll and screenshots.

## Task 4: Verification And Delivery

- [x] Run node --check for main.js/kill.js, server npm test -- --run, npm run build, npm run typecheck:test, focused C++ tests, and git diff --check.
- [x] Request focused independent spec/correctness review and fix findings.
- [x] Rebuild final provenance fixes to Release x64 at C:/Users/BRO/source/repos/DNFGameCapture/x64/Release and SHA-256 sync frontend assets.
- [x] Package test-server runtime/deploy files only, document server-first deployment and verification. No live production deployment and no secrets/data in archive.

## Verification Record

- Server: 140 tests, TypeScript build and test typecheck passed. Normalized identity limits allow large unions while keeping the aggregate byte cap; match protocol limits are unchanged.
- Web/bridge: 26 runtime/static checks passed; cloud conversion/queue native harness passed.
- C++ library core, identity service and OCR harnesses passed. Five actual INI copies audited without missing names/IDs or source-file changes.
- Both real Edge admin suites passed, covering 2048/1366/900/390 widths and legacy review compatibility.
- Release x64 build passed (existing unused fallback_printwindow label warning); version remains 5.1.0.0.
- Five frontend assets match the Release copies by SHA-256.
- ZIP contains 30 runtime/deploy/documentation/checksum files, forward-slash paths and LF shell scripts. No user DB, cards, password or node_modules included.
- No live server deployment or two-machine native/OCR concurrency run was performed. Those acceptance checks are documented for the isolated test environment.
