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

        // 【修改前】：长度越级熔断机制
        // if (s1.length() <= 2 && s2.length() > s1.length() + 3) {
        //     return -1; 
        // }

        // 【修改后】：改成返回 0，只算作两人不匹配，不发送终止信号
        if (s1.length() <= 2 && s2.length() > s1.length() + 3) {
            return 0;
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
            // --- 鬼剑士 (男/女) ---
            L"鬼剑士", L"剑魂", L"狂战士", L"阿修罗", L"鬼泣", L"剑影", L"剑圣", L"剑神", L"极武剑魂", L"狱血魔神", L"帝血弑天", L"极武暴君", L"大暗黑天", L"天帝", L"极武阿修罗", L"弑魂", L"黑暗君主", L"极武鬼泣", L"夜见罗刹", L"极武剑影", L"红眼", L"白手", L"瞎子",
            L"女鬼剑", L"驭剑士", L"契魔者", L"暗殿骑士", L"流浪武士", L"刃影", L"剑宗", L"剑皇", L"极武剑皇", L"剑魔", L"弑神者", L"极武剑魔", L"暗帝", L"裁决女神", L"极武暗帝", L"剑豪", L"剑帝", L"极武剑帝", L"苍暮", L"极武刃影", L"女鬼",
            // --- 格斗家 (男/女) ---
            L"格斗家", L"气功师", L"散打", L"街霸", L"柔道家", L"百花缭乱", L"念帝", L"极武气功师", L"武神", L"极武武神", L"毒王", L"毒神绝", L"极武毒王", L"暴风眼", L"风暴女皇", L"极武柔道家", L"狂虎帝", L"念皇", L"武极", L"霸皇", L"千手罗汉", L"暗街之王", L"风林火山", L"宗师", L"女气功", L"男气功",
            // --- 神枪手 (男/女) ---
            L"神枪手", L"漫游枪手", L"枪炮师", L"机械师", L"弹药专家", L"合金战士", L"枪神", L"掠天之翼", L"极武漫游", L"狂暴者", L"毁灭者", L"极武枪炮", L"机械战神", L"机械元首", L"大将军", L"战场统治者", L"沾血玫瑰", L"绯红玫瑰", L"重炮掌控者", L"风暴骑兵", L"金属之心", L"机械之灵", L"战争女神", L"芙蕾雅", L"超能者", L"极武合金", L"大枪", L"男女大枪", L"男漫", L"女漫",
            // --- 魔法师 (男/女) ---
            L"魔法师", L"元素师", L"召唤师", L"战斗法师", L"魔道学者", L"小魔女", L"大魔导师", L"元素圣灵", L"极武元素", L"月之女皇", L"月蚀", L"贝亚娜斗神", L"伊斯塔战灵", L"魔术师", L"古灵精怪", L"黑夜萝莉", L"赫卡忒", L"极武魔女", L"元素爆破师", L"冰结师", L"血法师", L"逐风者", L"次元行者", L"魔皇", L"湮灭之瞳", L"冰冻之心", L"刹那碎星", L"血狱伯爵", L"猩红法师", L"御风者", L"风神", L"虚空行者", L"混沌行者", L"奶萝",
            // --- 圣职者 (男/女) ---
            L"圣职者", L"圣骑士", L"蓝拳圣使", L"驱魔师", L"复仇者", L"天启者", L"神思者", L"极武神思", L"神之手", L"正义仲裁者", L"龙斗士", L"真龙星君", L"末日审判者", L"永生者", L"福音传道者", L"炽天使", L"极武炽天使", L"异端审判者", L"神焰处刑官", L"巫女", L"神龙天女", L"诱魔者", L"救世主", L"奶爸", L"奶妈", L"四叔", L"四姨",
            // --- 暗夜使者 ---
            L"暗夜使者", L"刺客", L"死灵术士", L"忍者", L"影舞者", L"银月", L"月影星劫", L"极武刺客", L"灵魂收割者", L"亡魂主宰", L"毕方之炎", L"不知火", L"梦魇", L"幽冥",
            // --- 守护者 ---
            L"守护者", L"精灵骑士", L"混沌魔灵", L"帕拉丁", L"龙骑士", L"星辰之光", L"大地女神", L"魔王", L"黑曜神", L"曙光", L"破晓女神", L"龙皇", L"龙神",
            // --- 魔枪士 ---
            L"魔枪士", L"征战者", L"决战者", L"狩猎者", L"暗枪士", L"战魂", L"不灭战神", L"无双魂", L"圣武枪魂", L"狂怒恶鬼", L"歼灭者", L"狂怒暗鬼", L"幽影夜神",
            // --- 枪剑士 ---
            L"枪剑士", L"暗刃", L"特工", L"战线佣兵", L"源能专家", L"统御者", L"铁血统帅", L"绝命谍影", L"弑心镇魂者", L"战场王牌", L"巅峰狂徒", L"源力剑胆", L"未来开拓者",
            // --- 弓箭手 ---
            L"弓箭手", L"缪斯", L"旅人", L"猎人", L"妖护使", L"仙界之音", L"神弦", L"流浪星辰", L"天穹",
            // --- 外传职业 ---
            L"黑暗武士", L"自我觉醒黑暗武士", L"缔造者", L"自我觉醒缔造者", L"黑武", L"鼠标妹"
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