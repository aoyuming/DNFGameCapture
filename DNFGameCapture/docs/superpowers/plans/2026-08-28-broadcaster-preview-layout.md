# Broadcaster Preview Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge online and recently offline broadcasters into one readable list and expand the Web score window while a complete broadcaster preview is open.

**Architecture:** The Web frontend owns list sorting, elapsed-offline labels, truncation tooltips, and preview open/close commands. `CWebScoreDlg` owns independent appearance and preview expansion flags and computes one final client size, clamped to the current monitor work area, so closing one panel does not collapse another open panel.

**Tech Stack:** Vanilla HTML/CSS/JavaScript, MFC C++, WebView2, PowerShell static regression checks.

---

### Task 1: Add failing UI contract checks

**Files:**
- Modify: `scripts/check-unified-broadcaster-pool-feature.ps1`

- [ ] Require a single `broadcaster-list`, reject the old online/offline list IDs, and require offline sorting/elapsed-time helpers.
- [ ] Require alias hover text, preview open/close resize messages, and C++ preview expansion APIs.
- [ ] Run `powershell -ExecutionPolicy Bypass -File scripts/check-unified-broadcaster-pool-feature.ps1` and confirm it fails because the new UI contract is not implemented.

### Task 2: Merge and restyle the broadcaster list

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`

- [ ] Replace the two list sections with one QQ-style list and a combined online/total count.
- [ ] Filter expired offline entries, sort online first, and sort each presence group by its relevant newest timestamp.
- [ ] Render offline cards in gray and show elapsed offline duration such as `离线 18分钟`.
- [ ] Increase broadcaster name and metadata typography while preserving truncation for long names and relation tags.

### Task 3: Improve the preview and resize the host window

**Files:**
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Modify: `WebScoreDlg.h`
- Modify: `WebScoreDlg.cpp`
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] Increase primary-account typography and add `title` hover text to full primary and alias strings.
- [ ] Send `cmd_set_broadcaster_preview_open` when the preview opens or closes.
- [ ] Add a preview-expanded state to `CWebScoreDlg` and compute the final width/height from both appearance and preview states.
- [ ] Clamp the expanded window to the current monitor work area and keep it visible; closing preview restores the normal size or the still-open appearance size.
- [ ] Handle `cmd_set_broadcaster_preview_open` in the C++ Web bridge.

### Task 4: Verify and synchronize Release frontend

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Copy to: `..\..\..\x64\Release\web前端\index.html`
- Copy to: `..\..\..\x64\Release\web前端\main.js`
- Copy to: `..\..\..\x64\Release\web前端\style.css`

- [ ] Run the updated static check and confirm it passes.
- [ ] Run `node --check web前端\main.js` and `git diff --check`.
- [ ] Build Release x64 with the installed Visual Studio MSBuild.
- [ ] Copy the three frontend files to Release and verify matching SHA-256 hashes.
- [ ] Inspect the final diff without changing unrelated existing work.
