$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$webDir = Get-ChildItem -LiteralPath $root -Directory -Filter "web*" |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName "kill.html")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "kill.js")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "kill.css"))
    } |
    Select-Object -First 1

if (-not $webDir) {
    Write-Error "Could not locate web frontend directory."
    exit 1
}

$files = @{
    "DNFGameCaptureDlg.cpp" = Join-Path $root "DNFGameCaptureDlg.cpp"
    "KillDisplayDlg.cpp" = Join-Path $root "KillDisplayDlg.cpp"
    "web/kill.html" = Join-Path $webDir.FullName "kill.html"
    "web/kill.js" = Join-Path $webDir.FullName "kill.js"
    "web/kill.css" = Join-Path $webDir.FullName "kill.css"
}

$errors = New-Object System.Collections.Generic.List[string]
$text = @{}
foreach ($entry in $files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        $errors.Add("Missing file: $($entry.Key)")
        continue
    }
    $text[$entry.Key] = Get-Content -LiteralPath $entry.Value -Raw -Encoding UTF8
}

if ($errors.Count -eq 0) {
    $required = @(
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "OpenKillDisplayWindow();" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "ToggleKillDisplayWindow" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "IsKillDisplayWindowVisible" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "cmd_toggle_kill_display" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = 'data["killDisplayWindowVisible"]' },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "if (m_bKillDisplayHttpReady)" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "cmd_kill_window_close" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "cmd_kill_window_edit_mode" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "SetEditModeWindowExpanded" },
        @{ File = "KillDisplayDlg.cpp"; Needle = "m_windowRectBeforeEdit" },
        @{ File = "web/kill.html"; Needle = "kill-window-close" },
        @{ File = "web/kill.html"; Needle = "kill-window-settings" },
        @{ File = "web/kill.html"; Needle = "kill-edit-exit" },
        @{ File = "web/kill.js"; Needle = "initKillWindowControls" },
        @{ File = "web/kill.js"; Needle = "cmd_kill_window_close" },
        @{ File = "web/kill.js"; Needle = "cmd_kill_window_edit_mode" },
        @{ File = "web/kill.js"; Needle = "lockKillBoardForEditMode" },
        @{ File = "web/kill.js"; Needle = "relockKillBoardForEditMode" },
        @{ File = "web/kill.js"; Needle = "unlockKillBoardAfterEditMode" },
        @{ File = "web/kill.js"; Needle = "kill-window-settings" },
        @{ File = "web/kill.js"; Needle = "kill-edit-exit" },
        @{ File = "web/kill.js"; Needle = "toggleKillEditMode" },
        @{ File = "web/kill.js"; Needle = "isKillShellWindow" },
        @{ File = "web/kill.css"; Needle = ".kill-window-close" },
        @{ File = "web/kill.css"; Needle = ".kill-window-settings" },
        @{ File = "web/kill.css"; Needle = "--kill-edit-board-left" },
        @{ File = "web/kill.css"; Needle = "--kill-edit-board-top" },
        @{ File = "web/kill.css"; Needle = "--kill-edit-board-width" },
        @{ File = "web/kill.css"; Needle = "--kill-edit-board-height" },
        @{ File = "web/kill.css"; Needle = "--kill-edit-toolbar-top" },
        @{ File = "web/kill.css"; Needle = ".kill-display-root.edit-mode .kill-board" },
        @{ File = "web/kill.css"; Needle = ".kill-display-root.shell-window:hover .kill-window-close" }
    )

    foreach ($item in $required) {
        if ($text[$item.File].IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
            $errors.Add("Missing '$($item.Needle)' in $($item.File)")
        }
    }

    if ($text["web/kill.css"] -notmatch "\.kill-window-close\s*\{[\s\S]*opacity:\s*0") {
        $errors.Add("Close button must be hidden by default.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-display-root\.shell-window:hover\s+\.kill-window-close[\s\S]*\.kill-display-root\.shell-window:hover\s+\.kill-window-settings\s*\{[\s\S]*opacity:\s*1") {
        $errors.Add("Close button must show only while hovering the shell window.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-window-settings\s*\{[\s\S]*opacity:\s*0") {
        $errors.Add("Settings button must be hidden by default.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-display-root\.shell-window:hover\s+\.kill-window-close[\s\S]*\.kill-display-root\.shell-window:hover\s+\.kill-window-settings\s*\{[\s\S]*opacity:\s*1") {
        $errors.Add("Settings button must show only while hovering the shell window.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-edit-toolbar\s*\{[\s\S]*top:\s*var\(--kill-edit-toolbar-top\)") {
        $errors.Add("Edit toolbar must be placed below the locked scoreboard.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-display-root\.edit-mode\s+\.kill-board\s*\{[\s\S]*position:\s*fixed[\s\S]*left:\s*var\(--kill-edit-board-left\)[\s\S]*top:\s*var\(--kill-edit-board-top\)") {
        $errors.Add("Edit mode must lock the scoreboard position, not only its size.")
    }

    if ($text["web/kill.css"] -match "\.kill-edit-toolbar\s*\{[\s\S]*bottom:\s*10px") {
        $errors.Add("Edit toolbar must not be docked to the window bottom over the scoreboard.")
    }

    if ($text["web/kill.css"] -match "\.kill-edit-toolbar\s*\{[\s\S]*overflow:\s*(auto|scroll)") {
        $errors.Add("Edit toolbar must not show native scrollbars.")
    }

    if ($text["web/kill.css"] -notmatch "\.kill-edit-toolbar\s*\{[\s\S]*overflow:\s*hidden") {
        $errors.Add("Edit toolbar must hide overflow instead of showing scrollbars.")
    }

    if ($text["web/kill.css"] -match "\.kill-display-root\.edit-mode\s*\{[\s\S]*padding-bottom") {
        $errors.Add("Edit mode must not change root padding because it moves the scoreboard.")
    }

    if ($text["web/kill.js"] -notmatch "function\s+setShowDeathNumber[\s\S]*unlockKillBoardAfterEditMode\(\)[\s\S]*applyKillDisplaySettings[\s\S]*relockKillBoardForEditMode\(\)") {
        $errors.Add("Toggling death number in edit mode must relock the scoreboard after layout changes.")
    }
}

if ($errors.Count -gt 0) {
    foreach ($err in $errors) {
        Write-Error $err -ErrorAction Continue
    }
    exit 1
}

Write-Host "Kill display window controls static checks passed."
