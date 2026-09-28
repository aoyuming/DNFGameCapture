import express, { type Request, type Response, type NextFunction, type Router } from 'express';
import { ZodError } from 'zod';
import { MAX_WAV, PHRASES, VoiceError, type VoiceStore } from './voices.js';

function asset(store:VoiceStore, req:Request, res:Response, immutable:boolean):void {
  const file=String(req.params.file), match=/^([a-f0-9]{64})\.wav$/.exec(file);
  const wav=match?store.asset(match[1]):null;
  if(!wav){res.status(404).json({ok:false,code:'audio_not_found'});return;}
  res.set('X-Content-Type-Options','nosniff').set('ETag',`"${match![1]}"`);
  if(immutable)res.set('Cache-Control','public, max-age=31536000, immutable');
  res.type('audio/wav').send(wav);
}
export function createPublicVoiceApi(store:VoiceStore):Router {
  const router=express.Router();
  router.get('/catalog',(req,res)=>{
    const catalog=store.catalog(), etag=`"${catalog.revision}"`;
    res.set('Cache-Control','public, max-age=0, must-revalidate').set('ETag',etag).set('X-Content-Type-Options','nosniff');
    if(req.get('if-none-match')===etag){res.status(304).end();return;}res.json(catalog);
  });
  router.get('/audio/:file',(req,res)=>asset(store,req,res,true));
  return router;
}
export function createVoiceAdminApi(store:VoiceStore):Router {
  const router=express.Router();
  router.get('/state',(_req,res)=>res.json({voices:store.voices().map(v=>({...v,clips:store.clips(v.id)})),phrases:PHRASES,jobs:store.jobs(),revision:store.catalog().revision,ttsConfigured:!!process.env.DOUBAO_TTS_API_KEY&&!!process.env.DOUBAO_TTS_APP_KEY}));
  router.get('/audio/:file',(req,res)=>asset(store,req,res,false));
  router.put('/:id/clips/:key',express.raw({type:['audio/wav','audio/x-wav','application/octet-stream'],limit:MAX_WAV}),(req,res)=>{
    if(!Buffer.isBuffer(req.body))throw new VoiceError(415,'wav_body_required');
    const clip=store.put(String(req.params.id),String(req.params.key),req.body);res.json({ok:true,clip});
  });
  router.use(express.json({limit:'16kb'}));
  router.post('/',(req,res)=>res.status(201).json({ok:true,voice:store.save(req.body)}));
  router.put('/:id',(req,res)=>{
    if(req.body?.id!==req.params.id)throw new VoiceError(400,'immutable_voice_id');
    res.json({ok:true,voice:store.save(req.body,true)});
  });
  router.post('/:id/generate',(req,res)=>res.status(202).json({ok:true,jobs:store.generation(String(req.params.id),req.body)}));
  router.use((error:unknown,_req:Request,res:Response,_next:NextFunction)=>{
    const status=error instanceof VoiceError?error.status:error instanceof ZodError?400:500;
    res.status(status).json({ok:false,code:error instanceof VoiceError?error.code:error instanceof ZodError?'invalid_voice_request':'voice_operation_failed'});
  });
  return router;
}
