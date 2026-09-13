# Server-Side Broadcaster Attribution Design

**Date:** 2026-09-13
**Release:** server package 5.2.3; desktop client remains 5.2.2

## Goal

Associate player-library submissions and activated licenses with broadcaster identities without changing the installed desktop client. The admin pages must show submission source, license user, broadcaster IP/region, and the broadcaster's full recoverable license key.

## Constraints

- The authorization machine ID and Socket.IO broadcaster device ID are intentionally different identities.
- Existing clients do not transmit an explicit relationship between those identities.
- IP equality is supporting evidence, not proof. Shared NAT addresses must never cause an ambiguous automatic link.
- Existing clients and database files must continue to work without manual migration commands.
- Plaintext license keys must not be added to list/state APIs or logs.

## Attribution Model

Add three backward-compatible SQLite structures during normal startup:

1. `license_ip_observations` records the activated license, authorization machine ID, normalized public IP, and latest authenticated HTTP activity time.
2. `broadcaster_network_observations` records each broadcaster device's latest normalized IP, resolved region, and observation time.
3. `broadcaster_license_links` records one current license attribution per broadcaster device, including the authorization machine ID, last known broadcaster name, source (`automatic` or `manual`), and timestamps.

The automatic linker runs after authenticated v2 HTTP activity and broadcaster socket activity. It creates or refreshes a link only when all of these are true:

- the address is a public IP;
- the license activity is recent;
- exactly one authorization identity and exactly one active broadcaster are candidates for that address;
- the license is still bound to that authorization identity; and
- no manual attribution would be replaced.

Ambiguous, private, loopback, malformed, or stale observations remain unlinked. A manual admin choice always wins and is never overwritten by automatic matching.

## Manual Attribution

The broadcaster detail view includes a selector containing activated licenses. An administrator can bind or change the selected broadcaster's license. The write endpoint validates the broadcaster, license, current license binding, and CSRF token before storing a `manual` link.

This provides a deterministic fallback for shared networks, five-day offline authorization leases, and historical broadcasters that cannot be matched automatically.

## Admin Views

### Player Library

Pending submissions are enriched through their authorization machine ID and the current verified link. The list and detail show the broadcaster name first and retain the machine ID as secondary diagnostic text. Search includes broadcaster name. Unlinked submissions explicitly show `未关联主播` rather than guessing.

### License Management

Each license row includes its linked broadcaster name or names alongside the bound machine ID. Search includes broadcaster names. The license list API remains plaintext-free.

### Broadcaster Management

Each broadcaster state item includes:

- online current IP, when connected;
- persisted last IP and observation time;
- locally resolved country/region/city text;
- linked license ID, label, authorization machine ID, and whether encrypted key material is available; and
- attribution source (`automatic`, `manual`, or unlinked).

Selecting a broadcaster automatically calls the existing protected reveal endpoint for that one linked license and renders the full key. Hash-only legacy records show `原密钥不可用`. The aggregate state endpoint never includes plaintext keys.

## IP Region Resolution

Use a packaged local GeoIP database/library. Normalize IPv4-mapped IPv6 before lookup. Public addresses produce the most specific available country/region/city label; private and loopback addresses show `内网`; unresolved public addresses show `未知地区`. No external geolocation request occurs during admin refresh.

## Data Flow

1. License activation, validation, or another authenticated v2 request records license-side IP activity.
2. Socket connection records broadcaster-side IP activity and updates the in-memory current-IP map.
3. The attribution service attempts only an unambiguous automatic match.
4. Admin state builders join submissions, licenses, and broadcasters through the stored attribution.
5. A manual admin bind replaces an automatic link and becomes authoritative.

## Failure Handling

- GeoIP lookup failure returns `未知地区` and does not block authentication or socket traffic.
- Attribution persistence failure must not reject license activation, library synchronization, or broadcaster connectivity; it is an admin-observability feature.
- Invalid manual bind requests fail atomically with a stable error code.
- License reveal keeps the current `key_unavailable` and `vault_unavailable` behavior.
- Cleanup of broadcaster lobby data does not delete license records. Network observations and stale automatic links may be pruned independently.

## Verification

- Database migration tests cover existing schemas and repeated startup.
- Attribution unit tests cover unique matching, ambiguity, private IPs, freshness, manual precedence, and changed license bindings.
- HTTP/socket integration tests prove existing clients remain compatible and server-observed IPs populate attribution.
- Admin API tests prove all three views contain the expected names and metadata while list/state JSON contains no plaintext key.
- Admin page tests cover search, manual binding, direct selected-key display, unlinked/legacy states, and mobile/desktop overflow.
- Run the complete server test suite, TypeScript build/typecheck, browser checks, package validation, and production preflight tests before publishing the 5.2.3 server ZIP.

## Out of Scope

- Desktop client changes.
- Guessing among multiple users behind one public IP.
- Exposing license plaintext in bulk APIs, logs, exports, or HTML source.
- Retrofactively identifying a historical submission when neither a verified link nor a manual association exists.
