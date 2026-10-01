/* ================= 场景规则（情景模式） =================
 * 击杀展示窗口（kill.js）与主界面（main.js）共用：
 *   触发（击杀 / 胜利 / 每局开始 / 血量变化）+ 条件（全部满足 / 任一满足）+ 动作（特效、横幅文字、语音）。
 * 规则从上到下匹配：第一条命中的规则决定主特效；勾选「追加播放」的规则如果排在后面也命中，
 * 会在主特效播完后再追加播放（默认的「复仇」就是这样）。
 * 规则保存在 %APPDATA%\DNFGameCapture\scene_rules.json（C++ /api/scene-rules）。
 * ======================================================= */
(function (global) {
    'use strict';

    const VERSION = 1;

    const TRIGGERS = [
        { id: 'kill', label: '击杀时' },
        { id: 'victory', label: '比赛胜利时' },
        { id: 'round', label: '每局开始时（识别到「开始!!!」）' },
        { id: 'hp', label: '对战中血量变化时' }
    ];

    // type：number / player / team / bool / chance；hp = 血量类（未识别到血条时条件不成立）
    const FIELDS = [
        { key: 'killer', label: '击杀者（选手）', type: 'player', triggers: ['kill'] },
        { key: 'victim', label: '死者（选手）', type: 'player', triggers: ['kill'] },
        { key: 'killerTeam', label: '击杀者队伍', type: 'team', triggers: ['kill'] },
        // 画面左右：不分红蓝、不管有没有翻转，只看这名选手的队伍当前显示在左边还是右边
        { key: 'killerLeft', label: '击杀者在左侧队伍', type: 'bool', triggers: ['kill'] },
        { key: 'victimLeft', label: '死者在左侧队伍', type: 'bool', triggers: ['kill'] },
        { key: 'streak', label: '当前连杀', type: 'number', triggers: ['kill'], unit: '杀' },
        { key: 'killerKills', label: '击杀者 击杀数', type: 'number', triggers: ['kill'] },
        { key: 'killerDeaths', label: '击杀者 死亡数', type: 'number', triggers: ['kill'] },
        { key: 'killerAk', label: '击杀者 AK 次数', type: 'number', triggers: ['kill'] },
        { key: 'victimKills', label: '死者 击杀数', type: 'number', triggers: ['kill'] },
        { key: 'victimDeaths', label: '死者 死亡数', type: 'number', triggers: ['kill'] },
        { key: 'victimStreak', label: '死者 被终结前连杀', type: 'number', triggers: ['kill'], unit: '杀' },
        { key: 'isFirst', label: '是本场一血', type: 'bool', triggers: ['kill'] },
        { key: 'isRevenge', label: '是复仇（上局被对方击杀）', type: 'bool', triggers: ['kill'] },
        { key: 'isAk', label: '达成 AK（一人团灭）', type: 'bool', triggers: ['kill'] },
        { key: 'killerHp', label: '击杀者 剩余血量', type: 'number', hp: true, triggers: ['kill'], unit: '%' },
        { key: 'killerStartHp', label: '击杀者 开局血量', type: 'number', hp: true, triggers: ['kill'], unit: '%' },
        { key: 'victimStartHp', label: '对方 开局血量', type: 'number', hp: true, triggers: ['kill'], unit: '%' },
        { key: 'killerMinHp', label: '击杀者 本局最低血量', type: 'number', hp: true, triggers: ['kill'], unit: '%' },
        { key: 'hpLost', label: '击杀者 本局掉血', type: 'number', hp: true, triggers: ['kill'], unit: '%' },
        { key: 'team', label: '本方队伍', type: 'team', triggers: ['hp'] },
        { key: 'teamLeft', label: '本方在左侧队伍', type: 'bool', triggers: ['hp'] },
        { key: 'selfHp', label: '本方 当前血量', type: 'number', hp: true, triggers: ['hp'], unit: '%' },
        { key: 'enemyHp', label: '对方 当前血量', type: 'number', hp: true, triggers: ['hp'], unit: '%' },
        { key: 'selfStartHp', label: '本方 开局血量', type: 'number', hp: true, triggers: ['hp'], unit: '%' },
        { key: 'enemyStartHp', label: '对方 开局血量', type: 'number', hp: true, triggers: ['hp'], unit: '%' },
        { key: 'winnerTeam', label: '获胜队伍', type: 'team', triggers: ['victory'] },
        { key: 'winnerLeft', label: '获胜队伍在左侧', type: 'bool', triggers: ['victory'] },
        { key: 'roundSec', label: '本局用时', type: 'number', hp: true, triggers: ['kill', 'hp'], unit: '秒' },
        { key: 'myScore', label: '本方大比分', type: 'number', triggers: ['kill', 'hp', 'victory'] },
        { key: 'enemyScore', label: '对方大比分', type: 'number', triggers: ['kill', 'hp', 'victory'] },
        { key: 'scoreDiff', label: '比分差（本方 - 对方）', type: 'number', triggers: ['kill', 'hp', 'victory'] },
        { key: 'redScore', label: '红队大比分', type: 'number', triggers: ['round'] },
        { key: 'blueScore', label: '蓝队大比分', type: 'number', triggers: ['round'] },
        { key: 'isMatchPoint', label: '赛点局（任一方 6 分）', type: 'bool', triggers: ['round', 'kill', 'hp'] },
        { key: 'roundNo', label: '第几局', type: 'number', triggers: ['kill', 'hp', 'round'] },
        { key: 'hour', label: '当前时间（几点，0-23）', type: 'number', triggers: ['kill', 'hp', 'round', 'victory'], unit: '点' },
        { key: 'chance', label: '随机概率', type: 'chance', triggers: ['kill', 'hp', 'round', 'victory'], unit: '%' },
        // 连续阵亡没拿人头：拿到人头清零；整场比赛重新开始时清零
        { key: 'victimDrought', label: '死者 连续阵亡没拿人头（含本次）', type: 'number', triggers: ['kill'], unit: '次' },
        { key: 'killerDrought', label: '击杀者 这次之前连续阵亡没人头', type: 'number', triggers: ['kill'], unit: '次' },
        // 历史击杀：hist = 按击杀顺序回看（上一次 / 往前第 N 次 / 本局第 N 次 / 本场第 N 次），条件里多存 ref + n
        { key: 'histKiller', label: '历史击杀·击杀者', type: 'player', hist: true, prop: 'killer', triggers: ['kill', 'hp', 'round', 'victory'] },
        { key: 'histVictim', label: '历史击杀·死者', type: 'player', hist: true, prop: 'victim', triggers: ['kill', 'hp', 'round', 'victory'] },
        { key: 'histStreak', label: '历史击杀·当时连杀', type: 'number', hist: true, prop: 'streak', triggers: ['kill', 'hp', 'round', 'victory'], unit: '杀' },
        { key: 'histKillerTeam', label: '历史击杀·击杀者队伍', type: 'team', hist: true, prop: 'killerTeam', triggers: ['kill', 'hp', 'round', 'victory'] },
        { key: 'histKillerLeft', label: '历史击杀·击杀者在左侧队伍', type: 'bool', hist: true, prop: 'killerLeft', triggers: ['kill', 'hp', 'round', 'victory'] },
        { key: 'histSameKiller', label: '历史击杀·和本次是同一个击杀者', type: 'bool', hist: true, prop: 'sameKiller', triggers: ['kill'] },
        { key: 'histVictimIsKiller', label: '历史击杀·死者就是本次击杀者（报仇）', type: 'bool', hist: true, prop: 'victimIsKiller', triggers: ['kill'] },
        // 选手状态：存活 = 本局（比分变动后算新的一局）还没被击杀
        { key: 'playerAlive', label: '选手存活状态（比分变动后全部复活）', type: 'alive', triggers: ['kill', 'hp', 'round', 'victory'] },
        { key: 'sameTeam', label: '两位选手是否同队', type: 'pair', triggers: ['kill', 'hp', 'round', 'victory'] }
    ];
    const HIST_REFS = [['back', '往前第 N 次击杀（1 = 上一次）'], ['round', '本局第 N 次击杀'], ['match', '本场第 N 次击杀']];
    const FIELD_MAP = Object.fromEntries(FIELDS.map(f => [f.key, f]));

    const OPS = {
        number: [['>=', '≥'], ['<=', '≤'], ['==', '='], ['!=', '≠'], ['>', '>'], ['<', '<']],
        player: [['is', '是'], ['not', '不是']],
        team: [['is', '是'], ['not', '不是']],
        bool: [['is', '是'], ['not', '不是']],
        chance: [['<=', '命中']],
        alive: [['alive', '存活'], ['dead', '已阵亡']],
        pair: [['same', '同一队'], ['diff', '不同队']]
    };

    const EFFECTS = [
        ['none', '不弹横幅（击杀时仍有文字特效）'],
        ['double', '双杀横幅'], ['triple', '三杀横幅（震屏）'],
        ['first', '一血'], ['shutdown', '终结'], ['revenge', '复仇'],
        ['ak', 'AK 团灭大场面'], ['victory', '胜利大场面'],
        ['custom', '自定义横幅']
    ];
    const STYLES = [['mini', '小横幅'], ['big', '大横幅'], ['huge', '超大横幅（震屏）']];
    const VOICE_EVENTS = [['double', '双杀'], ['triple', '三杀'], ['first', '一血'], ['shutdown', '终结'], ['revenge', '复仇'], ['ak', 'AK'], ['victory', '胜利']];
    const VOICES = [['default', '跟随特效默认台词'], ['none', '不播报'],
        ...VOICE_EVENTS.map(([id, label]) => ['event:' + id, '播报「' + label + '」台词（当前音色）']),
        ['lib', '服务器音频库（管理员发布）'], ['tts', '自定义文字（豆包音色合成）'], ['text', '自定义文字（Windows 系统语音）'], ['file', '自定义语音（本地音频）']];

    // 文字变量：{击杀者} 等，横幅标题 / 副标题 / 自定义语音都能用
    const VARS = [
        ['击杀者', c => c.killer], ['死者', c => c.victim], ['队伍', c => c.teamLabel],
        ['连杀', c => c.streak], ['击杀数', c => c.killerKills], ['死亡数', c => c.killerDeaths],
        ['血量', c => c.killerHp != null ? c.killerHp : c.selfHp], ['开局血量', c => c.killerStartHp != null ? c.killerStartHp : c.selfStartHp],
        ['对方血量', c => c.enemyHp], ['对方开局血量', c => c.victimStartHp != null ? c.victimStartHp : c.enemyStartHp],
        ['用时', c => c.roundSec], ['本方比分', c => c.myScore], ['对方比分', c => c.enemyScore],
        ['红队比分', c => c.redScore], ['蓝队比分', c => c.blueScore], ['局数', c => c.roundNo],
        // 搞名：选手别名里以「搞」结尾的那个（例如 马区 → 马搞）；没有这种别名就用他本来的名字
        ['死者搞名', c => c.victimGao || c.victim], ['击杀者搞名', c => c.killerGao || c.killer]
    ];

    function uid() { return 'r' + Date.now().toString(36) + Math.random().toString(36).slice(2, 7); }
    function clampInt(v, min, max, def) { const n = Math.round(Number(v)); return Number.isFinite(n) ? Math.max(min, Math.min(max, n)) : def; }

    function action(effect, extra) {
        return Object.assign({ effect, style: 'big', title: '', sub: '', voice: 'default', voiceText: '', voiceFile: '', voiceTts: '', voiceLib: '', voiceLibTitle: '', voicePrefix: '', durationMs: 0 }, extra || {});
    }
    function rule(id, name, trigger, conditions, act, extra) {
        return Object.assign({
            id, preset: id, name, enabled: true, trigger, match: 'all', conditions,
            action: act, append: false, appendMs: 1500, cooldownSec: 0
        }, extra || {});
    }
    const C = (f, op, v) => ({ f, op, v });

    // 预设：和 5.5.4 之前的内置逻辑完全一致（AK > 三杀 / 双杀 > 一血 > 终结 > 复仇 > 单杀），另附几条默认关闭的示例。
    function presets(layout) {
        const L = layout || {};
        const on = key => L[key] !== 0;
        return [
            rule('preset-ak', 'AK 团灭', 'kill', [C('isAk', 'is', true)], action('ak'), { enabled: on('fxEvtAk') }),
            rule('preset-triple', '三杀', 'kill', [C('streak', '==', 3)], action('triple'), { enabled: on('fxEvtTriple') }),
            rule('preset-double', '双杀', 'kill', [C('streak', '==', 2)], action('double'), { enabled: on('fxEvtDouble') }),
            rule('preset-first', '一血', 'kill', [C('isFirst', 'is', true)], action('first'), { enabled: on('fxEvtFirst') }),
            rule('preset-shutdown', '终结', 'kill', [C('victimStreak', '>=', 2)], action('shutdown'), { enabled: on('fxEvtShutdown') }),
            rule('preset-lowhp', '残血反杀（示例）', 'kill', [C('killerHp', '<=', 10)],
                action('custom', { style: 'big', title: '残血反杀', sub: '仅剩 {血量}% 血量', voice: 'text', voiceText: '{击杀者}残血反杀！' }), { enabled: false }),
            rule('preset-perfect', '无伤击杀（示例）', 'kill', [C('killerStartHp', '>=', 99), C('killerHp', '>=', 99)],
                action('custom', { style: 'big', title: '打了个洞！', sub: 'PERFECT', voice: 'tts', voiceText: '打了个洞！' }), { enabled: true, append: true }),
            rule('preset-comeback', '逆风翻盘（示例）', 'kill', [C('killerStartHp', '<=', 40), C('victimStartHp', '>=', 90)],
                action('custom', { style: 'big', title: '逆风翻盘', sub: '开局 {开局血量}% 血量拿下满血对手', voice: 'tts', voiceText: '残血翻盘了，有点厉害喔' }), { enabled: true, append: true }),
            rule('preset-revenge', '复仇', 'kill', [C('isRevenge', 'is', true)], action('revenge'),
                { enabled: on('fxEvtRevenge'), append: true, appendMs: 1500 }),
            rule('preset-single', '单杀（仅文字特效）', 'kill', [], action('none')),
            rule('preset-victory', '七分胜利', 'victory', [], action('victory'), { enabled: on('fxEvtVictory'), append: true }),
            rule('preset-matchpoint', '赛点局提醒（示例）', 'round', [C('isMatchPoint', 'is', true)],
                action('custom', { style: 'mini', title: '赛点局', sub: '{红队比分} : {蓝队比分}', voice: 'tts', voiceText: '赛点了喔' }), { enabled: true, append: true }),
            rule('preset-hpwarn', '残血预警（示例）', 'hp', [C('selfHp', '<=', 15), C('enemyHp', '>=', 60)],
                action('custom', { style: 'mini', title: '残血危机', sub: '{队伍} 仅剩 {血量}%', voice: 'none' }), { enabled: false, cooldownSec: 10 })
        ];
    }

    function presetById(id, layout) { return presets(layout).find(r => r.id === id) || null; }

    function names(v) {
        return (Array.isArray(v) ? v : [v]).map(s => String(s || '').trim()).filter(Boolean).slice(0, 40);
    }
    function normalizeCondition(c) {
        const f = FIELD_MAP[c?.f];
        if (!f) return null;
        const ops = OPS[f.type].map(o => o[0]);
        const op = ops.includes(c.op) ? c.op : ops[0];
        let v = c.v;
        if (f.type === 'number' || f.type === 'chance') v = clampInt(v, -9999, 9999, 0);
        else if (f.type === 'bool') v = !(v === false || v === 'false' || v === 0);
        else if (f.type === 'team') v = v === 'blue' ? 'blue' : 'red';
        else if (f.type === 'player' || f.type === 'alive') v = names(v);
        else if (f.type === 'pair') v = [names(Array.isArray(v) ? v[0] : []), names(Array.isArray(v) ? v[1] : [])];
        if (f.hist) {
            const ref = HIST_REFS.some(x => x[0] === c.ref) ? c.ref : 'back';
            return { f: f.key, op, v, ref, n: clampInt(c.n, 1, 99, 1) };
        }
        return { f: f.key, op, v };
    }

    function normalizeRule(r) {
        if (!r || typeof r !== 'object') return null;
        const trig = TRIGGERS.some(t => t.id === r.trigger) ? r.trigger : 'kill';
        const a = r.action || {};
        const effect = EFFECTS.some(e => e[0] === a.effect) ? a.effect : 'none';
        const voice = VOICES.some(v => v[0] === a.voice) ? a.voice : 'default';
        return {
            id: String(r.id || uid()).slice(0, 40),
            preset: typeof r.preset === 'string' && r.preset ? r.preset.slice(0, 40) : '',
            name: String(r.name || '未命名规则').slice(0, 40),
            enabled: r.enabled !== false,
            trigger: trig,
            match: r.match === 'any' ? 'any' : 'all',
            conditions: (Array.isArray(r.conditions) ? r.conditions : []).map(normalizeCondition)
                .filter(c => c && FIELD_MAP[c.f].triggers.includes(trig)).slice(0, 20),
            action: {
                effect,
                style: STYLES.some(s => s[0] === a.style) ? a.style : 'big',
                title: String(a.title || '').slice(0, 30),
                sub: String(a.sub || '').slice(0, 60),
                voice,
                voiceText: String(a.voiceText || '').slice(0, 80),
                voiceFile: String(a.voiceFile || '').replace(/[\\/:*?"<>|]/g, '').slice(0, 120),
                voiceTts: String(a.voiceTts || '').replace(/[^A-Za-z0-9_.-]/g, '').slice(0, 80),
                voiceLib: /^[0-9a-f]{64}$/.test(String(a.voiceLib || '')) ? String(a.voiceLib) : '',
                voiceLibTitle: String(a.voiceLibTitle || '').slice(0, 60),
                voicePrefix: String(a.voicePrefix || '').slice(0, 40),   // 服务器音频前面先念的文字（可带变量，例如 {死者搞名}）
                durationMs: clampInt(a.durationMs, 0, 10000, 0)
            },
            append: !!r.append,
            appendMs: clampInt(r.appendMs, 500, 10000, 1500),
            cooldownSec: clampInt(r.cooldownSec, 0, 3600, 0)
        };
    }

    // rules = null / 非数组 → 使用预设（首次使用，开关继承「特效管理」里原来的触发事件勾选）
    function normalize(rules, layout) {
        if (!Array.isArray(rules)) return presets(layout);
        const seen = new Set();
        return rules.map(normalizeRule).filter(r => {
            if (!r || seen.has(r.id)) return false;
            seen.add(r.id); return true;
        }).slice(0, 100);
    }

    function hash(str) {
        let h = 2166136261;
        for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
        return h >>> 0;
    }
    const normName = s => String(s || '').trim().toLowerCase();

    // 历史击杀：ctx.history = 本场击杀记录（旧→新）；historyHasCurrent = 最后一条就是本次击杀
    function histRecord(ctx, ref, n) {
        const list = Array.isArray(ctx.history) ? ctx.history : [];
        const k = Math.max(1, Number(n) || 1);
        if (ref === 'match') return list[k - 1] || null;
        if (ref === 'round') return list.filter(h => h.round === ctx.historyRound)[k - 1] || null;
        return list[list.length - (ctx.historyHasCurrent ? 1 : 0) - k] || null;
    }
    // names 里任一名字（含别名）在场上 → 返回对应的值；都不在场返回 undefined
    // 选手库别名分组：名字（小写）→ 组号；主窗口保存规则时附带，展示窗口加载规则时 setAliases
    let aliasGroups = {};
    function setAliases(map) { aliasGroups = map && typeof map === 'object' ? map : {}; }
    // 同一个选手：名字相同 / 选手库里是同一组别名 / 一方完整包含另一方（至少 2 个字，例如「阿旺」↔「抖音红狗阿旺」）
    function samePlayer(a, b) {
        const x = normName(a), y = normName(b);
        if (!x || !y) return false;
        if (x === y) return true;
        const gx = aliasGroups[x], gy = aliasGroups[y];
        if (gx != null && gx === gy) return true;
        return (y.length >= 2 && x.includes(y)) || (x.length >= 2 && y.includes(x));
    }
    function lookup(map, list) {
        if (!map) return undefined;
        for (const n of list || []) {
            const key = normName(n);
            if (key && Object.prototype.hasOwnProperty.call(map, key)) return map[key];
        }
        for (const n of list || []) {
            const key = Object.keys(map).find(k => samePlayer(k, n));
            if (key !== undefined) return map[key];
        }
        return undefined;
    }
    function histLabel(c) {
        const n = Number(c.n) || 1;
        if (c.ref === 'round') return '本局第 ' + n + ' 次击杀';
        if (c.ref === 'match') return '本场第 ' + n + ' 次击杀';
        return n === 1 ? '上一次击杀' : '往前第 ' + n + ' 次击杀';
    }

    function testCondition(cond, ctx, ruleId) {
        const f = FIELD_MAP[cond.f];
        if (!f) return false;
        let raw = ctx[cond.f];
        if (f.hist) {
            const rec = histRecord(ctx, cond.ref, cond.n);
            if (!rec) return false;   // 还没有这么多次击杀：条件不成立
            if (f.prop === 'sameKiller') raw = !!ctx.killer && samePlayer(rec.killer, ctx.killer);
            else if (f.prop === 'victimIsKiller') raw = !!ctx.killer && samePlayer(rec.victim, ctx.killer);
            else raw = rec[f.prop];
        }
        if (f.type === 'alive') {
            const alive = lookup(ctx.alive, cond.v);
            if (alive === undefined) return false;   // 选手不在场上
            return cond.op === 'dead' ? !alive : !!alive;
        }
        if (f.type === 'pair') {
            const a = lookup(ctx.teams, cond.v && cond.v[0]), b = lookup(ctx.teams, cond.v && cond.v[1]);
            if (a === undefined || b === undefined) return false;
            return cond.op === 'diff' ? a !== b : a === b;
        }
        if (f.type === 'chance') {
            // 同一事件在击杀小窗和全屏窗口得到相同结果（按事件 + 规则取哈希，而不是 Math.random）
            return hash(String(ctx.eventKey || '') + '|' + ruleId) % 100 < Number(cond.v);
        }
        if (f.type === 'number') {
            if (raw == null || raw === '') return false;
            const x = Number(raw), y = Number(cond.v);
            if (!Number.isFinite(x) || (f.hp && x < 0)) return false;
            switch (cond.op) {
                case '>=': return x >= y; case '<=': return x <= y; case '==': return x === y;
                case '!=': return x !== y; case '>': return x > y; case '<': return x < y;
                default: return false;
            }
        }
        let hit;
        if (f.type === 'player') {
            const names = Array.isArray(cond.v) ? cond.v : [cond.v];
            hit = !!normName(raw) && names.some(n => samePlayer(n, raw));
        } else if (f.type === 'team') hit = raw === cond.v;
        else hit = !!raw === !!cond.v;
        return cond.op === 'not' ? !hit : hit;
    }

    function matches(r, ctx) {
        if (!r.conditions.length) return true;
        return r.match === 'any'
            ? r.conditions.some(c => testCondition(c, ctx, r.id))
            : r.conditions.every(c => testCondition(c, ctx, r.id));
    }

    // 返回 { main, follows }；memo 用于冷却（每个窗口各自记录）
    function evaluate(rules, trigger, ctx, memo, now) {
        const t = now == null ? Date.now() : now;
        const cool = (memo && memo.lastFire) || {};
        const hits = [];
        for (const r of rules || []) {
            if (!r.enabled || r.trigger !== trigger) continue;
            if (r.cooldownSec > 0 && cool[r.id] && t - cool[r.id] < r.cooldownSec * 1000) continue;
            if (!matches(r, ctx)) continue;
            hits.push(r);
        }
        const main = hits[0] || null;
        const follows = main ? hits.slice(1).filter(r => r.append) : [];
        if (memo) {
            memo.lastFire = cool;
            [main, ...follows].forEach(r => { if (r) cool[r.id] = t; });
        }
        return { main, follows };
    }

    function fillText(tpl, ctx) {
        let s = String(tpl || '');
        if (!s.includes('{')) return s;
        VARS.forEach(([name, get]) => {
            if (!s.includes('{' + name + '}')) return;
            const v = get(ctx || {});
            s = s.split('{' + name + '}').join(v == null || v === '' || (typeof v === 'number' && v < 0) ? '?' : String(v));
        });
        return s;
    }

    function effectLabel(id) { return (EFFECTS.find(e => e[0] === id) || EFFECTS[0])[1]; }
    function triggerLabel(id) { return (TRIGGERS.find(t => t.id === id) || TRIGGERS[0]).label; }

    function describeCondition(c) {
        const f0 = FIELD_MAP[c.f];
        if (!f0) return '';
        const f = f0.hist ? Object.assign({}, f0, { label: histLabel(c) + '的' + f0.label.split('·')[1] }) : f0;
        const op = (OPS[f.type].find(o => o[0] === c.op) || OPS[f.type][0])[1];
        const who = list => (list && list[0]) ? list[0] + (list.length > 1 ? '（含别名）' : '') : '（未选择）';
        if (f.type === 'alive') return who(c.v) + ' ' + op;
        if (f.type === 'pair') return who(c.v && c.v[0]) + ' 和 ' + who(c.v && c.v[1]) + ' ' + op;
        let v;
        if (f.type === 'player') v = (c.v && c.v[0]) ? c.v.slice(0, 3).join(' / ') + (c.v.length > 3 ? ' 等' : '') + '（含所有别名）' : '（未选择）';
        else if (f.type === 'team') v = c.v === 'blue' ? '蓝队' : '红队';
        else if (f.type === 'bool') return ((c.op === 'is') === !!c.v) ? f.label : '非「' + f.label + '」';
        else if (f.type === 'chance') return '随机 ' + c.v + '% 概率';
        else v = c.v + (f.unit || '');
        return f.label + ' ' + op + ' ' + v;
    }

    global.SceneRules = {
        VERSION, TRIGGERS, FIELDS, FIELD_MAP, OPS, HIST_REFS, EFFECTS, STYLES, VOICES, VOICE_EVENTS, VARS,
        presets, presetById, normalize, normalizeRule, evaluate, matches, fillText,
        effectLabel, triggerLabel, describeCondition, uid, setAliases, samePlayer
    };
})(typeof window !== 'undefined' ? window : globalThis);
