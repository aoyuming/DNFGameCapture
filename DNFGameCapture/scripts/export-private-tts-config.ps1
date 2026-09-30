# Export only the credentials used by the existing fixed-voice generator.
# Never prints credentials, contacts TTS, or puts credentials in a release archive.
$ErrorActionPreference = 'Stop'
try {
    $root = Split-Path -Parent $PSScriptRoot
    $generator = Join-Path $PSScriptRoot 'generate-kill-voices.ps1'
    $apiKey = [Environment]::GetEnvironmentVariable('DOUBAO_TTS_API_KEY', 'Process')
    if ([string]::IsNullOrWhiteSpace($apiKey)) {
        $apiKey = [Environment]::GetEnvironmentVariable('DOUBAO_TTS_API_KEY', 'User')
    }
    if ([string]::IsNullOrWhiteSpace($apiKey)) { throw 'Missing API credential' }
    $source = [IO.File]::ReadAllText($generator)
    $matches = [regex]::Matches($source, "Headers\.Add\('X-Api-App-Key',\s*'([^']+)'\)")
    if ($matches.Count -ne 1) { throw 'App credential source must be unambiguous' }
    $appKey = $matches[0].Groups[1].Value
    $apiKey = $apiKey.Trim(); $appKey = $appKey.Trim()
    if ([string]::IsNullOrWhiteSpace($appKey) -or $apiKey -match '[\x00-\x1f\x7f]' -or $appKey -match '[\x00-\x1f\x7f]') { throw 'Invalid credential format' }
    $folder = Join-Path $root 'deployment-packages\private-tts-config'
    if (Test-Path -LiteralPath $folder) { throw 'Private output already exists; refusing overwrite' }
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetOwner($identity)
    $acl.SetAccessRuleProtection($true, $false)
    $inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($identity, 'FullControl', $inherit, $propagation, $allow))
    $system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($system, 'FullControl', $inherit, $propagation, $allow))
    Set-Acl -LiteralPath $folder -AclObject $acl
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText((Join-Path $folder '.gitignore'), "*`n!.gitignore`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $folder 'PRIVATE-DO-NOT-PUBLISH.txt'), "Contains private TTS credentials. Upload dnf-tts.env and import-private-tts-config.sh only to the server /root directory. Never add this directory to Git, release ZIPs or public web folders.`n", $utf8)
    [IO.File]::Copy((Join-Path $root 'deployment-packages\import-private-tts-config.sh'), (Join-Path $folder 'import-private-tts-config.sh'))
    $payload = 'DOUBAO_TTS_API_KEY=' + (ConvertTo-Json -InputObject $apiKey -Compress) + "`n" + 'DOUBAO_TTS_APP_KEY=' + (ConvertTo-Json -InputObject $appKey -Compress) + "`n"
    $file = Join-Path $folder 'dnf-tts.env'
    [IO.File]::WriteAllText($file, $payload, $utf8)
    if ([IO.File]::ReadAllText($file) -cne $payload) { throw 'Write verification failed' }
    Write-Output ('PRIVATE_CONFIG_READY=' + $file)
    Write-Output 'KEYS_PRESENT=2; USER_AND_SYSTEM_ACL_ONLY=True; GIT_IGNORED=True; TTS_API_CALLS=0'
} catch {
    Write-Error 'Private TTS configuration could not be prepared. No credential values were printed; no synthesis request was made.'
    exit 1
} finally {
    $apiKey = $null; $appKey = $null; $payload = $null; $source = $null; $matches = $null
}
