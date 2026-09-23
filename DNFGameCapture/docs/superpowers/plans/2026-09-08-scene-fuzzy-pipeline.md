# Scene Fuzzy Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow bounded fuzzy matching at every scene identity text boundary while retaining multi-frame, team, geometry, uniqueness and trigger-time safety checks.

**Architecture:** `SceneIdentityPolicy` owns normalization, fuzzy scoring, duplicate-line consolidation and bounded HUD memory. `SceneIdentityScanner` applies those policies only to successful current-generation real scans before confirmation. Existing score matching remains first priority and scene adventure evidence remains an owner-only fallback.

**Tech Stack:** C++17, MFC/Win32, injected scanner tests, local Umi-OCR recording replay.

---

### Task 1: Reproduce the failed recognition stages

**Files:**
- Modify: `scripts/scene_identity_test.cpp`
- Modify: `scripts/scene_scanner_test.cpp`

- [x] Add tests for `宝大枪` versus `王大枪`, `ist` versus `戦1st`, and reject the severely truncated `埃打` versus `埃及打dio团`.
- [x] Add tests proving overlapping OCR crops consolidate but separate corpse locations remain distinct.
- [x] Add a scanner test proving a recently linked game name remains available while the top HUD rotates to a profession.
- [x] Run `scripts/check-scene-identity.ps1` and `scripts/check-scene-scanner.ps1`; verify the new assertions fail before implementation.

### Task 2: Implement bounded fuzzy text policy

**Files:**
- Modify: `SceneIdentityPolicy.h`
- Modify: `SceneIdentityPolicy.cpp`

- [x] Normalize common OCR digit/letter confusion consistently on both values.
- [x] Permit one-edit matching for three-character names and high-coverage substring matching for mixed short IDs.
- [x] Keep two-character fuzzy guesses and heavily truncated text rejected.
- [x] Lower scene confirmation input confidence only to 72%, while retaining two independent frames and ambiguity rejection.
- [x] Consolidate highly overlapping same-location OCR rows by quality before analysis.

### Task 3: Preserve HUD identity through profession rotation

**Files:**
- Modify: `SceneIdentityPolicy.h`
- Modify: `SceneIdentityPolicy.cpp`
- Modify: `SceneIdentityScanner.h`
- Modify: `SceneIdentityScanner.cpp`
- Modify: `DNFGameCaptureDlg.cpp`

- [x] Cache a game name only after the current top HUD uniquely links to a scene game-name row.
- [x] Reuse it for at most 12 seconds when the HUD contains a profession or temporary OCR noise; never renew it from cached use.
- [x] Clear it on scanner reset, scope/roster change, conflicting unique link or expiry.
- [x] Preserve raw HUD text in diagnostics and expose the effective linked HUD separately.

### Task 4: Verify and deploy

**Files:**
- Modify: `web前端/scene-diagnostics.js` only if a new diagnostic label is required.

- [x] Run scene policy, scanner, OCR identity and Web diagnostic tests.
- [x] Replay the supplied recording and inspect the previously rejected frames.
- [x] Build Release x64 in the isolated staging directory.
- [x] Confirm `DNFGameCapture.exe` is stopped, back up the current Release executable, then copy the verified EXE/PDB and changed Web files into `C:/Users/BRO/source/repos/DNFGameCapture/x64/Release`.
- [x] Run `git -c core.safecrlf=false diff --check` and report remaining live-game verification limits.
