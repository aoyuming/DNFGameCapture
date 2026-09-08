# Admin Hub and Submission Reconciliation Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Preserve the existing dirty workspace and do not deploy to production.

**Goal:** Split administration into three authenticated workspaces, reconcile equivalent submitted identities before review, and move player actions immediately below the current player heading.

**Architecture:** Keep Basic authentication, CSRF protection, SQLite review transactions and revision guards. Derive reconciled views without changing stored submissions. An explicit editor save confirms any displayed inferred association and retains the first unedited payload in an audit table; publication still requires separate approval. Automatically map only unique, strongly evidenced public identities; retain all ambiguous ownership for review.

**Tech Stack:** TypeScript, Express, SQLite, existing HTML/CSS/JavaScript, Node tests and Playwright/Edge.

## Approved Design

- `/admin` becomes the authenticated home with exactly three management entries: licenses, broadcasters, shared library. Each workspace has navigation back home.
- A submitted entity can retain a valid public ID. Unknown IDs can map only to one public entity using strong unique evidence: exact nonempty identifier sets, at least three shared game IDs, or at least two shared adventure IDs. Names alone never authorize identity merging. Evidence owned by another public entity prevents automatic mapping.
- Several local aliases mapping to the same public entity form one additive review row. No incoming names or identifiers are discarded. Unchanged entries are distinguished from additions and unresolved conflicts.
- Existing pending submissions receive the same derived analysis. Batch approvals re-evaluate against the current library in one transaction; no silent publication or public-entity merging.
- Saving a draft with inferred associations requires explicit confirmation; accepted associations become administrator-selected IDs. Preserve the original payload transactionally in `player_library_submission_originals`. Failed or cancelled edits do not change either draft or audit data.
- A compact submission heading displays submission number, time, entity count, additions and genuine conflicts, not every submitted name.
- Client identity actions (add alias, edit game IDs, edit adventure IDs and applicable group actions) appear between the current-player heading and associated names. Keep all existing handlers and runtime disabled states.
- Additional approved interaction: game-ID chips retain their style, reveal edit and delete controls on hover/focus, and end with an add-game-ID control. Deletion requires confirmation; saves still update the whole identity group through existing commands.
- Only the test-server release archive and local Release frontend assets are updated. No production deployment, config or data changes.

## Tasks

### 1. Reconciliation (Primary Worker)
Files: `cloud-match-server/src/library-submission-reconcile.ts`, `library-admin-data.ts`, `tests/library-submission-reconcile.test.ts`, `tests/library-admin.test.ts`.
- [x] Add failing tests for public/local ID mismatch, split alias rows, exact equality, thresholds, name-only matches, multiple public owners, known public IDs, immutable input and repeated analysis.
- [x] Implement indexed, deterministic, additive reconciliation and summaries.
- [x] Integrate derived views into pending reads and transactional approvals. Preserve raw payload hash guards and reject real conflicts.
- [x] Verify old pending submissions, batch review, idempotency and stale revision handling with focused tests; align submit acknowledgement conflict counts too.

### 2. Independent Admin Workspaces (Admin Worker)
Files: `cloud-match-server/src/admin.ts`, `admin-page.ts`, new focused admin page modules, `library-admin-page.ts`, corresponding admin/browser tests.
- [x] Add failing authenticated route and page isolation checks.
- [x] Implement home, licenses and broadcaster pages reusing existing endpoints; retain protected shared-library page.
- [x] Render reconciliation summary, additions and compact pending headings when present.
- [x] Check login, CSRF, operations, back navigation and desktop/mobile layouts, including clipboard fallbacks and confirmed association saves.

### 3. Player Action Toolbar (Frontend Worker)
Files: `web前端/main.js`, `style.css`, relevant frontend tests.
- [x] Locate current group and standalone action rendering and write order/layout regression checks.
- [x] Place actions after selected-player heading and before associated names for both kinds of player.
- [x] Verify button handlers, responsive wrapping and editable modes without changing underlying identity logic; 31 frontend tests and Edge checks include chip edit/add/confirmed deletion and touch/focus behavior.

### 4. Integration and Delivery (Primary Worker)
- [x] Review each worker's spec compliance and code quality, resolve findings (incremental alias unions, confirmed mapping saves with original audit, HTTP copy fallback).
- [x] Run server tests, TypeScript build, frontend syntax and bridge/static checks, browser smoke and screenshots, `git diff --check`.
- [x] Sync frontend into `C:/Users/BRO/source/repos/DNFGameCapture/x64/Release/web前端` and compare SHA-256 (five assets match).
- [x] Build a distinctly named test-server package, verify archive contents and document deployment steps. Preserve existing database and test service configuration.

## Verification Record

- Server: 154 tests passed; TypeScript build passed.
- Frontend: 31 player-library tests and 6 match bridge checks passed; main.js/kill.js syntax checks passed.
- Native: player-library core compilation, database/mutation regressions and dependency checks passed. This task did not change the native EXE.
- Edge: player toolbar/chips, admin hub, shared-library CRUD and 82-row reconciliation review checks passed across desktop/narrow/touch fixtures. Screenshots inspected.
- Five source/Release asset SHA-256 pairs matched; git diff whitespace check passed.
- Package: `deployment-packages/dnf-cloud-match-server-test-admin-hub-20260907.zip`; SHA-256 `1bac14c339a1a2dcb1bdf372406a83dc9ab226df325ce421b24502fc79b5cf21`.
- No live deployment, production edits or Git commit. Native WebView interaction and deployed HTTP clipboard policy remain manual checks; browser fixtures are isolated from user data.

## Follow-Up: Default Password and Direct Tag Editing

Subsequent user requests supersede the two bulk edit buttons in the toolbar.

- [x] Set the test install template and empty-password fallback to the user-specified default. Preserve nonempty existing passwords, leave production installer/configuration unchanged, and test the real isolated shell selection block plus strict package guard (four tests passed).
- [x] Remove the toolbar's game-ID and adventure-ID edit buttons. Provide direct tag editing, confirmed deletion, and trailing add for both identifier types.
- [x] Provide rename/delete controls for associated names; reuse native `cmd_identity_rename_name` and existing alias deletion/unlink semantics, with explicit confirmation text.
- [x] Use native `cmd_identity_rename_id` for game-ID rename so per-ID statistics survive, and update focused/selected name only after a successful committed rename. Reject collisions with other game IDs and names before dispatch.
- [x] Re-run frontend/browser checks, sync Release assets and regenerate the test-server archive with the new default configuration.

### Follow-Up Verification

- Combined player-library frontend, match bridge and test-password suite: 56 tests passed after consolidating six rename scenarios into two regression tests. Frontend syntax and native integration checks passed. Confirmed rename completion also tolerates newer canonical snapshots reintroducing the old name or removing the replacement without leaving controls busy.
- Edge desktop, narrow and touch fixtures passed for name/game/adventure tags, confirmations, duplicate-name rejection, disabled state and committed rename selection. Screenshots inspected; native WebView remains a manual check.
- Five source/Release asset SHA-256 pairs matched. Frontend cache stamp: `20260907-5.1.0-identity-tag-actions`.
- Rebuilt test archive SHA-256: `cfe894a5ffb611a77fd086b9643f2f94b526f397c49ad57c9c31a3ee37b61759`; archive test environment checked for port 28880 and the requested default. This supersedes the earlier package hash above.
- No EXE rebuild was needed for the tag controls; native rename and deletion commands already exist. No live deployment or production configuration changes.
