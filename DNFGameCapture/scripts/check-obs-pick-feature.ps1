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

$checks = @(
    @{
        Path = Join-Path $root "DNFGameCaptureDlg.h"
        Needles = @(
            "CString pickLabelManual",
            "ObsPickStyle",
            "m_obsServerPort"
        )
    },
    @{
        Path = Join-Path $root "DNFGameCaptureDlg.cpp"
        Needles = @(
            "pickLabel",
            "pickLabelManual",
            "obsPickStyle",
            "StartObsHttpServer",
            "BuildObsStateJson",
            "18777"
        )
    },
    @{
        Path = Join-Path $root "DNFGameCapture.vcxproj"
        Needles = @(
            "ws2_32.lib",
            "obs.html",
            "obs.css",
            "obs.js"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "index.html"
        Needles = @(
            "btn-obs-style",
            "pick-label-header",
            "obs-style-modal"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "main.js"
        Needles = @(
            "pickLabelManual",
            "getEffectivePickLabel",
            "cmd_set_obs_pick_style",
            "cmd_get_system_fonts",
            "system_fonts_result"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "style.css"
        Needles = @(
            "pick-label-input",
            "obs-style-grid"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "obs.html"
        Needles = @(
            "obs.css",
            "obs.js",
            "obs-scoreboard"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "obs.js"
        Needles = @(
            "obs-state.json",
            "pick-tag",
            "pickLabel"
        )
    },
    @{
        Path = Join-Path $webDir.FullName "obs.css"
        Needles = @(
            "--pick-font-family",
            "--pick-font-size",
            "--pick-color",
            ".pick-tag"
        )
    }
)

$missing = New-Object System.Collections.Generic.List[string]
foreach ($check in $checks) {
    if (-not (Test-Path -LiteralPath $check.Path)) {
        $missing.Add("Missing file: $($check.Path)")
        continue
    }

    $text = Get-Content -LiteralPath $check.Path -Raw -Encoding UTF8
    foreach ($needle in $check.Needles) {
        if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
            $missing.Add("Missing '$needle' in $($check.Path)")
        }
    }
}

if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "OBS pick label feature static check passed."
