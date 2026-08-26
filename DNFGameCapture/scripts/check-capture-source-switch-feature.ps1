$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dialog = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw -Encoding UTF8
$dialogHeader = Get-Content -LiteralPath (Join-Path $root 'DNFGameCaptureDlg.h') -Raw -Encoding UTF8
$camera = Get-Content -LiteralPath (Join-Path $root 'CameraCapture.cpp') -Raw -Encoding UTF8
$wgc = Get-Content -LiteralPath (Join-Path $root 'WGCCapture.cpp') -Raw -Encoding UTF8

foreach ($needle in @('Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM)', 'Shutdown()', 'm_stopRequested')) {
    if (-not $camera.Contains($needle)) { throw "Camera cancellation is missing: $needle" }
}
foreach ($needle in @('FrameArrived(m_frameArrivedToken)', 'm_callbacksInFlight', 'm_callbackCv')) {
    if (-not $wgc.Contains($needle)) { throw "WGC callback shutdown guard is missing: $needle" }
}
if ($wgc -notmatch '(?s)FrameArrived\(m_frameArrivedToken\).*?m_callbackCv\.wait.*?m_session\.Close') {
    throw 'WGC resources must stay alive until in-flight frame callbacks have exited.'
}
if ($dialog -match '(?s)void CDNFGameCaptureDlg::OnCbnCloseupTargetWindow\(\).*?m_pCamera->StopCapture\(\).*?delete m_pCamera') {
    throw 'Target closeup still blocks the UI thread while stopping the camera.'
}
if ($dialog -match '(?s)void CDNFGameCaptureDlg::SafeDeleteWGC\(\).*?Sleep\(500\).*?detach\(\)') {
    throw 'WGC still uses arbitrary delayed detached destruction.'
}
foreach ($needle in @('QueueCaptureSourceSwitch', 'm_captureSwitchGeneration', 'm_captureSwitchPending')) {
    if (-not $dialog.Contains($needle)) { throw "Serialized capture switching is missing: $needle" }
}
foreach ($needle in @('m_captureSwitchWorker', 'm_captureSwitchQueue', 'm_captureSwitchCv')) {
    if (-not $dialogHeader.Contains($needle)) { throw "Capture switch worker state is missing: $needle" }
}
$queueStart = $dialog.IndexOf('void CDNFGameCaptureDlg::QueueCaptureSourceSwitch()')
$queueEnd = $dialog.IndexOf('LRESULT CDNFGameCaptureDlg::OnCaptureSourceSwitchDone', $queueStart)
$queueBody = if ($queueStart -ge 0 -and $queueEnd -gt $queueStart) {
    $dialog.Substring($queueStart, $queueEnd - $queueStart)
} else {
    ''
}
if ($queueBody.Contains('.detach()')) {
    throw 'Capture source teardown must use the serial worker instead of detached threads.'
}
if ($dialog -notmatch '(?s)void CDNFGameCaptureDlg::OnPaint\(\).*?lock.*?CopyImage.*?StretchBlt') {
    throw 'Professional preview does not copy the frame before drawing.'
}
Write-Host 'Capture source switching static checks passed.' -ForegroundColor Green
