$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$webRoot = Join-Path $root ('web' + [char]0x524D + [char]0x7AEF)

function Read-RequiredFile([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $relativePath"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Require-Text([string]$content, [string]$needle, [string]$message) {
    if (-not $content.Contains($needle)) {
        throw $message
    }
}

function Reject-Text([string]$content, [string]$needle, [string]$message) {
    if ($content.Contains($needle)) {
        throw $message
    }
}

function Read-WebFile([string]$name) {
    $path = Join-Path $webRoot $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required web file: $name"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

$index = Read-WebFile 'index.html'
$main = Read-WebFile 'main.js'
$style = Read-WebFile 'style.css'
$keysHtml = Read-WebFile 'keys.html'
$keysJs = Read-WebFile 'keys.js'
$keysCss = Read-WebFile 'keys.css'
$dialogHeader = Read-RequiredFile 'DNFGameCaptureDlg.h'
$dialogSource = Read-RequiredFile 'DNFGameCaptureDlg.cpp'
$keyWindowHeader = Read-RequiredFile 'KeyDisplayDlg.h'
$keyWindowSource = Read-RequiredFile 'KeyDisplayDlg.cpp'
$project = Read-RequiredFile 'DNFGameCapture.vcxproj'

Require-Text $index 'id="btn-key-mapping"' 'More menu is missing the key mapping entry.'
Require-Text $index 'id="key-mapping-overlay"' 'Key mapping settings panel is missing.'
Require-Text $index 'id="key-mapping-grid"' 'Key mapping panel is missing the 2x7 slot grid.'
Require-Text $index 'id="btn-key-reset-defaults"' 'Key mapping panel cannot restore default keys.'
Require-Text $main 'cmd_set_key_mapping_settings' 'Web settings are not persisted through C++.'
Require-Text $main 'cmd_toggle_key_display' 'Web cannot toggle the key display window.'
Require-Text $main 'const KEY_MAPPING_SLOT_COUNT = 14' 'Web key mapping is not fixed to 14 slots.'
Require-Text $main 'const KEY_MAPPING_DEFAULT_LABELS' 'Default key labels are missing.'
Require-Text $main "'Q', 'W', 'E', 'R', 'T', 'Y', 'Ctrl'" 'Default first-row keys are missing.'
Require-Text $main "'A', 'S', 'D', 'F', 'G', 'H', 'Alt'" 'Default second-row keys are missing.'
Require-Text $style '.key-mapping-grid' 'Key mapping grid styles are missing.'
Require-Text $style 'repeat(7, minmax(0, 1fr))' 'Key mapping settings are not using seven columns.'
Reject-Text $index 'btn-key-region-calibrate' 'Manual key-region calibration button still exists.'
Reject-Text $index 'btn-key-read' 'Key OCR button still exists.'
Reject-Text $index 'btn-key-obs-copy' 'Key display URL button still exists.'
Reject-Text $index 'key-ocr-actions' 'Key OCR candidate controls still exist.'
Reject-Text $main 'cmd_begin_key_region_calibration' 'Manual key-region calibration command still exists.'
Reject-Text $main 'cmd_read_key_mapping' 'Key OCR command still exists.'
Reject-Text $main 'cmd_copy_key_obs_url' 'Key display URL command still exists.'
Reject-Text $main 'keyMappingOcrCandidates' 'Key OCR candidate state still exists.'

Require-Text $keysHtml 'id="key-grid"' 'Transparent key display grid is missing.'
Require-Text $keysJs '/api/key-mapping-state' 'Key display does not poll its state endpoint.'
Require-Text $keysJs 'activeMask' 'Key display does not consume the active key mask.'
Require-Text $keysJs 'const SLOT_COUNT = 14' 'Key display is not fixed to 14 slots.'
Require-Text $keysCss 'background: transparent' 'Key display page is not explicitly transparent.'
Require-Text $keysCss 'repeat(7, minmax(0, 1fr))' 'Key display is not using seven columns.'

Require-Text $dialogHeader 'KeyMappingSlot' 'C++ key mapping slot state is missing.'
Require-Text $dialogHeader 'm_keyMappingActiveMask' 'C++ active key mask is missing.'
Require-Text $dialogHeader 'KEY_MAPPING_SLOT_COUNT = 14' 'C++ key mapping is not fixed to 14 slots.'
Require-Text $dialogSource 'GetAsyncKeyState' 'C++ key polling is missing.'
Require-Text $dialogSource '/api/key-mapping-state' 'HTTP key mapping state route is missing.'
Require-Text $dialogSource 'KeyMappingSlots' 'Key mapping slot persistence is missing.'
Require-Text $dialogSource 'hasStoredVk ? (UINT)_wtoi(vkText) : KEY_MAPPING_DEFAULTS[i].vk' 'Stored zero keys are not preserved as explicitly cleared slots.'
Reject-Text $dialogSource 'migrateLegacyEmptyLayout' 'Legacy migration may overwrite explicitly cleared key slots.'
Reject-Text $dialogHeader 'WM_KEY_MAPPING_OCR_RESULT' 'Key OCR window message still exists.'
Reject-Text $dialogHeader 'm_keyMappingRegion' 'Key-region state still exists.'
Reject-Text $dialogSource 'EnterKeyRegionCalibrationMode' 'Manual key-region calibration still exists.'
Reject-Text $dialogSource 'BeginKeyMappingOcr' 'Key OCR implementation still exists.'
Reject-Text $dialogSource 'cmd_copy_key_obs_url' 'Key display URL command still exists in C++.'
Reject-Text $dialogSource 'KEY_DISPLAY_OBS_URL' 'Public key display URL constant still exists.'

Require-Text $keyWindowHeader 'class CKeyDisplayDlg' 'Key display window class is missing.'
Require-Text $keyWindowSource 'DNF Key Display - DNF' 'Key display window title is missing.'
Require-Text $keyWindowSource 'http://127.0.0.1:18777/keys.html?shell=1' 'Key display shell URL is missing.'
Reject-Text $keyWindowSource 'WS_THICKFRAME' 'Key display window still has a native resize frame.'
Require-Text $project '<ClCompile Include="KeyDisplayDlg.cpp" />' 'Key display source is not in the project.'
Require-Text $project '<ClInclude Include="KeyDisplayDlg.h" />' 'Key display header is not in the project.'

Write-Host 'Key mapping feature static checks passed.' -ForegroundColor Green
