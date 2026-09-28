// Isolated loopback fixture: never contacts the production server or any TTS API.
const http=require('http'), crypto=require('crypto'), fs=require('fs'), path=require('path'), os=require('os'), {spawn}=require('child_process');
const exe=process.argv[2];if(!exe)throw Error('test EXE required');
const root=fs.mkdtempSync(path.join(os.tmpdir(),'dnf-voice-test-'));
let version=1, requests=0, offline=false, removed=false, failed=false;
function wav(value){const b=Buffer.alloc(48);b.write('RIFF');b.writeUInt32LE(40,4);b.write('WAVEfmt ',8);b.writeUInt32LE(16,16);b.writeUInt16LE(1,20);b.writeUInt16LE(1,22);b.writeUInt32LE(24000,24);b.writeUInt32LE(48000,28);b.writeUInt16LE(2,32);b.writeUInt16LE(16,34);b.write('data',36);b.writeUInt32LE(4,40);b.writeInt16LE(value,44);return b;}
const assets=[null,wav(100),wav(200),wav(900)], hashes=assets.map(b=>b&&crypto.createHash('sha256').update(b).digest('hex'));
const server=http.createServer((req,res)=>{
 if(req.url.startsWith('/test/')){const expected={'/test/assert-no-audio':0,'/test/assert-one-audio':1,'/test/assert-two-audio':2,'/test/assert-three-audio':3,'/test/assert-four-audio':4}[req.url];if(expected!==undefined&&requests!==expected){failed=true;console.error('Unexpected downloads',req.url,requests);}
 if(req.url==='/test/next')version=2;if(req.url==='/test/offline')offline=true;if(req.url==='/test/remove'){offline=false;removed=true;}res.end('ok');return;}
 if(offline){res.statusCode=503;res.end();return;}
 if(req.url==='/api/voice/catalog'){const revision=(removed?'c':version===1?'a':'b').repeat(64),tag='"'+revision+'"';res.setHeader('ETag',tag);if(req.headers['if-none-match']===tag){res.statusCode=304;res.end();return;}res.setHeader('Content-Type','application/json');res.end(JSON.stringify({schema:1,revision,defaults:{normal:'test-voice',ink:'test-voice'},phrases:{double:'双杀'},voices:removed?[]:[{id:'test-voice',index:2,label:'测试音色',clips:{double:{sha256:hashes[version],bytes:48,url:'/api/voice/audio/'+hashes[version]+'.wav'}}},{id:'lol-announcer',index:17,label:'LOL音效播报',clips:{double:{sha256:hashes[3],bytes:48,url:'/api/voice/audio/'+hashes[3]+'.wav'}}}]}));return;}
 if(req.url==='/api/voice/audio/'+hashes[3]+'.wav'){requests++;res.end(assets[3]);return;}
 if(req.url==='/api/voice/audio/'+hashes[version]+'.wav'){requests++;res.end(assets[version]);return;}res.statusCode=404;res.end();
});
server.listen(0,'127.0.0.1',()=>{const child=spawn(exe,['http://127.0.0.1:'+server.address().port],{env:{...process.env,LOCALAPPDATA:root},stdio:'inherit'});const timer=setTimeout(()=>{failed=true;child.kill();},90000);child.on('error',err=>{console.error(err);failed=true;});child.on('close',code=>{clearTimeout(timer);server.close(()=>{fs.rmSync(root,{recursive:true,force:true});process.exitCode=code|| (failed?1:0);});});});
