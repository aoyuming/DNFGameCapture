$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Read-RequiredFile([string]$path, [string]$encoding = 'UTF8') {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing required file: $path" }
    return Get-Content -LiteralPath $path -Raw -Encoding $encoding
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

function Reject-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        throw $message
    }
}

function Function-Body([string]$content, [string]$start, [string]$next) {
    $begin = $content.IndexOf($start, [System.StringComparison]::Ordinal)
    $end = $content.IndexOf($next, $begin + $start.Length,
        [System.StringComparison]::Ordinal)
    if ($begin -lt 0 -or $end -le $begin) {
        throw "Unable to inspect function: $start"
    }
    return $content.Substring($begin, $end - $begin)
}

$header = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.h')
$source = Read-RequiredFile (Join-Path $root 'DNFGameCaptureDlg.cpp')
$resources = Read-RequiredFile (Join-Path $root 'DNFGameCapture.rc') 'Unicode'
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)
$webMain = Read-RequiredFile (Join-Path $webRoot 'main.js')

Require-Text $header 'bool HasAuthorizedCloudMatchEndpoint() const;' `
    'The dialog has no centralized cloud authorization gate.'
Require-Text $header 'void DisableCloudMatchForAuthorization(const CString& reason);' `
    'The dialog has no authorization-loss teardown method.'
Require-Text $header 'CString m_cloudMatchServerUrl;' `
    'The cloud server URL must be process-memory state with no fallback initializer.'
Require-Text $header 'std::uint64_t m_cloudAuthRequestGeneration = 0;' `
    'Authorization callbacks have no request generation guard.'
Reject-Text $header 'm_cloudMatchServerUrl = L"http' `
    'The executable still initializes a fallback cloud server URL.'
Reject-Text $source 'http://47.109.149.111:18880' `
    'The executable still embeds the cloud match server address.'
Reject-Text $webMain 'http://47.109.149.111:18880' `
    'The Web frontend still embeds the cloud match server address.'
Reject-Text $webMain '47.109.149.111' `
    'The Web frontend still contains a locally embedded cloud match IP address.'
Require-Text $webMain "const CLOUD_MATCH_DEFAULT_SERVER_URL = '';" `
    'The Web frontend must default to no cloud match endpoint.'
Reject-Text $source 'GetPrivateProfileString(L"CloudMatch", L"ServerUrl"' `
    'The cloud match server URL is still restored from local configuration.'
Reject-Text $source 'writeSetting(L"ServerUrl"' `
    'The cloud match server URL is still persisted locally.'
Require-Text $source 'WritePrivateProfileString(L"CloudMatch", L"ServerUrl", nullptr, m_iniPath)' `
    'Legacy persisted cloud server URLs are not removed during migration.'

$startupCalls = [regex]::Matches($source, 'StartSavedCloudMatchSession\(\);').Count
if ($startupCalls -ne 1) {
    throw "Saved cloud sessions must start only from authorization success; found $startupCalls call sites."
}

$authFail = Function-Body $source `
    'LRESULT CDNFGameCaptureDlg::OnCloudAuthFail' `
    'LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime'
Require-Text $authFail 'DisableCloudMatchForAuthorization(' `
    'Authorization failure does not disconnect cloud match.'
Require-Text $authFail 'authFailure->requestGeneration != m_cloudAuthRequestGeneration' `
    'A stale authorization failure can still tear down a newer authorized session.'

$authSuccess = Function-Body $source `
    'LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime' `
    'bool CDNFGameCaptureDlg::VerifyKey'
Require-Text $authSuccess 'authSuccess->cloudServerUrl' `
    'Authorization success does not consume the returned server URL.'
Require-Text $authSuccess 'authSuccess->requestGeneration != m_cloudAuthRequestGeneration' `
    'A stale authorization success can still reconnect with an obsolete endpoint.'
Require-Text $authSuccess 'StartSavedCloudMatchSession();' `
    'Authorization success does not start the saved cloud session.'
Reject-Text $authSuccess 'authorizedServerUrl = L"http' `
    'Authorization success still falls back to a local server address.'
Reject-Text $authSuccess 'SaveCloudMatchSettings();' `
    'Authorization success still persists the returned server address.'

$authStart = Function-Body $source `
    'bool CDNFGameCaptureDlg::BeginLicenseCloudCheck' `
    'static bool DnfPostCloudJson'
Require-Text $authStart 'DisableCloudMatchForAuthorization(CString());' `
    'Starting a fresh authorization check does not stop the previous cloud session.'
Require-Text $authStart '++m_cloudAuthRequestGeneration' `
    'Authorization requests do not advance the current request generation.'
Require-Text $authStart 'requestGeneration' `
    'Authorization workers do not carry their request generation into callbacks.'
$generationIndex = $authStart.IndexOf('++m_cloudAuthRequestGeneration',
    [System.StringComparison]::Ordinal)
$localValidationIndex = $authStart.IndexOf('VerifyKey(normalized, hwid)',
    [System.StringComparison]::Ordinal)
if ($generationIndex -lt 0 -or $localValidationIndex -lt 0 -or
    $generationIndex -gt $localValidationIndex) {
    throw 'A locally rejected authorization attempt does not invalidate older in-flight callbacks.'
}

$authorizationGate = Function-Body $source `
    'bool CDNFGameCaptureDlg::HasAuthorizedCloudMatchEndpoint() const' `
    'void CDNFGameCaptureDlg::DisableCloudMatchForAuthorization'
Require-Text $authorizationGate 'time(nullptr)' `
    'The cloud authorization gate does not enforce limited-license expiry.'
Require-Text $authorizationGate '0xFFFFFFFF' `
    'The cloud authorization gate does not preserve permanent-license semantics.'

$savedStart = Function-Body $source `
    'void CDNFGameCaptureDlg::StartSavedCloudMatchSession()' `
    'void CDNFGameCaptureDlg::BeginCloudDeviceRegistration()'
Require-Text $savedStart 'HasAuthorizedCloudMatchEndpoint()' `
    'Saved cloud session restore is not authorization-gated.'

$registration = Function-Body $source `
    'void CDNFGameCaptureDlg::BeginCloudDeviceRegistration()' `
    'bool CDNFGameCaptureDlg::BeginCloudRoomRestore'
Require-Text $registration 'HasAuthorizedCloudMatchEndpoint()' `
    'Cloud device registration is not authorization-gated.'

$join = Function-Body $source `
    'void CDNFGameCaptureDlg::BeginCloudRoomJoin' `
    'void CDNFGameCaptureDlg::CancelCloudRoomJoin'
Require-Text $join 'HasAuthorizedCloudMatchEndpoint()' `
    'Cloud join is not authorization-gated.'
Reject-Text $join 'requestedServerUrl' `
    'Cloud join can still override the authorization-supplied endpoint.'

$poll = Function-Body $source `
    'void CDNFGameCaptureDlg::PollCloudMatch()' `
    'void CDNFGameCaptureDlg::SendCloudRoomPromptIfNeeded()'
Require-Text $poll 'if (!HasAuthorizedCloudMatchEndpoint())' `
    'Cloud polling and snapshot upload continue without authorization.'
Require-Text $poll 'DisableCloudMatchForAuthorization(' `
    'An expired in-memory cloud endpoint is not disconnected by the poller.'

Require-Text $source 'DnfIsCloudMatchWebAction(action) && !HasAuthorizedCloudMatchEndpoint()' `
    'Cloud Web commands are not blocked before authorization.'

Require-Text $header '#define CURRENT_VERSION L"5.0.0"' `
    'Application version is not 5.0.0.'
foreach ($needle in @(
    'FILEVERSION 5,0,0,0',
    'PRODUCTVERSION 5,0,0,0',
    'VALUE "FileVersion", "5.0.0.0"',
    'VALUE "ProductVersion", "5.0.0.0"'
)) {
    Require-Text $resources $needle "Windows version resource is outdated: $needle"
}
Require-Text $resources '5.0.0",IDC_STATIC' `
    'The About dialog still shows an outdated user-visible version.'

Write-Host 'Cloud authorization gate static checks passed.'
