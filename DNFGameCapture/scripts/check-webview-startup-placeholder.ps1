$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'WebScoreDlg.h'
$sourcePath = Join-Path $root 'WebScoreDlg.cpp'
$dialogPath = Join-Path $root 'DNFGameCaptureDlg.cpp'
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$dialog = Get-Content -LiteralPath $dialogPath -Raw -Encoding UTF8

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

Require-Text $header 'void SetWebViewLoadingState(bool loading, const wchar_t* message = nullptr);' `
    'WebView loading state API is missing.'
Require-Text $header 'm_webViewUsingFallbackDataFolder' `
    'WebView2 startup must have a recovery data folder for a busy profile.'
Require-Text $source 'DNFGameCapture-WebView2-Profile' `
    'WebView2 must use an application-specific user data folder.'
Require-Text $source 'GetTempPathW' `
    'The application-specific WebView2 data folder must be created without blocking startup.'
Require-Text $header 'CStatic m_webViewLoadingLabel;' `
    'WebView loading placeholder control is missing.'
Require-Text $header 'CBrush m_webViewBackgroundBrush;' `
    'WebView startup background brush is missing.'
Require-Text $header 'afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);' `
    'WebScoreDlg must paint its loading placeholder without a white background.'
Require-Text $header 'void ScheduleWebViewRetry(const CString& reason);' `
    'WebView initialization must have a non-blocking retry path.'
Require-Text $dialog 'm_startupBootstrapStarted' `
    'Startup work must wait for the first WebView screen or a bounded fallback.'

Require-Text $source 'SetWebViewLoadingState(true' `
    'The dialog does not show a loading state before WebView2 initialization.'
if ($source -match 'm_webViewLoadingLabel\.Create\([\s\S]*?WS_CHILD\s*\|\s*WS_VISIBLE') {
    throw 'The native startup placeholder must not be created as a full-window visible overlay.'
}
Require-Text $source 'showLoadingMessage' `
    'Loading text visibility must be gated so a stale native overlay cannot cover the scoreboard.'
Require-Text $source 'm_webViewLoadingLabel.ShowWindow(showLoadingMessage ? SW_SHOW : SW_HIDE);' `
    'Native loading text must only appear for compact failure/retry feedback.'
Require-Text $dialog 'SetWebViewLoadingState(false);' `
    'The dialog does not hide the loading state after page readiness.'
Require-Text $source 'ON_WM_CTLCOLOR()' `
    'The WebView dialog does not handle control colors.'
Require-Text $source 'm_webview->add_NavigationCompleted' `
    'WebView2 navigation completion is not tracked.'
Require-Text $source 'kWebViewRetryTimerId' `
    'WebView initialization retry timer is missing.'
Require-Text $source 'm_webViewInitInFlight' `
    'WebView initialization must prevent overlapping attempts.'
Require-Text $source 'const HRESULT environmentRequestResult' `
    'Synchronous WebView2 environment creation failures must be diagnosed.'
Require-Text $source 'const HRESULT controllerRequestResult' `
    'Synchronous WebView2 controller creation failures must be diagnosed.'
Require-Text $source 'get_CoreWebView2(&m_webview)' `
    'The WebView2 core interface result must be checked before use.'
Require-Text $source 'if (nIDEvent == kWebViewRetryTimerId)' `
    'WebView2 initialization retry timer is not handled.'
Require-Text $source 'SwitchToWebViewRecoveryDataFolder' `
    'Busy WebView2 profiles must switch to a recovery data folder.'
Require-Text $source 'HRESULT_FROM_WIN32(ERROR_BUSY)' `
    'WebView2 ERROR_BUSY must be handled explicitly.'
Require-Text $source 'ScheduleWebViewRetry(reason)' `
    'WebView2 initialization failures must schedule a retry.'
Require-Text $source 'get_WebErrorStatus' `
    'Navigation failures must expose the WebView2 error status for diagnosis.'
Require-Text $source 'm_webview->Navigate(pageUri.GetString())' `
    'The local WebView page must use the normalized file URI navigation call.'
Require-Text $source 'file:///' `
    'The local WebView page must be navigated with an explicit file URI.'
Require-Text $source 'pageUri' `
    'The local WebView page URI must be normalized before navigation.'
Require-Text $source 'SetVirtualHostNameToFolderMapping' `
    'The local WebView page must use the virtual host mapping fallback.'
Require-Text $source 'appassets.example' `
    'The local WebView virtual host name is missing.'
if ($source -notmatch 'add_NavigationCompleted[\s\S]*?if\s*\(\s*!m_webViewPageReady\s*\)\s*\{\s*SetWebViewLoadingState\(false\);') {
    throw 'Successful navigation must hide the native placeholder even if page_ready is delayed.'
}
Require-Text $source 'constexpr int kReferenceClientHeight = 480;' `
    'The compact Web height must remain the default height.'
Require-Text $source 'controller2->put_DefaultBackgroundColor' `
    'WebView2 default background color is not set.'
Require-Text $source 'return static_cast<HBRUSH>(m_webViewBackgroundBrush.GetSafeHandle());' `
    'The loading brush handle must be explicitly converted to HBRUSH.'
Require-Text $source 'const COREWEBVIEW2_COLOR backgroundColor = { 0xFF, 0x14, 0x18, 0x1E };' `
    'The WebView2 background color must use the SDK color structure.'

Require-Text $dialog 'kStartupBootstrapTimerId' `
    'The startup bootstrap fallback timer is missing.'
Require-Text $dialog 'SetTimer(kStartupBootstrapTimerId, 1500, nullptr);' `
    'Startup work must be deferred long enough for the first WebView screen.'
Require-Text $dialog 'm_startupBootstrapStarted.exchange(true' `
    'Startup bootstrap must be guarded against page_ready/fallback races.'

$constructor = [regex]::Match($dialog,
    'CDNFGameCaptureDlg::CDNFGameCaptureDlg\(\)[\s\S]*?void CDNFGameCaptureDlg::SendStartupProgress').Value
if ($constructor -match 'PostMessage\(WM_STARTUP_STAGE,\s*0,\s*0\)') {
    throw 'The constructor must not start heavy startup work before the first WebView screen.'
}

Require-Text $dialog 'm_deferredDisplayWindowsPending' `
    'Display WebView windows must be deferred until the primary page is ready.'
Require-Text $dialog 'void CDNFGameCaptureDlg::TryOpenDeferredDisplayWindows()' `
    'The deferred display window opener is missing.'
Require-Text $dialog 'TryOpenDeferredDisplayWindows();' `
    'The deferred display windows are not retried after the primary page handshake.'

$startupStageOne = [regex]::Match($dialog,
    'case 1:\s*[\s\S]*?case 2:').Value
if ($startupStageOne -match 'OpenKillDisplayWindow\(\)' -or
    $startupStageOne -match 'OpenKeyDisplayWindow\(\)') {
    throw 'Startup stage 1 must not construct secondary WebView windows before the primary page is ready.'
}

$pageReady = [regex]::Match($dialog, 'action\s*==\s*"page_ready"')
if (-not $pageReady.Success) {
    throw 'The C++ page_ready bridge handler is missing.'
}

Write-Host 'WebView startup placeholder static checks passed.'
