param(
    [string]$InstallDir = ''
)

$ErrorActionPreference = 'Stop'
$sourceModels = Join-Path $PSScriptRoot 'ocr-v4-models'

function Test-OcrInstall([string]$path) {
    return (Test-Path -LiteralPath (Join-Path $path 'Umi-OCR.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $path 'UmiOCR-data\plugins\win7_x64_PaddleOCR-json\models') -PathType Container)
}

if (-not (Test-Path -LiteralPath $sourceModels -PathType Container)) {
    throw "Missing bundled v4 models: $sourceModels"
}

if (-not $InstallDir) {
    $client = Get-Process -Name 'DNFGameCapture' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($client) {
        try { $InstallDir = Split-Path -Parent $client.MainModule.FileName } catch { }
    }
}
if (-not $InstallDir -and (Test-OcrInstall $PSScriptRoot)) {
    $InstallDir = $PSScriptRoot
}
if (-not $InstallDir) {
    $InstallDir = Read-Host 'Enter the DNFGameCapture install directory'
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir.Trim().Trim('"'))
if (-not (Test-OcrInstall $InstallDir)) {
    throw "Valid DNFGameCapture install directory not found: $InstallDir"
}

$pluginDir = Join-Path $InstallDir 'UmiOCR-data\plugins\win7_x64_PaddleOCR-json'
$modelsDir = Join-Path $pluginDir 'models'
$backupDir = Join-Path $modelsDir ('backup-v3-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
foreach ($name in @('ch_PP-OCRv3_det_infer', 'ch_PP-OCRv3_rec_infer', 'config_chinese.txt')) {
    $old = Join-Path $modelsDir $name
    if (Test-Path -LiteralPath $old) {
        Copy-Item -LiteralPath $old -Destination $backupDir -Recurse -Force
    }
}
$settingsPath = Join-Path $InstallDir 'UmiOCR-data\.settings'
if (Test-Path -LiteralPath $settingsPath) {
    Copy-Item -LiteralPath $settingsPath -Destination $backupDir -Force
}

Get-Process -Name 'DNFGameCapture', 'Umi-OCR', 'PaddleOCR-json' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
Copy-Item -LiteralPath (Join-Path $sourceModels 'ch_PP-OCRv4_det_infer') -Destination $modelsDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $sourceModels 'ch_PP-OCRv4_rec_infer') -Destination $modelsDir -Recurse -Force

$config = @"
# Chinese PP-OCRv4
det_model_dir models/ch_PP-OCRv4_det_infer
cls_model_dir models/ch_ppocr_mobile_v2.0_cls_infer
rec_model_dir models/ch_PP-OCRv4_rec_infer
rec_char_dict_path models/dict_chinese.txt
"@
[IO.File]::WriteAllText((Join-Path $modelsDir 'config_chinese.txt'), $config, [Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath $settingsPath) {
    $settings = [IO.File]::ReadAllText($settingsPath)
    $settingLine = 'ocr.win7_x64_PaddleOCR-json.enable_mkldnn=false'
    if ($settings -match '(?m)^ocr\.win7_x64_PaddleOCR-json\.enable_mkldnn=.*$') {
        $settings = $settings -replace '(?m)^ocr\.win7_x64_PaddleOCR-json\.enable_mkldnn=.*$', $settingLine
    } else {
        $settings += [Environment]::NewLine + $settingLine + [Environment]::NewLine
    }
    [IO.File]::WriteAllText($settingsPath, $settings, [Text.UTF8Encoding]::new($false))
}

foreach ($name in @('ch_PP-OCRv4_det_infer', 'ch_PP-OCRv4_rec_infer')) {
    if (-not (Test-Path -LiteralPath (Join-Path $modelsDir $name) -PathType Container)) {
        throw "Incomplete v4 model installation: $name"
    }
}
Write-Host "OCR upgraded to PP-OCRv4. Old model backup: $backupDir"
