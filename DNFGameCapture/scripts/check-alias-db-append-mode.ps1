$ErrorActionPreference = 'Stop'

$test = Join-Path $PSScriptRoot 'alias_db_append_mode_test.js'
if (-not (Test-Path -LiteralPath $test)) {
    throw "Missing alias DB append-only test: $test"
}

node $test
if ($LASTEXITCODE -ne 0) {
    throw "Alias DB append-only tests failed with exit code $LASTEXITCODE."
}

$lockTest = Join-Path $PSScriptRoot 'alias_append_lock_test.js'
if (-not (Test-Path -LiteralPath $lockTest)) {
    throw "Missing alias append lock test: $lockTest"
}

node $lockTest
if ($LASTEXITCODE -ne 0) {
    throw "Alias append lock tests failed with exit code $LASTEXITCODE."
}

Write-Host 'Alias DB append-only test check passed.' -ForegroundColor Green
