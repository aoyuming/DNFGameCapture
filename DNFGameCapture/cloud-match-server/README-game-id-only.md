# Game-ID-Only Player Library

This release removes adventure-group identifiers from the server. Player names,
aliases, game IDs, scores, licenses, sessions and broadcaster settings remain.
The previous adventure-identifier and mixed-threshold release notes are superseded
by this document. No deployment or packaging is performed by the source changes.

## API Changes

- Library entities contain only `entityId`, `names` and `gameIds`. Library revisions
  and `entityRedirects` keep their existing meanings.
- Submit, import, CRUD and review accept names with an empty `gameIds` array.
  Unknown entity properties, including old `adventureGroupIds` values, are
  stripped, even when those retired values have an invalid shape.
- Resolve accepts `gameIds` and optional `activeEntityIds` (at most eight distinct
  players). `matchedBy` is only `game` or `none`. Shared IDs remain ambiguous when
  multiple active players match; retired evidence cannot narrow the candidates.
- Conflicts, shared-identifier reports, addition counts and admin statistics expose
  game IDs only. Player-level unknown snapshot fields are stripped; all existing
  score, name, alias and snapshot-envelope validation remains.
- Automatic publication merges require at least five distinct normalized shared
  game IDs. One to four shared IDs, identical weak sets and matching names alone
  do not merge. Existing pairwise/original-evidence safeguards remain in place.
- Manual merges, durable redirects, independent shared game IDs, partial review,
  CSRF/authentication and optimistic revision guards remain supported.

## Transactional Migration

Startup runs `2026-09-08-remove-adventure-identifiers` once, recorded in
`server_schema_migrations`, inside schema initialization's IMMEDIATE transaction.

- Supports the old exclusive-owner table, interim shared-ID table and current
  spelling/link tables. Rebuilds identifiers and their dependent tables with a
  game-only CHECK constraint, retaining game identifier numbers, spellings and links.
- Removes retired fields from submissions, original submission copies, automatic
  merge evidence, merge audits and stored match players. Pending submissions retain
  their IDs, status, names, game IDs and review metadata and remain reviewable.
- Preserves player rows and names even when all their retired IDs are removed.
  Licenses/keys, sessions, devices, memberships, broadcaster policies, redirects,
  scores and match metadata are not reset. Existing merged players are not split
  speculatively; retained original evidence cannot be amplified into new proof.
- Recomputes changed snapshot content hashes so retired evidence alone cannot
  create a new match update. Library cleanup advances the library revision once;
  reopening does not repeat cleanup or advance it again. Reload open admin pages
  because old submission hashes/public revision guards may become stale.
- Invalid historical JSON, invalid changed snapshots, broken foreign keys or a
  failed write abort startup and roll back schema, data and the migration marker.
  Do not delete pending records to bypass a migration error.

## Deployment And Rollback

Stop the target service and back up its complete data directory, program and
configuration before installing this release. A live SQLite main-file-only copy
is insufficient when WAL files exist. Use the existing environment-specific
installer only after the backup succeeds, then verify service health and logs.
Update participating clients to the game-ID-only release and refresh admin pages.

The cleanup is destructive only for retired evidence and has no downgrade
migration. Never run an older server against the migrated database. To roll back,
stop the service and restore the matching pre-upgrade program, configuration and
complete database backup together. No live database was modified during validation.

## Verification

`npm run build`, `npm run typecheck:test` and `npm test -- --run` pass:
20 test files, 269 tests, including seven dedicated removal/migration regressions.

The three isolated Edge checks also pass: `check-cloud-library-admin.cjs`,
`check-cloud-library-review.cjs` and `check-cloud-library-partial-review.cjs` in
`../scripts`. They exercise SQLite CRUD, imports, review/partial review, redirects,
weak/strong shared IDs, revision races, escaping and responsive layouts.
