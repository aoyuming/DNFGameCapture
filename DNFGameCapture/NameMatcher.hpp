#pragma once
#define NOMINMAX
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <unordered_map>
#include <windows.h>

/**
 * @brief 游戏玩家名称匹配类
 * 采用 LCS + 动态字库映射 + 动态权重算法 + 区服噪音清洗 + 严格职业拦截
 */
class CNameMatcher
{
public:
    CNameMatcher() {
        InitConfusableMap();
        InitProfessionList();
    }

    /**
     * @brief 计算 OCR 识别名与名单原名的相似度分数
     * @return -1: 识别到职业干扰; 0-100: 相似度得分
     */
    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID)
    {
        std::wstring s1 = PreprocessString(realName);
        std::wstring s2 = PreprocessString(gameID);

        if (s1.empty() || s2.empty()) return 0;

        // 1. 剔除区服与平台噪音
        s2 = StripNoise(s2);
        if (s2.empty()) return 0;

        // 2. 严格的职业干扰拦截
        bool isJob = false;
        for (const auto& job : m_professions) {
            if (s2.find(job) != std::wstring::npos) {
                isJob = true;
                break;
            }
            if (job.length() >= 3 && s2.length() <= job.length() + 2) {
                int jobLcs = GetLCSLength(job, s2);
                if (jobLcs >= (int)job.length() - 1) {
                    isJob = true;
                    break;
                }
            }
        }
        if (isJob) return -1;

        // 3. 正常 ID 匹配计分
        int lcsLen = GetLCSLength(s1, s2);
        if (lcsLen == 0) return 0;

        // 4. 严苛的 2 字 ID 碰瓷拦截
        if (s1.length() == 2 && lcsLen == 1) {
            if (s2.length() <= 3) return 60;
            else return 0;
        }

        float matchRate = static_cast<float>(lcsLen) / s1.length();
        float lengthPenalty = 1.0f;

        if (s2.length() >= s1.length() * 3) {
            lengthPenalty = 0.5f;
        }
        else if (s2.length() >= s1.length() * 2) {
            lengthPenalty = 0.7f;
        }

        int finalScore = static_cast<int>(matchRate * 100.0f * lengthPenalty);

        if (s1 == s2) return 100;

        return (std::min)(100, finalScore);
    }

    static int GetDynamicThreshold(int realNameLen) {
        if (realNameLen <= 2) return 55;
        if (realNameLen <= 4) return 35;
        return 25;
    }

private:
    std::unordered_map<wchar_t, wchar_t> m_confusableMap;
    std::vector<std::wstring> m_professions;

    std::wstring StripNoise(std::wstring s) {
        std::vector<std::wstring> noises = {
            L"抖音", L"快手", L"斗鱼", L"虎牙", L"企鹅", L"b站", L"bilibili", L"tv", L"fsn", L"直播",
            L"江苏", L"工苏", L"浙江", L"上海", L"广东", L"厂东", L"广末", L"北京", L"山东",
            L"四川", L"湖南", L"湖北", L"辽宁", L"吉林", L"黑龙江", L"河北", L"河南", L"山西",
            L"安徽", L"福建", L"江西", L"广西", L"重庆", L"陕西", L"云南", L"贵州",
            L"西北", L"西南", L"东北", L"华北", L"华南", L"华东", L"跨区", L"跨", L"区"
        };

        for (const auto& noise : noises) {
            size_t pos;
            while ((pos = s.find(noise)) != std::wstring::npos) {
                s.erase(pos, noise.length());
            }
        }

        std::wstring res;
        for (wchar_t c : s) {
            if (c >= L'0' && c <= L'9') continue;
            res += c;
        }
        return res;
    }

    void InitProfessionList() {
        std::vector<std::wstring> list = {
            L"剑魂",L"鬼泣",L"狂战士",L"阿修罗",L"剑影",L"驭剑士",L"暗殿骑士",L"流浪武士",L"契魔者",L"刃影",
            L"漫游枪手",L"枪炮师",L"机械师",L"弹药专家",L"合金战士",L"气功师",L"散打",L"街霸",L"柔道家",
            L"元素师",L"召唤师",L"魔道学者",L"战斗法师",L"冰结师",L"逐风者",L"次元行者",L"血法师",L"元素爆破师",
            L"圣骑士",L"蓝拳圣使",L"驱魔师",L"复仇者",L"小魔女",L"断罪者",L"诱魔者",L"巫女",L"刺客",L"死灵术士",
            L"忍者",L"影舞者",L"精灵骑士",L"混沌魔灵",L"帕拉丁",L"龙骑士",L"征战者",L"决战者",L"狩猎者",L"暗枪士",
            L"特工",L"战线佣兵",L"暗刃",L"源能专家",L"缪斯",L"旅人",L"猎人",L"妖护使",L"森之者",L"缔造者",L"黑暗武士",L"自觉"
        };
        for (auto& s : list) m_professions.push_back(PreprocessString(s));
    }

    void InitConfusableMap() {
        m_confusableMap[L'曰'] = L'日'; m_confusableMap[L'目'] = L'且'; m_confusableMap[L'犬'] = L'大';
        m_confusableMap[L'太'] = L'大'; m_confusableMap[L'玉'] = L'王'; m_confusableMap[L'干'] = L'千';
        m_confusableMap[L'于'] = L'千'; m_confusableMap[L'未'] = L'末'; m_confusableMap[L'乌'] = L'鸟';
        m_confusableMap[L'免'] = L'兔'; m_confusableMap[L'找'] = L'我'; m_confusableMap[L'狐'] = L'孤';
        m_confusableMap[L'徽'] = L'微'; m_confusableMap[L'己'] = L'已'; m_confusableMap[L'巳'] = L'已';
        m_confusableMap[L'入'] = L'人'; m_confusableMap[L'八'] = L'人'; m_confusableMap[L'土'] = L'士';
        m_confusableMap[L'拔'] = L'拨'; m_confusableMap[L'拨'] = L'拔'; m_confusableMap[L'幻'] = L'幼';
        m_confusableMap[L'幼'] = L'幻'; m_confusableMap[L'待'] = L'侍'; m_confusableMap[L'侍'] = L'待';
        m_confusableMap[L'戰'] = L'战'; m_confusableMap[L'劍'] = L'剑'; m_confusableMap[L'龍'] = L'龙';
        m_confusableMap[L'殺'] = L'杀'; m_confusableMap[L'無'] = L'无'; m_confusableMap[L'愛'] = L'爱';
        m_confusableMap[L'夢'] = L'梦'; m_confusableMap[L'亞'] = L'亚'; m_confusableMap[L'區'] = L'区';
        m_confusableMap[L'網'] = L'网'; m_confusableMap[L'雲'] = L'云'; m_confusableMap[L'飛'] = L'飞';
        m_confusableMap[L'極'] = L'极'; m_confusableMap[L'傷'] = L'伤'; m_confusableMap[L'術'] = L'术';
        m_confusableMap[L'關'] = L'关'; m_confusableMap[L'風'] = L'风'; m_confusableMap[L'電'] = L'电';
        m_confusableMap[L'平'] = L'苹'; m_confusableMap[L'兰'] = L'蓝'; m_confusableMap[L'蓝'] = L'兰';
        m_confusableMap[L'愉'] = L'偷'; m_confusableMap[L'税'] = L'悦'; m_confusableMap[L'垫'] = L'电';
        m_confusableMap[L'沈'] = L'神'; m_confusableMap[L'度'] = L'渡'; m_confusableMap[L'清'] = L'青';
        m_confusableMap[L'暗'] = L'岸'; m_confusableMap[L'苍'] = L'枪'; m_confusableMap[L'抢'] = L'枪';
        m_confusableMap[L'虹'] = L'红'; m_confusableMap[L'逛'] = L'狂'; m_confusableMap[L'魄'] = L'魂';
        m_confusableMap[L'魁'] = L'鬼'; m_confusableMap[L'泣'] = L'立'; m_confusableMap[L'刀'] = L'刃';
        m_confusableMap[L'刃'] = L'刀'; m_confusableMap[L'皇'] = L'星';

        // 堕落专属防错字
        m_confusableMap[L'坠'] = L'堕'; m_confusableMap[L'随'] = L'堕'; m_confusableMap[L'隋'] = L'堕';
        m_confusableMap[L'洛'] = L'落'; m_confusableMap[L'幕'] = L'落'; m_confusableMap[L'菠'] = L'落';
    }

    int GetLCSLength(const std::wstring& s1, const std::wstring& s2) {
        int n = (int)s1.length(), m = (int)s2.length();
        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));

        for (int i = 1; i <= n; i++) {
            for (int j = 1; j <= m; j++) {
                if (s1[i - 1] == s2[j - 1]) {
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                }
                else {
                    dp[i][j] = (std::max)(dp[i - 1][j], dp[i][j - 1]);
                }
            }
        }
        return dp[n][m];
    }

    std::wstring PreprocessString(const std::wstring& s) {
        std::wstring result = L"";
        for (wchar_t c : s) {
            wchar_t lowerC = std::towlower(c);
            if (lowerC == L' ' || lowerC == L'[' || lowerC == L']' || lowerC == L'(' || lowerC == L')') {
                continue;
            }
            auto it = m_confusableMap.find(lowerC);
            if (it != m_confusableMap.end()) {
                result += it->second;
            }
            else {
                result += lowerC;
            }
        }
        return result;
    }
};