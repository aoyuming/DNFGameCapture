#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include "PlayerIdentityOcrCache.h"
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

    static TDnfPanelSide TeamToPanelSide(int team) {
        return team == 1 ? TDnfPanelSide::LeftNameArea : TDnfPanelSide::RightAreaName;
    }


    struct TAliasMetaForIdentity {
        CString fullId;
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
        const int wideSharp = body.Find(L'＃');
        if (sharp < 0 || (wideSharp >= 0 && wideSharp < sharp)) sharp = wideSharp;
        if (sharp >= 0) {
            meta.job = body.Mid(sharp + 1);
            meta.job.Trim();
            meta.hasJob = !meta.job.IsEmpty();
            body = body.Left(sharp);
            body.Trim();
        }

        meta.fullId = body;
        meta.fullId.Trim();
        meta.hasArea = DnfIdentityExtractArea(body, meta.area);
        body.Trim();
        meta.realId = body;
        meta.realId.Trim();
        return meta;
    }

    static dnf::identity::OcrGameIdMetadata DnfParseCachedGameId(const std::wstring& raw) {
        const CString id(raw.c_str());
        const auto meta = DnfParseAliasForIdentity(id);
        dnf::identity::OcrGameIdMetadata result;
        result.fullMatchName = (meta.fullId.IsEmpty() ? id : meta.fullId).GetString();
        result.matchName = (meta.realId.IsEmpty() ? id : meta.realId).GetString();
        result.declaredArea = meta.area.GetString();
        result.declaredJob = meta.job.GetString();
        result.isSymbolicId = DnfIdentityIsSymbolLikeId(CString(result.matchName.c_str()));
        return result;
    }

    static bool DnfPrepareIdentityCache(dnf::identity::OcrLookupCache& cache,
        dnf::player_library::SnapshotPtr snapshot, const PlayerData* players,
        CTemporalIdentityMatcher& matcher, DWORD now,
        const CTemporalIdentityMatcher::DebugSink& dbg = nullptr) {
        dnf::identity::OcrLookupCache::Roster roster;
        for (std::size_t i = 0; i < roster.size(); ++i) {
            roster[i].name = players[i].name.GetString();
            roster[i].team = players[i].team;
            for (const auto& alias : players[i].aliases) roster[i].gameIds.emplace_back(alias.name.GetString());
        }
        if (cache.Prepare(std::move(snapshot), roster, DnfParseCachedGameId)) {
            matcher.Reset(L"Player library or active roster changed", now, dbg);
            return true;
        }
        return false;
    }

    static std::vector<TDnfCandidateIdentity> DnfCachedCandidates(
        const dnf::identity::OcrLookupCache& cache, const PlayerData* players) {
        std::vector<TDnfCandidateIdentity> out;
        out.reserve(cache.GameCandidates().size());
        for (const auto& game : cache.GameCandidates()) {
            TDnfCandidateIdentity candidate;
            candidate.name = game.name.c_str();
            candidate.ownerName = players[game.playerIndex].name;
            candidate.team = players[game.playerIndex].team;
            candidate.isAlias = true;
            candidate.fullMatchName = game.metadata.fullMatchName.c_str();
            candidate.matchName = game.metadata.matchName.c_str();
            candidate.declaredArea = game.metadata.declaredArea.c_str();
            candidate.declaredJob = game.metadata.declaredJob.c_str();
            candidate.hasDeclaredArea = !game.metadata.declaredArea.empty();
            candidate.hasDeclaredJob = !game.metadata.declaredJob.empty();
            candidate.isSymbolicId = game.metadata.isSymbolicId;
            out.push_back(std::move(candidate));
        }
        return out;
    }
}

void CDNFGameCaptureDlg::UpdateIdentityPanelCache(int areaIndex, const CString& rawOcrText, std::uint64_t monitoringGeneration)
{
    if (areaIndex < 0 || areaIndex > 1) return;
    // Kill/reset notifications may already hold the data lock: never invert this order.
    std::lock_guard<std::mutex> dataLock(m_dataMutex);
    std::lock_guard<std::mutex> identityLock(m_identityMutex);
    if (monitoringGeneration != m_ocrMonitoringGeneration.load()) return;
    TDnfPanelSide side = AreaIndexToPanelSide(areaIndex);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line); // 身份融合详细日志只写入文件，不再刷软件界面
    };

    DnfPrepareIdentityCache(m_identityOcrCache, std::atomic_load(&m_playerLibrarySnapshot),
        m_players, m_identityMatcher, now, dbg);
    m_identityMatcher.UpdatePanelFromOcr(side, rawOcrText, now, dbg);
}

std::vector<TDnfCandidateIdentity> CDNFGameCaptureDlg::BuildIdentityCandidatesForPanel(TDnfPanelSide side)
{
    (void)side; // Both physical panels consider only the current eight players.
    std::lock_guard<std::mutex> dataLock(m_dataMutex);
    std::lock_guard<std::mutex> identityLock(m_identityMutex);
    DnfPrepareIdentityCache(m_identityOcrCache, std::atomic_load(&m_playerLibrarySnapshot),
        m_players, m_identityMatcher, GetTickCount());
    return DnfCachedCandidates(m_identityOcrCache, m_players);
}

TDnfPanelMatchResult CDNFGameCaptureDlg::MatchIdentityPanel(TDnfPanelSide side)
{
    std::lock_guard<std::mutex> dataLock(m_dataMutex);
    std::lock_guard<std::mutex> identityLock(m_identityMutex);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line); // 身份融合详细日志只写入文件，不再刷软件界面
    };

    DnfPrepareIdentityCache(m_identityOcrCache, std::atomic_load(&m_playerLibrarySnapshot),
        m_players, m_identityMatcher, now, dbg);
    const auto candidates = DnfCachedCandidates(m_identityOcrCache, m_players);
    TDnfPanelMatchResult result;
    if (candidates.empty()) {
        CString msg;
        msg.Format(L"[融合匹配][%s] 候选列表为空，请检查红蓝队上场数据。\r\n",
            side == TDnfPanelSide::LeftNameArea ? L"左框" : L"右框");
        dbg(msg);
        result.debugText = msg;
    }
    else {
        result = m_identityMatcher.MatchPanel(side, candidates, now, dbg);
    }

    return result;
}

void CDNFGameCaptureDlg::NotifyIdentityKillConfirmed(int deadTeam, const CString& deadName)
{
    std::lock_guard<std::mutex> identityLock(m_identityMutex);
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
    m_ocrMonitoringGeneration.fetch_add(1);
    std::lock_guard<std::mutex> identityLock(m_identityMutex);
    DWORD now = GetTickCount();

    auto dbg = [](const CString& line) {
        OutputDebugString(line);
        WriteMatchLog(line);
    };

    m_identityOcrCache.Reset();
    m_identityMatcher.Reset(reason, now, dbg);
}
