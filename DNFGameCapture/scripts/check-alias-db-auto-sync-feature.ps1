$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    node "scripts\alias_db_auto_sync_feature_test.js"
    if ($LASTEXITCODE -ne 0) { throw "Alias DB auto-sync feature check failed." }
    Write-Output "Alias DB auto-sync feature check passed."
}
finally {
    Pop-Location
}
