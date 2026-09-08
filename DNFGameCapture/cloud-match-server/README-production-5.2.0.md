# Production Server For 5.2.0

**Mandatory server-first rollout: upgrade and verify the production server before distributing the 5.2.0 client (update_v520.zip).** The production library endpoint was observed returning HTTP 404 before this upgrade. A valid manifest does not mean the server supports protocol v2.

## Production Contract

- Manifest: https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json
- Expected manifest: environment `production`, cloudServerUrl `http://47.109.149.111:18880`, protocolVersion `2`.
- Server package: `dnf-cloud-match-server-production-5.2.0.zip`.
- Public port `18880`; administrator port `18881`.
- Service `dnf-cloud-match.service`; user `dnfcloud`.
- App `/opt/dnf-cloud-match-server`; data `/var/lib/dnf-cloud-match`; environment `/etc/default/dnf-cloud-match`.
- Default database `/var/lib/dnf-cloud-match/cloud-match.sqlite`. A configured regular database inside that production directory is preserved.
- Test service, test ports 28880/28881 and test data directories are not deployment targets.

## Before Downtime

1. Use an authorized maintenance window. Stop client traffic and all other writers to the production database until validation completes. Automatic rollback restores the pre-upgrade snapshot, so writes accepted during a failed rollout would otherwise be lost.
2. Inspect the effective systemd unit and environment locally. Confirm the existing process actually uses the production database above. Custom drop-ins, external databases, symlinked data/vault files or nonstandard service layouts require a separately reviewed migration; do not relocate or create an empty database just to satisfy the installer.
3. Keep an independent protected backup. Reserve space for the complete application with dependencies, complete data directory and new staged dependencies. The vault sidecar `<database>.license-key` must accompany the database and any WAL/SHM files; losing it makes stored card plaintext unrecoverable.
4. Have Ubuntu/Debian systemd, Bash, coreutils, util-linux/flock, curl, CA certificates, `/usr/bin/node` 20+ and `/usr/bin/npm` available. Native dependency compilation may require build-essential and Python 3. Verify npm registry access before downtime. Install prerequisites separately; the upgrade intentionally does not update Node or OS packages, preserving the old native dependency ABI for rollback.
5. Extract the production ZIP outside the active app/data directories. Verify its SHA256 sidecar, then run `sha256sum -c SHA256SUMS.txt` inside the extracted `dnf-cloud-match-server` directory. Do not use the test installer or copy a private env file into the release archive.

## Install And Verify

From the extracted server directory on the production host:

```bash
sudo bash deploy/install.sh
sudo bash deploy/preflight.sh http://127.0.0.1:18880 /etc/default/dnf-cloud-match
bash deploy/preflight.sh http://47.109.149.111:18880
```

The installer serializes upgrades, validates paths/configuration, stops the existing service and verifies it is stopped **before** backing up or replacing the application/dependencies. Its root-only backup is `/var/backups/dnf-cloud-match/upgrade-*`. It contains `app`, the complete `data` directory, `env`, `unit`, `service-state` and a `backup.complete` marker only after every copy succeeds. Existing players, names, game IDs, pending submissions, licenses, bindings, device tokens, broadcaster data and vault remain in the production database/directory.

The staged environment fixes NODE_ENV, PORT, ADMIN_PORT and PUBLIC_URL to production. It preserves the existing admin password, custom entries, database location and ADMIN_HOST. A missing/empty password is generated locally and is never printed. A missing ALLOW_LEGACY_PERMANENT_KEYS entry becomes true; explicit `ALLOW_LEGACY_PERMANENT_KEYS=false` (including an explicit empty/zero policy) is preserved. Unsupported multiline/ambiguous configuration fails before downtime instead of being reinterpreted. Fresh installations bind admin to loopback; existing bindings are retained. The installer does not change firewall rules.

The probes use only unauthenticated GET requests:

- `/health` returns 200.
- `/api/v2/player-library` without authorization **must return 401, not 404 or 200**. A 404 means the server is not upgraded or routing is wrong; it does not mean a card is invalid.
- `/api/v2/health` returns `ok:true`, `protocolVersion:2`, the exact production `cloudServerUrl` and the effective boolean `allowLegacyPermanentKeys`. Local preflight also compares this flag with the installed environment. An explicit false flag is reported with a warning, not silently overridden.
- Local admin `/admin/health` on 18881 must be healthy.

Never POST a real user's card to `/auth/activate` as a deployment probe: that may bind the card. After all checks pass, verify existing library counts and registered card/broadcaster records in the authenticated administrator UI, restore client ingress, then distribute 5.2.0. Keep the backup.

Production public transport currently uses HTTP. Restrict administration to loopback with an SSH tunnel, or a trusted network/TLS reverse proxy. Do not expose Basic Auth passwords over public HTTP. Restrict public ingress during maintenance; opening 18881 to the world is not part of installation.

## Old Permanent Cards

Existing registered licenses and their disabled/expired/bound state remain authoritative. With compatibility enabled, a valid permanent card from the original Keygen can enroll without reissue. Original permanent-card samples are covered by tests. Finite or tampered cards are not automatically enrolled.

**Old unregistered permanent cards have a first-use binding caveat:** the server does not possess the historical OSS hardware binding. The first successful server activation binds that card to the submitted device; another device is rejected. Ensure the legitimate holder performs first activation. Do not bulk-activate users' cards. Explicitly disabled compatibility must be resolved by the operator before promising enrollment to unregistered old-card holders. Registered cards continue to work with compatibility disabled.

## Data Migration And Rollback

Startup runs the versioned transactional, idempotent game-ID-only removal migration. It removes obsolete adventure identifiers from storage, old pending JSON and match snapshots while preserving player entities/names (including players with no game IDs), game identifiers, scores and unrelated records. Snapshot hashes are recalculated. See `README-game-id-only.md`. There is no live database cleanup script or wholesale replacement database in this package.

**Never copy the test database wholesale into production.** That would replace production cards, sessions, bindings, broadcasters and the license vault relationship. Only export approved public player data as reviewed `entities` containing `entityId`, `names` and `gameIds`. Use the production player-library administrator import preview, review entity IDs/grouping and conflicts against the current production revision, then explicitly approve the import. Do not import pending/rejected submissions or test auth tables. Keep names and entities even when gameIds is empty. Do not infer identity from matching names.

Installation failure before active-file replacement restarts the unchanged old service only if it was previously active. Failure after replacement first stops the new service, then restores the matching old app, complete data, environment and unit, reloads systemd and restores the prior enable/active policy. A failed stop or incomplete restore prints `MANUAL RECOVERY REQUIRED` and does not start old code against migrated data. Backups and diagnostic staging are retained on failure.

For manual recovery, maintain the traffic block, stop and verify the service is inactive, preserve the failed state separately, and select a backup with `backup.complete`. Restore the entire matching `app/data/env/unit` set (including WAL/SHM and `.license-key`), not just dist or the main SQLite file. Remove newly created target files not present in that backup, restore ownership/modes, reload systemd, restore its recorded enabled state, and only then start the old service if it was previously active. If any restore step fails, leave it stopped. Never regenerate a vault to work around a recovery error.

## Build And Validation

```powershell
# From the repository root; no production connection or deployment.
./scripts/package-production-5.2.0.ps1 -ValidateOnly
./scripts/package-production-5.2.0.ps1
```

The separate production packager compiles into clean temporary dist, uses an explicit file allowlist and credential-free environment, and emits forward-slash ZIP entries, SHA256SUMS and a ZIP SHA256 sidecar. It never ships a database, vault, node_modules, device tokens, native EXE or private environment. It refuses to overwrite an existing production archive. ValidateOnly performs compilation and payload checks without creating an archive.

Server validation: `npm run build`, `npm run typecheck:test`, `npm test -- --run`. Installer regressions execute the actual installer control flow with isolated temporary paths and simulated systemctl/curl/npm failures; Windows tests do not validate Linux ownership or a real systemd host. A real authorized Linux deployment and its public preflight remain mandatory. No remote mutations were performed while preparing this release.
