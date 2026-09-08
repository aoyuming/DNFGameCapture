$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Get-Content (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw -Encoding UTF8
$header = Get-Content (Join-Path $root 'DNFGameCaptureDlg.h') -Raw -Encoding UTF8
$project = Get-Content (Join-Path $root 'DNFGameCapture.vcxproj') -Raw
function Assert-Contains($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}
Assert-Contains $source 'StartPlayerLibrary\(\);' 'Startup must initialize the background library store.'
Assert-Contains $source 'CaptureResetMatchLibraryBaseline\(\)' 'Reset must remember match-only IDs so a later pull/save cannot resurrect cleared data.'
Assert-Contains $source 'm_resetMatchLibraryBaseline.find\(player.name\)' 'Automatic match merge must skip the reset baseline.'
Assert-Contains $source 'PollPlayerLibrary\(\);' 'UI timer must drain completed results without waiting.'
Assert-Contains $header 'PlayerLibraryStore' 'Dialog must own a normalized store.'
Assert-Contains $source 'QueuePlayerIdentityCommand\(j\)' 'Identity writes must run on the database worker.'
Assert-Contains $source 'libraryApplied' 'Cloud success must wait for the local database commit.'
Assert-Contains $project 'third_party\\sqlite\\sqlite3.c' 'SQLite must be compiled into the client.'
Assert-Contains $source 'm_playerLibraryLastSentRevision' 'Full library state must not be rebuilt on every score update.'
Assert-Contains $source 'if \(!killerResolved\)\s*tryFusionMatch\(true' 'Unresolved killer must reach game-ID fusion.'
Assert-Contains $source 'if \(!deadResolved\)\s*tryFusionMatch\(false' 'Unresolved victim must reach game-ID fusion.'
$delta = [regex]::Match($source, 'bool CDNFGameCaptureDlg::ApplyPlayerLibraryDelta[\s\S]*?bool CDNFGameCaptureDlg::RejectPlayerLibraryEditWhileBusy').Value
Assert-Contains $delta 'renameId.*key == oldIdKey' 'Only explicit renames may carry game-ID statistics.'
if ($delta -match 'removed\.size\(\) == 1') { throw 'Bulk replacements must not infer an ID rename.' }
$poll = [regex]::Match($source, 'void CDNFGameCaptureDlg::PollPlayerLibrary[\s\S]*?bool CDNFGameCaptureDlg::QueuePlayerLibrarySave').Value
Assert-Contains $poll 'rosterChanged\) MarkMatchMutation' 'Committed roster edits must advance match mutation state.'
Assert-Contains $poll 'if \(rosterChanged\)[\s\S]*?SaveConfigToFile' 'Committed roster edits must be saved before exit.'
Assert-Contains $source 'reply\["importSource"\] = std::string\(CW2A\(endpoint' 'Remote revisions must retain their immutable request endpoint.'
Assert-Contains $delta 'renameId && key == newIdKey && oldIds.count\(oldIdKey\)' 'A pending ID rename must not resurrect a temporarily removed ID.'
Assert-Contains $poll 'if \(rosterChanged\)[\s\S]*?SyncDataToTree' 'The native tree must refresh after rename score callbacks.'
Assert-Contains $source '"rename_id" : "rename_name"' 'Native renames must retain normalized entity links.'
Write-Host 'Player library integration checks passed.'
