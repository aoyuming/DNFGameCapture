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
    "index.html" = Join-Path $webDir.FullName "index.html"
    "main.js" = Join-Path $webDir.FullName "main.js"
    "style.css" = Join-Path $webDir.FullName "style.css"
    "DNFGameCaptureDlg.cpp" = Join-Path $root "DNFGameCaptureDlg.cpp"
    "DNFGameCaptureDlg.h" = Join-Path $root "DNFGameCaptureDlg.h"
}

$text = @{}
foreach ($entry in $files.GetEnumerator()) {
    $text[$entry.Key] = Get-Content -LiteralPath $entry.Value -Raw -Encoding UTF8
}

$errors = New-Object System.Collections.Generic.List[string]

$required = @(
    @{ File = "index.html"; Needle = "control-row-match" },
    @{ File = "index.html"; Needle = "control-row-aux" },
    @{ File = "index.html"; Needle = "btn-review-toggle" },
    @{ File = "index.html"; Needle = "btn-auth" },
    @{ File = "index.html"; Needle = "btn-kill-display-toggle" },
    @{ File = "index.html"; Needle = "kill-show-death-toggle-main" },
    @{ File = "index.html"; Needle = "btn-more-controls" },
    @{ File = "index.html"; Needle = "more-controls-menu" },
    @{ File = "index.html"; Needle = "btn-appearance" },
    @{ File = "index.html"; Needle = "output-seat-label-toggle" },
    @{ File = "index.html"; Needle = "btn-random-teams" },
    @{ File = "index.html"; Needle = "btn-sync-alias-db" },
    @{ File = "index.html"; Needle = "btn-push-alias-db" },
    @{ File = "index.html"; Needle = "btn-pro" },
    @{ File = "index.html"; Needle = "death-algo-select" },
    @{ File = "index.html"; Needle = "btn-clear-teams" },
    @{ File = "main.js"; Needle = "setMoreControlsOpen" },
    @{ File = "main.js"; Needle = "syncMainShowDeathToggle" },
    @{ File = "main.js"; Needle = "syncKillDisplayToggle" },
    @{ File = "main.js"; Needle = "cmd_toggle_kill_display" },
    @{ File = "main.js"; Needle = "setKillDisplayShowDeathNumber" },
    @{ File = "main.js"; Needle = "kill-show-death-toggle-main" },
    @{ File = "style.css"; Needle = ".more-controls-wrap" },
    @{ File = "style.css"; Needle = ".more-controls-menu" },
    @{ File = "style.css"; Needle = ".kill-show-death-toggle" },
    @{ File = "DNFGameCaptureDlg.cpp"; Needle = "LastTargetWindowName" },
    @{ File = "DNFGameCaptureDlg.cpp"; Needle = "GetSelectedTargetWindowLabel" },
    @{ File = "DNFGameCaptureDlg.cpp"; Needle = "SaveSelectedTargetWindowName" },
    @{ File = "DNFGameCaptureDlg.cpp"; Needle = "GetLBText" },
    @{ File = "DNFGameCaptureDlg.h"; Needle = "m_lastTargetWindowName" },
    @{ File = "DNFGameCaptureDlg.h"; Needle = "SaveSelectedTargetWindowName" }
)

foreach ($item in $required) {
    if ($text[$item.File].IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
        $errors.Add("Missing '$($item.Needle)' in $($item.File)")
    }
}

if ($text["index.html"] -notmatch '<div class="control-row control-row-match">[\s\S]*btn-swap[\s\S]*btn-monitor[\s\S]*btn-reset[\s\S]*btn-kill-display-toggle[\s\S]*btn-pro[\s\S]*</div>\s*<div class="control-row control-row-aux">') {
    $errors.Add("First control row must contain swap, run, reset, display page, and professional mode in order.")
}

if ($text["index.html"] -notmatch '<div class="control-row control-row-aux">[\s\S]*btn-review-toggle[\s\S]*btn-auth[\s\S]*btn-appearance[\s\S]*output-seat-label-toggle[\s\S]*kill-show-death-toggle-main[\s\S]*btn-more-controls') {
    $errors.Add("Second control row must contain recent review, auth, appearance, pick order, show death, and more in order.")
}

if ($text["index.html"] -notmatch '<div class="more-controls-menu"[\s\S]*btn-random-teams[\s\S]*btn-sync-alias-db[\s\S]*btn-push-alias-db[\s\S]*death-algo-select[\s\S]*btn-clear-teams') {
    $errors.Add("More controls menu must contain only the low-frequency tool, data, system, and danger actions.")
}

if ($text["index.html"] -match '<div class="more-controls-menu"[\s\S]*?(btn-appearance|output-seat-label-toggle|btn-pro)[\s\S]*?</div>\s*</div>\s*</div>\s*<div class="dir-bar"') {
    $errors.Add("Appearance, pick order, and professional mode controls must not remain inside the More menu.")
}

if ($text["main.js"] -notmatch "control-panel.*more-controls-open|more-controls-open") {
    $errors.Add("More menu must toggle a high-layer control-panel state.")
}

if ($text["style.css"] -notmatch "\.control-panel\.more-controls-open") {
    $errors.Add("style.css must define the open More menu layering state.")
}

foreach ($needle in @('position: fixed', 'overflow-y: auto', 'scrollbar-gutter: stable')) {
    if ($text["style.css"].IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        $errors.Add("More menu viewport scrolling is missing CSS: $needle")
    }
}

foreach ($needle in @('positionMoreControlsMenu', 'document.body.appendChild(menu)', 'moreMenuMaxHeight', "addEventListener('resize'")) {
    if ($text["main.js"].IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        $errors.Add("More menu viewport positioning is missing JS: $needle")
    }
}

if ($text["main.js"].IndexOf("e.target.closest('.more-controls-menu')", [System.StringComparison]::Ordinal) -lt 0) {
    $errors.Add("Clicks inside the body-level More menu must not be treated as outside clicks.")
}

if ($errors.Count -gt 0) {
    foreach ($err in $errors) {
        Write-Error $err -ErrorAction Continue
    }
    exit 1
}

Write-Host "Main controls menu static checks passed."
