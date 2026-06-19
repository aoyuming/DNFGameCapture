$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$webDir = Get-ChildItem -LiteralPath $root -Directory -Filter "web*" |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName "index.html")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "main.js")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "style.css"))
    } |
    Select-Object -First 1

if (-not $webDir) {
    Write-Error "Could not locate web frontend directory."
    exit 1
}

$files = @{
    "DNFGameCaptureDlg.cpp" = Join-Path $root "DNFGameCaptureDlg.cpp"
    "DNFGameCaptureDlg.h" = Join-Path $root "DNFGameCaptureDlg.h"
    "KillDisplayDlg.cpp" = Join-Path $root "KillDisplayDlg.cpp"
    "KillDisplayDlg.h" = Join-Path $root "KillDisplayDlg.h"
    "WebScoreDlg.cpp" = Join-Path $root "WebScoreDlg.cpp"
    "DNFGameCapture.vcxproj" = Join-Path $root "DNFGameCapture.vcxproj"
    "web/kill.html" = Join-Path $webDir.FullName "kill.html"
    "web/kill.js" = Join-Path $webDir.FullName "kill.js"
    "web/kill.css" = Join-Path $webDir.FullName "kill.css"
    "web/index.html" = Join-Path $webDir.FullName "index.html"
    "web/main.js" = Join-Path $webDir.FullName "main.js"
    "web/style.css" = Join-Path $webDir.FullName "style.css"
}

$missing = New-Object System.Collections.Generic.List[string]
foreach ($entry in $files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        $missing.Add("Missing file: $($entry.Key)")
    }
}

if ($missing.Count -eq 0) {
    $text = @{}
    foreach ($entry in $files.GetEnumerator()) {
        $text[$entry.Key] = Get-Content -LiteralPath $entry.Value -Raw -Encoding UTF8
    }

    $required = @(
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "DnfBuildSharedWebStateJson" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "DnfBuildKillDisplaySettingsJson" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "DnfSaveKillDisplaySettingsJson" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "DnfStartKillDisplayHttpServer" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "/api/kill-display-settings" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "killNumber" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "deathNumber" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "akCountBadge" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "pageScale" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "playerNameOffsetX" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "cmd_set_kill_display_settings" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "cmd_open_kill_display" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "cmd_copy_kill_obs_url" },
        @{ File = "DNFGameCaptureDlg.h"; Needle = "CKillDisplayDlg* m_pKillDisplayDlg" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "kill.html" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "kKillDisplayWindowTitle" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "DNF Kill Display" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "removeStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addStyle = WS_POPUP" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "removeExStyle = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addExStyle = WS_EX_APPWINDOW | WS_EX_LAYERED" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "SetLayeredWindowAttributes" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "BeginWindowDrag" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "BeginWindowResize" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "cmd_kill_window_drag" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "cmd_kill_window_resize" },
        @{ File = "KillDisplayDlg.h"; Needle = "CKillDisplayDlg" },
        @{ File = "WebScoreDlg.cpp"; Needle = "SetWindowText(title)" },
        @{ File = "DNFGameCapture.vcxproj"; Needle = "KillDisplayDlg.cpp" },
        @{ File = "DNFGameCapture.vcxproj"; Needle = "KillDisplayDlg.h" },
        @{ File = "web/kill.html"; Needle = "kill-display-root" },
        @{ File = "web/kill.html"; Needle = "kill-edit-toolbar" },
        @{ File = "web/kill.html"; Needle = "<select id=`"kill-edit-font`">" },
        @{ File = "web/kill.html"; Needle = "data-style-key=`"killNumber`"" },
        @{ File = "web/kill.html"; Needle = "data-style-key=`"deathNumber`"" },
        @{ File = "web/kill.html"; Needle = "data-style-key=`"akMark`"" },
        @{ File = "web/kill.html"; Needle = "value=`"akCountBadge`"" },
        @{ File = "web/kill.html"; Needle = "kill.css" },
        @{ File = "web/kill.html"; Needle = "kill.js" },
        @{ File = "web/kill.js"; Needle = "http://127.0.0.1:18777/api/state" },
        @{ File = "web/kill.js"; Needle = "/api/kill-display-settings" },
        @{ File = "web/kill.js"; Needle = "formatAkMark" },
        @{ File = "web/kill.js"; Needle = "renderAkMark" },
        @{ File = "web/kill.js"; Needle = "getFitTextNeededWidth" },
        @{ File = "web/kill.js"; Needle = "compact-death" },
        @{ File = "web/kill.js"; Needle = "fitKillTextElements" },
        @{ File = "web/kill.js"; Needle = "toggleKillEditMode" },
        @{ File = "web/kill.js"; Needle = "dragLayoutState" },
        @{ File = "web/kill.js"; Needle = "beginKillLayoutDrag" },
        @{ File = "web/kill.js"; Needle = "updateKillLayoutOffset" },
        @{ File = "web/kill.js"; Needle = "isVerticalLayoutDragAllowed" },
        @{ File = "web/kill.js"; Needle = "dataset.styleKey = 'teamName'" },
        @{ File = "web/kill.js"; Needle = "pageScale" },
        @{ File = "web/kill.js"; Needle = "killNumber" },
        @{ File = "web/kill.js"; Needle = "deathNumber" },
        @{ File = "web/kill.js"; Needle = "akCountBadge" },
        @{ File = "web/kill.js"; Needle = "applyKillDisplaySettings" },
        @{ File = "web/kill.js"; Needle = "renderKillDisplay" },
        @{ File = "web/kill.js"; Needle = "postKillHostCommand" },
        @{ File = "web/kill.js"; Needle = "cmd_kill_window_drag" },
        @{ File = "web/kill.js"; Needle = "cmd_kill_window_resize" },
        @{ File = "web/kill.js"; Needle = "bgAlpha: 0" },
        @{ File = "web/kill.js"; Needle = "panelAlpha: 49" },
        @{ File = "web/kill.js"; Needle = "teamGap: 0" },
        @{ File = "web/kill.css"; Needle = "--kill-bg-alpha" },
        @{ File = "web/kill.css"; Needle = "--kill-team-gap" },
        @{ File = "web/kill.css"; Needle = "--kill-kill-number-font-size" },
        @{ File = "web/kill.css"; Needle = "--kill-death-number-font-size" },
        @{ File = "web/kill.css"; Needle = "--kill-ak-mark-letter-spacing" },
        @{ File = "web/kill.css"; Needle = "--kill-ak-count-font-size" },
        @{ File = "web/kill.css"; Needle = ".kill-ak-symbol" },
        @{ File = "web/kill.css"; Needle = ".kill-ak-count" },
        @{ File = "web/kill.css"; Needle = "--kill-page-scale" },
        @{ File = "web/kill.css"; Needle = "--kill-player-name-offset-x" },
        @{ File = "web/kill.css"; Needle = ".kill-edit-toolbar" },
        @{ File = "web/kill.css"; Needle = ".compact-death" },
        @{ File = "web/kill.css"; Needle = ".kill-resize-grip" },
        @{ File = "web/kill.css"; Needle = "cursor: move" },
        @{ File = "web/kill.css"; Needle = "cursor: nwse-resize" },
        @{ File = "web/kill.css"; Needle = "rgba(64, 64, 72, var(--kill-bg-alpha))" },
        @{ File = "web/index.html"; Needle = "appearance-scope-kill" },
        @{ File = "web/index.html"; Needle = "btn-kill-display-open" },
        @{ File = "web/index.html"; Needle = "btn-kill-obs-copy" },
        @{ File = "web/main.js"; Needle = "KILL_DISPLAY_LAYOUT_DEFAULTS" },
        @{ File = "web/main.js"; Needle = "KILL_DISPLAY_TEXT_STYLE_TYPES" },
        @{ File = "web/main.js"; Needle = "killNumber" },
        @{ File = "web/main.js"; Needle = "deathNumber" },
        @{ File = "web/main.js"; Needle = "akCountBadge" },
        @{ File = "web/main.js"; Needle = "pageScale" },
        @{ File = "web/main.js"; Needle = "playerNameOffsetX" },
        @{ File = "web/main.js"; Needle = "cmd_set_kill_display_settings" },
        @{ File = "web/main.js"; Needle = "cmd_open_kill_display" },
        @{ File = "web/main.js"; Needle = "cmd_copy_kill_obs_url" },
        @{ File = "web/style.css"; Needle = ".appearance-scope-tabs" },
        @{ File = "web/style.css"; Needle = ".kill-layout-grid" }
    )

    $forbidden = @(
        @{ File = "KillDisplayDlg.cpp"; Needle = "WS_EX_TOOLWINDOW" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "WS_EX_TOPMOST" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addStyle = WS_POPUP |" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addStyle = WS_THICKFRAME" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addStyle = WS_BORDER" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addStyle = WS_DLGFRAME" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addExStyle = WS_EX_CLIENTEDGE" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "addExStyle = WS_EX_WINDOWEDGE" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "OnNcHitTest" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "OnNcCalcSize" },
        @{ File = "KillDisplayDlg.h"; Needle = "OnNcHitTest" },
        @{ File = "KillDisplayDlg.h"; Needle = "OnNcCalcSize" },
        @{ File = "web/kill.css"; Needle = ".kill-resize-grip::after" },
        @{ File = "web/kill.css"; Needle = "border-right: 2px solid rgba(255, 255, 255, 0.34)" },
        @{ File = "web/kill.css"; Needle = "border-bottom: 2px solid rgba(255, 255, 255, 0.34)" },
        @{ File = "web/kill.css"; Needle = "text-overflow: ellipsis" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "m_pKillDisplayDlg->Create(IDD_WEB_SCORE_DIALOG, GetDesktopWindow())" }
    )

    foreach ($item in $required) {
        if ($text[$item.File].IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
            $missing.Add("Missing '$($item.Needle)' in $($item.File)")
        }
    }

    foreach ($item in $forbidden) {
        if ($text[$item.File].IndexOf($item.Needle, [System.StringComparison]::Ordinal) -ge 0) {
            $missing.Add("Forbidden capture-hidden window pattern '$($item.Needle)' remains in $($item.File)")
        }
    }

    $regexChecks = @(
        @{
            File = "web/kill.js"
            Pattern = "key:\s*'killNumber'(?:(?!\r?\n\s*key:\s*')[\s\S])*?color:\s*'#f7ca69'"
            Message = "web/kill.js killNumber default color must be #f7ca69"
        },
        @{
            File = "web/kill.js"
            Pattern = "key:\s*'deathNumber'(?:(?!\r?\n\s*key:\s*')[\s\S])*?color:\s*'#ab986d'"
            Message = "web/kill.js deathNumber default color must be #ab986d"
        },
        @{
            File = "web/main.js"
            Pattern = "key:\s*'killNumber'(?:(?!\r?\n\s*key:\s*')[\s\S])*?color:\s*'#f7ca69'"
            Message = "web/main.js killNumber default color must be #f7ca69"
        },
        @{
            File = "web/main.js"
            Pattern = "key:\s*'deathNumber'(?:(?!\r?\n\s*key:\s*')[\s\S])*?color:\s*'#ab986d'"
            Message = "web/main.js deathNumber default color must be #ab986d"
        },
        @{
            File = "web/kill.js"
            Pattern = "suppressRemoteKillSettingsUntil"
            Message = "web/kill.js must keep local font/style edits from being overwritten by state polling"
        },
        @{
            File = "DNFGameCaptureDlg.cpp"
            Pattern = "DnfSaveKillDisplaySettingsJson\(m_iniPath,\s*incoming\[`"settings`"\]\);(?:(?!json\s+response;)[\s\S])*?PostMessage\(WM_UPDATE_ALL_UI,\s*0,\s*0\)"
            Message = "SaveKillDisplaySettingsPayload must notify the main UI thread after saving kill display settings"
        }
    )

    foreach ($item in $regexChecks) {
        if (-not [regex]::IsMatch($text[$item.File], $item.Pattern)) {
            $missing.Add($item.Message)
        }
    }
}

if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Kill display feature static check passed."
