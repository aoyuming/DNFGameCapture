$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$webDir = Get-ChildItem -LiteralPath $root -Directory -Filter "web*" |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName "index.html")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "main.js"))
    } |
    Select-Object -First 1

if (-not $webDir) {
    Write-Error "Could not locate web frontend directory."
    exit 1
}

$files = @{
    "WebScoreDlg.cpp" = Join-Path $root "WebScoreDlg.cpp"
    "WebScoreDlg.h" = Join-Path $root "WebScoreDlg.h"
    "DNFGameCaptureDlg.cpp" = Join-Path $root "DNFGameCaptureDlg.cpp"
    "web/main.js" = Join-Path $webDir.FullName "main.js"
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
        @{ File = "web/main.js"; Needle = "cmd_set_appearance_panel_open" },
        @{ File = "web/main.js"; Needle = "setAppearancePanelWindowExpanded(true)" },
        @{ File = "web/main.js"; Needle = "setAppearancePanelWindowExpanded(false)" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "cmd_set_appearance_panel_open" },
        @{ File = "DNFGameCaptureDlg.cpp"; Needle = "SetAppearancePanelExpanded" },
        @{ File = "WebScoreDlg.h"; Needle = "SetAppearancePanelExpanded" },
        @{ File = "WebScoreDlg.cpp"; Needle = "kAppearanceExtraClientHeight = 300" },
        @{ File = "WebScoreDlg.cpp"; Needle = "SetAppearancePanelExpanded(bool expanded)" },
        @{ File = "WebScoreDlg.cpp"; Needle = "m_appearanceExpanded" }
    )

    foreach ($item in $required) {
        if ($text[$item.File].IndexOf($item.Needle, [System.StringComparison]::Ordinal) -lt 0) {
            $errors.Add("Missing '$($item.Needle)' in $($item.File)")
        }
    }
}

if ($errors.Count -gt 0) {
    foreach ($err in $errors) {
        Write-Error $err -ErrorAction Continue
    }
    exit 1
}

Write-Host "Appearance window resize static checks passed."
