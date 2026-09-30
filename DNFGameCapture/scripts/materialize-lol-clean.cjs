// Only the 28 known LOL slots; leave all TTS and unrelated assets alone.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const base=path.resolve(__dirname,'..'),stage=path.join(base,'build/lol-clean-verify/audio'),voice=path.join(base,'web前端/voice/lol-announcer'),backup=path.join(base,'build/lol-clean-verify/original-voice');
const meta=JSON.parse(fs.readFileSync(path.join(base,'docs/lol-clean-edits.json'))),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const entries=Object.entries(meta.mapping).map(([key,m])=>{const bytes=Buffer.from(fs.readFileSync(path.join(stage,m.source+'.wav.b64'),'utf8'),'base64');if(hash(bytes)!==m.clean)throw Error('Bad transfer '+key);const dest=path.join(voice,key+'.wav'),old=fs.readFileSync(dest),sha=hash(old);if(sha!==m.old&&sha!==m.clean)throw Error('Unknown custom audio; refusing overwrite '+key);return{key,m,bytes,dest,old,sha};});
if(entries.length!==28)throw Error('Expected 28 slots');
fs.mkdirSync(backup,{recursive:true});for(const e of entries){if(e.sha===e.m.clean)continue;const original=path.join(backup,e.key+'.wav');if(fs.existsSync(original)&&hash(fs.readFileSync(original))!==e.m.old)throw Error('Unexpected backup');if(!fs.existsSync(original))fs.writeFileSync(original,e.old);fs.writeFileSync(e.dest+'.tmp',e.bytes);fs.renameSync(e.dest+'.tmp',e.dest);}
console.log('LOL: 28 slots updated with 7 clean clips; originals preserved; no TTS request.');
