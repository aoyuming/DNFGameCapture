# Cloud Authorization Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Require a fresh successful license response before any cloud broadcaster connection and release the result as version 5.0.0.

**Architecture:** Keep the cloud server endpoint only in `CDNFGameCaptureDlg` process memory. Centralize the authorization gate and teardown in two dialog methods, then apply the gate at startup, Web commands, registration, restore, and upload boundaries.

**Tech Stack:** C++17, MFC, WinHTTP/WebSocket cloud client, PowerShell static regression checks, MSBuild x64 Release.

---

### Task 1: Add authorization gate regression checks

**Files:**
- Create: `scripts/check-cloud-authorization-gate-feature.ps1`
- Modify: `scripts/check-cloud-match-feature.ps1`
- Modify: `scripts/check-unified-broadcaster-pool-feature.ps1`

- [ ] Add checks that reject local ServerUrl persistence and executable fallback addresses.
- [ ] Add checks for authorization-success startup and authorization-failure teardown.
- [ ] Add checks for the 5.0.0 header and Windows resource versions.
- [ ] Run the new check and confirm it fails against the current implementation.

### Task 2: Implement the runtime authorization boundary

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] Add `HasAuthorizedCloudMatchEndpoint()` and `DisableCloudMatchForAuthorization()`.
- [ ] Remove the pre-license startup connection.
- [ ] Accept only a valid URL returned by the successful authorization callback.
- [ ] Stop and clear cloud runtime state on authorization failure.
- [ ] Gate Web commands, registration, room restore, room join, and upload polling.
- [ ] Delete legacy ServerUrl configuration and stop persisting it.
- [ ] Run the authorization gate and existing cloud checks until they pass.

### Task 3: Release version 5.0.0

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCapture.rc`

- [ ] Set `CURRENT_VERSION` to `5.0.0`.
- [ ] Set `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and `ProductVersion` to 5.0.0.
- [ ] Run the version regression checks.

### Task 4: Build, package, verify, and commit

**Files:**
- Package: `C:\Users\BRO\source\repos\DNFGameCapture\x64\Release`

- [ ] Run JavaScript syntax and cloud static checks.
- [ ] Build Release x64 with MSBuild and confirm zero errors.
- [ ] Stop any occupied DNFGameCapture process and copy the EXE, WebView2Loader, and all nine frontend files.
- [ ] Verify SHA-256 equality between build outputs, source frontend files, and the final package.
- [ ] Run `git diff --check`, inspect the staged diff, and commit without pushing.
