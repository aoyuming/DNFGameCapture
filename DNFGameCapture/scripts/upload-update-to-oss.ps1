param(
    [string]$ReleaseDir = "",
    [string]$UpdateFile = "",
    [string]$ConfigPath = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$nodeScript = Join-Path $scriptDir "upload-update-to-oss.js"

if (-not (Test-Path -LiteralPath $nodeScript)) {
    throw "Missing Node helper: $nodeScript"
}

$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node) {
    throw "Node.js was not found in PATH."
}

$nodeArgs = @($nodeScript)

if ($ReleaseDir) {
    $nodeArgs += @("--release-dir", $ReleaseDir)
}

if ($UpdateFile) {
    $nodeArgs += @("--update-file", $UpdateFile)
}

if ($ConfigPath) {
    $nodeArgs += @("--config-path", $ConfigPath)
}

if ($DryRun) {
    $nodeArgs += "--dry-run"
}

& $node.Source @nodeArgs
exit $LASTEXITCODE
