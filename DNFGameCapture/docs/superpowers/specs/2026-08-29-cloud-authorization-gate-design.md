# Cloud Authorization Gate Design

## Goal

Cloud broadcaster connectivity is available only after the current process receives a successful license response containing a valid `cloudServerUrl`. The URL is process memory only and is never restored from or written to `config.ini`.

## Runtime Flow

1. Startup loads the saved broadcaster identity but clears and migrates away any legacy `[CloudMatch] ServerUrl` value.
2. No cloud connection starts before license validation finishes.
3. A valid license response validates `cloudServerUrl`, stores it in memory, and restores the saved broadcaster session.
4. A missing or invalid URL leaves local licensing intact but keeps cloud features locked and disconnected.
5. A failed or expired license stops the cloud client, clears all connection-level state and the in-memory URL, and blocks cloud Web commands.
6. Device ID, device token, broadcaster name, and snapshot revision remain persisted so a later successful authorization can restore the same identity.

## Compatibility And Security

- The executable contains no fallback cloud-match server address.
- Existing `ServerUrl` keys are deleted during settings migration.
- The authorization cloud function remains the only source of the match server address.
- Trial-only local authorization does not enable cloud access because it has no current authorization response URL.
- This is a client-side product gate; the match server protocol itself remains unchanged.

## Verification

- Static checks reject fallback URLs, ServerUrl reads/writes, pre-auth startup connections, and ungated cloud commands.
- A successful authorization path must apply the returned URL and start the saved session.
- Failure paths must stop the client and clear the in-memory endpoint.
- Release version and Windows resources must report 5.0.0.
