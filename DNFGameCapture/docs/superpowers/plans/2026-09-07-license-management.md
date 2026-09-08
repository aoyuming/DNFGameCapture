# License Management Implementation Plan

> Use subagent-driven-development with disjoint backend and UI write scopes. Work on the user's current dirty checkout; do not commit, reset, deploy live services or modify production configuration.

**Goal:** Generate preset/batch cards, reveal stored keys, extend validity and rebind devices through the test admin page.

**Architecture:** SQLite stores hashes for validation and AES-256-GCM ciphertext for administrator recovery. A persistent random vault key sits beside the database, separate from admin passwords. New cards count down from first activation; old fixed expiries remain unchanged. Admin mutations are transactional, revision-guarded and request-ID idempotent.

**Tech Stack:** Existing TypeScript/Express/SQLite, Node crypto, server-rendered HTML and vanilla JS, Vitest/Supertest/Playwright.

## Approved Behavior
- Presets: `hour`=3600, `day`=86400, `week`=604800, `ten_days`=864000, `quarter`=7776000, `half_year`=15552000, `year`=31536000, `permanent`=null.
- Up to 200 cards per atomic batch; copy single/all and export TXT. Custom native-compatible keys remain available for one card.
- First activation persists activation and expiry once in the binding/session transaction. Rebinding never restarts validity.
- Extension uses `max(expiresAt, now) + duration`; unactivated cards add duration; permanent stays permanent. A permanent extension converts a finite card.
- Rebinding selects a known device or clears the binding for next activation; revoke old server sessions. Offline client leases are not remotely erased.
- List keys are concealed until an authenticated/CSRF-protected reveal. No plaintext keys in audit or batch operation records. Old hash-only records can be supplied again or backfilled on successful key activation.
- Preserve existing card validity, legacy API callers, library/admin hub and production deployment.

## Tasks
- [x] Tests first: preset boundaries, delayed activation, repeated activation, batch rollback/replay, extension, revision conflict, rebind/revocation, encrypted recovery and legacy migration.
- [x] Add focused `license-store.ts`, `license-vault.ts`, `license-admin.ts` and transactional schema migration in `db.ts`.
- [x] Integrate `v2-api.ts` activation and compatible exports; mount admin router. Catch encryption backfill failures without invalidating an otherwise valid old key.
- [x] Update `license-admin-page.ts` with preset select, quantity, compact table, reveal/copy/export, extension/rebind/confirm dialogs and audit history. Never accept manually entered expiry timestamps in the UI.
- [x] Run all server tests, TS build/typecheck and authenticated browser flows at desktop/mobile sizes. Verify unauthenticated/CSRF failures, escaping, retry, error and no-op paths.
- [x] Package test-only server with backup instructions including the vault key; verify ZIP hashes and no live credentials/data bundled.

## Verification Results
- `npm test`: 18 files, 238 tests passed, including 21 license lifecycle/vault regressions and 6 admin UI checks.
- `npm run build` and `npm run typecheck:test`: passed.
- `node scripts/check-license-admin.cjs`: contract and real isolated SQLite/HTTP/Edge flows passed at desktop/mobile sizes, including 200-card batches, lost-response retries and response-body timeouts. Screenshots inspected.
- `node scripts/check-cloud-admin-hub.cjs`: three management entries, clipboard fallback, confirmation dialogs and responsive navigation passed.
- `git -c core.safecrlf=false diff --check`: passed.
- Read-only backend recheck: no blocking findings; wrong vault keys cannot cause mixed ciphertext, and rebinding excludes lobby IDs.
- Test package: `deployment-packages/dnf-cloud-match-server-test-license-management-20260907.zip`, 37 entries, 128588 bytes.
- SHA256: `d7f8e99fe068398683fbe91b039f16819a72c6c52741f6164e9223e65c0c38d3`. ZIP entries checked against staged runtime files; no live database or vault key included.
- No remote deployment, production changes, client rebuild, commit or push performed for this task.

## API Contract
`GET /admin/api/licenses` returns `licenses`, `presets`, `maxBatch=200`, `devices`.
Each license: existing fields plus `revision`, `activationMode` (`first_use` or `fixed`), `activatedAt`, `durationSeconds`, `hasKey`.
Each preset: `{id,label,durationSeconds}`. Each device: `{deviceId,name}`.

`POST /admin/api/licenses` new body `{preset,count,label,key?,requestId}` returns `{ok,licenses:[{id,key,expiresAt,...}],replayed}`. Legacy `{expiresAt,key,label}` stays supported.
`POST /:id/reveal` returns `{ok,key}`; `POST /:id/key` takes `{key,revision,requestId}` to recover a hash-only record.
`POST /:id/extend` takes `{preset,revision,requestId}`.
`POST /:id/rebind` takes `{deviceId:null|string,revision,requestId}`.
`POST /:id/disable` takes `{disabled,revision?,requestId?}`; legacy remains supported.
`GET /:id/audit` returns `{ok,events:[{id,action,createdAt,details}]}` with no secrets.

Mutations return updated `license`, except batches. Errors use `{ok:false,code}` with `invalid_request`, `invalid_preset`, `invalid_license_format`, `license_already_exists`, `license_not_found`, `stale_license`, `request_id_conflict`, `key_unavailable`, `key_mismatch`, `unknown_device`, `vault_unavailable`.
