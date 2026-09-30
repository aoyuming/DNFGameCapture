# Fixed phrases are a permanent audio asset, NOT an on-demand paid API cache.
# Default: inspect/reuse only. -CheckOnly fails on missing/invalid clips without networking.
# Paid synthesis requires explicit -Generate; valid existing WAVs are always reused unless -Force.
# Run from any directory. Credentials stay in environment variables, never in the audio archive.
param([string[]]$Only, [string[]]$Voice, [switch]$Generate, [switch]$Force,
      [switch]$CheckOnly, [string]$OutputRoot = '')
$ErrorActionPreference = 'Stop'
if ($Force -and -not $Generate) { throw '-Force requires explicit -Generate (paid API).' }
if ($CheckOnly -and ($Generate -or $Force)) { throw '-CheckOnly cannot generate or overwrite audio.' }
if (-not $OutputRoot) { $OutputRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'web前端\voice' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$voices = @(
  @{ id = "doubao-meihuo";   speaker = "zh_female_jiaochuannv_uranus_bigtts";  resource = "seed-tts-2.0" },  # 魅惑女声
  @{ id = "doubao-gufeng2";  speaker = "zh_female_gufengshaoyu_uranus_bigtts"; resource = "seed-tts-2.0" },  # 古风雅韵 2.0
  @{ id = "doubao-gufeng1";  speaker = "zh_female_gufengshaoyu_mars_bigtts";   resource = "seed-tts-1.0" },  # 古风雅韵 1.0
  @{ id = "doubao-wuzetian"; speaker = "zh_female_wuzetian_mars_bigtts";       resource = "seed-tts-1.0" }   # 武则天 1.0
)
$phrases = @(@{ key = "double"; text = "双杀！"; lang = "zh" }, @{ key = "triple"; text = "三杀！"; lang = "zh" }, @{ key = "first"; text = "一血！"; lang = "zh" }, @{ key = "shutdown"; text = "终结！"; lang = "zh" }, @{ key = "revenge"; text = "复仇！"; lang = "zh" }, @{ key = "ink-double"; text = "双斩！"; lang = "zh" }, @{ key = "ink-triple"; text = "三斩！"; lang = "zh" }, @{ key = "ink-first"; text = "首胜！"; lang = "zh" }, @{ key = "ink-shutdown"; text = "断其锋！"; lang = "zh" }, @{ key = "ink-revenge"; text = "雪耻！"; lang = "zh" }, @{ key = "pixel-double"; text = "Double!"; lang = "" }, @{ key = "pixel-triple"; text = "Triple!"; lang = "" }, @{ key = "pixel-first"; text = "First blood!"; lang = "" }, @{ key = "pixel-shutdown"; text = "Stopped!"; lang = "" }, @{ key = "pixel-revenge"; text = "Revenge!"; lang = "" }, @{ key = "ak-allkill"; text = "AK！一人团灭！"; lang = "" }, @{ key = "ak-ace"; text = "ACE！一人团灭！"; lang = "" }, @{ key = "ak-ink"; text = "全歼！一人破阵！"; lang = "zh" }, @{ key = "ak-pixel"; text = "AK! Perfect!"; lang = "" }, @{ key = "ak-inferno"; text = "AK！焚尽一切！"; lang = "" }, @{ key = "ak-frost"; text = "AK！冰封全场！"; lang = "" }, @{ key = 'victory'; text = '胜利！'; lang = 'zh' }, @{ key = 'ink-victory'; text = '大捷！凯歌还！'; lang = 'zh' }, @{ key = 'victory-en'; text = 'Victory!'; lang = 'en' }, @{ key = 'victory-neon'; text = 'Victory! Mission complete!'; lang = 'en' }, @{ key = 'victory-inferno'; text = '烈焰凯旋！'; lang = 'zh' }, @{ key = 'victory-broadcast'; text = '比赛胜利！恭喜！'; lang = 'zh' }, @{ key = 'victory-frost'; text = '冰封全场，凯旋而归！'; lang = 'zh' })

foreach ($id in $Voice) { if ($id -notin $voices.id) { throw "Unknown voice: $id" } }
foreach ($key in $Only) { if ($key -notin $phrases.key) { throw "Unknown phrase: $key" } }

function Test-VoiceWav([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { return $false }
    $stream = $null; $reader = $null
    try {
        $stream = [IO.File]::OpenRead($Path); $reader = New-Object IO.BinaryReader($stream)
        if ($stream.Length -lt 46 -or $stream.Length -gt 10MB) { return $false }
        if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'RIFF') { return $false }
        $end = [long]$reader.ReadUInt32() + 8
        if ($end -ne $stream.Length -or [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'WAVE') { return $false }
        $fmt = $false; $data = $false
        while ($stream.Position + 8 -le $end) {
            $chunk = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)); $size = [long]$reader.ReadUInt32()
            $next = $stream.Position + $size + ($size % 2)
            if ($next -gt $end) { return $false }
            if ($chunk -eq 'fmt ') {
                if ($size -lt 16) { return $false }
                $encoding = $reader.ReadUInt16(); $channels = $reader.ReadUInt16(); $rate = $reader.ReadUInt32()
                $byteRate = $reader.ReadUInt32(); $align = $reader.ReadUInt16(); $bits = $reader.ReadUInt16()
                $fmt = $encoding -eq 1 -and $channels -eq 1 -and $rate -eq 24000 -and $bits -eq 16 -and $align -eq 2 -and $byteRate -eq 48000
            } elseif ($chunk -eq 'data') {
                if ($size -lt 2 -or $size % 2 -ne 0) { return $false }
                $pcm = $reader.ReadBytes([int]$size); $peak = 0
                for ($i = 0; $i -lt $pcm.Length; $i += 2) { $peak = [Math]::Max($peak, [Math]::Abs([int][BitConverter]::ToInt16($pcm, $i))) }
                $data = $peak -gt 0
            }
            $stream.Position = $next
        }
        return $fmt -and $data -and $stream.Position -eq $end
    } catch { return $false } finally { if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() } }
}
function Get-VoiceFingerprint($v, $p) {
    $spec = [ordered]@{ schema = 1; speaker = $v.speaker; resource = $v.resource; text = $p.text;
        language = $p.lang; format = 'pcm'; sampleRate = 24000; normalize = 'peak29000-max4' } | ConvertTo-Json -Compress
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($spec)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Get-VoiceCacheStatus($Path, $Fingerprint) {
    if (-not (Test-VoiceWav $Path)) { return 'missing-or-invalid' }
    if (-not [IO.File]::Exists("$Path.meta.json")) { return 'legacy-valid' }
    try {
        $meta = Get-Content -LiteralPath "$Path.meta.json" -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($meta.fingerprint -eq $Fingerprint -and $meta.audioSha256 -eq (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash) { return 'valid' }
    } catch { }
    return 'stale-metadata'
}
function Write-VoiceWav([string]$Path, [byte[]]$Pcm) {
    if ($Pcm.Length -lt 2 -or $Pcm.Length % 2 -ne 0) { throw 'Empty/truncated PCM; existing audio retained.' }
    $peak = 0
    for ($i = 0; $i -lt $Pcm.Length; $i += 2) { $peak = [Math]::Max($peak, [Math]::Abs([int][BitConverter]::ToInt16($Pcm, $i))) }
    if ($peak -eq 0) { throw 'Silent PCM; existing audio retained.' }
    $gain = [Math]::Min(4.0, 29000.0 / $peak)
    for ($i = 0; $i -lt $Pcm.Length; $i += 2) {
        $v = [int][Math]::Round([BitConverter]::ToInt16($Pcm, $i) * $gain)
        $b = [BitConverter]::GetBytes([int16][Math]::Max(-32768, [Math]::Min(32767, $v)))
        $Pcm[$i] = $b[0]; $Pcm[$i + 1] = $b[1]
    }
    $stream = [IO.File]::Create($Path); $w = New-Object IO.BinaryWriter($stream)
    try {
        $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $Pcm.Length))
        $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $w.Write([int]16)
        $w.Write([int16]1); $w.Write([int16]1); $w.Write([int]24000); $w.Write([int]48000)
        $w.Write([int16]2); $w.Write([int16]16); $w.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $w.Write([int]$Pcm.Length); $w.Write($Pcm)
    } finally { $w.Dispose() }
}
function Request-VoicePcm($v, $p, $ApiKey) {
    $body = @{ user = @{ uid = 'dnf-game-capture' }; req_params = @{ text = $p.text; speaker = $v.speaker;
        audio_params = @{ format = 'pcm'; sample_rate = 24000; enable_timestamp = $false };
        additions = $(if ($p.lang) { @{ explicit_language = $p.lang } | ConvertTo-Json -Compress } else { '{}' }) } } | ConvertTo-Json -Depth 6
    $req = [Net.HttpWebRequest]::Create('https://openspeech.bytedance.com/api/v3/tts/unidirectional')
    $req.Method = 'POST'; $req.ContentType = 'application/json'; $req.Timeout = 20000; $req.ReadWriteTimeout = 30000
    $req.Headers.Add('X-Api-App-Key', 'aGjiRDfUWi'); $req.Headers.Add('X-Api-Resource-Id', $v.resource)
    $req.Headers.Add('X-Api-Request-Id', [guid]::NewGuid().ToString()); $req.Headers.Add('X-Api-Key', $ApiKey)
    $bytes = [Text.Encoding]::UTF8.GetBytes($body); $s = $req.GetRequestStream()
    try { $s.Write($bytes, 0, $bytes.Length) } finally { $s.Dispose() }
    $resp = $req.GetResponse(); $reader = New-Object IO.StreamReader($resp.GetResponseStream(), [Text.Encoding]::UTF8)
    $all = New-Object IO.MemoryStream; $finished = $false
    try {
        # The service returns one JSON object per line; nested usage/sentence objects are valid.
        while ($null -ne ($line = $reader.ReadLine())) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $o = $line | ConvertFrom-Json
            if ($null -eq $o.code -or [long]$o.code -notin @(0, 20000000)) { throw "TTS failed (code $($o.code)); existing audio retained." }
            if ($o.data) { $chunk = [Convert]::FromBase64String($o.data); $all.Write($chunk, 0, $chunk.Length) }
            if ($all.Length -gt 10MB) { throw 'Unexpected oversized audio response.' }
            if ([long]$o.code -eq 20000000) { $finished = $true; break }
        }
        if (-not $finished -or $all.Length -lt 2) { throw 'Incomplete TTS response; existing audio retained. No automatic retry.' }
        return ,$all.ToArray()
    } finally { $all.Dispose(); $reader.Dispose(); $resp.Close() }
}

$lock = $null; $reused = 0; $legacy = 0; $pending = 0; $generated = 0
try {
    if ($Generate) {
        [IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
        # Held across cache checks and requests: two generation jobs cannot bill for the same missing clip.
        $lock = [IO.File]::Open((Join-Path $OutputRoot '.generation.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    }
    foreach ($v in ($voices | Where-Object { -not $Voice -or $Voice -contains $_.id })) {
        foreach ($p in ($phrases | Where-Object { -not $Only -or $Only -contains $_.key })) {
            $out = Join-Path (Join-Path $OutputRoot $v.id) ($p.key + '.wav')
            $fingerprint = Get-VoiceFingerprint $v $p
            $status = Get-VoiceCacheStatus $out $fingerprint
            if (-not $Force -and $status -in @('valid', 'legacy-valid')) {
                $reused++; if ($status -eq 'legacy-valid') { $legacy++ }
                Write-Output "REUSE $($v.id)/$($p.key) ($status)"; continue
            }
            # Changed text/model metadata needs an intentional overwrite, not an automatic billed request.
            if (-not $Generate -or ($status -eq 'stale-metadata' -and -not $Force)) {
                $pending++; Write-Output "NEEDS-REVIEW $($v.id)/$($p.key) ($status)"; continue
            }
            $apiKey = [Environment]::GetEnvironmentVariable('DOUBAO_TTS_API_KEY', 'Process')
            if (-not $apiKey) { $apiKey = [Environment]::GetEnvironmentVariable('DOUBAO_TTS_API_KEY', 'User') }
            if ([string]::IsNullOrWhiteSpace($apiKey)) { throw 'DOUBAO_TTS_API_KEY is required only for explicit paid generation.' }
            [IO.Directory]::CreateDirectory((Split-Path -Parent $out)) | Out-Null
            $tmp = "$out.$([guid]::NewGuid().ToString('N')).tmp"
            try {
                Write-Output "GENERATE (paid) $($v.id)/$($p.key)"
                $pcm = Request-VoicePcm $v $p $apiKey
                Write-VoiceWav $tmp $pcm
                if (-not (Test-VoiceWav $tmp)) { throw 'Generated WAV validation failed; existing audio retained.' }
                if ([IO.File]::Exists($out)) {
                    # Keep the previous paid clip, even on explicit regeneration.
                    $backup = "$out.$([DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')).bak"
                    [IO.File]::Replace($tmp, $out, $backup)
                } else { [IO.File]::Move($tmp, $out) }
                $meta = @{ fingerprint = $fingerprint; audioSha256 = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash }
                $meta | ConvertTo-Json | Set-Content -LiteralPath "$out.meta.json" -Encoding UTF8
                $generated++
            } finally { if ([IO.File]::Exists($tmp)) { [IO.File]::Delete($tmp) } }
        }
    }
} finally { if ($lock) { $lock.Dispose() } }
Write-Output "SUMMARY reused=$reused legacy=$legacy needsReview=$pending generated=$generated"
if ($legacy) { Write-Output 'Legacy WAVs validated and kept unchanged; their historical speaker/text metadata is not available.' }
if ($pending) {
    if ($CheckOnly -or $Generate) { throw 'Some clips need review. Missing clips require -Generate; changed metadata additionally requires -Force. No automatic retries.' }
    Write-Output 'Dry run only. No API calls. Use -Generate explicitly for missing audio; add -Force only for deliberate regeneration.'
}
