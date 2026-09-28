import { createHash, randomUUID } from 'node:crypto';
import type Database from 'better-sqlite3';
import { z } from 'zod';

export const PHRASES: Record<string, string> = {
  double: '双杀！', triple: '三杀！', first: '一血！', shutdown: '终结！', revenge: '复仇！',
  'ink-double': '双斩！', 'ink-triple': '三斩！', 'ink-first': '首胜！', 'ink-shutdown': '断其锋！', 'ink-revenge': '雪耻！',
  'pixel-double': 'Double!', 'pixel-triple': 'Triple!', 'pixel-first': 'First blood!', 'pixel-shutdown': 'Stopped!', 'pixel-revenge': 'Revenge!',
  'ak-allkill': 'AK！一人团灭！', 'ak-ace': 'ACE！一人团灭！', 'ak-ink': '全歼！一人破阵！', 'ak-pixel': 'AK! Perfect!', 'ak-inferno': 'AK！焚尽一切！', 'ak-frost': 'AK！冰封全场！',
  'victory': '胜利！',
  'ink-victory': '大捷！凯歌还！',
  'victory-en': 'Victory!',
  'victory-neon': 'Victory! Mission complete!',
  'victory-inferno': '烈焰凯旋！',
  'victory-broadcast': '比赛胜利！恭喜！',
  'victory-frost': '冰封全场，凯旋而归！',
};
export const MAX_WAV = 8 * 1024 * 1024;
export const sha256 = (data: Buffer | string): string => createHash('sha256').update(data).digest('hex');
export class VoiceError extends Error {
  constructor(public status: number, public code: string) { super(code); }
}
const safeId = z.string().regex(/^[a-z0-9][a-z0-9-]{0,63}$/);
export const voiceInput = z.object({
  id: safeId, label: z.string().trim().min(1).max(80), kind: z.enum(['tts', 'custom']),
  speaker: z.string().regex(/^[a-zA-Z0-9_]{0,128}$/).default(''),
  resource: z.enum(['seed-tts-1.0', 'seed-tts-2.0']).default('seed-tts-2.0'), enabled: z.boolean().default(true),
}).strict().refine(v => v.kind !== 'tts' || !!v.speaker, 'speaker_required');
export type Voice = z.infer<typeof voiceInput> & { index: number };
export type Synthesize = (voice: Voice, text: string, signal: AbortSignal) => Promise<Buffer>;
export function validateWav(b: Buffer): void {
  if (b.length < 46 || b.length > MAX_WAV || b.toString('ascii',0,4) !== 'RIFF' || b.toString('ascii',8,12) !== 'WAVE' || b.readUInt32LE(4) + 8 !== b.length) throw new VoiceError(400,'invalid_wav');
  let pos = 12, format = false, audio = false, byteRate = 0, dataBytes = 0, align = 0;
  while (pos + 8 <= b.length) {
    const type = b.toString('ascii',pos,pos+4), size = b.readUInt32LE(pos+4); pos += 8;
    const end = pos + size + size % 2;
    if (end > b.length) throw new VoiceError(400,'invalid_wav');
    if (type === 'fmt ') {
      if (format || size < 16) throw new VoiceError(400,'invalid_wav');
      const channels = b.readUInt16LE(pos+2), rate = b.readUInt32LE(pos+4), bits = b.readUInt16LE(pos+14);
      align = channels * bits / 8; byteRate = rate * align;
      format = b.readUInt16LE(pos) === 1 && [1,2].includes(channels) && rate >= 8000 && rate <= 48000 && [8,16].includes(bits) && b.readUInt32LE(pos+8) === byteRate && b.readUInt16LE(pos+12) === align;
      if (!format) throw new VoiceError(400,'pcm_wav_required');
    } else if (type === 'data') { if (audio || size < 2) throw new VoiceError(400,'invalid_wav'); audio = true; dataBytes = size; }
    pos = end;
  }
  if (!format || !audio || pos !== b.length || dataBytes % align || dataBytes / byteRate > 30) throw new VoiceError(400,'wav_max_30_seconds');
}
export function pcmToWav(pcm: Buffer): Buffer {
  if (!pcm.length || pcm.length % 2 || pcm.length > 24000 * 2 * 30) throw new VoiceError(502,'invalid_tts_pcm');
  let peak = 0; for (let i=0;i<pcm.length;i+=2) peak = Math.max(peak,Math.abs(pcm.readInt16LE(i)));
  if (!peak) throw new VoiceError(502,'silent_tts');
  const out = Buffer.alloc(44 + pcm.length), gain = Math.min(4,29000/peak);
  out.write('RIFF'); out.writeUInt32LE(out.length-8,4); out.write('WAVEfmt ',8); out.writeUInt32LE(16,16);
  out.writeUInt16LE(1,20); out.writeUInt16LE(1,22); out.writeUInt32LE(24000,24); out.writeUInt32LE(48000,28); out.writeUInt16LE(2,32); out.writeUInt16LE(16,34); out.write('data',36); out.writeUInt32LE(pcm.length,40);
  for(let i=0;i<pcm.length;i+=2) out.writeInt16LE(Math.max(-32768,Math.min(32767,Math.round(pcm.readInt16LE(i)*gain))),44+i);
  return out;
}
export const doubaoSynthesize: Synthesize = async (voice, text, signal) => {
  const key = process.env.DOUBAO_TTS_API_KEY, appKey = process.env.DOUBAO_TTS_APP_KEY;
  if (!key || !appKey) throw new VoiceError(503,'tts_not_configured');
  const controller = new AbortController(), abort = () => controller.abort();
  signal.addEventListener('abort',abort,{once:true}); if(signal.aborted) abort();
  const timer = setTimeout(abort,30000);
  try {
    const response = await fetch('https://openspeech.bytedance.com/api/v3/tts/unidirectional', {
      method:'POST', signal:controller.signal, headers:{'Content-Type':'application/json','X-Api-Key':key,'X-Api-App-Key':appKey,'X-Api-Resource-Id':voice.resource,'X-Api-Request-Id':randomUUID()},
      body:JSON.stringify({user:{uid:'dnf-cloud-voices'},req_params:{text,speaker:voice.speaker,audio_params:{format:'pcm',sample_rate:24000}, additions:'{}'}}),
    });
    if(!response.ok || !response.body) throw new VoiceError(502,'tts_request_failed');
    const chunks: Buffer[]=[]; let bytes=0;
    for await (const part of response.body as unknown as AsyncIterable<Uint8Array>) {
      bytes+=part.length; if(bytes>MAX_WAV*2) {abort();throw new VoiceError(502,'tts_response_too_large');} chunks.push(Buffer.from(part));
    }
    let done=false; const pcm:Buffer[]=[];
    for(const line of Buffer.concat(chunks).toString('utf8').split(/\r?\n/).filter(s=>s.trim())) {
      const obj=JSON.parse(line) as {code:number;data?:string};
      if(obj.code!==0 && obj.code!==20000000) throw new VoiceError(502,'tts_provider_error');
      if(obj.data) pcm.push(Buffer.from(obj.data,'base64')); if(obj.code===20000000) done=true;
    }
    if(!done) throw new VoiceError(502,'tts_incomplete');
    return pcmToWav(Buffer.concat(pcm));
  } finally {clearTimeout(timer);signal.removeEventListener('abort',abort);}
};
const SEEDS = [
  [1,'doubao-meihuo','魅惑女声','zh_female_jiaochuannv_uranus_bigtts','seed-tts-2.0'],
  [2,'doubao-gufeng2','古风雅韵 2.0','zh_female_gufengshaoyu_uranus_bigtts','seed-tts-2.0'],
  [3,'doubao-gufeng1','古风雅韵 1.0','zh_female_gufengshaoyu_mars_bigtts','seed-tts-1.0'],
  [4,'doubao-wuzetian','武则天 1.0','zh_female_wuzetian_mars_bigtts','seed-tts-1.0'],
] as const;
interface Clip { voiceId:string; key:string; sha256:string; bytes:number; fingerprint:string; source:string }
interface Job { id:string; voiceId:string; key:string; state:string; error:string; fingerprint:string; requestId:string }
export class VoiceStore {
  private abort = new AbortController();
  private running: Promise<void> | null = null;
  constructor(private db: Database.Database, private synthesize: Synthesize = doubaoSynthesize, recoverJobs = true) {
    db.exec(`CREATE TABLE IF NOT EXISTS voice_definitions (voice_index INTEGER PRIMARY KEY, id TEXT UNIQUE NOT NULL, label TEXT NOT NULL, kind TEXT NOT NULL, speaker TEXT NOT NULL, resource TEXT NOT NULL, enabled INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS voice_assets (sha256 TEXT PRIMARY KEY, bytes INTEGER NOT NULL, wav BLOB NOT NULL);
      CREATE TABLE IF NOT EXISTS voice_clips (voice_id TEXT NOT NULL, phrase_key TEXT NOT NULL, sha256 TEXT NOT NULL, fingerprint TEXT NOT NULL, source TEXT NOT NULL, PRIMARY KEY(voice_id,phrase_key));
      CREATE TABLE IF NOT EXISTS voice_jobs (id TEXT PRIMARY KEY, request_id TEXT NOT NULL, voice_id TEXT NOT NULL, phrase_key TEXT NOT NULL, fingerprint TEXT NOT NULL, state TEXT NOT NULL, error TEXT NOT NULL DEFAULT '', UNIQUE(request_id,phrase_key));
      CREATE UNIQUE INDEX IF NOT EXISTS voice_one_active_job ON voice_jobs(voice_id,phrase_key) WHERE state IN ('queued','running');`);
    for(const [index,id,label,speaker,resource] of SEEDS) db.prepare('INSERT OR IGNORE INTO voice_definitions VALUES(?,?,?,?,?,?,1)').run(index,id,label,'tts',speaker,resource);
    // Allocate rather than reserve index 6: existing custom voices must never be displaced.
    if (!this.voices().some(v=>v.id==='lol-announcer')) this.save({id:'lol-announcer',label:'LOL音效播报',kind:'custom'});
    // A crashed request may already have incurred a charge: never automatically retry it.
    if (recoverJobs) db.prepare("UPDATE voice_jobs SET state='failed',error='interrupted_no_auto_retry' WHERE state IN ('queued','running')").run();
  }
  voices(): Voice[] { return (this.db.prepare('SELECT voice_index AS "index",id,label,kind,speaker,resource,enabled FROM voice_definitions ORDER BY voice_index').all() as Array<Omit<Voice,'enabled'>&{enabled:number}>).map(v=>({...v,enabled:!!v.enabled})); }
  voice(id:string):Voice {const v=this.voices().find(v=>v.id===id);if(!v)throw new VoiceError(404,'voice_not_found');return v;}
  clips(id:string): Clip[] {return this.db.prepare('SELECT c.voice_id AS voiceId,c.phrase_key AS key,c.sha256,a.bytes,c.fingerprint,c.source FROM voice_clips c JOIN voice_assets a ON a.sha256=c.sha256 WHERE c.voice_id=? ORDER BY c.phrase_key').all(id) as Clip[];}
  jobs():Job[] {return this.db.prepare('SELECT id,request_id AS requestId,voice_id AS voiceId,phrase_key AS key,state,error,fingerprint FROM voice_jobs ORDER BY rowid DESC LIMIT 200').all() as Job[];}
  busy(id:string,key?:string):boolean {return !!this.db.prepare("SELECT 1 FROM voice_jobs WHERE voice_id=? AND state IN ('queued','running')"+(key?' AND phrase_key=?':'')).get(...(key?[id,key]:[id]));}
  save(input:unknown, update=false):Voice {
    const v=voiceInput.parse(input);if(v.id==='auto'||v.id==='windows')throw new VoiceError(400,'reserved_voice_id');
    if(update) {this.voice(v.id);if(this.busy(v.id))throw new VoiceError(409,'voice_busy');}
    else if(this.voices().some(x=>x.id===v.id))throw new VoiceError(409,'voice_exists');
    if(!update && this.voices().length>=256)throw new VoiceError(409,'voice_limit');
    this.db.transaction(()=>{
      if(update)this.db.prepare('UPDATE voice_definitions SET label=?,kind=?,speaker=?,resource=?,enabled=? WHERE id=?').run(v.label,v.kind,v.speaker,v.resource,+v.enabled,v.id);
      else {const index=Math.max(5,...this.voices().map(x=>x.index))+1;this.db.prepare('INSERT INTO voice_definitions VALUES(?,?,?,?,?,?,?)').run(index,v.id,v.label,v.kind,v.speaker,v.resource,+v.enabled);}
    })();return this.voice(v.id);
  }
  put(id:string,key:string,wav:Buffer,source='custom',fingerprint='',job=false):Clip {
    this.voice(id);if(!Object.hasOwn(PHRASES,key))throw new VoiceError(400,'unknown_phrase');
    if(!job && this.busy(id,key))throw new VoiceError(409,'voice_busy');validateWav(wav);const hash=sha256(wav);
    this.db.transaction(()=>{
      const size=(this.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM voice_assets').get() as {n:number}).n;
      if(size+wav.length>1024*1024*1024 && !this.db.prepare('SELECT 1 FROM voice_assets WHERE sha256=?').get(hash))throw new VoiceError(413,'voice_storage_limit');
      this.db.prepare('INSERT OR IGNORE INTO voice_assets VALUES(?,?,?)').run(hash,wav.length,wav);
      this.db.prepare('INSERT INTO voice_clips VALUES(?,?,?,?,?) ON CONFLICT(voice_id,phrase_key) DO UPDATE SET sha256=excluded.sha256,fingerprint=excluded.fingerprint,source=excluded.source').run(id,key,hash,fingerprint,source);
    })();return this.clips(id).find(c=>c.key===key)!;
  }
  asset(hash:string):Buffer|null {if(!/^[a-f0-9]{64}$/.test(hash))return null;return (this.db.prepare('SELECT wav FROM voice_assets WHERE sha256=?').get(hash) as {wav:Buffer}|undefined)?.wav??null;}
  catalog() {
    const data={schema:1,defaults:{normal:'lol-announcer',ink:'lol-announcer'},voices:this.voices().filter(v=>v.enabled).map(v=>({index:v.index,id:v.id,label:v.label,clips:Object.fromEntries(this.clips(v.id).map(c=>[c.key,{sha256:c.sha256,bytes:c.bytes,url:`/api/voice/audio/${c.sha256}.wav`}]))})),phrases:PHRASES};
    return {...data,revision:sha256(JSON.stringify(data))};
  }
  generation(id:string,input:unknown):Job[] {
    const v=this.voice(id);if(v.kind!=='tts')throw new VoiceError(400,'custom_voice_upload_only');
    const parsed=z.object({keys:z.array(z.string().refine(k=>Object.hasOwn(PHRASES,k))).min(1).max(Object.keys(PHRASES).length),requestId:z.string().uuid(),confirmPaid:z.literal(true),force:z.boolean().default(false)}).strict().parse(input);
    const keys=[...new Set(parsed.keys)], requestId=parsed.requestId;
    const prior=this.db.prepare('SELECT DISTINCT voice_id AS id FROM voice_jobs WHERE request_id=?').all(requestId) as {id:string}[];
    if(prior.length && prior.some(p=>p.id!==id))throw new VoiceError(409,'request_id_conflict');
    this.db.transaction(()=>{
      for(const key of keys) {
        if(this.db.prepare('SELECT 1 FROM voice_jobs WHERE request_id=? AND phrase_key=?').get(requestId,key))continue;
        if(this.busy(id,key))throw new VoiceError(409,'voice_busy');
        const exists=this.clips(id).find(c=>c.key===key), fingerprint=sha256(JSON.stringify([v.speaker,v.resource,PHRASES[key],24000,'peak29000-max4']));
        const state=exists&&!parsed.force?'reused':'queued';
        if(state==='queued' && this.synthesize===doubaoSynthesize && (!process.env.DOUBAO_TTS_API_KEY||!process.env.DOUBAO_TTS_APP_KEY))throw new VoiceError(503,'tts_not_configured');
        this.db.prepare('INSERT INTO voice_jobs(id,request_id,voice_id,phrase_key,fingerprint,state) VALUES(?,?,?,?,?,?)').run(randomUUID(),requestId,id,key,fingerprint,state);
      }
    })();this.pump();return this.jobs().filter(j=>j.requestId===requestId);
  }
  private pump():void {
    if(this.running||this.abort.signal.aborted)return;
    this.running=(async()=>{
      while(!this.abort.signal.aborted) {
        const job=this.db.prepare("SELECT id,voice_id AS voiceId,phrase_key AS key,fingerprint FROM voice_jobs WHERE state='queued' ORDER BY rowid LIMIT 1").get() as Job|undefined;if(!job)break;
        this.db.prepare("UPDATE voice_jobs SET state='running' WHERE id=?").run(job.id);
        try {
          const wav=await this.synthesize(this.voice(job.voiceId),PHRASES[job.key],this.abort.signal);
          if(this.abort.signal.aborted)throw new VoiceError(503,'interrupted_no_auto_retry');
          this.put(job.voiceId,job.key,wav,'tts',job.fingerprint,true);
          this.db.prepare("UPDATE voice_jobs SET state='done' WHERE id=?").run(job.id);
        } catch(error) {this.db.prepare("UPDATE voice_jobs SET state='failed',error=? WHERE id=?").run(error instanceof VoiceError?error.code:'generation_failed_no_auto_retry',job.id);}
      }
    })().finally(()=>{this.running=null;if(!this.abort.signal.aborted && this.db.prepare("SELECT 1 FROM voice_jobs WHERE state='queued'").get())this.pump();});
  }
  async idle():Promise<void> {await this.running;}
  async close():Promise<void> {this.abort.abort();await this.running;this.db.prepare("UPDATE voice_jobs SET state='failed',error='interrupted_no_auto_retry' WHERE state='queued'").run();}
}
