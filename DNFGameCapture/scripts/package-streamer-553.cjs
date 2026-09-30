// Offline combined upgrade, reusing the verified server/audio archive. No deployment or TTS.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const root=path.resolve(__dirname,'..'),base=path.join(root,'deployment-packages/DNF-CloudVoice-LOL-Victory-20260929.zip');
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
if(hash(base)!=='b369c9394c7bae7564d7cfa2d6b4d2205408e4c02306668e52a501c0bae7bbb0')throw Error('Prior verified server/audio archive changed; inspect before reuse.');
const label='DNF-5.5.3-Streamer-Tools',verify=path.join(root,'build/streamer-tools-553-verify'),stage=path.join(verify,'package',label),zip=path.join(root,'deployment-packages',label+'.zip');
if(fs.existsSync(stage)||fs.existsSync(zip))throw Error('Output exists; never overwrite a verified release.');
fs.mkdirSync(path.dirname(stage),{recursive:true});
const q=s=>"'"+s.replaceAll("'","''")+"'",ps=s=>cp.execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',"$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; "+s],{stdio:'inherit'});
ps("$v=(Get-Item -LiteralPath "+q(path.join(verify,'bin/DNFGameCapture.exe'))+").VersionInfo; if($v.FileVersion -ne '5.5.3.0' -or $v.ProductVersion -ne '5.5.3.0'){throw 'Expected EXE 5.5.3.0'}; [IO.Compression.ZipFile]::ExtractToDirectory("+q(base)+','+q(stage)+')');
const original=JSON.parse(fs.readFileSync(path.join(stage,'SHA256SUMS.json'),'utf8'));
for(const record of original.files)if(hash(path.join(stage,record.path))!==record.sha256)throw Error('Base manifest mismatch '+record.path);
function copy(src,rel){const dest=path.join(stage,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(src,dest);}
copy(path.join(stage,'README.md'),'docs/voice-victory-deployment.md');
copy(path.join(root,'docs/release-5.5.3.md'),'README.md');copy(path.join(root,'docs/release-5.5.3.md'),'docs/release-5.5.3.md');
copy(path.join(verify,'bin/DNFGameCapture.exe'),'client-update/DNFGameCapture.exe');
const dll=path.join(verify,'bin/WebView2Loader.dll');if(fs.existsSync(dll))copy(dll,'client-update/WebView2Loader.dll');
for(const file of ['autocomplete-worker.js','index.html','keys.css','keys.html','keys.js','kill.css','kill.html','kill.js','main.js','style.css'])copy(path.join(root,'web前端',file),'client-update/web前端/'+file);
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
const records=walk(stage).filter(f=>path.basename(f)!=='SHA256SUMS.json').sort().map(f=>({path:path.relative(stage,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:hash(f)}));
for(const r of records)if(/(^|\/)(\.env(?:\..*)?|config\.ini|node_modules|.*\.(sqlite|db|pdb|b64|key|pem))$/i.test(r.path))throw Error('Forbidden package file '+r.path);
const reused=original.files.filter(r=>r.path.startsWith('server/')||r.path.startsWith('assets/voice/'));
for(const record of reused)if(hash(path.join(stage,record.path))!==record.sha256)throw Error('Server/audio should be unchanged');
fs.writeFileSync(path.join(stage,'SHA256SUMS.json'),JSON.stringify({package:label,version:'5.5.3',productionDeployed:false,paidTtsCallsThisUpdate:0,serverAndAudioBaseSha256:hash(base),files:records},null,2));
ps('$base='+q(stage)+'; $z=[IO.Compression.ZipFile]::Open('+q(zip)+",[IO.Compression.ZipArchiveMode]::Create); try { foreach($f in Get-ChildItem -LiteralPath $base -File -Recurse) { $entry=$f.FullName.Substring($base.Length+1).Replace([char]92,[char]47); [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,$f.FullName,$entry,[IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $z.Dispose() }");
const result={zip,bytes:fs.statSync(zip).size,sha256:hash(zip),files:records.length+1,version:'5.5.3',wavSlots:records.filter(r=>r.path.endsWith('.wav')).length,reusedServerAudioFiles:reused.length};fs.writeFileSync(zip+'.manifest.json',JSON.stringify(result,null,2));console.log('PACKAGE_RESULT:'+JSON.stringify(result));
