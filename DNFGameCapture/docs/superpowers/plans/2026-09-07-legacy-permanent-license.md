# Legacy Permanent Card Enrollment Implementation Plan

> Execute inline using the test-driven-development and verification-before-completion skills. The user approved accepting previously issued permanent cards without reading OSS, with a fresh first-use device binding. Preserve the current dirty checkout; no live deployment, commit or client changes.

**Goal:** Existing broadcasters keep their original permanent CDK when they move to the new authorization server.

**Architecture:** An opt-in server policy enrolls only unknown, signature-valid cards in the exact old Keygen.cpp permanent format (`CDK-FFFFFFFF-` + four hexadecimal nonce characters + eight signature characters). Existing license rows always take precedence. Enrollment, encrypted key storage, binding, audit and session creation share the existing immediate SQLite transaction. No OSS request or new client protocol is needed.

**Tech Stack:** TypeScript, Express, SQLite, Node crypto, Vitest/Supertest and existing admin browser checks.

## Approved Boundaries
- OSS device bindings, expiry and bans are intentionally not imported. The first device presenting an unknown valid old card becomes its new binding.
- Only permanent cards from the old generator are auto-enrolled; unknown finite, malformed, tampered or noncanonical cards are rejected.
- Stored disabled, expired or differently bound licenses cannot be replaced by enrollment. Canonical fixed-width enrollment prevents alternate leading-zero spellings from creating another row.
- Before enrolling a missing canonical card, check hashes of equivalent duration/signature spellings within the old 256-character API limit. If any spelling already exists, reject enrollment with `license_already_exists` and leave the original row intact. This also protects old hash-only rows without requiring a vault or changing ordinary exact-key login.
- Encryption failure or any later database write failure rolls back the whole activation. Ordinary existing hash-based activation retains its current vault-failure behavior.
- `ALLOW_LEGACY_PERMANENT_KEYS=true` enables enrollment. It defaults off when absent. The test installer enables it only when the setting is missing; an explicit administrator setting is preserved. Turning it off does not invalidate already enrolled cards.
- The legacy algorithm is not cryptographic proof of issuance. This is the accepted small-group compatibility policy, not a new anti-forgery scheme.

## Tasks
- [x] Add failing regressions in `cloud-match-server/tests/legacy-permanent-license.test.ts`: unknown old-format permanent activation succeeds, admin can reveal the original card and see its origin, repeats create one row, two devices cannot claim one card, invalid/finite/alternate spellings never enroll, existing restrictions win, rebind/disable still work, persistence survives reopen, and failed audit/vault writes roll back. Exercise `createCloudMatchApp` so route wiring is covered.
- [x] Extend `auth.ts` with the exact old generator format plus `isNativeLicenseKey` checksum test; gate enrollment in `license-store.ts` inside `activateStoredLicense` before the existing row checks. Add an `enroll_legacy` audit and an administrator-visible origin label, without plaintext in audit or API lists.
- [x] Thread the optional boolean through `config.ts`, `app.ts` and `v2-api.ts`, default false. Preserve the current response fields and all stored-card behavior. Add the Chinese audit label in `license-admin-page.ts`.
- [x] Add test-only environment/installer defaults, preserving explicit opt-out. Test config parsing and installer isolation. Do not edit the production installer/environment.
- [x] Run `npm test`, `npm run build`, `npm run typecheck:test`, the license admin and admin-hub browser checks, and `git -c core.safecrlf=false diff --check`.
- [x] Add `README-legacy-permanent-license.md`, extend the package script README selection and build a separately named test ZIP. Verify all ZIP entry hashes and absence of real DB/vault files. Explain that deployment is still required and no client update/OSS migration is needed for v2 clients.

## Verification Commands
```powershell
cd cloud-match-server
npm test -- --run tests/legacy-permanent-license.test.ts
npm test -- --run
npm run build
npm run typecheck:test
cd ..
$env:NODE_PATH='C:/Users/BRO/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules'
node scripts/check-license-admin.cjs
node scripts/check-cloud-admin-hub.cjs
git -c core.safecrlf=false diff --check
powershell -ExecutionPolicy Bypass -File scripts/package-test-identity-admin.ps1 -PackageLabel legacy-permanent-license
```

## Results
- Red/green verified: initial enrollment tests failed with 401; implementation passed. Added review regressions separately reproduced padded-record bypass and whitespace opt-out overwrite, then passed after fixes.
- `npm test -- --run`: 19 files, 264 tests passed, including 26 legacy compatibility tests.
- `npm run build` and `npm run typecheck:test`: passed.
- `node --test scripts/test-admin-default-password.cjs`: 6 passed; real grep covers missing/comment/space/tab/explicit false settings.
- License admin and admin-hub Edge browser checks passed for desktop/mobile. Compiled server environment flag was also checked against isolated in-memory databases.
- Bash installer syntax and `git -c core.safecrlf=false diff --check`: passed.
- Independent bounded review: both findings closed; no remaining important findings.
- Package: `deployment-packages/dnf-cloud-match-server-test-legacy-permanent-license-20260907.zip`, 37 entries, 130003 bytes. SHA256: `ca033bd08faa4b719fceb6aabd90abd5ce0cef840c9a25064a2aa18caa4c3d93`.
- ZIP entry hashes verified; compiled enrollment/alias protection and test environment/installer opt-out fixes are present. No live database, vault file, client binary or tests bundled.
- No remote deployment, production configuration changes, EXE rebuild, commit or push performed.
