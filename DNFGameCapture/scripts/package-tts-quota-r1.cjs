// 服务端升级包：语音生成额度 r1。只打包服务端运行文件，不含音频、密钥、数据库、node_modules。
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cp=require('child_process');
const root=path.resolve(__dirname,'..'),srv=path.join(root,'cloud-match-server');
const label='DNF-5.5.4-TTS-Quota-r1',stage=path.join(root,'build/tts-quota-r1-verify/package',label);
const zip=path.join(root,'deployment-packages',label+'.zip'),deploy=path.join(root,'deployment-packages','deploy-dnf-tts-quota-r1.sh');
if(fs.existsSync(stage)||fs.existsSync(zip))throw Error('Output exists; never overwrite a verified release.');
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
for(const f of ['dist/tts-generate.js','dist/tts-admin.js','dist/server.js','package.json','package-lock.json'])if(!fs.existsSync(path.join(srv,f)))throw Error('Missing '+f+' (run npm run build first)');
function copy(src,rel){const dest=path.join(stage,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(src,dest);}
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
for(const f of walk(path.join(srv,'dist')))copy(f,'server/dist/'+path.relative(path.join(srv,'dist'),f).replaceAll('\\','/'));
copy(path.join(srv,'package.json'),'server/package.json');copy(path.join(srv,'package-lock.json'),'server/package-lock.json');
copy(path.join(root,'docs/release-tts-quota-r1.md'),'README.md');
const records=walk(stage).sort().map(f=>({path:path.relative(stage,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:hash(f)}));
for(const r of records)if(/(^|\/)(\.env(?:\..*)?|config\.ini|node_modules|.*\.(sqlite|db|pdb|b64|key|pem|wav))$/i.test(r.path))throw Error('Forbidden package file '+r.path);
fs.writeFileSync(path.join(stage,'SHA256SUMS.json'),JSON.stringify({package:label,version:'5.5.4',revision:'tts-quota-r1',productionDeployed:false,paidTtsCallsThisUpdate:0,files:records},null,2));
const q=s=>"'"+s.replaceAll("'","''")+"'";
cp.execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',"$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; $base="+q(stage)+"; $z=[IO.Compression.ZipFile]::Open("+q(zip)+",[IO.Compression.ZipArchiveMode]::Create); try { foreach($f in Get-ChildItem -LiteralPath $base -File -Recurse) { $entry=$f.FullName.Substring($base.Length+1).Replace([char]92,[char]47); [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,$f.FullName,$entry,[IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $z.Dispose() }"],{stdio:'inherit'});
const sha=hash(zip);
const script=fs.readFileSync(path.join(__dirname,'deploy-tts-quota-r1.template.sh'),'utf8').replace(/\r\n/g,'\n').replace('__ZIP_SHA256__',sha);
if(script.includes('__ZIP_SHA256__'))throw Error('Template placeholder left');
fs.writeFileSync(deploy,script);
const result={zip,bytes:fs.statSync(zip).size,sha256:sha,files:records.length+1,deployScript:deploy,deployScriptSha256:hash(deploy)};
fs.writeFileSync(zip+'.manifest.json',JSON.stringify(result,null,2));console.log('PACKAGE_RESULT:'+JSON.stringify(result));
