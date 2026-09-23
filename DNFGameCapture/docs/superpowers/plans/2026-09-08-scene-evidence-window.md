# Scene OCR Evidence Window

Approved scope: tolerate intermittent bad OCR without widening fuzzy matching,
and preserve trigger-time adventure evidence for the current scoring task only.

- [x] Add failing regressions for interleaved good/noisy frames, expiry, switches,
  conflicts and trigger snapshots.
- [x] Keep two independent agreeing samples within five seconds. Noise never
  renews evidence. Unique ownership conflicts invalidate immediately.
- [x] Freeze confirmed evidence at the X trigger; validate against task HUD
  samples and scanner generation. Stop, roster/library changes and round resets
  invalidate frozen evidence. Game-ID decisions remain authoritative.
- [x] Run policy/scanner and affected integration regressions, review changes,
  build Release x64 and deploy only when the client is not running.

No cloud, protocol, database, fuzzy-threshold or scan-frequency changes.

Review follow-ups implemented:
- Newer repeated task HUD disagreement blocks an older matching sample;
  profession-only samples do not vote for a game-ID switch.
- Keep identity-contributing image fingerprints across empty/failed samples.
- Revalidate frozen evidence and roster under locks when scores are mutated;
  release the scanner lock before logs, UI or reset notifications.

Conservative boundary: if a mixed game-ID/scene decision loses its frozen
evidence at final commit (including the 15-second task limit), reject the whole
score event. Do not silently demote one participant after duplicate-event rules
have already been evaluated. This can miss a score at the deadline; manual
scoring remains available.

Verification: policy tests, 17 scanner tests, player-identity OCR, OCR health and
supervisor checks, native integration checks, 81 Web tests and Release x64 build
passed. Read-only review completed. Deployed EXE/PDB to `../x64/Release` after
checking that the client was stopped; copied-file SHA256 values matched staging.
Frontend files were unchanged and their Release copies matched the source.
Backup: `../build/before-scene-evidence-window-20260908-234802`.
Live-game extended verification was not performed.
