# Shared Identifiers and Five/Three Identity Policy

**Goal:** Permit multiple independent players to reference a game/adventure ID. Automatically group only when distinct overlapping game IDs reach five or adventure IDs reach three. Preserve explicit manual groups.

**Architecture:** Keep names unique, store identifiers once with many-to-many links, and scope OCR ownership checks to the eight active slots. Submission reconciliation resolves durable redirects only; automatic grouping runs once on the complete publication projection. Persist original automatic-group evidence so a saved union cannot create new identity evidence on repeat import. Small identical sets are not identity evidence. Preserve revision checks, transactions, redirects and pending-review auditing.

## Tasks
- [x] Server storage: add failing shared-ID/migration tests; migrate the unique-owner schema transactionally to a dictionary and links; retain name-conflict rejection and partial approval. Persist original automatic evidence with audit/rollback/restart coverage, and use it in submission acknowledgements and import previews.
- [x] Native policy: test four/five games, two/three adventures, duplicates, small exact sets, manual groups and old automatic metadata; update policy and regression fixtures. Cover 257 distinct sets, exhausted suggestion budgets, stale deleted evidence and restart.
- [x] Submission reconciliation: resolve only durable redirects, retain unknown source rows, and defer inferred grouping to the complete publication projection. Test threshold boundaries, weak shared IDs and incompatible sources matching a broad public entity.
- [x] OCR: test shared IDs with off-field owners and ambiguous active slots; remove global-only uniqueness restrictions while preserving active-roster ambiguity guards and game-first matching.
- [x] Admin UI: shared IDs must not create conflict-merge actions; explicit identity merges retain confirmation. Update affected smoke-test fixtures and show the groups that approval will automatically merge.
- [x] Verify server unit/integration tests and TypeScript, native core/identity/OCR tests, frontend checks, admin browser smoke, git diff --check and Release x64 build.
- [x] Deliver updated EXE/frontend to the existing Release folder and package test server with migration notes. Do not deploy to production, mutate real libraries, commit or push.

## Acceptance Examples
- Xia and Chong can both reference one shared ID without approval failures or merging.
- Four distinct shared games plus two shared adventures remain independent; counts are not added together.
- Five shared games OR three shared adventures qualify for automatic grouping, even if other IDs differ.
- Repeated copies of one ID count once; below-threshold identical sets stay separate.
- A shared ID held only by one active slot is usable. Multiple active owners must never be resolved by iteration order.
- Removing one player's reference leaves the other references intact.
- A/B and B/C overlap is not enough to collapse A/B/C when A/C do not meet either threshold. Repeat import and restart must preserve that separation.
- Popular-ID buckets may limit suggestion display, but must not drop qualifying automatic groups.

## Verification (2026-09-07)
- Server: 211 tests, test typecheck and TypeScript build passed.
- Native identity, normalized database/core and OCR suites passed; bridge/Web: 55 tests passed.
- Admin partial review, compact review and player-detail browser tests passed across desktop/mobile viewports. Fixtures used isolated databases only.
- Frontend syntax and static integration/scroll checks passed. Identity checks now read UTF-8 explicitly under Windows PowerShell as well as PowerShell 7.
- Release x64 build passed; pre-existing C4102 `fallback_printwindow` warning remains.
- Release EXE/PDB and all ten frontend files verified against staging/source SHA-256. Version remains 5.1.0.0.
- EXE SHA-256: `3ad82134dede692c23ac8c25a035ec699d03b2e20b5fa7d3e9426bcb50c36e0d`.
- Test package: `deployment-packages/dnf-cloud-match-server-test-shared-identifiers-20260907.zip`.
- Package SHA-256: `70d20a86d585e9493056b173ce27c5a4a547b01ad660bb436ba4ab81922d52ca`.
- No live service deployment or real-library mutation. Old automatic groups without complete recovery evidence remain marked for review rather than guessing a destructive split.
