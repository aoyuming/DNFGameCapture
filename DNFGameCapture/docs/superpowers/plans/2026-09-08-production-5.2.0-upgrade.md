# 5.2.0 Production Upgrade Implementation Plan

**Goal:** Release a production client that discovers the confirmed production server, accepts existing permanent CDKs through server validation, and preserves the user's previously active player library.

**Architecture:** The release environment is selected at build time, independently of the authorization protocol. Production uses `cloud-server-prod.json` and server API v2. Endpoints and protected leases are environment-scoped. The production player database is initialized from the prior active SQLite database, or from the existing INI and identity metadata if no SQLite database exists. Source data is never deleted.

**Tech Stack:** C++17/MFC, WinHTTP, DPAPI, SQLite backup/transactions, Node.js/TypeScript, PowerShell, native and browser regression tests.

## Confirmed Environment

- Manifest: `https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json`.
- Read-only verification returned HTTP 200, environment `production`, protocol version `2`, endpoint `http://47.109.149.111:18880`.
- The live production `/api/v2/player-library` endpoint currently returns HTTP 404. The server must be upgraded before distributing the new client. No remote mutations are authorized or performed by this plan.

## Work

- [x] Add testable `CloudReleasePolicy.h/.cpp` for compiled environment, cache scoping and manifest validation; extend `LicenseLease.h/.cpp` with protected manifest scope. Old unscoped leases trigger online revalidation without deleting the stored CDK.
- [x] Update `DNFGameCaptureDlg.cpp/.h`: force server v2 for this release, select the compiled manifest, disregard old environment caches, persist prior database-source selection before changing old settings, and distinguish missing server API/network failures from invalid cards.
- [x] Update `PlayerLibraryDatabase.h/.cpp` and tests: initialize `%APPDATA%/DNFGameCapture/production/player_library.db` from the prior active root/test database using SQLite backup and verified staging. If no source database exists, retain existing INI migration. Existing destination is authoritative; failed migration must not install an empty database or overwrite the INI.
- [x] Update production deployment scripts and tests: preserve production identities, licenses, vault and configuration; back up before replacement; enable old permanent-card enrollment by default only when no explicit policy exists; set correct public endpoint and keep test service isolated.
- [x] Set native/file/product and frontend release markers to 5.2.0. Create an update ZIP containing executable/runtime/frontend files only, never developer config, local databases, authorization records or match data.
- [x] Run native policy, lease, actual-library-copy migration tests; server build/typecheck/full tests; browser regressions; Release x64 build; verify release files and package manifests.
- [x] Write exact production server-first deployment and client-upgrade instructions, including backup/rollback, HTTP administration risks and first-device binding for old permanent cards without OSS bindings.

## Preservation Rules

- Never overwrite the production server database with the entire test database.
- Existing permanent-key format/checksum and hardware identity stay unchanged; only a successful server response grants online authorization.
- New production database retains names, game IDs and local grouping metadata. Removed adventure identifiers remain removed.
- The prior active SQLite source is selected from old settings before they are migrated: test source for old server-v2 test installs, regular source otherwise. Inactive source databases remain untouched.
- The installer/update ZIP must not replace `config.ini`, `alias_db.ini`, `player_identity_groups.json`, SQLite databases, broadcaster device tokens, license files or match settings.
- Do not commit or push unrelated dirty worktree changes.
