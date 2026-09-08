# Partial Library Review and Identity Redirects Implementation Plan

> Use subagent-driven-development and test-driven-development. Work in the existing feature branch, preserve all current edits and local user databases. No production deployment or automatic public-data edits.

**Goal:** Accept conflict-free submitted players, allow administrators to explicitly merge public identities, and keep client pulls responsive to ownership conflicts.

**Architecture:** Retain additive updates, transactional SQLite publication and revision/hash guards. Add durable old-to-current public entity redirects. Partial approval skips entire conflicting entities and leaves their original content pending; explicit public merge unions names and both identifier kinds without requiring overlap. Client public pulls opt into ownership-only skipping and resolve redirects before comparing cloud ownership. Invalid payloads and failed persistence still abort atomically.

**Tech Stack:** Existing TypeScript/Express/SQLite admin, C++ SQLite worker, WebView bridge and Edge fixture tests.

## Contracts

Public library response adds `entityRedirects: [{fromEntityId, toEntityId}]`. These are full, acyclic mappings to existing canonical public entities; source IDs must never be recreated. Admin-confirmed merges advance public revision, redirect earlier sources to the surviving ID and preserve an audit of the merge.

Approval requests explicitly select `skipConflicts: true`; older strict callers retain atomic rejection. Analyze the combined projection without request-order bias, exclude all incoming entities involved in ownership collisions, then publish the remaining projection in one transaction. Preserve original submission payloads and leave skipped rows pending with a partial-review reason. Return accepted/skipped counts and conflict details, including all-conflict/no-change cases.

Client `ImportV2(payload, ImportReport*)` retains strict defaults. Public pull bridge sets `skipOwnershipConflicts: true` internally, not from local match-sync data. Report fields include accepted entity count and skipped entities with cloud ID, names and reason. Never treat malformed JSON, invalid mappings, stale revisions or storage failures as skippable conflicts. Successful partial pulls log counts and skipped names in Chinese; no blocking English error for expected ownership skips.

## Task 1: Server and Admin

Files: `cloud-match-server/src/db.ts`, `library-store.ts`, `library-submission-reconcile.ts`, `library-admin-data.ts`, `library-admin.ts`, `library-admin-page.ts`, `v2-api.ts`; focused tests and admin browser checks.

- [x] Add failing regressions for mixed clean/conflicted submissions, conflicting rows across a batch, all-conflict handling, repeated partial approval, raw audit preservation, explicit disjoint-ID merge, redirect chains and stale guards.
- [x] Add redirect schema/read/resolve helpers. Reject cycles, missing targets and retired-ID reuse; submission analysis and publication resolve old IDs without silently merging distinct current public entities.
- [x] Implement partial approval under existing revision/hash guards. Keep conflict rows pending and save original payload before rewriting the remaining pending rows.
- [x] Add confirmed admin merge endpoint and controls. Show source names/IDs and resulting union, retain one target ID and audit redirects in the same transaction. Expose conflict counts and partial success messages. Independent conflict components receive separate merge actions.
- [x] Run server regressions, TypeScript build and Edge admin interactions.

## Task 2: Native Import and Reporting

Files: `PlayerLibraryDatabase.h/.cpp`, `PlayerLibraryStore.h/.cpp`, `DNFGameCaptureDlg.cpp`, `scripts/player_library_core_test.cpp`, focused bridge/static tests.

- [x] Reproduce one conflicting entity blocking an unrelated valid entity; verify strict import stays unchanged and public-pull partial mode accepts only the safe entity.
- [x] Validate full redirects and resolve live/history cloud ownership through canonical IDs, including multiple local constituents of one administratively merged public entity. Preserve local merge/split metadata and match stats.
- [x] Preflight all ownership assignments before modifying IDs. Remove entire conflicting incoming entities without order-dependent winners. Persist accepted data and revision watermark atomically; allow repeat of the same remote revision after local conflict resolution.
- [x] Return import report through worker result. Public pull opt-in is host-controlled. Display Chinese partial results and individual skipped names in logs (first 20 entities, with truncation notice), retain useful error detail for genuine failures. Partial pulls do not reset the manual baseline or claim a complete seven-day checkpoint.
- [x] Test old/current ID remapping, chains, local unions, repeated imports, invalid/cyclic mapping, partial no-op, restart and failed-commit rollback. A wholly rejected pull cannot trigger unrelated automatic unions.

## Task 3: Verify and Deliver

- [x] Review scoped diff for spec compliance and data-safety risks; resolve findings. Read-only native/server review concluded with no remaining concrete findings; primary verified UI and artifacts separately.
- [x] Run `npm test -- --run`, `npm run build`, native core and bridge/static checks, frontend syntax and `git diff --check`.
- [x] Build Release x64, sync required assets to `C:/Users/BRO/source/repos/DNFGameCapture/x64/Release`, compare SHA-256.
- [x] Create a distinctly named isolated test-server archive and checksum, document server-first deployment and pending-data reanalysis. No live deployment, no commit unless requested.

## Verification Record

- Server: 167 tests passed; TypeScript runtime and test typechecks passed. Both the two-pair partial review Edge workflow and existing 82-row review workflow passed at desktop/narrow/mobile widths.
- Native: full core compilation/tests passed, including original strict conflicts, partial no-ops, mapping chains/history/split/reopen, SQL write and deferred COMMIT rollback, worker publication and indexed lookups. Seven bridge checks passed; frontend regression suite passed (46 tests).
- Release x64 build passed with the pre-existing unused `fallback_printwindow` label warning. EXE and PDB copied after client exit, all five frontend assets SHA-256 matched. EXE SHA-256: `1876780f7f70c9549a0a6ece37629b1d033775a36db38eec83a8f36a716d54e6`.
- Test archive: `deployment-packages/dnf-cloud-match-server-test-partial-review-20260907.zip` (111772 bytes), SHA-256 `43b84470d47c03b8b17b4c68006ef15c24eadf9f4588520d369159eca4824ccb`. Package entries checked against source hashes; default-password guard passed four tests.
- No real library data, live service, production configuration, cloud function, OSS or Git history modified. Native WebView against the deployed service remains a manual integration check.
