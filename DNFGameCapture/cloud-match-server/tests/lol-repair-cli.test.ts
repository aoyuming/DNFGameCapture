import {test,expect} from 'vitest';
import Database from 'better-sqlite3';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join,resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {VoiceStore,pcmToWav} from '../src/voices.js';
import {LOL_REPAIR} from '../src/lol-repair.js';
const project=resolve(dirname(fileURLToPath(import.meta.url)),'..');
test('real offline repair imports only LOL, preserves paid/custom clips and original assets, is idempotent and rolls back conflicts',async()=>{
 const dir=mkdtempSync(join(tmpdir(),'dnf-lol-repair-')),dbPath=join(dir,'test.sqlite');
 try {
  let db=new Database(dbPath),store=new VoiceStore(db,undefined,false);
  for(const key of Object.keys(LOL_REPAIR))store.put('lol-announcer',key,readFileSync(resolve(project,'../build/lol-clean-verify/original-voice',key+'.wav')),'imported');
  const custom=pcmToWav(Buffer.from([99,0,3,0]));const paid=store.put('doubao-meihuo','double',custom,'custom');await store.close();db.close();
  const run=()=>spawnSync(process.execPath,['--import','tsx','src/import-voices.ts','--from','../web前端/voice','--database',dbPath,'--apply','--repair-lol','--only-lol'],{cwd:project,encoding:'utf8'});
  let r=run();expect(r.status,r.stderr).toBe(0);expect(r.stdout).toContain('Imported=28; reused=0');
  db=new Database(dbPath);store=new VoiceStore(db,undefined,false);
  for(const clip of store.clips('lol-announcer')){expect(clip.sha256).toBe(LOL_REPAIR[clip.key].clean);expect(store.asset(LOL_REPAIR[clip.key].old)).not.toBeNull();}
  expect(store.clips('doubao-meihuo')).toHaveLength(1);expect(store.clips('doubao-meihuo')[0].sha256).toBe(paid.sha256);await store.close();db.close();
  r=run();expect(r.status,r.stderr).toBe(0);expect(r.stdout).toContain('Imported=0; reused=28');
  db=new Database(dbPath);store=new VoiceStore(db,undefined,false);
  store.put('lol-announcer','double',readFileSync(resolve(project,'../build/lol-clean-verify/original-voice/double.wav')),'imported');
  store.put('lol-announcer','victory-frost',custom,'custom');const before=store.clips('lol-announcer');await store.close();db.close();
  r=run();expect(r.status).not.toBe(0);expect(r.stderr).toContain('Refusing to overwrite');
  db=new Database(dbPath);store=new VoiceStore(db,undefined,false);expect(store.clips('lol-announcer')).toEqual(before);expect(store.clips('doubao-meihuo')[0].sha256).toBe(paid.sha256);await store.close();db.close();
 } finally {rmSync(dir,{recursive:true,force:true});}
},20000);
