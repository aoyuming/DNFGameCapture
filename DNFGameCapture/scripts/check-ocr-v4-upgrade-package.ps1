$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$script = Join-Path $PSScriptRoot 'upgrade-ocr-v4.ps1'
if (-not (Test-Path -LiteralPath $script -PathType Leaf)) {
    throw 'OCR v4 upgrade script is missing.'
}
$text = Get-Content -LiteralPath $script -Raw
foreach ($needle in @(
    'ch_PP-OCRv4_det_infer',
    'ch_PP-OCRv4_rec_infer',
    'config_chinese.txt',
    'Umi-OCR.exe',
    'DNFGameCapture',
    'PaddleOCR-json',
    'enable_mkldnn=false',
    'backup-v3'
)) {
    if ($text -notmatch [regex]::Escape($needle)) {
        throw "OCR v4 upgrade script is missing required behavior: $needle"
    }
}
Write-Host 'OCR v4 upgrade package checks passed.'
