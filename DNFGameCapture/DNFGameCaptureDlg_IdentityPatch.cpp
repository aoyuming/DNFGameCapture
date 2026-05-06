#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <cwctype>

void WriteMatchLog(const CString& logLine);

// ================================================================
// 固定红框身份融合补丁实现
// 接入方式：把本文件加入工程，或者把这些函数复制进 DNFGameCaptureDlg.cpp。
// 前提：DNFGameCaptureDlg.h 已声明对应函数和 m_identityMatcher 成员。
// ================================================================

namespace {
    static TDnfPanelSide AreaIndexToPanelSide(int areaIndex) {
        // 约定：0 = 左上固定红框，1 = 右上固定红框。
        // 如果你的 RunOCR_Internal(nAreaIndex) 编号不同，只改这里。
        return areaIndex == 0 ? TDnfPanelSide::LeftNameArea : TDnfPanelSide::RightAreaName;
    }

    static int PanelSideToTeam(TDnfPanelSide side) {
        // 当前截图规则：左框是蓝队，右框是红队。
        // 你的 JS 同步里 red=0, blue=1，所以这里默认 left->1, right->0。
        // 如果 C++ 内部 team 语义相反，只改这里。
        return side == TDnfPanelSide::LeftNameArea ? 1 : 0;
    }

    static TDnfPanelSide TeamToPanelSide(int team) {
        return team == 1 ? TDnfPanelSide::LeftNameArea : TDnfPanelSide::RightAreaName;
    }


    struct TAliasMetaForIdentity {
        CString realId;
        CString area;
        CString job;
        bool hasArea = false;
        bool hasJob = false;
    };

    static CString DnfIdentityNormalize(CString s) {
        s.Trim();
        s.Replace(L" ", L"");
        s.Replace(L"　", L"");
        s.MakeLower();
        return s;
    }

    static bool DnfIdentityIsSymbolLikeId(const CString& raw) {
        CString s = DnfIdentityNormalize(raw);
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

        if (meaningful == 0) return true;
        if (meaningful <= 1 && symbol >= 1) return true;
        if (meaningful <= 2 && symbol >= meaningful) return true;
        return false;
    }

    static bool DnfIdentityExtractArea(CString& body, CString& areaOut) {
        static const wchar_t* kAreas[] = {
            L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"四川", L"山东", L"河南", L"湖北", L"湖南",
            L"河北", L"辽宁", L"吉林", L"黑龙江", L"安徽", L"江西", L"广西", L"陕西", L"山西", L"重庆", L"天津",
            L"云南", L"贵州", L"新疆", L"西藏", L"青海", L"甘肃", L"宁夏", L"内蒙古", L"东北", L"西北", L"西南", L"跨"
        };
        CString compact = DnfIdentityNormalize(body);
        for (const wchar_t* area : kAreas) {
            for (int n = 1; n <= 9; ++n) {
                CString token;
                token.Format(L"%s%d", area, n);
                CString ntoken = DnfIdentityNormalize(token);
                int pos = compact.Find(ntoken);
                if (pos >= 0) {
                    int rawPos = body.Find(token);
                    if (rawPos < 0) rawPos = pos;
                    if (rawPos >= 0 && rawPos + token.GetLength() <= body.GetLength()) {
                        body = body.Left(rawPos) + body.Mid(rawPos + token.GetLength());
                        body.Trim();
                    }
                    areaOut = token;
                    return true;
                }
            }
        }
        return false;
    }

    static TAliasMetaForIdentity DnfParseAliasForIdentity(const CString& aliasRaw) {
        TAliasMetaForIdentity meta;
        CString body = aliasRaw;
        body.Trim();

        int sharp = body.Find(L'#');
        if (sharp >= 0) {
            meta.job = body.Mid(sharp + 1);
            meta.job.Trim();
            meta.hasJob = !meta.job.IsEmpty();
            body = body.Left(sharp);
            body.Trim();
        }

        meta.hasArea = DnfIdentityExtractArea(body, meta.area);
        body.Trim();
        meta.realId = body;
        meta.realId.Trim();
        return meta;
    }
}

void CDNFGameCaptureDlg::UpdateIdentityPanelCache(int areaIndex, const CString& rawOcrText)
{
    TDnfPanelSide side = AreaIndexToPanelSide(areaIndex);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line); // 身份融合详细日志只写入文件，不再刷软件界面
    };

    m_identityMatcher.UpdatePanelFromOcr(side, rawOcrText, now, dbg);
}

std::vector<TDnfCandidateIdentity> CDNFGameCaptureDlg::BuildIdentityCandidatesForPanel(TDnfPanelSide side)
{
    std::vector<TDnfCandidateIdentity> out;
    std::lock_guard<std::mutex> lock(m_dataMutex);

    for (int i = 0; i < 8; ++i) {
        if (m_players[i].name.IsEmpty()) continue;

        // 不再在候选构建阶段强行按“左框=蓝队/右框=红队”过滤。
        // 原因：录像翻转、红蓝互换或用户手动翻转时，固定框与队伍映射可能变化；
        // 纯符号 ID 兜底更需要从 8 人中按“职业/大区唯一性”判断。
        // 最终若两侧候选同队，DoRetryMatchingTask 里仍会按 lockedTeam 做冲突拒绝。

        // 主号只作为归属 owner，不参与身份融合名称匹配。
        // 真正用于 OCR 命中的候选只有小号。
        for (const auto& a : m_players[i].aliases) {
            if (a.name.IsEmpty()) continue;
            TDnfCandidateIdentity alias;
            alias.name = a.name;
            alias.ownerName = m_players[i].name;
            alias.team = m_players[i].team;
            alias.isAlias = true;

            TAliasMetaForIdentity meta = DnfParseAliasForIdentity(a.name);
            alias.matchName = meta.realId.IsEmpty() ? a.name : meta.realId;
            alias.declaredArea = meta.area;
            alias.declaredJob = meta.job;
            alias.hasDeclaredArea = meta.hasArea;
            alias.hasDeclaredJob = meta.hasJob;
            alias.isSymbolicId = DnfIdentityIsSymbolLikeId(alias.matchName);

            out.push_back(alias);
        }
    }

    return out;
}

TDnfPanelMatchResult CDNFGameCaptureDlg::MatchIdentityPanel(TDnfPanelSide side)
{
    auto candidates = BuildIdentityCandidatesForPanel(side);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line); // 身份融合详细日志只写入文件，不再刷软件界面
    };

    if (candidates.empty()) {
        CString msg;
        msg.Format(L"[融合匹配][%s] 候选列表为空，请检查红蓝队上场数据。\r\n",
            side == TDnfPanelSide::LeftNameArea ? L"左框" : L"右框");
        dbg(msg);
        TDnfPanelMatchResult r;
        r.ok = false;
        r.debugText = msg;
        return r;
    }

    return m_identityMatcher.MatchPanel(side, candidates, now, dbg);
}

void CDNFGameCaptureDlg::NotifyIdentityKillConfirmed(int deadTeam, const CString& deadName)
{
    TDnfPanelSide deadSide = TeamToPanelSide(deadTeam);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line);
    };

    m_identityMatcher.NotifyKillConfirmed(deadSide, deadName, now, dbg);
}

void CDNFGameCaptureDlg::NotifyIdentityRoundReset(const CString& reason)
{
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line);
    };

    m_identityMatcher.Reset(reason, now, dbg);
}
