#pragma once
#define NOMINMAX
#include <windows.h>
#include <atlstr.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#include <functional>
#include <cwctype>
#include <sstream>
#include <cstdarg>
#include "NameMatcher.hpp"

// ================================================================
// DNF 固定红框身份融合匹配器
// 目标：不替换 CNameMatcher，而是在外层把「固定位置 + 20秒缓存 + 大区 + 职业」作为上下文证据。
// 使用场景：
//   左框：游戏ID + 大区，例如 “抖音月与海 上海1”
//   右框：大区 + 游戏ID，例如 “上海1 抖音DNF帅丁”
//   职业和 ID+大区 每 5 秒轮换显示。
// ================================================================

enum class TDnfPanelSide {
    LeftNameArea = 0,   // 左框：ID + 大区
    RightAreaName = 1   // 右框：大区 + ID
};

enum class TDnfFrameKind {
    Noise = 0,
    NameArea = 1,
    Profession = 2
};

struct TDnfTextHit {
    CString value;
    int score = 0;
    int pos = -1;
    int len = 0;
};

struct TDnfParsedPanelText {
    CString raw;
    TDnfFrameKind kind = TDnfFrameKind::Noise;

    CString area;
    int areaScore = 0;
    int areaPos = -1;
    int areaLen = 0;

    CString profession;
    int professionScore = 0;

    CString nameText;
};

struct TDnfVote {
    CString value;
    int score = 0;
    DWORD tick = 0;
};

struct TDnfCandidateIdentity {
    CString name;       // 候选原始游戏ID字符串，例如：上海1夏雫#气功师
    CString ownerName;  // 归属选手。选手候选可等于 name
    int team = -1;      // 0/1，保持你现有 m_players.team 语义
    bool isAlias = false;

    // 解析后的匹配字段。用于让“大区在前/在后”等价：
    //   上海1夏雫 == 夏雫上海1
    //   上海1夏雫#气功师 -> fullMatchName=上海1夏雫 matchName=夏雫 declaredArea=上海1 declaredJob=气功师
    CString fullMatchName;
    CString matchName;
    CString declaredArea;
    CString declaredJob;
    bool hasDeclaredArea = false;
    bool hasDeclaredJob = false;

    // 纯符号/弱 ID 游戏ID：例如 ---#次元行者、一~一.#次元行者。
    // 这类 ID OCR 往往完全读不到，允许后续用“职业/大区唯一性”兜底。
    bool isSymbolicId = false;
};

struct TDnfCandidateScore {
    TDnfCandidateIdentity candidate;
    int idScore = 0;
    int areaCtxScore = 0;
    int jobCtxScore = 0;
    int stableBonus = 0;
    int penalty = 0;
    int finalScore = 0;
    int gapToSecond = 0;

    bool areaMatched = false;
    bool areaShortIdAssist = false;
    bool jobMatched = false;
    bool areaConflict = false;
    bool jobConflict = false;
    bool accepted = false;

    CString bestIdText;
    CString scoreCandidateId;
    CString scoreOcrId;
    CString idMatchMode;
    CString idMatchNote;
    bool allowStrongIdLock = false;
    CString areaNow;
    CString jobNow;
    CString decisionReason;
};

struct TDnfPanelMatchResult {
    bool ok = false;
    bool cacheInsufficient = false;
    TDnfCandidateScore best;
    TDnfCandidateScore second;
    std::vector<TDnfCandidateScore> topScores;
    CString debugText;
};

class CTemporalIdentityMatcher {
public:
    using DebugSink = std::function<void(const CString&)>;

    CTemporalIdentityMatcher() {
        InitAreaList();
    }

    // 运行/新局/录像跳转/手动重置时可直接清空。
    void Reset(const CString& reason = L"手动重置", DWORD now = GetTickCount(), DebugSink debug = nullptr) {
        ResetPanel(TDnfPanelSide::LeftNameArea, reason, now, debug);
        ResetPanel(TDnfPanelSide::RightAreaName, reason, now, debug);
        m_runtimeHints.clear();
        Log(debug, Format(L"[身份缓存][全局] 已清空：%s\r\n", reason.GetString()));
    }

    void ResetPanel(TDnfPanelSide side, const CString& reason = L"重置", DWORD now = GetTickCount(), DebugSink debug = nullptr) {
        PanelCache& c = Cache(side);
        int oldEpoch = c.epochId;
        c = PanelCache();
        c.epochId = oldEpoch + 1;
        c.lastTick = now;
        c.pendingUntilTick = now;
        Log(debug, Format(L"[身份缓存][%s] 开启新身份段 epoch=%d，原因：%s\r\n",
            SideName(side).GetString(), c.epochId, reason.GetString()));
    }

    // 击杀确认后调用：只切死者侧；杀手侧保留。
    void NotifyKillConfirmed(TDnfPanelSide deadSide, const CString& deadName, DWORD now = GetTickCount(), DebugSink debug = nullptr) {
        ResetPanel(deadSide, Format(L"击杀确认，死者[%s]下场", deadName.GetString()), now, debug);
        Cache(deadSide).pendingUntilTick = now + KILL_SWITCH_GUARD_MS;
        Log(debug, Format(L"[身份缓存][%s] 击杀后保护期 %dms：保护期内 OCR 只记录弱证据，不学习新人\r\n",
            SideName(deadSide).GetString(), KILL_SWITCH_GUARD_MS));
    }

    // 录像拖动/画面跳变时可从外部主动调用；内部也有软切段兜底。
    void NotifyVideoSeekOrJump(const CString& reason = L"录像拖动/画面跳变", DWORD now = GetTickCount(), DebugSink debug = nullptr) {
        Reset(reason, now, debug);
    }

    // 每次 OCR 到左/右固定红框文本后调用。
    TDnfParsedPanelText UpdatePanelFromOcr(TDnfPanelSide side, const CString& raw, DWORD now = GetTickCount(), DebugSink debug = nullptr) {
        PanelCache& c = Cache(side);

        // 程序长时间没有稳定 OCR，通常是暂停、拖动录像、切窗口、卡顿。直接切段避免串缓存。
        if (c.lastTick != 0 && now > c.lastTick && now - c.lastTick > SEEK_GAP_RESET_MS) {
            CString reason;
            reason.Format(L"OCR间隔过长 %lu ms，疑似暂停/拖动录像", now - c.lastTick);
            ResetPanel(side, reason, now, debug);
        }
        c.lastTick = now;

        TDnfParsedPanelText p = ParsePanelText(raw, side);
        c.rawFrameCount++;

        if (p.kind == TDnfFrameKind::NameArea) {
            c.nameAreaFrameCount++;
            PushVote(c.areaVotes, p.area, p.areaScore, now);
            if (!p.nameText.IsEmpty()) {
                PushVote(c.nameVotes, p.nameText, (std::max)(60, p.areaScore), now);
                c.nameTexts.push_back({ p.nameText, (std::max)(60, p.areaScore), now });
            }

            // 软切段兜底：没有击杀事件但固定框连续稳定变成另一个 ID+大区。
            if (!c.anchorName.IsEmpty() && now >= c.pendingUntilTick) {
                int oldScore = m_nameMatcher.GetMatchScore(ToW(c.anchorName), ToW(p.nameText), false);
                bool areaChanged = !c.anchorArea.IsEmpty() && !p.area.IsEmpty() && c.anchorArea != p.area;
                if ((oldScore < 40 && !p.nameText.IsEmpty()) || (areaChanged && oldScore < 65)) {
                    c.consecutiveMismatch++;
                    Log(debug, Format(L"[软切段检测][%s] 新帧 name=\"%s\" area=%s 与锚点 name=\"%s\" area=%s 差异大，oldScore=%d mismatch=%d/%d\r\n",
                        SideName(side).GetString(), p.nameText.GetString(), p.area.GetString(),
                        c.anchorName.GetString(), c.anchorArea.GetString(), oldScore,
                        c.consecutiveMismatch, SOFT_SWITCH_CONFIRM_FRAMES));
                    if (c.consecutiveMismatch >= SOFT_SWITCH_CONFIRM_FRAMES) {
                        CString reason;
                        reason.Format(L"连续%d帧 ID+大区与当前锚点差异大，疑似漏杀/录像拖动/换人", c.consecutiveMismatch);
                        ResetPanel(side, reason, now, debug);
                        PanelCache& nc = Cache(side);
                        PushVote(nc.areaVotes, p.area, p.areaScore, now);
                        PushVote(nc.nameVotes, p.nameText, (std::max)(60, p.areaScore), now);
                        nc.nameTexts.push_back({ p.nameText, (std::max)(60, p.areaScore), now });
                        nc.nameAreaFrameCount = 1;
                    }
                }
                else {
                    c.consecutiveMismatch = 0;
                }
            }
        }
        else if (p.kind == TDnfFrameKind::Profession) {
            c.professionFrameCount++;
            PushVote(c.jobVotes, p.profession, p.professionScore, now);
        }

        TrimOldVotes(c, now);

        CString detail;
        if (p.kind == TDnfFrameKind::NameArea) {
            detail.Format(L"[身份解析][%s] raw=\"%s\" => 类型=ID大区帧 area=%s(%d) nameText=\"%s\" epoch=%d\r\n",
                SideName(side).GetString(), raw.GetString(), p.area.GetString(), p.areaScore, p.nameText.GetString(), c.epochId);
        }
        else if (p.kind == TDnfFrameKind::Profession) {
            detail.Format(L"[身份解析][%s] raw=\"%s\" => 类型=职业帧 job=%s(%d) epoch=%d\r\n",
                SideName(side).GetString(), raw.GetString(), p.profession.GetString(), p.professionScore, c.epochId);
        }
        else {
            detail.Format(L"[身份解析][%s] raw=\"%s\" => 类型=噪声/证据不足 epoch=%d\r\n",
                SideName(side).GetString(), raw.GetString(), c.epochId);
        }
        Log(debug, detail);

        CString bestArea = BestVote(c.areaVotes).value;
        CString bestJob = BestVote(c.jobVotes).value;
        CString bestName = BestVote(c.nameVotes).value;
        Log(debug, Format(L"[身份缓存][%s] epoch=%d raw=%d ID帧=%d 职业帧=%d topName=\"%s\" topArea=%s topJob=%s anchor=\"%s/%s\"\r\n",
            SideName(side).GetString(), c.epochId, c.rawFrameCount, c.nameAreaFrameCount, c.professionFrameCount,
            bestName.GetString(), bestArea.GetString(), bestJob.GetString(), c.anchorName.GetString(), c.anchorArea.GetString()));

        if (c.nameAreaFrameCount < MIN_NAME_AREA_FRAMES_FOR_STABLE) {
            Log(debug, Format(L"[缓存不足][%s] 当前 ID+大区有效帧=%d，低于稳定阈值%d；允许旧算法兜底，但暂不学习身份。\r\n",
                SideName(side).GetString(), c.nameAreaFrameCount, MIN_NAME_AREA_FRAMES_FOR_STABLE));
        }

        return p;
    }

    // 对某个固定框对应的候选人做融合评分。候选列表建议只传该框对应队伍的 4 人 + 游戏ID。
    TDnfPanelMatchResult MatchPanel(TDnfPanelSide side, const std::vector<TDnfCandidateIdentity>& candidates,
        DWORD now = GetTickCount(), DebugSink debug = nullptr) {

        TDnfPanelMatchResult result;
        PanelCache& c = Cache(side);
        TrimOldVotes(c, now);

        TDnfVote areaTop = BestVote(c.areaVotes);
        TDnfVote jobTop = BestVote(c.jobVotes);

        result.cacheInsufficient = c.nameAreaFrameCount < MIN_NAME_AREA_FRAMES_FOR_STABLE;

        // 纯符号 ID 场景：OCR 可能永远读不到 ID，只能读到职业或弱大区。
        // 因此不能在 nameTexts 为空时直接失败；只要有职业/大区证据，仍进入候选评分。
        if (c.nameTexts.empty() && areaTop.value.IsEmpty() && jobTop.value.IsEmpty()) {
            result.debugText = Format(L"[融合匹配][%s] 失败：缓存中没有 ID/大区/职业有效证据。raw=%d 职业帧=%d\r\n",
                SideName(side).GetString(), c.rawFrameCount, c.professionFrameCount);
            Log(debug, result.debugText);
            return result;
        }

        std::vector<TDnfCandidateScore> scores;
        for (const auto& cand : candidates) {
            if (cand.name.IsEmpty()) continue;

            TDnfCandidateScore s;
            s.candidate = cand;
            s.areaNow = areaTop.value;
            s.jobNow = jobTop.value;

            CString candMatchName = cand.matchName.IsEmpty() ? cand.name : cand.matchName;
            CString candFullName = cand.fullMatchName.IsEmpty() ? candMatchName : cand.fullMatchName;
            const bool symbolIdMode = cand.isSymbolicId || IsSymbolLikeId(candMatchName);

            // ID 分：取最近窗口内所有 nameText 的最高分。
            // 注意：纯符号 ID 不强求 ID 分，因为 OCR 很可能只读到职业/大区。
            if (!symbolIdMode) {
                for (const auto& nt : c.nameTexts) {
                    bool bothHaveArea = cand.hasDeclaredArea && !areaTop.value.IsEmpty();
                    CString scoreCandId = bothHaveArea ? candFullName : candMatchName;
                    CString scoreOcrId = nt.value;
                    int idScore = 0;
                    CString mode = bothHaveArea ? L"完整ID" : L"真实ID";
                    bool shortAssist = false;
                    if (bothHaveArea) {
                        int exactFullScore = ExactFullIdWithAreaScore(cand.declaredArea, candMatchName,
                            areaTop.value, nt.value);
                        int realScore = IdPartScore(candMatchName, nt.value);
                        bool twoCharAssist = VisibleLen(candMatchName) == 2 && LcsLen(NormalizeRaw(candMatchName), NormalizeRaw(nt.value)) >= 1 && realScore > 0;
                        if (exactFullScore >= 95) {
                            idScore = exactFullScore;
                        }
                        else if (twoCharAssist) {
                            idScore = (std::max)(realScore, 55);
                            shortAssist = true;
                        }
                        else {
                            idScore = realScore;
                        }
                    }
                    else {
                        idScore = IdPartScore(scoreCandId, scoreOcrId);
                    }
                    if (idScore > s.idScore) {
                        s.idScore = idScore;
                        s.bestIdText = nt.value;
                        s.scoreCandidateId = scoreCandId;
                        s.scoreOcrId = scoreOcrId;
                        s.idMatchMode = mode;
                        s.areaShortIdAssist = shortAssist;
                    }
                }
            }

            const int nameLen = VisibleLen(candMatchName);
            if (nameLen <= 1) {
                bool oneCharExactRealId = NormalizeRaw(candMatchName) == NormalizeRaw(s.bestIdText);
                s.allowStrongIdLock = (cand.hasDeclaredArea && !areaTop.value.IsEmpty() && s.idScore >= 95 && oneCharExactRealId);
                s.idMatchNote = s.allowStrongIdLock ? L"两边都有大区，且1字真实ID精确一致" : L"真实ID仅1字，不作为强ID锁定依据";
            }
            else {
                s.allowStrongIdLock = true;
                s.idMatchNote = s.idMatchMode.IsEmpty() ? L"无ID证据" : (s.idMatchMode + L"匹配");
            }
            RuntimeHint hint = GetRuntimeHint(cand.name);

            // 大区不再作为普通加分项；只保留匹配标记，真正的短 ID 辅助在 ID 分里收窄处理。
            if (!areaTop.value.IsEmpty() && cand.hasDeclaredArea) {
                if (AreaFuzzySame(areaTop.value, cand.declaredArea)) {
                    s.areaMatched = true;
                }
                else {
                    s.areaConflict = true;
                    s.penalty += symbolIdMode ? 8 : ((nameLen <= 2) ? 6 : 3);
                }
            }

            // 动态大区上下文：仍然保留已学习画像。若候选已经声明大区并匹配，这里不重复加太多分。
            CString learnedArea = BestVote(hint.areaVotes).value;
            if (!areaTop.value.IsEmpty() && !learnedArea.IsEmpty()) {
                if (AreaFuzzySame(areaTop.value, learnedArea)) {
                    s.areaMatched = true;
                }
                else if (!cand.hasDeclaredArea) {
                    s.areaConflict = true;
                    s.penalty += (nameLen <= 2) ? 6 : 3;
                }
            }

            // 声明式职业上下文：游戏ID里手动写了 #职业 时，首次识别也能加分。
            if (!jobTop.value.IsEmpty() && cand.hasDeclaredJob) {
                if (JobFuzzySame(jobTop.value, cand.declaredJob)) {
                    s.jobMatched = true;
                    // 职业做模糊匹配：柔道/柔道家/柔道室 等价。
                    s.jobCtxScore += symbolIdMode ? 58 : ((nameLen <= 2) ? 28 : 12);
                }
                else {
                    s.jobConflict = true;
                    s.penalty += symbolIdMode ? 10 : ((nameLen <= 2) ? 8 : 4);
                }
            }

            // 动态职业上下文，同样保留已学习职业。
            CString learnedJob = BestVote(hint.jobVotes).value;
            if (!jobTop.value.IsEmpty() && !learnedJob.IsEmpty()) {
                if (JobFuzzySame(jobTop.value, learnedJob)) {
                    s.jobMatched = true;
                    s.jobCtxScore += (nameLen <= 2) ? 16 : 7;
                }
                else if (!cand.hasDeclaredJob) {
                    s.jobConflict = true;
                    s.penalty += (nameLen <= 2) ? 8 : 4;
                }
            }

            if (c.nameAreaFrameCount >= 2) s.stableBonus += 5;
            if (c.professionFrameCount >= 2) s.stableBonus += symbolIdMode ? 8 : 3;

            s.finalScore = s.idScore + s.areaCtxScore + s.jobCtxScore + s.stableBonus - s.penalty;
            if (s.finalScore < 0) s.finalScore = 0;
            if (s.finalScore > 160) s.finalScore = 160;

            scores.push_back(s);
        }

        std::sort(scores.begin(), scores.end(), [](const TDnfCandidateScore& a, const TDnfCandidateScore& b) {
            return a.finalScore > b.finalScore;
        });

        result.topScores = scores;
        if (!scores.empty()) result.best = scores[0];
        if (scores.size() > 1) result.second = scores[1];
        result.best.gapToSecond = result.best.finalScore - result.second.finalScore;

        result.ok = AcceptCandidate(result.best, result.second, result.cacheInsufficient);
        result.best.accepted = result.ok;

        Log(debug, Format(L"[融合匹配][%s] 当前证据 topArea=%s topJob=%s ID帧=%d 职业帧=%d cacheInsufficient=%s\r\n",
            SideName(side).GetString(), areaTop.value.GetString(), jobTop.value.GetString(),
            c.nameAreaFrameCount, c.professionFrameCount, result.cacheInsufficient ? L"是" : L"否"));

        int topN = (int)(scores.size() < 3 ? scores.size() : 3);
        for (int i = 0; i < topN; ++i) {
            const auto& s = scores[i];
            Log(debug, Format(L"  ├ Top%d 候选=%s(owner=%s%s) id=%d text=\"%s\" scoreId=\"%s\" ocrId=\"%s\" mode=%s note=%s areaCtx=%+d jobCtx=%+d stable=%+d penalty=-%d final=%d\r\n",
                i + 1, s.candidate.name.GetString(), s.candidate.ownerName.GetString(), s.candidate.isAlias ? L"/游戏ID" : L"",
                s.idScore, s.bestIdText.GetString(),
                s.scoreCandidateId.GetString(), s.scoreOcrId.GetString(), s.idMatchMode.GetString(), s.idMatchNote.GetString(),
                s.areaCtxScore, s.jobCtxScore, s.stableBonus, s.penalty, s.finalScore));
        }

        if (result.ok) {
            Log(debug, Format(L"[融合匹配][%s] ✅ 通过：%s final=%d gap=%d。%s\r\n",
                SideName(side).GetString(), result.best.candidate.name.GetString(), result.best.finalScore,
                result.best.gapToSecond, result.best.decisionReason.GetString()));
            LearnFromAcceptedMatch(side, result.best, now, debug);
        }
        else {
            Log(debug, Format(L"[融合匹配][%s] ❌ 未通过：best=%s final=%d gap=%d。%s\r\n",
                SideName(side).GetString(), result.best.candidate.name.GetString(), result.best.finalScore,
                result.best.gapToSecond, result.best.decisionReason.GetString()));
        }

        return result;
    }

    TDnfParsedPanelText ParsePanelText(const CString& raw, TDnfPanelSide side) {
        TDnfParsedPanelText out;
        out.raw = raw;

        CString compact = NormalizeRaw(raw);
        if (compact.IsEmpty()) return out;

        TDnfTextHit areaHit = MatchArea(compact);
        TDnfTextHit jobHit = MatchProfession(compact);

        // 有大区数字时，优先按 ID+大区帧处理。职业不会和 ID 同帧绑定。
        if (!areaHit.value.IsEmpty() && areaHit.score >= AREA_ACCEPT_SCORE) {
            out.kind = TDnfFrameKind::NameArea;
            out.area = areaHit.value;
            out.areaScore = areaHit.score;
            out.areaPos = areaHit.pos;
            out.areaLen = areaHit.len;
            out.nameText = ExtractNameByArea(compact, side, areaHit);
            out.nameText = CleanNameText(out.nameText);
            if (out.nameText.IsEmpty()) out.kind = TDnfFrameKind::Noise;
            return out;
        }

        // 没大区时才允许识别成纯职业帧；避免“上海1次元行者”这种 ID 帧被当职业。
        if (!jobHit.value.IsEmpty() && jobHit.score >= JOB_ACCEPT_SCORE && IsPureProfessionLike(compact, jobHit)) {
            out.kind = TDnfFrameKind::Profession;
            out.profession = jobHit.value;
            out.professionScore = jobHit.score;
            return out;
        }

        return out;
    }

    TDnfTextHit MatchArea(const CString& raw) const {
        CString s = NormalizeRaw(raw);
        TDnfTextHit best;

        // 1) 精确命中优先。
        for (const auto& area : m_areas) {
            int pos = s.Find(area);
            if (pos >= 0) {
                best.value = area;
                best.score = 100;
                best.pos = pos;
                best.len = area.GetLength();
                return best;
            }
        }

        // 2) 半模糊：数字严格，地区文字允许轻微 OCR 错误。
        for (const auto& area : m_areas) {
            if (area.GetLength() < 2) continue;
            wchar_t targetDigit = area[area.GetLength() - 1];
            if (!iswdigit(targetDigit)) continue;

            for (int i = 0; i < s.GetLength(); ++i) {
                if (s[i] != targetDigit) continue;

                int provinceLen = area.GetLength() - 1;
                int startMin = (std::max)(0, i - provinceLen - 1);
                int startMax = (std::min)(i, i - provinceLen + 1);
                for (int st = startMin; st <= startMax; ++st) {
                    int len = i - st;
                    if (len <= 0 || len > provinceLen + 1) continue;
                    CString provinceOcr = s.Mid(st, len);
                    CString provinceTarget = area.Left(provinceLen);
                    int score = SimilarityScore(provinceOcr, provinceTarget);
                    int finalScore = 0;
                    if (score >= 88) finalScore = 88;
                    else if (score >= 72 && provinceTarget.GetLength() >= 3) finalScore = 76;

                    if (finalScore > best.score) {
                        best.value = area;
                        best.score = finalScore;
                        best.pos = st;
                        best.len = (i - st + 1);
                    }
                }
            }
        }
        return best;
    }

    TDnfTextHit MatchProfession(const CString& raw) const {
        CString s = NormalizeRaw(raw);
        TDnfTextHit best;
        const auto& jobs = m_nameMatcher.GetProfessions();

        for (const auto& wjob : jobs) {
            CString job(wjob.c_str());
            CString njob = NormalizeRaw(job);
            if (njob.IsEmpty()) continue;

            int score = 0;
            int pos = s.Find(njob);
            if (s == njob) score = 100;
            else if (pos >= 0 && s.GetLength() <= njob.GetLength() + 2) score = 96;
            else {
                score = SimilarityScore(s, njob, 75, 25);
            }

            // 职业白名单要高阈值，避免 “散打猪猪” 这种 ID 被吞掉。
            if (score > best.score) {
                best.value = job;
                best.score = score;
                best.pos = (pos >= 0 ? pos : 0);
                best.len = njob.GetLength();
            }
        }
        return best;
    }

private:
    static const DWORD CACHE_WINDOW_MS = 22000;          // 比 20 秒略宽，兼容定时误差
    static const DWORD SEEK_GAP_RESET_MS = 8000;         // 8 秒没更新，疑似暂停/拖动/切窗口
    static const DWORD KILL_SWITCH_GUARD_MS = 1800;      // 击杀后 UI 残留保护
    static const int AREA_ACCEPT_SCORE = 75;
    static const int JOB_ACCEPT_SCORE = 84;
    static const int MIN_NAME_AREA_FRAMES_FOR_STABLE = 2;
    static const int SOFT_SWITCH_CONFIRM_FRAMES = 2;

    static CString NormalizeLoose(CString s) {
        s.Trim();
        s.Replace(L" ", L"");
        s.Replace(L"　", L"");
        s.Replace(L"-", L"");
        s.Replace(L"_", L"");
        s.Replace(L"·", L"");
        s.Replace(L"・", L"");
        s.MakeLower();
        return s;
    }

    static int LongestCommonSubstringLen(const CString& a, const CString& b) {
        CString x = NormalizeLoose(a), y = NormalizeLoose(b);
        int best = 0;
        for (int i = 0; i < x.GetLength(); ++i) {
            for (int j = 0; j < y.GetLength(); ++j) {
                int k = 0;
                while (i + k < x.GetLength() && j + k < y.GetLength() && x[i + k] == y[j + k]) ++k;
                if (k > best) best = k;
            }
        }
        return best;
    }

    static int FuzzyTextScore(const CString& a, const CString& b) {
        CString x = NormalizeLoose(a), y = NormalizeLoose(b);
        if (x.IsEmpty() || y.IsEmpty()) return 0;
        if (x == y) return 100;
        if (x.Find(y) >= 0 || y.Find(x) >= 0) return (std::min)(x.GetLength(), y.GetLength()) >= 2 ? 88 : 70;
        int lcs = LongestCommonSubstringLen(x, y);
        int mx = (std::max)(x.GetLength(), y.GetLength());
        int mn = (std::min)(x.GetLength(), y.GetLength());
        int score = mx > 0 ? (lcs * 100) / mx : 0;
        if (lcs >= 2 && mn <= 3) score = (std::max)(score, 76);
        return score;
    }

    static CString NormalizeJobAlias(CString s) {
        CString n = NormalizeLoose(s);
        if (n.Find(L"柔道") >= 0) return L"柔道家";
        if (n.Find(L"枪炮") >= 0 || n.Find(L"大枪") >= 0) return L"枪炮师";
        if (n.Find(L"漫游") >= 0) return L"漫游枪手";
        if (n.Find(L"蓝拳") >= 0) return L"蓝拳使者";
        if (n.Find(L"次元") >= 0) return L"次元行者";
        if (n.Find(L"魔道") >= 0) return L"魔道学者";
        if (n.Find(L"驱魔") >= 0) return L"驱魔师";
        if (n.Find(L"气功") >= 0) return L"气功师";
        if (n.Find(L"合金") >= 0) return L"合金战士";
        return n;
    }

    static bool JobFuzzySame(const CString& a, const CString& b) {
        return FuzzyTextScore(NormalizeJobAlias(a), NormalizeJobAlias(b)) >= 72;
    }

    static CString AreaBase(CString s) {
        s.Trim();
        while (!s.IsEmpty()) {
            wchar_t ch = s[s.GetLength() - 1];
            if (ch >= L'0' && ch <= L'9') s.Delete(s.GetLength() - 1, 1);
            else break;
        }
        return s;
    }

    static bool AreaFuzzySame(const CString& a, const CString& b) {
        CString aa = AreaBase(a), bb = AreaBase(b);
        if (aa.IsEmpty() || bb.IsEmpty()) return false;
        return FuzzyTextScore(aa, bb) >= 70;
    }

    struct RuntimeHint {
        std::deque<TDnfVote> areaVotes;
        std::deque<TDnfVote> jobVotes;
    };

    struct PanelCache {
        int epochId = 0;
        DWORD lastTick = 0;
        DWORD pendingUntilTick = 0;

        int rawFrameCount = 0;
        int nameAreaFrameCount = 0;
        int professionFrameCount = 0;
        int consecutiveMismatch = 0;

        CString anchorName;
        CString anchorArea;

        std::deque<TDnfVote> areaVotes;
        std::deque<TDnfVote> jobVotes;
        std::deque<TDnfVote> nameVotes;
        std::deque<TDnfVote> nameTexts;
    };

    CNameMatcher m_nameMatcher;
    PanelCache m_left;
    PanelCache m_right;
    std::map<CString, RuntimeHint> m_runtimeHints;
    std::vector<CString> m_areas;

    PanelCache& Cache(TDnfPanelSide side) {
        return side == TDnfPanelSide::LeftNameArea ? m_left : m_right;
    }

    const PanelCache& Cache(TDnfPanelSide side) const {
        return side == TDnfPanelSide::LeftNameArea ? m_left : m_right;
    }

    RuntimeHint GetRuntimeHint(const CString& key) const {
        auto it = m_runtimeHints.find(key);
        if (it == m_runtimeHints.end()) return RuntimeHint();
        return it->second;
    }

    void LearnFromAcceptedMatch(TDnfPanelSide side, const TDnfCandidateScore& s, DWORD now, DebugSink debug) {
        PanelCache& c = Cache(side);
        TDnfVote areaTop = BestVote(c.areaVotes);
        TDnfVote jobTop = BestVote(c.jobVotes);

        CString learnMatchName = s.candidate.matchName.IsEmpty() ? s.candidate.name : s.candidate.matchName;
        const int nameLen = VisibleLen(learnMatchName);
        bool canLearnAnchor = false;
        if (s.idScore >= 90 && s.gapToSecond >= 18) canLearnAnchor = true;
        if (nameLen > 2 && s.idScore >= 80 && s.gapToSecond >= 12) canLearnAnchor = true;
        if (nameLen <= 2 && s.idScore >= 85 && s.gapToSecond >= 18 && c.nameAreaFrameCount >= 2) canLearnAnchor = true;

        if (!canLearnAnchor) {
            Log(debug, Format(L"[身份学习][%s] 暂不学习：候选=%s id=%d gap=%d ID帧=%d，避免错误污染缓存。\r\n",
                SideName(side).GetString(), s.candidate.name.GetString(), s.idScore, s.gapToSecond, c.nameAreaFrameCount));
            return;
        }

        c.anchorName = s.candidate.name;
        c.anchorArea = areaTop.value;
        c.consecutiveMismatch = 0;

        RuntimeHint& hint = m_runtimeHints[s.candidate.name];
        if (!areaTop.value.IsEmpty() && areaTop.score >= AREA_ACCEPT_SCORE) {
            PushVote(hint.areaVotes, areaTop.value, areaTop.score, now);
        }
        // 职业至少出现 2 次，或者职业分特别高才学习；防止换人瞬间串职业。
        if (!jobTop.value.IsEmpty() && (c.professionFrameCount >= 2 || jobTop.score >= 180)) {
            PushVote(hint.jobVotes, jobTop.value, jobTop.score, now);
        }
        TrimRuntimeHint(hint, now);

        Log(debug, Format(L"[身份学习][%s] ✅ 锚点=%s area=%s job=%s；学习条件 id=%d gap=%d ID帧=%d 职业帧=%d\r\n",
            SideName(side).GetString(), c.anchorName.GetString(), areaTop.value.GetString(), jobTop.value.GetString(),
            s.idScore, s.gapToSecond, c.nameAreaFrameCount, c.professionFrameCount));
    }

    bool AcceptCandidate(TDnfCandidateScore& best, const TDnfCandidateScore& second, bool cacheInsufficient) const {
        if (best.candidate.name.IsEmpty()) {
            best.decisionReason = L"无候选";
            return false;
        }

        CString bestMatchName = best.candidate.matchName.IsEmpty() ? best.candidate.name : best.candidate.matchName;
        int nameLen = VisibleLen(bestMatchName);
        int gap = best.finalScore - second.finalScore;
        best.gapToSecond = gap;

        const bool symbolIdMode = best.candidate.isSymbolicId || IsSymbolLikeId(bestMatchName);

        if (symbolIdMode) {
            // 纯符号 ID：允许“职业/大区唯一”通过。
            // 要求分差足够大，避免同队多个相同职业时误判。
            if (best.jobMatched && best.finalScore >= 58 && gap >= 25) {
                best.decisionReason = L"纯符号 ID：#职业唯一锁定";
                return true;
            }
            if (best.areaMatched && best.jobMatched && best.finalScore >= 78 && gap >= 18) {
                best.decisionReason = L"纯符号 ID：大区 + #职业 上下文锁定";
                return true;
            }
            best.decisionReason = L"纯符号 ID 保护：职业/大区不唯一或证据不足";
            return false;
        }

        if (cacheInsufficient) {
            // 缓存不足时仍然谨慎：
            // 1) 高置信 ID 可以过；
            // 2) 手动声明的大区/#职业已命中时，短 ID 也允许低一点的 ID 分通过，
            //    解决“两字 ID + 上海1 / #职业”第一轮帧数不足的问题。
            if (best.allowStrongIdLock && best.idScore >= 95 && gap >= 18) {
                best.decisionReason = L"缓存不足，但 ID 高置信通过；不建议学习职业";
                return true;
            }
            if (nameLen <= 2 && best.allowStrongIdLock && best.idScore >= 35 && best.finalScore >= 65 && gap >= 10 && (best.areaMatched || best.jobMatched)) {
                best.decisionReason = L"缓存不足，但短 ID 有手动大区/职业上下文支撑";
                return true;
            }
            best.decisionReason = L"缓存帧不足，且 ID/上下文/分差不够强，拒绝强判";
            return false;
        }

        if (nameLen <= 2) {
            if (best.allowStrongIdLock && best.idScore >= 95 && best.finalScore >= 95 && gap >= 18) {
                best.decisionReason = L"两字 ID 完整高置信命中";
                return true;
            }
            if (best.allowStrongIdLock && best.idScore >= 50 && best.finalScore >= 85 && gap >= 12 && (best.areaMatched || best.jobMatched)) {
                best.decisionReason = L"两字 ID 模糊命中，且有已学习大区/职业上下文支撑";
                return true;
            }
            if (!best.allowStrongIdLock) {
                best.decisionReason = L"短 ID 保护：真实ID仅1字，不靠ID分强锁，等待职业唯一";
                return false;
            }
            best.decisionReason = L"两字 ID 保护：缺少高置信 ID 或上下文支撑";
            return false;
        }

        if (nameLen <= 4) {
            if (best.finalScore >= 68 && gap >= 10) {
                best.decisionReason = L"中短 ID 通过";
                return true;
            }
            best.decisionReason = L"中短 ID 分数或分差不足";
            return false;
        }

        if (best.finalScore >= 50 && gap >= 8) {
            best.decisionReason = L"长 ID 通过";
            return true;
        }
        best.decisionReason = L"长 ID 分数或分差不足";
        return false;
    }

    static bool IsSymbolLikeId(const CString& raw) {
        CString s = NormalizeRaw(raw);
        if (s.IsEmpty()) return true;

        int meaningful = 0;
        int symbol = 0;
        for (int i = 0; i < s.GetLength(); ++i) {
            wchar_t ch = s[i];
            bool isCjk = (ch >= 0x4E00 && ch <= 0x9FFF);
            bool isAlphaNum = !!iswalnum(ch);
            if (isCjk || isAlphaNum) meaningful++;
            else symbol++;
        }

        // 纯符号，或符号占比高且可读字符过少，都视为“无法依赖 OCR ID”。
        if (meaningful == 0) return true;
        if (meaningful <= 1 && symbol >= 1) return true;
        if (meaningful <= 2 && symbol >= meaningful) return true;
        return false;
    }

    CString ExtractNameByArea(const CString& compact, TDnfPanelSide side, const TDnfTextHit& areaHit) const {
        if (areaHit.pos < 0 || areaHit.len <= 0) return L"";
        CString left = compact.Left(areaHit.pos);
        CString right = compact.Mid(areaHit.pos + areaHit.len);
        return side == TDnfPanelSide::LeftNameArea ? left : right;
    }

    CString CleanNameText(CString s) const {
        static const wchar_t* prefixes[] = { L"首", L"斗鱼", L"虎牙", L"虎多", L"抖音", L"科音", L"快手", L"B站", L"哔哩", L"企鹅", L"TV", L"FSN", L"直播", L"JK", L">", L"《", L"“", L"·", L"-" };
        bool changed = true;
        while (changed) {
            changed = false;
            s.Trim();
            for (auto p : prefixes) {
                CString pre(p);
                if (s.Left(pre.GetLength()).CompareNoCase(pre) == 0) {
                    s = s.Mid(pre.GetLength());
                    changed = true;
                }
            }
        }
        s.Trim();
        return s;
    }

    bool IsPureProfessionLike(const CString& compact, const TDnfTextHit& hit) const {
        if (hit.value.IsEmpty()) return false;
        CString s = NormalizeRaw(compact);
        CString j = NormalizeRaw(hit.value);
        if (s == j) return true;
        if (s.Find(j) >= 0 && s.GetLength() <= j.GetLength() + 2) return true;
        if (hit.score >= 88 && s.GetLength() <= j.GetLength() + 2) return true;
        return false;
    }

    void PushVote(std::deque<TDnfVote>& votes, const CString& value, int score, DWORD tick) const {
        if (value.IsEmpty()) return;
        votes.push_back({ value, score, tick });
    }

    void TrimOldVotes(PanelCache& c, DWORD now) const {
        TrimDeque(c.areaVotes, now);
        TrimDeque(c.jobVotes, now);
        TrimDeque(c.nameVotes, now);
        TrimDeque(c.nameTexts, now);
    }

    void TrimRuntimeHint(RuntimeHint& h, DWORD now) const {
        // 玩家动态身份可以比面板缓存活得久一点，但仍然要衰减，避免录像拖动或换角色后长期污染。
        const DWORD HINT_WINDOW_MS = 10 * 60 * 1000;
        auto trimLong = [&](std::deque<TDnfVote>& q) {
            while (!q.empty() && now > q.front().tick && now - q.front().tick > HINT_WINDOW_MS) q.pop_front();
        };
        trimLong(h.areaVotes);
        trimLong(h.jobVotes);
    }

    void TrimDeque(std::deque<TDnfVote>& q, DWORD now) const {
        while (!q.empty() && now > q.front().tick && now - q.front().tick > CACHE_WINDOW_MS) q.pop_front();
    }

    TDnfVote BestVote(const std::deque<TDnfVote>& votes) const {
        std::map<CString, int> sum;
        for (const auto& v : votes) {
            sum[v.value] += (std::max)(1, v.score);
        }
        TDnfVote best;
        for (const auto& kv : sum) {
            if (kv.second > best.score) {
                best.value = kv.first;
                best.score = kv.second;
            }
        }
        return best;
    }

    static CString NormalizeRaw(CString s) {
        s.Remove(L' '); s.Remove(L'\t'); s.Remove(L'\r'); s.Remove(L'\n');
        s.Replace(L"：", L":");
        s.Replace(L"|", L"");
        s.Replace(L"【", L""); s.Replace(L"】", L"");
        s.Replace(L"[", L""); s.Replace(L"]", L"");
        s.MakeUpper();
        // 大区数字不做 1->L 归一，数字是强约束。
        return s;
    }

    static int SimilarityScore(const CString& a, const CString& b, int coverAWeight = 50, int coverBWeight = 50) {
        CString aa = NormalizeRaw(a);
        CString bb = NormalizeRaw(b);
        if (aa.IsEmpty() || bb.IsEmpty()) return 0;
        int lcs = LcsLen(aa, bb);
        double coverA = (double)lcs / (std::max)(1, aa.GetLength());
        double coverB = (double)lcs / (std::max)(1, bb.GetLength());
        int score = (int)(coverA * coverAWeight + coverB * coverBWeight);
        if (score < 0) score = 0;
        if (score > 100) score = 100;
        return score;
    }

    static int NgramCoverageScore(const CString& candidateText, const CString& ocrText, int n) {
        CString x = NormalizeRaw(candidateText);
        CString y = NormalizeRaw(ocrText);
        if (x.IsEmpty() || y.IsEmpty() || n <= 0 || x.GetLength() < n) return 0;

        int total = x.GetLength() - n + 1;
        int hits = 0;
        for (int i = 0; i <= x.GetLength() - n; ++i) {
            CString part = x.Mid(i, n);
            if (!part.IsEmpty() && y.Find(part) >= 0) hits++;
        }
        if (total <= 0) return 0;
        return (hits * 100) / total;
    }

    static int IdPartScore(const CString& candidateText, const CString& ocrText) {
        CString x = NormalizeRaw(candidateText);
        CString y = NormalizeRaw(ocrText);
        if (x.IsEmpty() || y.IsEmpty()) return 0;

        int candLen = x.GetLength();
        if (x == y) return candLen <= 1 ? 35 : 100;
        if (y.Find(x) >= 0) return candLen <= 1 ? 28 : 100;

        int lcs = LcsLen(x, y);
        if (lcs <= 0) return 0;
        if (candLen <= 1) return (std::min)(28, lcs * 28);
        if (candLen == 2 && lcs == 1) return 28;

        int score = (std::max)(NgramCoverageScore(x, y, 2), NgramCoverageScore(x, y, 3));
        if (lcs >= 2 && candLen <= 4) score = (std::max)(score, 55);

        int lenDiff = abs(candLen - y.GetLength());
        if (lenDiff > candLen + 2) {
            score -= (std::min)(25, (lenDiff - candLen - 2) * 3);
        }
        if (score < 0) score = 0;
        if (score > 100) score = 100;
        return score;
    }

    static int ExactFullIdWithAreaScore(const CString& candArea, const CString& candReal,
        const CString& ocrArea, const CString& ocrReal) {
        CString candForms[2] = { candArea + candReal, candReal + candArea };
        CString ocrForms[2] = { ocrArea + ocrReal, ocrReal + ocrArea };
        int best = 0;
        for (const CString& c : candForms) {
            for (const CString& o : ocrForms) {
                best = (std::max)(best, IdPartScore(c, o));
            }
        }
        return best;
    }

    static int LcsLen(const CString& a, const CString& b) {
        int n = a.GetLength();
        int m = b.GetLength();
        if (n <= 0 || m <= 0) return 0;
        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
        for (int i = 1; i <= n; ++i) {
            for (int j = 1; j <= m; ++j) {
                if (a[i - 1] == b[j - 1]) dp[i][j] = dp[i - 1][j - 1] + 1;
                else dp[i][j] = (std::max)(dp[i - 1][j], dp[i][j - 1]);
            }
        }
        return dp[n][m];
    }

    static int VisibleLen(const CString& s) {
        CString t = NormalizeRaw(s);
        return t.GetLength();
    }

    static std::wstring ToW(const CString& s) {
        return std::wstring(s.GetString());
    }

    static CString SideName(TDnfPanelSide side) {
        return side == TDnfPanelSide::LeftNameArea ? L"左框" : L"右框";
    }

    static CString Format(const wchar_t* fmt, ...) {
        CString s;
        va_list args;
        va_start(args, fmt);
        s.FormatV(fmt, args);
        va_end(args);
        return s;
    }

    static void Log(DebugSink debug, const CString& s) {
        if (debug) debug(s);
    }

    void InitAreaList() {
        const wchar_t* provinces[] = {
            L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"山东", L"四川", L"湖北", L"湖南", L"河南", L"河北",
            L"东北", L"西北", L"西南", L"广西", L"安徽", L"江西", L"辽宁", L"吉林", L"黑龙江", L"陕西", L"山西", L"重庆", L"天津", L"云南", L"贵州"
        };
        for (auto p : provinces) {
            for (int i = 1; i <= 8; ++i) {
                CString area;
                area.Format(L"%s%d", p, i);
                m_areas.push_back(area);
            }
        }
        for (int i = 1; i <= 8; ++i) {
            CString area;
            area.Format(L"跨%d", i);
            m_areas.push_back(area);
        }
        // 长词优先，避免 “上海1” 被 “海1” 之类异常截断。
        std::sort(m_areas.begin(), m_areas.end(), [](const CString& a, const CString& b) {
            return a.GetLength() > b.GetLength();
        });
    }
};
