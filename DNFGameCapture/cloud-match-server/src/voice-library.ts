// 服务器「自定义音频库」：管理员在后台「声音管理」里用任意音色把一句话生成音频（或上传 WAV），
// 发布后所有客户端都能在「声音管理 → 自定义语音」里看到、试听、下载使用。
// 这是管理员自己的操作：不计入主播额度，也不计入 /api/v2/tts 的每日预算累计（单独统计字数供参考）。
import { randomUUID } from 'node:crypto';
import express, { type NextFunction, type Request, type Response, type Router } from 'express';
import type Database from 'better-sqlite3';
import { z } from 'zod';
import { normalizeTtsText, TtsQuotaError } from './tts-generate.js';
import { MAX_WAV, sha256, validateWav, VoiceError, type Synthesize, type VoiceStore } from './voices.js';

export const LIBRARY_MAX_TEXT_CHARS = 200;
export const LIBRARY_MAX_ITEMS = 500;

export interface LibraryItem {
  id: string;
  title: string;
  text: string;
  voiceId: string;
  voiceLabel: string;
  source: 'tts' | 'upload';
  sha256: string;
  bytes: number;
  chars: number;
  enabled: boolean;
  createdAt: number;
  updatedAt: number;
}

const titleSchema = z.string().trim().min(1).max(60)
  .refine(v => !/[\\/:*?"<>|\u0000-\u001f]/.test(v), 'invalid_title');
const generateSchema = z.object({
  voiceId: z.string().regex(/^[a-z0-9][a-z0-9-]{0,63}$/),
  text: z.string().max(1000),
  title: z.string().max(200).optional(),
  requestId: z.string().uuid(),
}).strict();
const updateSchema = z.object({ title: titleSchema.optional(), enabled: z.boolean().optional() }).strict();

/** 标题做成客户端文件名：去掉非法字符，限制长度 */
export function libraryTitle(raw: string | undefined, fallbackText: string): string {
  const base = (raw && raw.trim() ? raw : fallbackText).normalize('NFKC')
    .replace(/[\\/:*?"<>|\u0000-\u001f]/g, '').replace(/\s+/g, ' ').trim();
  return [...base].slice(0, 30).join('') || '自定义音频';
}

export class VoiceLibrary {
  private readonly abort = new AbortController();
  private readonly inflight = new Map<string, Promise<LibraryItem>>();

  constructor(
    private readonly db: Database.Database,
    private readonly voices: VoiceStore,
    private readonly synthesize: Synthesize,
    private readonly now: () => number,
    private readonly configured: () => boolean = () => !!process.env.DOUBAO_TTS_API_KEY && !!process.env.DOUBAO_TTS_APP_KEY,
  ) {
    db.exec(`CREATE TABLE IF NOT EXISTS voice_library (id TEXT PRIMARY KEY, title TEXT NOT NULL, text TEXT NOT NULL,
        voice_id TEXT NOT NULL, source TEXT NOT NULL, sha256 TEXT NOT NULL, bytes INTEGER NOT NULL, chars INTEGER NOT NULL,
        enabled INTEGER NOT NULL DEFAULT 1, request_id TEXT UNIQUE, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS voice_assets (sha256 TEXT PRIMARY KEY, bytes INTEGER NOT NULL, wav BLOB NOT NULL);`);
  }

  private rows(where = '', ...args: unknown[]): LibraryItem[] {
    const labels = new Map(this.voices.voices().map(v => [v.id, v.label]));
    return (this.db.prepare(`SELECT id, title, text, voice_id AS voiceId, source, sha256, bytes, chars, enabled,
        created_at AS createdAt, updated_at AS updatedAt FROM voice_library ${where} ORDER BY created_at DESC, rowid DESC`).all(...args) as
      Array<Omit<LibraryItem, 'enabled' | 'voiceLabel'> & { enabled: number }>)
      .map(r => ({ ...r, enabled: !!r.enabled, voiceLabel: labels.get(r.voiceId) ?? (r.source === 'upload' ? '上传' : r.voiceId) }));
  }

  list(): LibraryItem[] { return this.rows(); }

  item(id: string): LibraryItem {
    const found = this.rows('WHERE id=?', id)[0];
    if (!found) throw new VoiceError(404, 'library_item_not_found');
    return found;
  }

  /** 客户端可见的已发布列表 */
  publicCatalog() {
    const items = this.rows('WHERE enabled=1').map(i => ({
      id: i.id, title: i.title, text: i.text, voiceId: i.voiceId, voiceLabel: i.voiceLabel,
      sha256: i.sha256, bytes: i.bytes, url: `/api/voice/audio/${i.sha256}.wav`, createdAt: i.createdAt,
    }));
    return { schema: 1, items, revision: sha256(JSON.stringify(items)) };
  }

  /** 管理员生成累计字数（仅供参考，不计入主播额度 / 每日预算） */
  stats() {
    const row = this.db.prepare(`SELECT COUNT(*) AS items, COALESCE(SUM(CASE WHEN source='tts' THEN chars END),0) AS chars,
        COALESCE(SUM(enabled),0) AS published FROM voice_library`).get() as { items: number; chars: number; published: number };
    return { ...row, configured: this.configured(), maxTextChars: LIBRARY_MAX_TEXT_CHARS, maxItems: LIBRARY_MAX_ITEMS };
  }

  private storeAsset(wav: Buffer): string {
    validateWav(wav);
    const hash = sha256(wav);
    const size = (this.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM voice_assets').get() as { n: number }).n;
    if (size + wav.length > 1024 * 1024 * 1024 && !this.db.prepare('SELECT 1 FROM voice_assets WHERE sha256=?').get(hash)) {
      throw new VoiceError(413, 'voice_storage_limit');
    }
    this.db.prepare('INSERT OR IGNORE INTO voice_assets VALUES(?,?,?)').run(hash, wav.length, wav);
    return hash;
  }

  private insert(fields: { title: string; text: string; voiceId: string; source: 'tts' | 'upload'; wav: Buffer; chars: number; requestId: string | null }): LibraryItem {
    const id = randomUUID(), nowSec = this.now();
    this.db.transaction(() => {
      if ((this.db.prepare('SELECT COUNT(*) AS n FROM voice_library').get() as { n: number }).n >= LIBRARY_MAX_ITEMS) {
        throw new VoiceError(409, 'library_full');
      }
      const hash = this.storeAsset(fields.wav);
      this.db.prepare(`INSERT INTO voice_library(id, title, text, voice_id, source, sha256, bytes, chars, enabled, request_id, created_at, updated_at)
        VALUES(?,?,?,?,?,?,?,?,1,?,?,?)`).run(id, fields.title, fields.text, fields.voiceId, fields.source, hash, fields.wav.length,
        fields.chars, fields.requestId, nowSec, nowSec);
    })();
    return this.item(id);
  }

  /** 管理员用音色生成一句话（付费，但不计入主播额度 / 每日预算） */
  async generate(input: unknown): Promise<LibraryItem> {
    const parsed = generateSchema.safeParse(input);
    if (!parsed.success) throw new VoiceError(400, 'invalid_library_request');
    const prior = this.db.prepare('SELECT id FROM voice_library WHERE request_id=?').get(parsed.data.requestId) as { id: string } | undefined;
    if (prior) return this.item(prior.id); // 重复提交同一请求：不再扣费
    const running = this.inflight.get(parsed.data.requestId);
    if (running) return running;
    let text: string;
    try { text = normalizeTtsText(parsed.data.text, LIBRARY_MAX_TEXT_CHARS, []); }
    catch (error) { throw new VoiceError(400, error instanceof TtsQuotaError ? error.code : 'invalid_text'); }
    const voice = this.voices.voice(parsed.data.voiceId);
    if (voice.kind !== 'tts' || !voice.speaker) throw new VoiceError(400, 'voice_not_tts');
    if (!this.configured()) throw new VoiceError(503, 'tts_not_configured');
    const title = libraryTitle(parsed.data.title, text);
    const job = (async () => {
      const controller = new AbortController(), onClose = () => controller.abort();
      this.abort.signal.addEventListener('abort', onClose, { once: true });
      const timer = setTimeout(() => controller.abort(), 60_000);
      try {
        const wav = await this.synthesize(voice, text, controller.signal);
        return this.insert({ title, text, voiceId: voice.id, source: 'tts', wav, chars: [...text].length, requestId: parsed.data.requestId });
      } finally {
        clearTimeout(timer);
        this.abort.signal.removeEventListener('abort', onClose);
      }
    })();
    this.inflight.set(parsed.data.requestId, job);
    try { return await job; } finally { this.inflight.delete(parsed.data.requestId); }
  }

  /** 管理员上传一段 WAV（PCM，≤30 秒） */
  upload(titleRaw: string, wav: Buffer): LibraryItem {
    const title = titleSchema.safeParse(libraryTitle(titleRaw, ''));
    if (!title.success || !titleRaw.trim()) throw new VoiceError(400, 'invalid_title');
    return this.insert({ title: title.data, text: '', voiceId: '', source: 'upload', wav, chars: 0, requestId: null });
  }

  update(id: string, input: unknown): LibraryItem {
    this.item(id);
    const parsed = updateSchema.safeParse(input);
    if (!parsed.success) throw new VoiceError(400, 'invalid_library_request');
    if (parsed.data.title !== undefined) this.db.prepare('UPDATE voice_library SET title=?, updated_at=? WHERE id=?').run(parsed.data.title, this.now(), id);
    if (parsed.data.enabled !== undefined) this.db.prepare('UPDATE voice_library SET enabled=?, updated_at=? WHERE id=?').run(+parsed.data.enabled, this.now(), id);
    return this.item(id);
  }

  remove(id: string): void {
    const item = this.item(id);
    this.db.transaction(() => {
      this.db.prepare('DELETE FROM voice_library WHERE id=?').run(id);
      // 音频没有被固定台词或其它条目引用时一并删除
      const referenced = this.db.prepare('SELECT 1 FROM voice_library WHERE sha256=? LIMIT 1').get(item.sha256)
        || this.db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='voice_clips'").get()
          && this.db.prepare('SELECT 1 FROM voice_clips WHERE sha256=? LIMIT 1').get(item.sha256);
      if (!referenced) this.db.prepare('DELETE FROM voice_assets WHERE sha256=?').run(item.sha256);
    })();
  }

  close(): void { this.abort.abort(); }
}

function sendError(error: unknown, _request: Request, response: Response, _next: NextFunction): void {
  const status = error instanceof VoiceError ? error.status : 500;
  response.status(status).json({ ok: false, code: error instanceof VoiceError ? error.code : 'library_operation_failed' });
}

/** 公开：GET /api/voice/library（音频文件走已有的 /api/voice/audio/<sha>.wav） */
export function createPublicLibraryRoute(library: VoiceLibrary) {
  return (request: Request, response: Response): void => {
    const catalog = library.publicCatalog(), etag = `"${catalog.revision}"`;
    response.set('Cache-Control', 'public, max-age=0, must-revalidate').set('ETag', etag).set('X-Content-Type-Options', 'nosniff');
    if (request.get('if-none-match') === etag) { response.status(304).end(); return; }
    response.json(catalog);
  };
}

/** 管理：/admin/api/voice-library */
export function createLibraryAdminApi(library: VoiceLibrary): Router {
  const router = express.Router();
  router.get('/', (_request, response) => response.json({ ok: true, items: library.list(), stats: library.stats() }));
  router.put('/upload', express.raw({ type: ['audio/wav', 'audio/x-wav', 'application/octet-stream'], limit: MAX_WAV }), (request, response) => {
    if (!Buffer.isBuffer(request.body)) throw new VoiceError(415, 'wav_body_required');
    const title = typeof request.query.title === 'string' ? request.query.title : '';
    response.status(201).json({ ok: true, item: library.upload(title, request.body) });
  });
  router.use(express.json({ limit: '16kb' }));
  router.post('/generate', (request, response, next) => {
    library.generate(request.body).then(item => response.status(201).json({ ok: true, item }), next);
  });
  router.put('/:id', (request, response) => response.json({ ok: true, item: library.update(String(request.params.id), request.body) }));
  router.delete('/:id', (request, response) => { library.remove(String(request.params.id)); response.json({ ok: true }); });
  router.use(sendError);
  return router;
}
