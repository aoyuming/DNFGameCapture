#pragma once
#define NOMINMAX
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <unordered_map>
#include <windows.h>

class CNameMatcher
{
public:
    CNameMatcher() {
        InitConfusableMap();
        InitProfessionList();
    }

    // 【修改点】：增加 aggressive 参数。如果为 true，开启二轮降级匹配
    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID, bool aggressive = false)
    {
        std::wstring s1 = PreprocessString(realName);
        std::wstring s2 = PreprocessString(gameID);

        if (s1.empty() || s2.empty()) return 0;

        s2 = StripNoise(s2);
        if (s2.empty()) return 0;

        // 如果不是二轮降级匹配，才执行严格的职业拦截
        if (!aggressive) {
            bool isJob = false;
            for (const auto& job : m_professions) {
                if (s2.find(job) != std::wstring::npos) {
                    if (s1.find(job) != std::wstring::npos) {
                        continue;
                    }
                    isJob = true;
                    break;
                }
            }
            if (isJob) return -1;
        }

        int lcs = GetLCSLength(s1, s2);
        if (lcs == 0) return 0;

        double lcsWeight = 0.5;
        double lenWeight = 0.5;

        double score1 = (double)lcs / s1.length();
        double score2 = (double)lcs / s2.length();
        double baseScore = (score1 * lcsWeight + score2 * lenWeight) * 100.0;

        // 【核心修改】：如果是二轮匹配，大幅降低长度惩罚（从 5.0 降到 1.0），充当强力子串提取器！
        double penaltyMultiplier = aggressive ? 1.0 : 5.0;
        double lenDiffPenalty = abs((int)s1.length() - (int)s2.length()) * penaltyMultiplier;

        baseScore -= lenDiffPenalty;

        if (baseScore < 0) baseScore = 0;
        return (int)baseScore;
    }

    static int GetDynamicThreshold(int nameLen) {
        if (nameLen <= 2) return 55;
        if (nameLen <= 4) return 45;
        if (nameLen <= 6) return 35;
        return 30;
    }

private:
    std::unordered_map<wchar_t, wchar_t> m_confusableMap;
    std::vector<std::wstring> m_professions;

    void InitProfessionList() {
        m_professions = {
            L"狂战士", L"剑魂", L"阿修罗", L"鬼泣", L"剑影", L"红眼", L"白手", L"瞎子",
            L"漫游", L"枪炮师", L"机械师", L"弹药专家", L"大枪", L"男漫", L"女漫", L"男机", L"女机",
            L"气功师", L"散打", L"街霸", L"柔道家", L"百花", L"毒王", L"武神", L"男气功", L"女气功",
            L"元素爆破师", L"冰结师", L"血法师", L"逐风者", L"次元行者", L"男法", L"女法",
            L"圣骑士", L"蓝拳圣使", L"驱魔师", L"复仇者", L"奶爸", L"蓝拳", L"驱魔", L"复仇",
            L"刺客", L"死灵术士", L"忍者", L"影舞者",
            L"精灵骑士", L"混沌魔灵", L"帕拉丁", L"龙骑士",
            L"暗枪士", L"特工", L"战线佣兵", L"源能专家",
            L"狩猎者", L"暗刃",
            L"小魔女", L"异端审判者", L"巫女", L"诱魔者", L"奶妈", L"四叔", L"四姨",
            L"男柔道", L"男街霸", L"男散打",
            L"暗帝", L"剑魔", L"剑宗", L"剑帝", L"女鬼剑", L"男鬼剑", L"女刺客"
        };
    }

    void InitConfusableMap() {
        m_confusableMap[L'I'] = L'l'; m_confusableMap[L'1'] = L'l';
        m_confusableMap[L'0'] = L'O'; m_confusableMap[L'o'] = L'O';
        m_confusableMap[L'Z'] = L'2'; m_confusableMap[L'z'] = L'2';
        m_confusableMap[L'S'] = L'5'; m_confusableMap[L's'] = L'5';
        m_confusableMap[L'丶'] = L'、'; m_confusableMap[L'·'] = L'、';
        m_confusableMap[L'灬'] = L'、'; m_confusableMap[L'-'] = L'、';
        m_confusableMap[L'王'] = L'玉'; m_confusableMap[L'玉'] = L'王';
        m_confusableMap[L'大'] = L'太'; m_confusableMap[L'太'] = L'大';
        m_confusableMap[L'天'] = L'大'; m_confusableMap[L'人'] = L'入';
        m_confusableMap[L'士'] = L'土'; m_confusableMap[L'土'] = L'士';
        m_confusableMap[L'日'] = L'曰'; m_confusableMap[L'曰'] = L'日';
        m_confusableMap[L'干'] = L'千'; m_confusableMap[L'千'] = L'干';
        m_confusableMap[L'于'] = L'干'; m_confusableMap[L'子'] = L'孑';
        m_confusableMap[L'甲'] = L'申'; m_confusableMap[L'己'] = L'已';
        m_confusableMap[L'已'] = L'己'; m_confusableMap[L'巳'] = L'己';
        m_confusableMap[L'刃'] = L'刀'; m_confusableMap[L'皇'] = L'星';m_confusableMap[L'心'] = L'芯';
        m_confusableMap[L'坠'] = L'堕'; m_confusableMap[L'随'] = L'堕'; m_confusableMap[L'隋'] = L'堕';
        m_confusableMap[L'洛'] = L'落'; m_confusableMap[L'幕'] = L'落'; m_confusableMap[L'菠'] = L'落';
    }

    int GetLCSLength(const std::wstring& s1, const std::wstring& s2) {
        int n = (int)s1.length(), m = (int)s2.length();
        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
        for (int i = 1; i <= n; i++) {
            for (int j = 1; j <= m; j++) {
                if (s1[i - 1] == s2[j - 1]) dp[i][j] = dp[i - 1][j - 1] + 1;
                else dp[i][j] = (std::max)(dp[i - 1][j], dp[i][j - 1]);
            }
        }
        return dp[n][m];
    }

    std::wstring PreprocessString(const std::wstring& s) {
        std::wstring res;
        for (wchar_t c : s) {
            if (iswspace(c)) continue;
            wchar_t uc = towupper(c);
            if (m_confusableMap.count(uc)) res += m_confusableMap[uc];
            else res += uc;
        }
        return res;
    }

    std::wstring StripNoise(std::wstring s) {
        std::vector<std::wstring> noiseWords = {
            L"跨1", L"跨2", L"跨3", L"跨4", L"跨5", L"跨6", L"跨7", L"跨8",
            L"跨一", L"跨二", L"跨三", L"跨四", L"跨五", L"跨六", L"跨七", L"跨八",
            L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"山东", L"四川", L"湖北", L"湖南", L"河南", L"河北", L"东北", L"西北", L"西南"
        };
        for (const auto& w : noiseWords) {
            size_t pos = s.find(w);
            while (pos != std::wstring::npos) {
                s.erase(pos, w.length());
                pos = s.find(w);
            }
        }
        return s;
    }
};