# Offline test: replace the entire HTTP function with a local PCM fixture BEFORE executing the generator.
param([string]$TestRoot = '')
$ErrorActionPreference = 'Stop'
if (-not $TestRoot) { $TestRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\voice-verify\cache-tests' }
$work = Join-Path $TestRoot ([guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work) | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'generate-kill-voices.ps1'), [Text.Encoding]::UTF8)
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$fn = $ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Request-VoicePcm'}, $true)
if (-not $fn) { throw 'Missing synthesis function' }
$mock = @'
function Request-VoicePcm($v, $p, $ApiKey) {
    $global:VoiceCacheTestCalls++
    if ($global:VoiceCacheTestFail) { throw 'Mock synthesis failure (no network)' }
    return ,([byte[]](1,0,2,0,3,0,4,0))
}
'@
$source = $source.Replace($fn.Extent.Text, $mock)
if ($source.Contains('HttpWebRequest') -or $source.Contains('openspeech.bytedance.com')) { throw 'Network code was not fully removed' }
$runner = [ScriptBlock]::Create($source)
$global:VoiceCacheTestCalls = 0; $global:VoiceCacheTestFail = $false
$oldKey = $env:DOUBAO_TTS_API_KEY; $env:DOUBAO_TTS_API_KEY = 'offline-test-not-a-real-key'
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Expect-Failure([scriptblock]$Action) { $failed = $false; try { & $Action | Out-Null } catch { $failed = $true }; Assert $failed 'Expected failure' }
try {
    $selection = @{OutputRoot=$work; Voice=@('doubao-gufeng2'); Only=@('ink-double')}
    & $runner @selection | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 0) 'Dry run called synthesis'
    Expect-Failure { & $runner @selection -CheckOnly }
    Expect-Failure { & $runner @selection -Force }
    Expect-Failure { & $runner @selection -Generate -CheckOnly }
    Expect-Failure { & $runner -OutputRoot $work -Voice 'bad-voice' -Generate }
    Assert ($global:VoiceCacheTestCalls -eq 0) 'Invalid flags called synthesis'
    & $runner @selection -Generate | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 1) 'Missing clip not generated once'
    $wav = Join-Path $work 'doubao-gufeng2\ink-double.wav'
    $hash = (Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash
    & $runner @selection -Generate | Out-Null
    & $runner @selection -CheckOnly | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 1) 'Valid cached audio regenerated'
    Remove-Item -LiteralPath "$wav.meta.json"
    & $runner @selection -Generate | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 1) 'Legacy audio regenerated'
    Assert ((Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash -eq $hash) 'Legacy audio changed'
    & $runner @selection -Generate -Force | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 2) 'Force did not regenerate'
    Assert (@(Get-ChildItem -LiteralPath (Split-Path -Parent $wav) -Filter '*.bak').Count -eq 1) 'Prior paid audio not backed up'
    $global:VoiceCacheTestFail = $true
    Expect-Failure { & $runner @selection -Generate -Force }
    Assert ((Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash -eq $hash) 'Failure overwrote valid audio'
    Assert ($global:VoiceCacheTestCalls -eq 3) 'Unexpected retries'
    $global:VoiceCacheTestFail = $false
    '{"fingerprint":"changed","audioSha256":"changed"}' | Set-Content -LiteralPath "$wav.meta.json" -Encoding UTF8
    Expect-Failure { & $runner @selection -Generate }
    Assert ($global:VoiceCacheTestCalls -eq 3) 'Stale metadata silently triggered paid synthesis'
    $lock = [IO.File]::Open((Join-Path $work '.generation.lock'), 'Open', 'ReadWrite', 'None')
    try { Expect-Failure { & $runner @selection -Generate -Force } } finally { $lock.Dispose() }
    Assert ($global:VoiceCacheTestCalls -eq 3) 'Concurrent job called synthesis'
    [IO.File]::WriteAllBytes($wav, [byte[]](0,0,0,0))
    Expect-Failure { & $runner @selection -CheckOnly }
    & $runner @selection | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 3) 'Invalid WAV caused an implicit billed request'
    & $runner @selection -Generate | Out-Null
    Assert ($global:VoiceCacheTestCalls -eq 4) 'Invalid WAV not repaired by explicit Generate'
    Write-Output 'PASS: dry-run, validation, reuse, legacy files, explicit regeneration, backup, failure preservation, metadata drift and concurrent lock. ZERO network requests.'
} finally {
    $env:DOUBAO_TTS_API_KEY = $oldKey
    Remove-Item -LiteralPath $work -Recurse -Force
    Remove-Variable VoiceCacheTestCalls,VoiceCacheTestFail -Scope Global -ErrorAction SilentlyContinue
}
