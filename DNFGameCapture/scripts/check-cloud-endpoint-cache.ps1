$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$headerPath = Join-Path $root 'DNFGameCaptureDlg.h'

foreach ($path in @($sourcePath, $headerPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing cloud endpoint cache source file: $path"
    }
}

$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $header 'bool m_cloudServerAuthV2 = false;' `
    'Server authorization v2 must remain opt-in for production clients.'
Require-Text $header 'CString m_cloudEndpointManifestUrl;' `
    'The endpoint manifest URL is not persisted in the client state.'
Require-Text $header 'CString m_cloudServerLastKnownUrl;' `
    'The cached server URL field is missing.'

Require-Text $source 'L"CloudMatch", L"ServerAuthV2", 0' `
    'ServerAuthV2 must default to disabled.'
Require-Text $source 'DnfFetchServerEndpointManifest(' `
    'The OSS endpoint-manifest fallback is missing.'
Require-Text $source 'L"CloudMatch", L"LastKnownServerUrl"' `
    'The cached server URL is not loaded from local configuration.'
Require-Text $source 'writeSetting(L"LastKnownServerUrl", m_cloudServerLastKnownUrl);' `
    'The successful authorization endpoint is not cached.'
Require-Text $source 'm_cloudServerLastKnownUrl = authorizedServerUrl;' `
    'The authorization response URL is not promoted to the active endpoint.'

$begin = $source.IndexOf('static bool DnfCheckServerV2Binding(',
    [System.StringComparison]::Ordinal)
$end = $source.IndexOf('static bool DnfReadV2Revision(',
    $begin + 1, [System.StringComparison]::Ordinal)
if ($begin -lt 0 -or $end -le $begin) {
    throw 'Unable to inspect the v2 endpoint decision function.'
}
$decision = $source.Substring($begin, $end - $begin)

$requestIndex = $decision.IndexOf('DnfHttpRequestUtf8(activeEndpoint',
    [System.StringComparison]::Ordinal)
$transportFallbackIndex = $decision.IndexOf('if (!transportOk)',
    [System.StringComparison]::Ordinal)
$manifestFirstIndex = $decision.IndexOf(
    'DnfFetchServerEndpointManifest(manifestUrl',
    [System.StringComparison]::Ordinal)
$manifestFallbackIndex = $decision.IndexOf(
    'DnfFetchServerEndpointManifest(manifestUrl',
    $requestIndex, [System.StringComparison]::Ordinal)
$replyIndex = $decision.IndexOf('const json reply =',
    [System.StringComparison]::Ordinal)
if ($requestIndex -lt 0 -or $manifestFirstIndex -lt 0 -or
    $transportFallbackIndex -lt $requestIndex -or
    $manifestFallbackIndex -lt $transportFallbackIndex -or
    $replyIndex -lt $transportFallbackIndex) {
    throw 'The endpoint decision order is not cache-first with transport-only OSS fallback.'
}

if ($decision.IndexOf('DnfFetchServerEndpointManifest(manifestUrl',
        $replyIndex, [System.StringComparison]::Ordinal) -ge 0) {
    throw 'An authorization response failure must not trigger an OSS endpoint refresh.'
}

$legacyBranch = $source.IndexOf('cloudResult = CheckCloudBinding(',
    [System.StringComparison]::Ordinal)
$v2Branch = $source.IndexOf('DnfCheckServerV2Binding(',
    [System.StringComparison]::Ordinal)
if ($legacyBranch -lt 0 -or $v2Branch -lt 0) {
    throw 'The legacy cloud-function authorization branch is missing.'
}

Write-Host 'Cloud endpoint cache static checks passed.' -ForegroundColor Green
