$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)
$webDialog = Get-Content -LiteralPath (Join-Path $root 'WebScoreDlg.cpp') -Raw -Encoding UTF8
$style = Get-Content -LiteralPath (Join-Path $webRoot 'style.css') -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $webDialog 'constexpr int kCompactClientWidth = 1140;' `
    'The compact Web window span was not widened.'
Require-Text $webDialog 'constexpr int kExpandedClientWidth = 1400;' `
    'The expanded Web window span was not widened.'
Require-Text $style 'flex: 0 0 280px;' `
    'The broadcaster sidebar was not widened.'
Require-Text $style 'width: 280px;' `
    'The broadcaster sidebar width was not updated.'
Require-Text $style 'left: 4px;' `
    'The scoreboard stack was not shifted to the final horizontal position.'

Write-Host 'Main layout span static checks passed.'
