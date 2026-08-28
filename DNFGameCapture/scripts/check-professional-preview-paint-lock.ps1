param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$dialogPath = Join-Path $ProjectRoot 'DNFGameCaptureDlg.cpp'
$headerPath = Join-Path $ProjectRoot 'DNFGameCaptureDlg.h'
$dialog = Get-Content -LiteralPath $dialogPath -Raw -Encoding UTF8
$header = Get-Content -LiteralPath $headerPath -Raw -Encoding UTF8

$signature = 'void CDNFGameCaptureDlg::Draw(CDC& dc, HBITMAP previewFrame, int previewW, int previewH)'
$start = $dialog.IndexOf($signature, [System.StringComparison]::Ordinal)
if ($start -lt 0) {
    throw 'Draw must receive the immutable preview frame copied by OnPaint.'
}
$next = $dialog.IndexOf('void CDNFGameCaptureDlg::', $start + $signature.Length,
    [System.StringComparison]::Ordinal)
if ($next -lt 0) {
    throw 'Unable to locate the end of Draw.'
}
$drawBody = $dialog.Substring($start, $next - $start)

foreach ($forbidden in @('g_bmpMutex', 'm_bmp', 'm_w', 'm_h')) {
    if ($drawBody.IndexOf($forbidden, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Draw still reads shared frame state: $forbidden"
    }
}

if ($header.IndexOf('void Draw(CDC& dc, HBITMAP previewFrame, int previewW, int previewH);',
        [System.StringComparison]::Ordinal) -lt 0) {
    throw 'The Draw declaration does not expose the immutable preview frame.'
}
if ($dialog.IndexOf('Draw(memDC, previewCopy, previewW, previewH);',
        [System.StringComparison]::Ordinal) -lt 0) {
    throw 'OnPaint does not pass its copied frame to Draw.'
}

$drawCall = $dialog.IndexOf('Draw(memDC, previewCopy, previewW, previewH);',
    [System.StringComparison]::Ordinal)
$deleteCopy = $dialog.IndexOf('::DeleteObject(previewCopy);', $drawCall,
    [System.StringComparison]::Ordinal)
if ($deleteCopy -lt $drawCall) {
    throw 'OnPaint deletes the copied frame before Draw finishes using it.'
}

Write-Host 'Professional preview paint-lock checks passed.'
