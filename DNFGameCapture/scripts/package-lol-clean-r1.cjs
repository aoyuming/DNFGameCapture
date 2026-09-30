// Isolated package only. Reuse prior verified archive; never read production configs or call TTS.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const root=path.resolve(__dirname,'..'),verify=path.join(root,'build/lol-clean-verify'),label='DNF-5.5.3-LOL-Clean-r1',stage=path.join(verify,'package',label),zip=path.join(root,'deployment-packages',label+'.zip'),base=path.join(root,'deployment-packages/DNF-5.5.3-Streamer-Tools.zip');
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
if(hash(base)!=='722ff42a29c723d21ebcdb6eac196826e975078cc82f06ef7eb38a1a00003966')throw Error('Base package changed');
if(fs.existsSync(stage)||fs.existsSync(zip))throw Error('Do not overwrite an existing release');
fs.mkdirSync(path.dirname(stage),{recursive:true});const q=s=>"'"+s.replaceAll("'","''")+"'",ps=s=>cp.execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',"$ErrorActionPreference='Stop';Add-Type -AssemblyName System.IO.Compression;Add-Type -AssemblyName System.IO.Compression.FileSystem;"+s],{stdio:'inherit'});
ps("$v=(Get-Item -LiteralPath "+q(path.join(verify,'bin/DNFGameCapture.exe'))+").VersionInfo; if($v.FileVersion -ne '5.5.3.0'){throw 'Wrong EXE version'};[IO.Compression.ZipFile]::ExtractToDirectory("+q(base)+','+q(stage)+')');
const old=JSON.parse(fs.readFileSync(path.join(stage,'SHA256SUMS.json')));
function copy(src,rel){const dst=path.join(stage,rel);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(src,dst);}
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
for(const r of old.files)if(hash(path.join(stage,r.path))!==r.sha256)throw Error('Base hash mismatch');
// This dist belongs solely to the newly-created staging directory, never the installed service.
fs.rmSync(path.join(stage,'server/dist'),{recursive:true});for(const f of walk(path.join(verify,'server')).filter(f=>f.endsWith('.js')))copy(f,'server/dist/'+path.relative(path.join(verify,'server'),f));
copy(path.join(verify,'bin/DNFGameCapture.exe'),'client-update/DNFGameCapture.exe');
for(const file of ['autocomplete-worker.js','index.html','keys.css','keys.html','keys.js','kill.css','kill.html','kill.js','main.js','style.css'])copy(path.join(root,'web前端',file),'client-update/web前端/'+file);
const meta=JSON.parse(fs.readFileSync(path.join(root,'docs/lol-clean-edits.json')));
for(const [key,e] of Object.entries(meta.mapping)){const f=path.join(root,'web前端/voice/lol-announcer',key+'.wav');if(hash(f)!==e.clean)throw Error('Clean audio mismatch');copy(f,'assets/voice/lol-announcer/'+key+'.wav');}
for(const f of ['lol-announcer-sources.json','lol-clean-edits.json','cloud-voice-management.md','lol-clean-r1.md'])copy(path.join(root,'docs',f),'docs/'+f);
copy(path.join(root,'docs/lol-clean-r1.md'),'README.md');
for(const r of old.files.filter(r=>r.path.startsWith('assets/voice/')&&!r.path.startsWith('assets/voice/lol-announcer/')))if(hash(path.join(stage,r.path))!==r.sha256)throw Error('TTS assets changed');
const records=walk(stage).filter(f=>path.basename(f)!=='SHA256SUMS.json').sort().map(f=>({path:path.relative(stage,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:hash(f)}));
for(const r of records)if(/(^|\/)(\.env(?:\..*)?|config\.ini|node_modules|.*\.(sqlite|db|pdb|b64|key|pem))$/i.test(r.path))throw Error('Forbidden package file');
fs.writeFileSync(path.join(stage,'SHA256SUMS.json'),JSON.stringify({package:label,version:'5.5.3',revision:'lol-clean-r1',productionDeployed:false,paidTtsCallsThisUpdate:0,files:records},null,2));
ps('$base='+q(stage)+';$z=[IO.Compression.ZipFile]::Open('+q(zip)+",[IO.Compression.ZipArchiveMode]::Create);try{foreach($f in Get-ChildItem -LiteralPath $base -File -Recurse){$entry=$f.FullName.Substring($base.Length+1).Replace([char]92,[char]47);[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,$f.FullName,$entry,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}finally{$z.Dispose()}");
// Standalone seven-clip listening sample, separated by 350ms silence, no synthetic narration.
const samples=['double','triple','first','shutdown','revenge','ak','victory'].flatMap((name,i)=>{const key=name==='ak'?'ak-ace':name,b=fs.readFileSync(path.join(stage,'assets/voice/lol-announcer',key+'.wav'));if(b.toString('ascii',36,40)!=='data')throw Error('Unexpected PCM header');return i?[Buffer.alloc(16800),b.subarray(44)]:[b.subarray(44)];});
const pcm=Buffer.concat(samples),head=Buffer.alloc(44);head.write('RIFF');head.writeUInt32LE(pcm.length+36,4);head.write('WAVEfmt ',8);head.writeUInt32LE(16,16);head.writeUInt16LE(1,20);head.writeUInt16LE(1,22);head.writeUInt32LE(24000,24);head.writeUInt32LE(48000,28);head.writeUInt16LE(2,32);head.writeUInt16LE(16,34);head.write('data',36);head.writeUInt32LE(pcm.length,40);
const preview=path.join(root,'deployment-packages/LOL-clean-r1-preview.wav');if(fs.existsSync(preview))throw Error('Preview exists');fs.writeFileSync(preview,Buffer.concat([head,pcm]));
const result={zip,sha256:hash(zip),bytes:fs.statSync(zip).size,files:records.length+1,preview,wavSlots:140,ttsFilesUnchanged:112,cleanLolSlots:28};fs.writeFileSync(zip+'.manifest.json',JSON.stringify(result,null,2));console.log('PACKAGE_RESULT:'+JSON.stringify(result));
