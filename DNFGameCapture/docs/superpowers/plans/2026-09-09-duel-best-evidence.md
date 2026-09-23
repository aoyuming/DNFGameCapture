# Duel best identity evidence

Approved scope: keep reliable adventure/game/player evidence for a duel lasting
up to four minutes, with a 270-second hard bound. Freeze before scoring. After a
committed kill clear the defeated side and preserve the winner. Full round,
roster, capture and monitoring resets invalidate both sides.

- [x] Policy: rank validated samples, preserve the best text, require distinct
  supporting samples, stage ownership switches, expose retention and rejection.
- [x] Lifecycle: block defeated slots until a full reset, invalidate in-flight
  scans at kill commit, reset only the defeated HUD memory.
- [x] Diagnostics: show current selection and best sample separately from raw
  OCR; include both line confidences and reasons for keeping/discarding evidence.
- [x] Verify policy/scanner/Web regressions, build Release x64, back up and deploy
  EXE/PDB and changed Web assets to the parent x64/Release directory.

Storage is bounded in-memory evidence, never library writes or cloud payloads.
Short-lived HUD memory remains short-lived; retaining an identity association
does not establish who is on screen without current score-task HUD evidence.

Review refinements:
- A long-lived selection requires a combined evidence score of at least 90 and
  independent supporting OCR. Two weak samples remain tentative.
- The frozen scoring snapshot retains the original evidence expiry; winning
  never renews that expiry by itself.
- A killer-only committed event still resets the physically defeated side using
  the detected X side, even when the defeated player's name is unknown.
- Recent raw diagnostics retain their existing 20-sample cap. The best sample's
  text, confidence and source frame are retained separately for the full bound.

Verification: policy and identity OCR suites passed; scanner 19/19, Web unit
6/6, diagnostics browser checks/screenshots at 1140/900/390 widths passed.
Release x64 built successfully (existing unused fallback_printwindow label
warning remains). Focused independent review findings were fixed and rechecked.
Live-game recognition accuracy remains to be assessed with actual gameplay.

Deployment: user-authorized termination of the Release client, EXE/PDB plus
index/style/scene-diagnostics files copied and SHA256 matched. Main, kill and
autocomplete scripts already matched the source and were left untouched.
Backup: C:/Users/BRO/source/repos/DNFGameCapture/build/before-duel-evidence-20260909-021543
EXE SHA256: 411FE824824F937D5006B48EA74BFCC7A95C836811E445BE2974EFE4AFC9517E
