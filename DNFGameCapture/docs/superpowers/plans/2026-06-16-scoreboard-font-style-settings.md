# Scoreboard Font Style Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an Appearance panel that lets users edit the upper red/blue scoreboard text styles by text type.

**Architecture:** C++ persists sanitized style values in `config.ini` under `[ScoreboardTextStyles]` and broadcasts the style JSON plus installed font families to WebView. The Web frontend renders a compact Appearance modal, applies styles through CSS variables, previews changes immediately, and posts full style state back through `cmd_set_scoreboard_text_styles`.

**Tech Stack:** MFC C++17, nlohmann/json, WebView2 message bridge, plain HTML/CSS/JavaScript, PowerShell static regression checks.

---

### Task 1: Add Static Regression Check

**Files:**
- Create: `scripts/check-scoreboard-style-feature.ps1`

- [ ] **Step 1: Write failing static check**

Create a PowerShell script that reads `DNFGameCaptureDlg.cpp`, `web前端/index.html`, `web前端/main.js`, and `web前端/style.css`, then requires these markers:

```powershell
$required = @(
    @{ Text = $cpp; Needle = "DnfBuildScoreboardTextStylesJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "DnfSaveScoreboardTextStylesJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "DnfBuildInstalledFontListJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "cmd_set_scoreboard_text_styles"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '"scoreboardTextStyles"'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '"systemFonts"'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $html; Needle = "btn-appearance"; File = "web/index.html" },
    @{ Text = $html; Needle = "appearance-overlay"; File = "web/index.html" },
    @{ Text = $html; Needle = "style-font-family"; File = "web/index.html" },
    @{ Text = $js; Needle = "SCOREBOARD_TEXT_STYLE_TYPES"; File = "web/main.js" },
    @{ Text = $js; Needle = "applyScoreboardTextStyles"; File = "web/main.js" },
    @{ Text = $js; Needle = "cmd_set_scoreboard_text_styles"; File = "web/main.js" },
    @{ Text = $js; Needle = "resetCurrentScoreboardStyle"; File = "web/main.js" },
    @{ Text = $css; Needle = "--sb-team-name-font-family"; File = "web/style.css" },
    @{ Text = $css; Needle = ".appearance-overlay"; File = "web/style.css" },
    @{ Text = $css; Needle = ".team-name-label"; File = "web/style.css" }
)
```

- [ ] **Step 2: Verify RED**

Run: `powershell -ExecutionPolicy Bypass -File scripts\check-scoreboard-style-feature.ps1`

Expected: FAIL with missing markers such as `DnfBuildScoreboardTextStylesJson` and `btn-appearance`.

### Task 2: Add C++ Style Persistence and Broadcast

**Files:**
- Modify: `DNFGameCaptureDlg.cpp`

- [ ] **Step 1: Add helper code**

Add static helpers near existing file-scope helper declarations:

```cpp
static json DnfBuildScoreboardTextStylesJson(const CString& iniPath);
static void DnfSaveScoreboardTextStylesJson(const CString& iniPath, const json& styles);
static json DnfBuildInstalledFontListJson();
```

Implement defaults for `teamName`, `score`, `header`, `pickLabel`, `playerName`, and `statNumber`. Clamp font size to 10-48, stroke width to 0-4, glow to 0-24. Validate colors as `#rgb` or `#rrggbb`. Keep `colorMode=team` only for `teamName` and `score`; all other types use `custom`.

- [ ] **Step 2: Broadcast style state**

Inside `CDNFGameCaptureDlg::BroadcastStateToWeb()`, add:

```cpp
j["data"]["scoreboardTextStyles"] = DnfBuildScoreboardTextStylesJson(m_iniPath);
j["data"]["systemFonts"] = DnfBuildInstalledFontListJson();
```

- [ ] **Step 3: Save Web style command**

Inside `CDNFGameCaptureDlg::OnWebCmdReceived()`, add an `else if` branch:

```cpp
else if (action == "cmd_set_scoreboard_text_styles") {
    if (j.contains("styles") && j["styles"].is_object()) {
        DnfSaveScoreboardTextStylesJson(m_iniPath, j["styles"]);
        AppLog(L"🎨 [外观] 记分板文字样式已保存。", RGB(255, 210, 106));
        BroadcastStateToWeb();
    }
}
```

### Task 3: Add Web Appearance UI and CSS Variables

**Files:**
- Modify: `web前端/index.html`
- Modify: `web前端/style.css`

- [ ] **Step 1: Add semantic scoreboard text spans**

Wrap team names in `.team-name-label`:

```html
<div class="team-name"><span class="team-name-label">红队</span> <input type="text" class="team-score-input" value="0"></div>
```

- [ ] **Step 2: Add Appearance button and modal**

Add a `🎨 外观` button with `id="btn-appearance"` near `专业模式`. Add an `#appearance-overlay` modal containing:

```html
<div class="appearance-style-list" id="appearance-style-list"></div>
<select id="style-font-family"></select>
<input id="style-font-size" type="range" min="10" max="48">
<select id="style-color-mode"></select>
<input id="style-color" type="color">
<input id="style-stroke-color" type="color">
<input id="style-stroke-width" type="range" min="0" max="4">
<input id="style-glow" type="range" min="0" max="24">
<button id="btn-style-reset-current">恢复当前默认</button>
<button id="btn-style-reset-all">恢复全部默认</button>
```

- [ ] **Step 3: Add scoreboard CSS variables and modal styles**

Define `--sb-team-name-*`, `--sb-score-*`, `--sb-header-*`, `--sb-pick-label-*`, `--sb-player-name-*`, and `--sb-stat-number-*`. Apply them to `.team-name-label`, `.team-score-input`, `.stats-labels span`, `.seat-number`, `.name-input`, and `.stat-item input`. Add compact dark modal styles for `.appearance-overlay` and `.appearance-panel`.

### Task 4: Add Web State Logic

**Files:**
- Modify: `web前端/main.js`

- [ ] **Step 1: Add defaults and normalization**

Add `SCOREBOARD_TEXT_STYLE_TYPES`, `scoreboardTextStyles`, `systemFonts`, and normalization helpers for font names, colors, number ranges, `colorMode`, and default state.

- [ ] **Step 2: Apply styles from C++**

In `applyStateFromServer(state)`, update `systemFonts`, normalize `state.scoreboardTextStyles`, call `applyScoreboardTextStyles(scoreboardTextStyles)`, and refresh the Appearance editor if open.

- [ ] **Step 3: Wire modal controls**

Implement `openAppearancePanel()`, `closeAppearancePanel()`, `renderAppearanceStyleList()`, `syncAppearanceEditorFromActiveStyle()`, `updateActiveStyleFromEditor()`, `resetCurrentScoreboardStyle()`, `resetAllScoreboardStyles()`, and `queueScoreboardTextStyleSync()`.

- [ ] **Step 4: Bind events**

Bind `#btn-appearance`, `#btn-appearance-close`, `#btn-style-reset-current`, `#btn-style-reset-all`, and editor inputs. Send:

```js
window.chrome.webview.postMessage({
    action: 'cmd_set_scoreboard_text_styles',
    styles: scoreboardTextStyles
});
```

### Task 5: Verify and Build

**Files:**
- Test: `scripts/check-scoreboard-style-feature.ps1`
- Test: `scripts/check-seat-label-feature.ps1`
- Test: `web前端/main.js`

- [ ] **Step 1: Verify GREEN**

Run: `powershell -ExecutionPolicy Bypass -File scripts\check-scoreboard-style-feature.ps1`

Expected: PASS with `Scoreboard text style feature static check passed.`

- [ ] **Step 2: Existing regression checks**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-seat-label-feature.ps1
node --check web前端\main.js
git diff --check
```

Expected: all exit 0.

- [ ] **Step 3: Release build**

Run:

```powershell
E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe ..\DNFGameCapture.slnx /p:Configuration=Release /p:Platform=x64 /m
```

Expected: 0 errors. Existing warnings in `CameraCapture.h` and `fallback_printwindow` may remain.
