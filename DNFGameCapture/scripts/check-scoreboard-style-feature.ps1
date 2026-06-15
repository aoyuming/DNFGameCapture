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

$cpp = Get-Content -LiteralPath (Join-Path $root "DNFGameCaptureDlg.cpp") -Raw -Encoding UTF8
$html = Get-Content -LiteralPath (Join-Path $webDir.FullName "index.html") -Raw -Encoding UTF8
$js = Get-Content -LiteralPath (Join-Path $webDir.FullName "main.js") -Raw -Encoding UTF8
$css = Get-Content -LiteralPath (Join-Path $webDir.FullName "style.css") -Raw -Encoding UTF8

$required = @(
    @{ Text = $cpp; Needle = "DnfBuildScoreboardTextStylesJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "DnfSaveScoreboardTextStylesJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "DnfBuildInstalledFontListJson"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "cmd_set_scoreboard_text_styles"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '"scoreboardTextStyles"'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '"systemFonts"'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "teamName", L"Microsoft YaHei", 38, L"team",'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "score",    L"Arial Black",     39, L"team",'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "header",   L"Microsoft YaHei", 22, L"custom",'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "pickLabel",L"Microsoft YaHei", 18, L"custom", L"#a6b7bf", L"#000000", 1, 0,'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "playerName",L"Arial Black",    22, L"custom", L"#ffffff", L"#000000", 1, 2,'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = '{ "statNumber",L"Microsoft YaHei",25, L"custom", L"#ffffff", L"#000000", 1, 0,'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $html; Needle = "btn-appearance"; File = "web/index.html" },
    @{ Text = $html; Needle = "appearance-overlay"; File = "web/index.html" },
    @{ Text = $html; Needle = "style-font-family"; File = "web/index.html" },
    @{ Text = $js; Needle = "SCOREBOARD_TEXT_STYLE_TYPES"; File = "web/main.js" },
    @{ Text = $js; Needle = "applyScoreboardTextStyles"; File = "web/main.js" },
    @{ Text = $js; Needle = "cmd_set_scoreboard_text_styles"; File = "web/main.js" },
    @{ Text = $js; Needle = "resetCurrentScoreboardStyle"; File = "web/main.js" },
    @{ Text = $js; Needle = "layout-fit-measure-defaults"; File = "web/main.js" },
    @{ Text = $js; Needle = "appearanceOpen"; File = "web/main.js" },
    @{ Text = $js; Needle = "document.documentElement.classList.add('appearance-open')"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Microsoft YaHei', fontSize: 38, colorMode: 'team'"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Arial Black', fontSize: 39, colorMode: 'team'"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Microsoft YaHei', fontSize: 22, colorMode: 'custom', color: '#8b8b9f'"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Microsoft YaHei', fontSize: 18, colorMode: 'custom', color: '#a6b7bf', strokeColor: '#000000', strokeWidth: 1, glow: 0 }"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Arial Black', fontSize: 22, colorMode: 'custom', color: '#ffffff', strokeColor: '#000000', strokeWidth: 1, glow: 2 }"; File = "web/main.js" },
    @{ Text = $js; Needle = "defaults: { fontFamily: 'Microsoft YaHei', fontSize: 25, colorMode: 'custom', color: '#ffffff', strokeColor: '#000000', strokeWidth: 1, glow: 0 }"; File = "web/main.js" },
    @{ Text = $css; Needle = "--sb-team-name-font-family"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-team-name-font-size: 38px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-score-font-size: 39px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-header-font-size: 22px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-pick-label-font-size: 18px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-pick-label-color: #a6b7bf"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-pick-label-stroke-width: 1px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-player-name-font-family: ""Arial Black"", sans-serif"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-player-name-font-size: 22px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-player-name-stroke-width: 1px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-stat-number-font-size: 25px"; File = "web/style.css" },
    @{ Text = $css; Needle = "--sb-stat-number-stroke-width: 1px"; File = "web/style.css" },
    @{ Text = $css; Needle = "html.layout-fit-measure-defaults"; File = "web/style.css" },
    @{ Text = $css; Needle = "--appearance-dock-top"; File = "web/style.css" },
    @{ Text = $css; Needle = "top: var(--appearance-dock-top)"; File = "web/style.css" },
    @{ Text = $css; Needle = ".appearance-panel"; File = "web/style.css" },
    @{ Text = $css; Needle = "height: 100%"; File = "web/style.css" },
    @{ Text = $css; Needle = ".appearance-overlay"; File = "web/style.css" },
    @{ Text = $css; Needle = ".team-name-label"; File = "web/style.css" }
)

$forbidden = @(
    @{ Text = $js; Needle = "red.teamName"; File = "web/main.js" },
    @{ Text = $js; Needle = "blue.teamName"; File = "web/main.js" },
    @{ Text = $cpp; Needle = "red.teamName"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "blue.teamName"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $css; Needle = "background: rgba(8, 8, 14, 0.72)"; File = "web/style.css" }
)

$missing = New-Object System.Collections.Generic.List[string]
foreach ($item in $required) {
    if ($item.Text.IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
        $missing.Add("Missing '$($item.Needle)' in $($item.File)")
    }
}

foreach ($item in $forbidden) {
    if ($item.Text.IndexOf($item.Needle, [System.StringComparison]::Ordinal) -ge 0) {
        $missing.Add("Forbidden per-team style key '$($item.Needle)' remains in $($item.File)")
    }
}

if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Scoreboard text style feature static check passed."
