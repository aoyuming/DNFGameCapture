$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

function Require-Text([string]$text, [string]$description) {
    if ($source -notmatch $text) {
        throw "Missing alias database request safeguard: $description"
    }
}

Require-Text 'DNF_ALIAS_DB_REQUEST_TIMEOUT_MS\s*=\s*60000' '60 second alias database timeout constant'
Require-Text 'DnfPostCloudJson\(req\.dump\(\),\s*response,\s*err,\s*DNF_ALIAS_DB_REQUEST_TIMEOUT_MS\)' 'manual review submission timeout'
Require-Text 'DnfPostCloudJson\(req\.dump\(\),\s*response,\s*err,\s*DNF_ALIAS_DB_REQUEST_TIMEOUT_MS\)' 'direct alias database submission timeout'
Require-Text 'DnfPostCloudJson\(req\.dump\(\),\s*response,\s*err,\s*DNF_ALIAS_DB_REQUEST_TIMEOUT_MS\)' 'manual public database pull timeout'
Require-Text 'getError,\s*DNF_ALIAS_DB_REQUEST_TIMEOUT_MS' 'automatic public database pull timeout'
Require-Text 'pushError,\s*DNF_ALIAS_DB_REQUEST_TIMEOUT_MS' 'automatic append submission timeout'
Require-Text '(?s)WinHttpQueryHeaders\(.*?WINHTTP_QUERY_STATUS_CODE' 'HTTP status diagnostics'
Require-Text 'GetLastError\(\)' 'WinHTTP error diagnostics'
Require-Text 'elapsedMs' 'request duration diagnostics'

Write-Output 'Alias database request timeout check passed.'
