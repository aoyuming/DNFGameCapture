$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$headerPath = Join-Path $root 'DNFGameCaptureDlg.h'

foreach ($path in @($sourcePath, $headerPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing endpoint refresh source file: $path"
    }
}

$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
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

foreach ($needle in @(
    'bool m_cloudMatchUsingLicenseLease = false;',
    'bool m_cloudMatchLeaseRefreshAttempted = false;',
    'bool m_cloudMatchLeaseRefreshInFlight = false;',
    'ULONGLONG m_cloudMatchLeaseDisconnectedSinceTick = 0;',
    'bool BeginLicenseLeaseEndpointRefresh();'
)) {
    Require-Text $header $needle "Missing lease endpoint refresh state: $needle"
}
Require-Text $header 'static CString CheckCloudBinding(CString key, CString hwid,' `
    'Detached authorization workers must use a static cloud request helper.'

$leaseActivation = Function-Body $source `
    'bool CDNFGameCaptureDlg::TryActivateFromLicenseLease' `
    'bool CDNFGameCaptureDlg::BeginLicenseCloudCheck'
foreach ($needle in @(
    'm_cloudMatchUsingLicenseLease = true;',
    'm_cloudMatchLeaseRefreshAttempted = false;',
    'm_cloudMatchLeaseRefreshInFlight = false;',
    'm_cloudMatchLeaseDisconnectedSinceTick = ::GetTickCount64();'
)) {
    Require-Text $leaseActivation $needle "Lease activation does not arm endpoint refresh: $needle"
}

$refresh = Function-Body $source `
    'bool CDNFGameCaptureDlg::BeginLicenseLeaseEndpointRefresh()' `
    'bool CDNFGameCaptureDlg::BeginLicenseCloudCheck'
foreach ($needle in @(
    'DnfLoadProtectedLicenseLease(lease)',
    'DnfValidateLicenseLease(lease,',
    'm_cloudMatchLeaseRefreshAttempted = true;',
    'm_cloudMatchLeaseRefreshInFlight = true;',
    'CheckCloudBinding(',
    'leaseEndpointRefresh',
    'WM_UPDATE_AUTH_TIME',
    'WM_CLOUD_AUTH_FAIL'
)) {
    Require-Text $refresh $needle "Lease endpoint refresh flow is incomplete: $needle"
}
if ($refresh.Contains('std::thread([this,')) {
    throw 'The detached endpoint refresh worker must not capture the dialog object.'
}

$authFailure = Function-Body $source `
    'LRESULT CDNFGameCaptureDlg::OnCloudAuthFail' `
    'LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime'
$preserveIndex = $authFailure.IndexOf('authFailure->leaseEndpointRefresh',
    [System.StringComparison]::Ordinal)
$invalidateIndex = $authFailure.IndexOf('m_bIsAuthValid = false;',
    [System.StringComparison]::Ordinal)
if ($preserveIndex -lt 0 -or $invalidateIndex -lt 0 -or
    $preserveIndex -gt $invalidateIndex) {
    throw 'A failed endpoint refresh can invalidate an otherwise valid five-day lease.'
}
$preserveBody = $authFailure.Substring($preserveIndex,
    [Math]::Min(900, $authFailure.Length - $preserveIndex))
foreach ($needle in @('m_cloudMatchLeaseRefreshInFlight = false;', 'return 0;')) {
    Require-Text $preserveBody $needle `
        "Endpoint refresh failure does not preserve and exit safely: $needle"
}

$authSuccess = Function-Body $source `
    'LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime' `
    'bool CDNFGameCaptureDlg::VerifyKey'
$invalidRefreshIndex = $authSuccess.IndexOf(
    'if (authSuccess->leaseEndpointRefresh && !serverUrlValid)',
    [System.StringComparison]::Ordinal)
$authMutationIndex = $authSuccess.IndexOf('m_cloudExpireTime = cloudTime;',
    [System.StringComparison]::Ordinal)
if ($invalidRefreshIndex -lt 0 -or $authMutationIndex -lt 0 -or
    $invalidRefreshIndex -gt $authMutationIndex) {
    throw 'An invalid successful refresh response can replace the active lease before validation.'
}
$invalidRefreshBody = $authSuccess.Substring($invalidRefreshIndex,
    $authMutationIndex - $invalidRefreshIndex)
foreach ($needle in @('const CString refreshLog =',
        'BroadcastStateToWeb();', 'return 0;')) {
    Require-Text $invalidRefreshBody $needle `
        "Invalid refresh response is not preserved safely: $needle"
}
foreach ($forbidden in @('DnfClearProtectedLicenseLease();',
        'DisableCloudMatchForAuthorization(')) {
    if ($invalidRefreshBody.Contains($forbidden)) {
        throw "Invalid refresh response performs destructive authorization cleanup: $forbidden"
    }
}
$refreshLeaseSaveIndex = $authSuccess.IndexOf(
    'DnfSaveProtectedLicenseLease(refreshedLease)',
    [System.StringComparison]::Ordinal)
if ($refreshLeaseSaveIndex -lt 0 -or
    $refreshLeaseSaveIndex -gt $authMutationIndex) {
    throw 'A refreshed endpoint can be activated before its encrypted lease is saved.'
}
$refreshLeaseSaveBody = $authSuccess.Substring($refreshLeaseSaveIndex,
    $authMutationIndex - $refreshLeaseSaveIndex)
foreach ($needle in @('return 0;', 'BroadcastStateToWeb();')) {
    Require-Text $refreshLeaseSaveBody $needle `
        "A failed refreshed-lease save does not preserve the active endpoint: $needle"
}

$poll = Function-Body $source `
    'void CDNFGameCaptureDlg::PollCloudMatch()' `
    'void CDNFGameCaptureDlg::SendCloudRoomPromptIfNeeded()'
foreach ($needle in @(
    'DnfShouldRefreshLicenseEndpoint(',
    'BeginLicenseLeaseEndpointRefresh()',
    'm_cloudMatchLeaseDisconnectedSinceTick'
)) {
    Require-Text $poll $needle "Cloud polling does not drive lease endpoint refresh: $needle"
}

Write-Host 'License endpoint refresh static checks passed.' -ForegroundColor Green
