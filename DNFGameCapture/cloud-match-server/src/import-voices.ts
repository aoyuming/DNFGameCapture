// Offline import. No TTS request, API key or HTTP connection is involved.
// node dist/import-voices.js --from /path/voice [--database /path/cloud-match.sqlite --apply] [--replace | --repair-lol] [--only-lol]
import { readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import Database from 'better-sqlite3';
import { canRepairLol } from './lol-repair.js';
import { PHRASES, VoiceStore, sha256, validateWav } from './voices.js';
const args=process.argv.slice(2), value=(name:string)=>{const i=args.indexOf(name);return i<0?'':args[i+1]??'';};
const from=value('--from');if(!from)throw Error('--from is required');
const apply=args.includes('--apply'), replace=args.includes('--replace'), database=value('--database');
if(apply&&!database)throw Error('--apply requires an explicit --database path');
if(replace&&!apply)throw Error('--replace requires --apply');
const repairLol=args.includes('--repair-lol');
if(repairLol&&(!apply||replace))throw Error('--repair-lol requires --apply and cannot be combined with --replace');
const ids=args.includes('--only-lol')?['lol-announcer']:['doubao-meihuo','doubao-gufeng2','doubao-gufeng1','doubao-wuzetian','lol-announcer'];
const clips=ids.flatMap(id=>Object.keys(PHRASES).map(key=>{const wav=readFileSync(join(resolve(from),id,key+'.wav'));validateWav(wav);return {id,key,wav,hash:sha256(wav)};}));
if(!apply){console.log(`Validated ${clips.length} WAV files. Dry run only; database unchanged; ZERO API calls.`);}
else {
  const db=new Database(resolve(database),{fileMustExist:true});const store=new VoiceStore(db,undefined,false);
  try {
    let reused=0, imported=0;
    db.transaction(()=>{for(const clip of clips){const old=store.clips(clip.id).find(c=>c.key===clip.key);if(old?.sha256===clip.hash){reused++;continue;}if(old&&!replace&&!(repairLol&&canRepairLol(clip.id,clip.key,old.sha256,clip.hash)))throw Error(`Refusing to overwrite ${clip.id}/${clip.key}; use --replace deliberately`);store.put(clip.id,clip.key,clip.wav,'imported');imported++;}})();
    console.log(`Imported=${imported}; reused=${reused}; old revisions retained; ZERO API calls.`);
  } finally {db.close();}
}
