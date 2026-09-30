// 用户侧「用豆包音色生成自定义语音」：只在服务器上持有火山引擎密钥，客户端凭 v2 授权会话调用。
// 成本控制（任何一项不满足都直接拒绝，客户端会回退到 Windows 系统语音）：
//   1. 全局缓存：同一音色 + 同一句话只合成一次，之后所有用户免费复用（缓存命中不计额度）。
//   2. 每个授权卡密每日 / 每月字符额度 + 每分钟请求频率。
//   3. 全局每日熔断：按字符和单价预估当日花费，封顶 5 元（HARD_DAILY_BUDGET_YUAN，环境变量只能调低）。
//   4. 单句长度上限、字符白名单、敏感词过滤。
//   5. 后台（/admin/tts）可以：改全局默认额度 / 总开关 / 每日预算（≤5 元）；给单个卡密（主播）
//      单独设置日 / 月额度、加减「额外额度」（日月额度用完后再扣，不过期）、禁止生成、清零今日 / 本月用量。
import type Database from 'better-sqlite3';
import { z } from 'zod';
import { sha256, VoiceError, type Synthesize, type Voice, type VoiceStore } from './voices.js';

/** 每日总花费硬上限（元）。环境变量 TTS_DAILY_BUDGET_YUAN 只能比它低。 */
export const HARD_DAILY_BUDGET_YUAN = 5;

export interface TtsLimits {
  enabled: boolean;
  dailyBudgetYuan: number;
  /** 元 / 万字符；按资源分别计价，未知资源按最贵的算 */
  pricePer10k: Record<string, number>;
  licenseDailyChars: number;
  licenseMonthlyChars: number;
  maxTextChars: number;
  /** 每个卡密每分钟最多真正合成几句（缓存命中不算） */
  licenseGeneratePerMinute: number;
  /** 每个卡密每分钟最多请求几次（含缓存命中） */
  licenseRequestsPerMinute: number;
  globalConcurrency: number;
  cacheMaxBytes: number;
  synthTimeoutMs: number;
}

function num(env: NodeJS.ProcessEnv, name: string, fallback: number, min: number, max: number): number {
  const raw = env[name]?.trim();
  if (!raw) return fallback;
  const value = Number(raw);
  return Number.isFinite(value) ? Math.max(min, Math.min(max, value)) : fallback;
}

export function ttsLimitsFromEnv(env: NodeJS.ProcessEnv = process.env): TtsLimits {
  return {
    enabled: !/^(0|false|off|no)$/i.test(env.TTS_GENERATE_ENABLED?.trim() ?? ''),
    dailyBudgetYuan: num(env, 'TTS_DAILY_BUDGET_YUAN', HARD_DAILY_BUDGET_YUAN, 0, HARD_DAILY_BUDGET_YUAN),
    // 官方后付费价：豆包语音合成 2.0 = 3 元/万字符；1.0（大模型语音合成）按 5 元/万字符保守估算。
    pricePer10k: {
      'seed-tts-2.0': num(env, 'TTS_PRICE_SEED2_PER_10K', 3, 0.1, 100),
      'seed-tts-1.0': num(env, 'TTS_PRICE_SEED1_PER_10K', 5, 0.1, 100),
    },
    licenseDailyChars: Math.round(num(env, 'TTS_LICENSE_DAILY_CHARS', 1500, 0, 1_000_000)),
    licenseMonthlyChars: Math.round(num(env, 'TTS_LICENSE_MONTHLY_CHARS', 20_000, 0, 10_000_000)),
    maxTextChars: Math.round(num(env, 'TTS_MAX_TEXT_CHARS', 30, 1, 100)),
    licenseGeneratePerMinute: Math.round(num(env, 'TTS_LICENSE_GENERATE_PER_MINUTE', 4, 1, 120)),
    licenseRequestsPerMinute: Math.round(num(env, 'TTS_LICENSE_REQUESTS_PER_MINUTE', 30, 1, 600)),
    globalConcurrency: Math.round(num(env, 'TTS_GLOBAL_CONCURRENCY', 3, 1, 32)),
    cacheMaxBytes: Math.round(num(env, 'TTS_CACHE_MAX_MB', 1024, 16, 65_536) * 1024 * 1024),
    synthTimeoutMs: Math.round(num(env, 'TTS_SYNTH_TIMEOUT_MS', 20_000, 2_000, 60_000)),
  };
}

export class TtsQuotaError extends Error {
  constructor(public status: number, public code: string, public details: Record<string, unknown> = {}) {
    super(code);
  }
}

// 明显不适合在直播里念出来的词（辱骂 / 色情 / 违法 / 政治）。可用 TTS_BLOCKLIST_FILE 追加（每行一个）。
const BUILTIN_BLOCKLIST = [
  '傻逼', '傻b', 'sb', '煞笔', '沙比', '操你', '草你', '肏', '日你', '干你娘', '草泥马', '尼玛', '你妈', '妈的', '他妈', 'tmd', 'nmsl',
  '死全家', '全家死', '去死吧', '狗日', '杂种', '畜生', '婊子', '贱人', '贱货', '滚蛋', '废物东西', '脑残', '智障',
  '鸡巴', '屌', '阴茎', '阴道', '做爱', '性交', '强奸', '轮奸', '裸聊', '约炮', '嫖', '妓女', '色情', '黄片', 'av女优',
  'fuck', 'shit', 'bitch', 'cunt', 'nigger', 'nigga', 'dick', 'pussy',
  '毒品', '冰毒', '海洛因', '大麻', '赌博', '博彩', '六合彩', '代开发票', '枪支', '炸药', '恐怖袭击', '自杀',
  '习近平', '毛泽东', '共产党', '共匪', '法轮', '六四', '天安门事件', '台独', '藏独', '疆独', '港独', '反共', '推翻政府',
];

export function compactForBlocklist(text: string): string {
  return text.normalize('NFKC').toLowerCase().replace(/[^\p{L}\p{N}]/gu, '');
}

const ALLOWED_TEXT = /^[\p{Script=Han}\p{L}\p{N} ，。！？、；：“”‘’（）《》…—·,.!?;:'"()\-~%+]+$/u;

export function normalizeTtsText(raw: unknown, maxChars: number, blocklist: readonly string[]): string {
  if (typeof raw !== 'string') throw new TtsQuotaError(400, 'invalid_text');
  const text = raw.normalize('NFKC')
    .replace(/[\u200B-\u200F\u2028-\u202F\u2060-\u206F\uFEFF]/g, '')
    .replace(/[\u0000-\u001F\u007F]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
  if (!text) throw new TtsQuotaError(400, 'empty_text');
  const chars = [...text].length;
  if (chars > maxChars) throw new TtsQuotaError(400, 'text_too_long', { maxChars, chars });
  if (!ALLOWED_TEXT.test(text)) throw new TtsQuotaError(400, 'text_invalid_chars');
  const compact = compactForBlocklist(text);
  if (blocklist.some(word => word && compact.includes(word))) throw new TtsQuotaError(400, 'text_blocked');
  return text;
}

export interface TtsUsage {
  dayUsed: number;
  dayLimit: number;
  monthUsed: number;
  monthLimit: number;
  /** 后台单独加的额外额度（日 / 月额度用完后再扣，不过期） */
  bonusLeft: number;
  disabled: boolean;
}

export interface TtsLicenseQuota {
  dailyLimit: number | null;
  monthlyLimit: number | null;
  bonusChars: number;
  disabled: boolean;
  note: string;
}

/** 后台可在线修改的全局设置（未设置的项沿用环境变量） */
export interface TtsSettings {
  enabled?: boolean;
  dailyBudgetYuan?: number;
  licenseDailyChars?: number;
  licenseMonthlyChars?: number;
}

export const ttsSettingsSchema = z.object({
  enabled: z.boolean().optional(),
  dailyBudgetYuan: z.number().min(0).max(HARD_DAILY_BUDGET_YUAN).optional(),
  licenseDailyChars: z.number().int().min(0).max(1_000_000).optional(),
  licenseMonthlyChars: z.number().int().min(0).max(10_000_000).optional(),
}).strict();

export const ttsLicenseQuotaSchema = z.object({
  dailyLimit: z.number().int().min(0).max(1_000_000).nullable(),
  monthlyLimit: z.number().int().min(0).max(10_000_000).nullable(),
  disabled: z.boolean(),
  note: z.string().trim().max(200).default(''),
}).strict();

export const ttsBonusSchema = z.object({
  delta: z.number().int().min(-10_000_000).max(10_000_000).refine(v => v !== 0),
  note: z.string().trim().max(200).default(''),
}).strict();

export const ttsResetSchema = z.object({ scope: z.enum(['day', 'month']) }).strict();

export interface TtsResult {
  wav: Buffer;
  cached: boolean;
  voiceId: string;
  text: string;
  chars: number;
  usage: TtsUsage;
}

/** 按北京时间切日 / 切月 */
export function shanghaiPeriods(nowSec: number): { day: string; month: string } {
  const iso = new Date((nowSec + 8 * 3600) * 1000).toISOString();
  return { day: iso.slice(0, 10), month: iso.slice(0, 7) };
}

const generateSchema = z.object({
  voice: z.string().regex(/^[a-z0-9][a-z0-9-]{0,63}$/),
  text: z.string().max(400),
}).strict();

export class TtsGenerator {
  private readonly blocklist: string[];
  private readonly abort = new AbortController();
  private readonly inflight = new Map<string, Promise<Buffer>>();
  private readonly hits = new Map<string, number[]>();
  private active = 0;
  private warnedDay = '';

  constructor(
    private readonly db: Database.Database,
    private readonly voices: VoiceStore,
    private readonly synthesize: Synthesize,
    private readonly now: () => number,
    private readonly baseLimits: TtsLimits = ttsLimitsFromEnv(),
    extraBlocklist: readonly string[] = [],
    private readonly configured: () => boolean = () => !!process.env.DOUBAO_TTS_API_KEY && !!process.env.DOUBAO_TTS_APP_KEY,
  ) {
    this.blocklist = [...new Set([...BUILTIN_BLOCKLIST, ...extraBlocklist].map(compactForBlocklist).filter(Boolean))];
    db.exec(`CREATE TABLE IF NOT EXISTS tts_cache (cache_key TEXT PRIMARY KEY, voice_id TEXT NOT NULL, text TEXT NOT NULL,
        wav BLOB NOT NULL, bytes INTEGER NOT NULL, chars INTEGER NOT NULL, created_at INTEGER NOT NULL,
        last_used_at INTEGER NOT NULL, hits INTEGER NOT NULL DEFAULT 0);
      CREATE INDEX IF NOT EXISTS tts_cache_lru ON tts_cache(last_used_at);
      CREATE TABLE IF NOT EXISTS tts_license_usage (period TEXT NOT NULL, license_id INTEGER NOT NULL,
        chars INTEGER NOT NULL DEFAULT 0, requests INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(period, license_id));
      CREATE TABLE IF NOT EXISTS tts_daily_spend (day TEXT PRIMARY KEY, chars INTEGER NOT NULL DEFAULT 0,
        cost_micro INTEGER NOT NULL DEFAULT 0, requests INTEGER NOT NULL DEFAULT 0);
      CREATE TABLE IF NOT EXISTS tts_license_quota (license_id INTEGER PRIMARY KEY, daily_limit INTEGER, monthly_limit INTEGER,
        bonus_chars INTEGER NOT NULL DEFAULT 0, disabled INTEGER NOT NULL DEFAULT 0, note TEXT NOT NULL DEFAULT '',
        updated_at INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS tts_settings (name TEXT PRIMARY KEY, value_json TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS tts_quota_audit (id INTEGER PRIMARY KEY AUTOINCREMENT, license_id INTEGER, action TEXT NOT NULL,
        details_json TEXT NOT NULL, created_at INTEGER NOT NULL);`);
    const usageColumns = db.prepare('PRAGMA table_info(tts_license_usage)').all() as Array<{ name: string }>;
    if (!usageColumns.some(c => c.name === 'bonus')) db.exec('ALTER TABLE tts_license_usage ADD COLUMN bonus INTEGER NOT NULL DEFAULT 0');
  }

  /** 生效中的限制 = 环境变量 + 后台在线修改的设置 */
  private get limits(): TtsLimits {
    const settings = this.settings();
    return {
      ...this.baseLimits,
      enabled: settings.enabled ?? this.baseLimits.enabled,
      dailyBudgetYuan: Math.min(settings.dailyBudgetYuan ?? this.baseLimits.dailyBudgetYuan, HARD_DAILY_BUDGET_YUAN),
      licenseDailyChars: settings.licenseDailyChars ?? this.baseLimits.licenseDailyChars,
      licenseMonthlyChars: settings.licenseMonthlyChars ?? this.baseLimits.licenseMonthlyChars,
    };
  }

  settings(): TtsSettings {
    const rows = this.db.prepare('SELECT name, value_json FROM tts_settings').all() as Array<{ name: string; value_json: string }>;
    const raw: Record<string, unknown> = {};
    for (const row of rows) { try { raw[row.name] = JSON.parse(row.value_json); } catch { /* ignore */ } }
    const parsed = ttsSettingsSchema.safeParse(raw);
    return parsed.success ? parsed.data : {};
  }

  private audit(licenseId: number | null, action: string, details: object): void {
    this.db.prepare('INSERT INTO tts_quota_audit(license_id, action, details_json, created_at) VALUES(?,?,?,?)')
      .run(licenseId, action, JSON.stringify(details), this.now());
  }

  quota(licenseId: number): TtsLicenseQuota {
    const row = this.db.prepare('SELECT daily_limit, monthly_limit, bonus_chars, disabled, note FROM tts_license_quota WHERE license_id=?')
      .get(licenseId) as { daily_limit: number | null; monthly_limit: number | null; bonus_chars: number; disabled: number; note: string } | undefined;
    return {
      dailyLimit: row?.daily_limit ?? null,
      monthlyLimit: row?.monthly_limit ?? null,
      bonusChars: row?.bonus_chars ?? 0,
      disabled: !!row?.disabled,
      note: row?.note ?? '',
    };
  }

  private effectiveLimits(licenseId: number, limits = this.limits): { day: number; month: number; quota: TtsLicenseQuota } {
    const quota = this.quota(licenseId);
    return { day: quota.dailyLimit ?? limits.licenseDailyChars, month: quota.monthlyLimit ?? limits.licenseMonthlyChars, quota };
  }

  private ttsVoices(): Voice[] {
    return this.voices.voices().filter(v => v.kind === 'tts' && v.enabled && !!v.speaker);
  }

  private priceMicroPerChar(voice: Voice): number {
    const prices = Object.values(this.limits.pricePer10k);
    const per10k = this.limits.pricePer10k[voice.resource] ?? Math.max(...prices, 5);
    return per10k * 100; // 元/万字符 → 微元/字符
  }

  private budgetMicro(): number {
    return Math.floor(Math.min(this.limits.dailyBudgetYuan, HARD_DAILY_BUDGET_YUAN) * 1_000_000);
  }

  private licenseChars(period: string, licenseId: number): number {
    return (this.db.prepare('SELECT chars FROM tts_license_usage WHERE period=? AND license_id=?')
      .get(period, licenseId) as { chars: number } | undefined)?.chars ?? 0;
  }

  private spend(day: string): { chars: number; cost: number } {
    const row = this.db.prepare('SELECT chars, cost_micro AS cost FROM tts_daily_spend WHERE day=?')
      .get(day) as { chars: number; cost: number } | undefined;
    return row ?? { chars: 0, cost: 0 };
  }

  usage(licenseId: number): TtsUsage {
    const { day, month } = shanghaiPeriods(this.now());
    const eff = this.effectiveLimits(licenseId);
    return {
      dayUsed: this.licenseChars('d:' + day, licenseId),
      dayLimit: eff.day,
      monthUsed: this.licenseChars('m:' + month, licenseId),
      monthLimit: eff.month,
      bonusLeft: eff.quota.bonusChars,
      disabled: eff.quota.disabled,
    };
  }

  status(licenseId: number) {
    const { day } = shanghaiPeriods(this.now());
    const limits = this.limits;
    const spent = this.spend(day).cost;
    const cheapest = Math.min(...this.ttsVoices().map(v => this.priceMicroPerChar(v)), Number.POSITIVE_INFINITY);
    return {
      ok: true,
      enabled: limits.enabled && this.configured() && !this.quota(licenseId).disabled,
      maxTextChars: limits.maxTextChars,
      voices: this.ttsVoices().map(v => ({ index: v.index, id: v.id, label: v.label })),
      usage: this.usage(licenseId),
      // 只告诉客户端「今天是否还能生成」，不暴露具体花费。
      budgetExhausted: spent + (Number.isFinite(cheapest) ? cheapest : 0) > this.budgetMicro(),
    };
  }

  /** 管理员查看：最近几天的字符数与预估花费（元） */
  spendSummary(days = 7): Array<{ day: string; chars: number; costYuan: number; requests: number }> {
    return (this.db.prepare('SELECT day, chars, cost_micro, requests FROM tts_daily_spend ORDER BY day DESC LIMIT ?')
      .all(days) as Array<{ day: string; chars: number; cost_micro: number; requests: number }>)
      .map(r => ({ day: r.day, chars: r.chars, costYuan: r.cost_micro / 1_000_000, requests: r.requests }));
  }

  private allow(bucket: string, perMinute: number): boolean {
    const nowSec = this.now();
    const recent = (this.hits.get(bucket) ?? []).filter(t => nowSec - t < 60);
    if (recent.length >= perMinute) { this.hits.set(bucket, recent); return false; }
    recent.push(nowSec);
    this.hits.set(bucket, recent);
    if (this.hits.size > 20_000) {
      for (const [key, list] of this.hits) if (!list.some(t => nowSec - t < 60)) this.hits.delete(key);
    }
    return true;
  }

  private cacheGet(key: string): Buffer | null {
    const row = this.db.prepare('SELECT wav FROM tts_cache WHERE cache_key=?').get(key) as { wav: Buffer } | undefined;
    if (!row) return null;
    this.db.prepare('UPDATE tts_cache SET last_used_at=?, hits=hits+1 WHERE cache_key=?').run(this.now(), key);
    return row.wav;
  }

  private cachePut(key: string, voiceId: string, text: string, chars: number, wav: Buffer): void {
    const nowSec = this.now();
    this.db.prepare(`INSERT INTO tts_cache(cache_key, voice_id, text, wav, bytes, chars, created_at, last_used_at)
      VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(cache_key) DO UPDATE SET wav=excluded.wav, bytes=excluded.bytes, last_used_at=excluded.last_used_at`)
      .run(key, voiceId, text, wav, wav.length, chars, nowSec, nowSec);
    // 超出容量时按最久未使用淘汰（客户端本地仍有自己的缓存）。
    for (let guard = 0; guard < 100; guard++) {
      const total = (this.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM tts_cache').get() as { n: number }).n;
      if (total <= this.limits.cacheMaxBytes) break;
      this.db.prepare('DELETE FROM tts_cache WHERE cache_key IN (SELECT cache_key FROM tts_cache WHERE cache_key<>? ORDER BY last_used_at ASC LIMIT 50)').run(key);
    }
  }

  /**
   * 预留额度（一个事务内检查并扣除）：先用卡密的日 / 月额度，不够时整句改扣「额外额度」；
   * 全局每日预算任何情况都不能突破。返回扣费来源，失败时按来源退回。
   */
  private reserve(licenseId: number, chars: number, cost: number): 'normal' | 'bonus' {
    const { day, month } = shanghaiPeriods(this.now());
    return this.db.transaction((): 'normal' | 'bonus' => {
      const eff = this.effectiveLimits(licenseId);
      if (eff.quota.disabled) throw new TtsQuotaError(403, 'tts_disabled');
      const dayUsed = this.licenseChars('d:' + day, licenseId);
      const monthUsed = this.licenseChars('m:' + month, licenseId);
      const normalOk = dayUsed + chars <= eff.day && monthUsed + chars <= eff.month;
      const source = normalOk ? 'normal' : eff.quota.bonusChars >= chars ? 'bonus' : null;
      if (!source) {
        if (dayUsed + chars > eff.day) {
          throw new TtsQuotaError(429, 'quota_daily', { dayUsed, dayLimit: eff.day, bonusLeft: eff.quota.bonusChars });
        }
        throw new TtsQuotaError(429, 'quota_monthly', { monthUsed, monthLimit: eff.month, bonusLeft: eff.quota.bonusChars });
      }
      const spent = this.spend(day).cost;
      if (spent + cost > this.budgetMicro()) throw new TtsQuotaError(429, 'budget_exhausted');
      const upsertLicense = this.db.prepare(`INSERT INTO tts_license_usage(period, license_id, chars, requests, bonus) VALUES(?,?,?,1,?)
        ON CONFLICT(period, license_id) DO UPDATE SET chars=chars+excluded.chars, bonus=bonus+excluded.bonus, requests=requests+1`);
      const normalChars = source === 'normal' ? chars : 0, bonusChars = source === 'bonus' ? chars : 0;
      upsertLicense.run('d:' + day, licenseId, normalChars, bonusChars);
      upsertLicense.run('m:' + month, licenseId, normalChars, bonusChars);
      if (source === 'bonus') {
        this.db.prepare('UPDATE tts_license_quota SET bonus_chars=bonus_chars-? WHERE license_id=?').run(chars, licenseId);
      }
      this.db.prepare(`INSERT INTO tts_daily_spend(day, chars, cost_micro, requests) VALUES(?,?,?,1)
        ON CONFLICT(day) DO UPDATE SET chars=chars+excluded.chars, cost_micro=cost_micro+excluded.cost_micro, requests=requests+1`)
        .run(day, chars, cost);
      if (this.warnedDay !== day && spent + cost > this.budgetMicro() * 0.8) {
        this.warnedDay = day;
        console.warn(`[tts] ${day} 预估花费已超过每日预算的 80%（${((spent + cost) / 1_000_000).toFixed(2)} 元）`);
      }
      return source;
    })();
  }

  private refund(licenseId: number, chars: number, cost: number, refundGlobal: boolean, source: 'normal' | 'bonus'): void {
    const { day, month } = shanghaiPeriods(this.now());
    this.db.transaction(() => {
      const column = source === 'bonus' ? 'bonus' : 'chars';
      const dec = this.db.prepare(`UPDATE tts_license_usage SET ${column}=MAX(0, ${column}-?) WHERE period=? AND license_id=?`);
      dec.run(chars, 'd:' + day, licenseId);
      dec.run(chars, 'm:' + month, licenseId);
      if (source === 'bonus') this.db.prepare('UPDATE tts_license_quota SET bonus_chars=bonus_chars+? WHERE license_id=?').run(chars, licenseId);
      if (refundGlobal) {
        this.db.prepare('UPDATE tts_daily_spend SET chars=MAX(0, chars-?), cost_micro=MAX(0, cost_micro-?) WHERE day=?').run(chars, cost, day);
      }
    })();
  }

  /* ================= 后台管理 ================= */

  private requireLicense(licenseId: number): void {
    if (!this.db.prepare('SELECT 1 FROM licenses WHERE id=?').get(licenseId)) throw new TtsQuotaError(404, 'license_not_found');
  }

  private upsertQuota(licenseId: number, patch: Partial<{ daily_limit: number | null; monthly_limit: number | null; bonus_chars: number; disabled: number; note: string }>): void {
    const current = this.quota(licenseId);
    const next = {
      daily_limit: current.dailyLimit, monthly_limit: current.monthlyLimit, bonus_chars: current.bonusChars,
      disabled: current.disabled ? 1 : 0, note: current.note, ...patch,
    };
    this.db.prepare(`INSERT INTO tts_license_quota(license_id, daily_limit, monthly_limit, bonus_chars, disabled, note, updated_at)
      VALUES(?,?,?,?,?,?,?) ON CONFLICT(license_id) DO UPDATE SET daily_limit=excluded.daily_limit, monthly_limit=excluded.monthly_limit,
      bonus_chars=excluded.bonus_chars, disabled=excluded.disabled, note=excluded.note, updated_at=excluded.updated_at`)
      .run(licenseId, next.daily_limit, next.monthly_limit, next.bonus_chars, next.disabled, next.note, this.now());
  }

  updateSettings(input: unknown): TtsSettings {
    const parsed = ttsSettingsSchema.safeParse(input);
    if (!parsed.success) throw new TtsQuotaError(400, 'invalid_request');
    this.db.transaction(() => {
      for (const [name, value] of Object.entries(parsed.data)) {
        if (value === undefined) continue;
        this.db.prepare('INSERT INTO tts_settings(name, value_json) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET value_json=excluded.value_json')
          .run(name, JSON.stringify(value));
      }
      this.audit(null, 'settings', parsed.data);
    })();
    return this.settings();
  }

  setLicenseQuota(licenseId: number, input: unknown): TtsLicenseQuota {
    this.requireLicense(licenseId);
    const parsed = ttsLicenseQuotaSchema.safeParse(input);
    if (!parsed.success) throw new TtsQuotaError(400, 'invalid_request');
    this.db.transaction(() => {
      this.upsertQuota(licenseId, {
        daily_limit: parsed.data.dailyLimit, monthly_limit: parsed.data.monthlyLimit,
        disabled: parsed.data.disabled ? 1 : 0, note: parsed.data.note,
      });
      this.audit(licenseId, 'quota', parsed.data);
    })();
    return this.quota(licenseId);
  }

  addBonus(licenseId: number, input: unknown): TtsLicenseQuota {
    this.requireLicense(licenseId);
    const parsed = ttsBonusSchema.safeParse(input);
    if (!parsed.success) throw new TtsQuotaError(400, 'invalid_request');
    this.db.transaction(() => {
      const before = this.quota(licenseId).bonusChars;
      const after = Math.max(0, before + parsed.data.delta);
      this.upsertQuota(licenseId, { bonus_chars: after });
      this.audit(licenseId, 'bonus', { delta: parsed.data.delta, before, after, note: parsed.data.note });
    })();
    return this.quota(licenseId);
  }

  resetUsage(licenseId: number, input: unknown): TtsUsage {
    this.requireLicense(licenseId);
    const parsed = ttsResetSchema.safeParse(input);
    if (!parsed.success) throw new TtsQuotaError(400, 'invalid_request');
    const { day, month } = shanghaiPeriods(this.now());
    const period = parsed.data.scope === 'day' ? 'd:' + day : 'm:' + month;
    this.db.transaction(() => {
      const before = this.licenseChars(period, licenseId);
      // 只清零「日 / 月额度」的用量；已经花掉的额外额度不退，全局预算也不退（钱已经花了）。
      this.db.prepare('UPDATE tts_license_usage SET chars=0 WHERE period=? AND license_id=?').run(period, licenseId);
      this.audit(licenseId, 'reset', { scope: parsed.data.scope, period, before });
    })();
    return this.usage(licenseId);
  }

  quotaAudit(licenseId: number | null, limit = 100) {
    const rows = (licenseId === null
      ? this.db.prepare('SELECT id, license_id AS licenseId, action, details_json, created_at AS createdAt FROM tts_quota_audit ORDER BY id DESC LIMIT ?').all(limit)
      : this.db.prepare('SELECT id, license_id AS licenseId, action, details_json, created_at AS createdAt FROM tts_quota_audit WHERE license_id=? ORDER BY id DESC LIMIT ?').all(licenseId, limit)
    ) as Array<{ id: number; licenseId: number | null; action: string; details_json: string; createdAt: number }>;
    return rows.map(({ details_json, ...row }) => ({ ...row, details: JSON.parse(details_json) as object }));
  }

  /** 后台总览：全局设置、今日花费、最近几天、每个卡密的额度与用量 */
  adminState() {
    const limits = this.limits;
    const { day, month } = shanghaiPeriods(this.now());
    const spent = this.spend(day);
    const cache = this.db.prepare('SELECT COUNT(*) AS entries, COALESCE(SUM(bytes),0) AS bytes, COALESCE(SUM(hits),0) AS hits FROM tts_cache')
      .get() as { entries: number; bytes: number; hits: number };
    // 主播名来自 broadcaster_license_links（由主播归属服务建表；单独使用时可能不存在）
    const hasLinks = !!this.db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='broadcaster_license_links'").get();
    const broadcasterName = hasLinks ? `(SELECT link.broadcaster_name FROM broadcaster_license_links AS link
               WHERE link.license_device_id = l.bound_device_id
               ORDER BY link.source='manual' DESC, link.updated_at DESC, link.rowid DESC LIMIT 1)` : 'NULL';
    const licenses = this.db.prepare(`
      SELECT l.id, l.label, l.bound_device_id AS boundDeviceId, l.expires_at AS expiresAt, l.disabled_at AS disabledAt,
             q.daily_limit AS dailyLimit, q.monthly_limit AS monthlyLimit, COALESCE(q.bonus_chars,0) AS bonusChars,
             COALESCE(q.disabled,0) AS ttsDisabled, COALESCE(q.note,'') AS note,
             COALESCE(d.chars,0) AS dayUsed, COALESCE(d.bonus,0) AS dayBonusUsed, COALESCE(d.requests,0) AS dayRequests,
             COALESCE(m.chars,0) AS monthUsed, COALESCE(m.bonus,0) AS monthBonusUsed,
             ${broadcasterName} AS broadcasterName
      FROM licenses AS l
      LEFT JOIN tts_license_quota AS q ON q.license_id = l.id
      LEFT JOIN tts_license_usage AS d ON d.license_id = l.id AND d.period = ?
      LEFT JOIN tts_license_usage AS m ON m.license_id = l.id AND m.period = ?
      ORDER BY (COALESCE(m.chars,0) + COALESCE(m.bonus,0)) DESC, l.id DESC`).all('d:' + day, 'm:' + month) as Array<Record<string, unknown>>;
    return {
      ok: true,
      configured: this.configured(),
      day,
      month,
      hardDailyBudgetYuan: HARD_DAILY_BUDGET_YUAN,
      settings: {
        enabled: limits.enabled,
        dailyBudgetYuan: limits.dailyBudgetYuan,
        licenseDailyChars: limits.licenseDailyChars,
        licenseMonthlyChars: limits.licenseMonthlyChars,
        maxTextChars: limits.maxTextChars,
        licenseGeneratePerMinute: limits.licenseGeneratePerMinute,
        pricePer10k: limits.pricePer10k,
      },
      today: { chars: spent.chars, costYuan: spent.cost / 1_000_000, budgetYuan: this.budgetMicro() / 1_000_000 },
      recent: this.spendSummary(14),
      cache: { entries: cache.entries, megabytes: Math.round(cache.bytes / 1048576 * 10) / 10, hits: cache.hits },
      licenses: licenses.map(l => ({ ...l, ttsDisabled: !!l.ttsDisabled, broadcasterName: l.broadcasterName ?? '' })),
    };
  }

  async generate(licenseId: number, input: unknown): Promise<TtsResult> {
    const parsed = generateSchema.safeParse(input);
    if (!parsed.success) throw new TtsQuotaError(400, 'invalid_request');
    const text = normalizeTtsText(parsed.data.text, this.limits.maxTextChars, this.blocklist);
    const chars = [...text].length;
    let voice: Voice;
    try { voice = this.voices.voice(parsed.data.voice); } catch { throw new TtsQuotaError(404, 'voice_not_found'); }
    if (voice.kind !== 'tts' || !voice.enabled || !voice.speaker) throw new TtsQuotaError(400, 'voice_not_tts');
    if (this.quota(licenseId).disabled) throw new TtsQuotaError(403, 'tts_disabled');
    const limits = this.limits;
    if (!this.allow('r:' + licenseId, limits.licenseRequestsPerMinute)) throw new TtsQuotaError(429, 'rate_limited', { retryAfterSec: 60 });

    const key = sha256(JSON.stringify([voice.id, voice.speaker, voice.resource, text, 'pcm24k-wav']));
    const result = (wav: Buffer, cached: boolean): TtsResult => ({ wav, cached, voiceId: voice.id, text, chars, usage: this.usage(licenseId) });
    const hit = this.cacheGet(key);
    if (hit) return result(hit, true);
    const pending = this.inflight.get(key);
    if (pending) return result(await pending, true);

    if (!limits.enabled) throw new TtsQuotaError(503, 'tts_generation_disabled');
    if (!this.configured()) throw new TtsQuotaError(503, 'tts_not_configured');
    if (!this.allow('g:' + licenseId, limits.licenseGeneratePerMinute)) throw new TtsQuotaError(429, 'rate_limited', { retryAfterSec: 60 });
    if (this.active >= limits.globalConcurrency) throw new TtsQuotaError(429, 'busy', { retryAfterSec: 3 });

    const cost = Math.ceil(chars * this.priceMicroPerChar(voice));
    const source = this.reserve(licenseId, chars, cost);
    this.active++;
    const controller = new AbortController();
    const onClose = () => controller.abort();
    this.abort.signal.addEventListener('abort', onClose, { once: true });
    const timer = setTimeout(() => controller.abort(), this.limits.synthTimeoutMs);
    const job = (async () => {
      const wav = await this.synthesize(voice, text, controller.signal);
      if (!Buffer.isBuffer(wav) || wav.length < 46 || wav.toString('ascii', 0, 4) !== 'RIFF') throw new VoiceError(502, 'invalid_tts_audio');
      this.cachePut(key, voice.id, text, chars, wav);
      return wav;
    })();
    this.inflight.set(key, job);
    try {
      return result(await job, false);
    } catch (error) {
      // 请求没到火山（未配置 / HTTP 失败）一般不计费，全局额度也退回；中途失败 / 超时可能已计费，只退卡密额度。
      const code = error instanceof VoiceError ? error.code : '';
      this.refund(licenseId, chars, cost, code === 'tts_not_configured' || code === 'tts_request_failed', source);
      if (error instanceof VoiceError || error instanceof TtsQuotaError) throw error;
      throw new VoiceError(502, controller.signal.aborted ? 'tts_timeout' : 'tts_failed');
    } finally {
      clearTimeout(timer);
      this.abort.signal.removeEventListener('abort', onClose);
      this.inflight.delete(key);
      this.active--;
    }
  }

  close(): void {
    this.abort.abort();
  }
}
