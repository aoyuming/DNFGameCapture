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

$header = Get-Content -LiteralPath (Join-Path $root "DNFGameCaptureDlg.h") -Raw -Encoding UTF8
$cpp = Get-Content -LiteralPath (Join-Path $root "DNFGameCaptureDlg.cpp") -Raw -Encoding UTF8
$html = Get-Content -LiteralPath (Join-Path $webDir.FullName "index.html") -Raw -Encoding UTF8
$js = Get-Content -LiteralPath (Join-Path $webDir.FullName "main.js") -Raw -Encoding UTF8
$css = Get-Content -LiteralPath (Join-Path $webDir.FullName "style.css") -Raw -Encoding UTF8
$xPick = "x$([char]0x9009)"
$hPick = "h$([char]0x9009)"
$firstPickText = "$([char]0x5148)$([char]0x9009)"
$secondPickText = "$([char]0x540E)$([char]0x9009)"
$pickOrderText = "$([char]0x9009)$([char]0x4EBA)$([char]0x987A)$([char]0x5E8F)"

$required = @(
    @{ Text = $header; Needle = "m_bRedPickFirst"; File = "DNFGameCaptureDlg.h" },
    @{ Text = $header; Needle = "GetPickSeatLabelForIndex"; File = "DNFGameCaptureDlg.h" },
    @{ Text = $cpp; Needle = "RedPickFirst"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "cmd_set_red_pick_mode"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "redPickMode"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = "SEAT="; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = 'fwprintf(fKill, L"%s %s\n", ls.GetString(), rs.GetString());'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $html; Needle = "output-seat-label-toggle"; File = "web/index.html" },
    @{ Text = $html; Needle = $pickOrderText; File = "web/index.html" },
    @{ Text = $js; Needle = "PICK_LABEL_SEQUENCES"; File = "web/main.js" },
    @{ Text = $js; Needle = "setRedPickMode"; File = "web/main.js" },
    @{ Text = $js; Needle = "refreshPickLabels"; File = "web/main.js" },
    @{ Text = $js; Needle = "cmd_set_red_pick_mode"; File = "web/main.js" },
    @{ Text = $js; Needle = "redPickMode"; File = "web/main.js" },
    @{ Text = $js; Needle = "seat-label-toggle"; File = "web/main.js" },
    @{ Text = $js; Needle = "displayPickChoiceLabel"; File = "web/main.js" },
    @{ Text = $js; Needle = "togglePickModeForRow"; File = "web/main.js" },
    @{ Text = $js; Needle = "getRandomPlaceholderSeatNumber"; File = "web/main.js" },
    @{ Text = $js; Needle = "if (normalized === '$xPick') return '7';"; File = "web/main.js" },
    @{ Text = $js; Needle = "if (normalized === '$hPick') return '8';"; File = "web/main.js" },
    @{ Text = $js; Needle = "const seatNumber = getRandomPlaceholderSeatNumber(getRandomSeatLabel(row, idx), idx);"; File = "web/main.js" },
    @{ Text = $js; Needle = $xPick; File = "web/main.js" },
    @{ Text = $js; Needle = $hPick; File = "web/main.js" },
    @{ Text = $js; Needle = $firstPickText; File = "web/main.js" },
    @{ Text = $js; Needle = $secondPickText; File = "web/main.js" },
    @{ Text = $css; Needle = "seat-label-input[readonly]"; File = "web/style.css" },
    @{ Text = $css; Needle = "seat-number[hidden]"; File = "web/style.css" },
    @{ Text = $css; Needle = "seat-label-toggle"; File = "web/style.css" },
    @{ Text = $css; Needle = "flex: 0 0 38px"; File = "web/style.css" },
    @{ Text = $css; Needle = "padding: 6px 6px 8px"; File = "web/style.css" },
    @{ Text = $css; Needle = "width: 100%; max-width: none"; File = "web/style.css" },
    @{ Text = $css; Needle = "flex: 1 1 0"; File = "web/style.css" },
    @{ Text = $css; Needle = "flex: 1 1 auto;"; File = "web/style.css" },
    @{ Text = $css; Needle = "align-items: stretch;"; File = "web/style.css" }
)

$missing = New-Object System.Collections.Generic.List[string]
foreach ($item in $required) {
    if ($item.Text.IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
        $missing.Add("Missing '$($item.Needle)' in $($item.File)")
    }
}

$forbidden = @(
    @{ Text = $header; Needle = "CString seatLabelManual"; File = "DNFGameCaptureDlg.h" },
    @{ Text = $cpp; Needle = "seatLabelManual"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $js; Needle = "getSeatLabelManual"; File = "web/main.js" },
    @{ Text = $js; Needle = "pick-mode-control"; File = "web/main.js" },
    @{ Text = $js; Needle = "seat-label-select"; File = "web/main.js" },
    @{ Text = $css; Needle = "seat-label-select"; File = "web/style.css" },
    @{ Text = $css; Needle = "appearance: auto"; File = "web/style.css" },
    @{ Text = $cpp; Needle = "targetWidth = m_bOutputSeatLabelToKillFile"; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $cpp; Needle = 'fwprintf(fKill, L"%s%s%s\n"'; File = "DNFGameCaptureDlg.cpp" },
    @{ Text = $html; Needle = 'id="output-seat-label-toggle" checked'; File = "web/index.html" }
)

foreach ($item in $forbidden) {
    if ($item.Text.IndexOf($item.Needle, [System.StringComparison]::Ordinal) -ge 0) {
        $missing.Add("Forbidden legacy per-player label '$($item.Needle)' remains in $($item.File)")
    }
}

if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Fixed pick-order seat label feature static check passed."
