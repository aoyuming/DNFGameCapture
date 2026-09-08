$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webDir = (Get-ChildItem -LiteralPath $root -Directory |
    Where-Object { $_.Name -like 'web*' } | Select-Object -First 1).FullName
$main = Get-Content -LiteralPath (Join-Path $webDir 'main.js') -Raw -Encoding UTF8
$workerPath = Join-Path $webDir 'autocomplete-worker.js'

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $main "new Worker('autocomplete-worker.js')" `
    'Name autocomplete must use a dedicated worker.'
Require-Text $main 'requestAutocompleteSuggestions' `
    'Name autocomplete must request matches asynchronously.'
Require-Text $main 'autocompleteRequestSerial' `
    'Name autocomplete requests must reject stale worker results.'
Require-Text $main 'getLibraryMainNamesForAutocomplete' `
    'Name autocomplete must use a cached library-name list.'

$processLogic = [regex]::Match($main,
    'function processInputLogic[\s\S]*?return row;').Value
if (-not $processLogic) { throw 'Cannot locate processInputLogic.' }
if ($processLogic -match 'for\s*\(\s*let\s+name\s+in\s+savedDB\s*\)') {
    throw 'processInputLogic must not copy the complete savedDB on every key event.'
}
if (-not (Test-Path -LiteralPath $workerPath)) {
    throw 'Missing autocomplete-worker.js.'
}
$worker = Get-Content -LiteralPath $workerPath -Raw -Encoding UTF8
Require-Text $worker 'self.onmessage' 'Autocomplete worker has no message handler.'
Require-Text $worker 'type === ''query''' 'Autocomplete worker has no query path.'

Write-Host 'Async name autocomplete static checks passed.'
