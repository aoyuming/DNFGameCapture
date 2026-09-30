// Offline packaging only. Never launches the app, deploys, reads production config, or calls TTS.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const root=path.resolve(__dirname,'..'),verify=path.join(root,'build/lol-victory-verify'),release=path.join(root,'deployment-packages');
const label='DNF-CloudVoice-LOL-Victory-20260929',stage=path.join(verify,'package',label),zip=path.join(release,label+'.zip');
if(fs.existsSync(stage)||fs.existsSync(zip))throw Error('Package output already exists; do not overwrite a verified release.');
fs.mkdirSync(stage,{recursive:true});fs.mkdirSync(release,{recursive:true});
function copy(src,rel){const dest=path.join(stage,rel);if(!fs.statSync(src).isFile())throw Error('Not a file: '+src);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(src,dest);}
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
for(const src of walk(path.join(verify,'server')).filter(f=>f.endsWith('.js')))copy(src,path.join('server/dist',path.relative(path.join(verify,'server'),src)));
for(const name of ['package.json','package-lock.json'])copy(path.join(root,'cloud-match-server',name),'server/'+name);
copy(path.join(verify,'bin/DNFGameCapture.exe'),'client-update/DNFGameCapture.exe');
const loader=path.join(verify,'bin/WebView2Loader.dll');if(fs.existsSync(loader))copy(loader,'client-update/WebView2Loader.dll');
for(const name of ['autocomplete-worker.js','index.html','keys.css','keys.html','keys.js','kill.css','kill.html','kill.js','main.js','style.css'])copy(path.join(root,'web前端',name),'client-update/web前端/'+name);
const voices=['doubao-meihuo','doubao-gufeng2','doubao-gufeng1','doubao-wuzetian','lol-announcer'];
const keys=['double','triple','first','shutdown','revenge','ink-double','ink-triple','ink-first','ink-shutdown','ink-revenge','pixel-double','pixel-triple','pixel-first','pixel-shutdown','pixel-revenge','ak-allkill','ak-ace','ak-ink','ak-pixel','ak-inferno','ak-frost','victory','ink-victory','victory-en','victory-neon','victory-inferno','victory-broadcast','victory-frost'];
for(const id of voices)for(const key of keys)copy(path.join(root,'web前端/voice',id,key+'.wav'),'assets/voice/'+id+'/'+key+'.wav');
for(const name of ['cloud-voice-management.md','lol-announcer-sources.json','voice-victory-validation.md'])copy(path.join(root,'docs',name),'docs/'+name);
copy(path.join(root,'docs/voice-victory-deployment.md'),'README.md');
const records=walk(stage).sort().map(f=>({path:path.relative(stage,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')}));
for(const r of records)if(/(^|\/)(\.env(?:\..*)?|config\.ini|node_modules|.*\.(sqlite|db|pdb|b64|key|pem))$/i.test(r.path))throw Error('Forbidden package file: '+r.path);
if(records.filter(r=>r.path.startsWith('assets/voice/')&&r.path.endsWith('.wav')).length!==140)throw Error('Expected 140 WAV slots');
fs.writeFileSync(path.join(stage,'SHA256SUMS.json'),JSON.stringify({package:label,productionDeployed:false,paidVictoryClipsGenerated:28,existingTtsClipsReused:84,files:records},null,2));
const q=s=>"'"+s.replaceAll("'","''")+"'";
// Explicit forward-slash names: Windows PowerShell's older .NET otherwise emits backslashes,
// which are not portable ZIP directory separators on Linux deployment hosts.
const zipCommand="$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; $base="+q(stage)+"; $z=[IO.Compression.ZipFile]::Open("+q(zip)+",[IO.Compression.ZipArchiveMode]::Create); try { foreach($f in Get-ChildItem -LiteralPath $base -File -Recurse) { $entry=$f.FullName.Substring($base.Length+1).Replace([char]92,[char]47); [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,$f.FullName,$entry,[IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $z.Dispose() }";
cp.execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',zipCommand],{stdio:'inherit'});
const result={zip,bytes:fs.statSync(zip).size,sha256:crypto.createHash('sha256').update(fs.readFileSync(zip)).digest('hex'),files:records.length+1,wavSlots:140,uniqueAudioHashes:new Set(records.filter(r=>r.path.endsWith('.wav')).map(r=>r.sha256)).size};
fs.writeFileSync(zip+'.manifest.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
