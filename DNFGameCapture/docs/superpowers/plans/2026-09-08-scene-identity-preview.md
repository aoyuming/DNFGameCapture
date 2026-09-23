# Scene Identity Preview and Local Library Reset

> For agentic workers: use subagent-driven-development for the independent UI/store tasks and test-driven-development for the OCR policy. Preserve the existing dirty worktree; do not commit or deploy server changes.

**Goal:** Show live adventure/game text evidence in the professional preview, move X magnifiers into a right-side diagnostic strip, and add a confirmed local-library reset command.

**Architecture:** Capture-owned bitmaps are cloned before background work. A bounded scene OCR worker owns its request and publishes immutable timestamped results. Game IDs are authoritative; roster-scoped adventure matching is a conservative fallback with geometry, runner-up margin and distinct-frame confirmation. Scene evidence never changes library ownership. Local reset is serialized by the existing SQLite worker, backs up data, and prevents legacy migration resurrection.

**Tech Stack:** MFC/GDI, WinHTTP, Umi-OCR, C++17/SQLite, plain WebView2 JavaScript, native unit tests and Playwright.

## Task 1: Professional Preview Layout
- [x] Test aspect-preserving left-aligned game rectangle and diagnostic sidebar at narrow/normal/large sizes.
- [x] Move both X magnifier panels to the sidebar without covering gameplay; keep calibration coordinates relative to the game rectangle.
- [x] Verify OnPaint uses one cloned source frame for game and magnifiers and releases the capture lock before drawing (code inspection).

## Task 2: Local Library Reset
- [x] Test backup/reset/reopen, pending request gating, write failure rollback and legacy-file non-resurrection on temporary databases.
- [x] Implement reset through the database/store queue, preserving config/license/current match and cloud data.
- [x] Add a guarded Web More/Data command with confirmation, busy state and explicit result; cancel and failure must not alter data.
- [x] Cancel/gate manual and automatic library imports and stale results during reset; suppress automatic reimport of preserved match-only IDs even after a public-library pull.

## Task 3: Scene Evidence
- [x] Test edit-distance ranking by distinct player, strict short names, tie rejection, game priority and conflict rejection.
- [x] Test box pairing, unrelated text rejection, two different-frame confirmations, guard/generation invalidation and frozen-overlay expiry. Review verified roster/capture/eligibility reset hooks. Competing corpse HUD links reject even when their adventure pairing is ambiguous.
- [x] Locate green text bands cheaply in the game scene, crop original color pixels with padding, and parse structured OCR boxes/text/confidence.
- [x] Run at most one bounded background request; throttle to roughly 1 scan/second; yield new requests to kill OCR and drop stale results.
- [x] Render recognized adventure/game rectangles, original text, candidate/status/score and sample age on the game preview only. Do not infer death from scene text or movement.
- [x] Feed only confirmed, current-roster, HUD-linked evidence into the last identity fallback; preserve game-first behavior and alias-statistics boundaries.

## Verification and Release
- [x] Run new native tests, player-library/identity regression suites, Web syntax and Playwright reset UI tests: scanner 7/7, policy/layout, core reset 16 regressions, existing identity OCR/group, Web 61/61 and bridge 8/8, reset UI at 3 viewports, existing fixed-footer UI.
- [x] Analyze supplied local recording with real local Umi-OCR: two sampled frames completed in 797-1297 ms during crop tuning. Mixed Chinese/Latin/symbol names sometimes have low confidence or omitted characters; they remain unconfirmed. Later batch probes failed because the user closed both client and OCR; these are unavailable-service results, not recognition measurements.
- [x] Run git diff --check and Release x64 build; no automatic live client termination. Existing unrelated C4102 warning remains.
- [x] Synchronize EXE/PDB and index/style/main/autocomplete-worker/kill assets to parent-root x64/Release; SHA-256 matches. Reset was exercised only on temporary fixtures.

## Release Record

- Existing release backup: `C:/Users/BRO/source/repos/DNFGameCapture/build/before-scene-preview-20260908-101727`.
- Release EXE SHA-256: `129B5CB49E44E09C05E7E710E24052A2A458FA0BC87D2FD92CBFABF078953C7F`.
- The user closed the client before replacement; no process was forcibly stopped.
- Native interactive visual sign-off remains manual. Layout geometry and paint ownership were verified by tests/code review; Web confirmation layouts were screenshot-tested.
- No cloud-function/server deployment, Git commit, license/config changes, or real local-library reset was performed.
