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
 * 采用 LCS + 动态字库映射 + 动态权重算法
 */
class CNameMatcher
{
public:
    CNameMatcher() {
        InitConfusableMap();
    }

    /**
     * @brief 计算 OCR 识别名与名单原名的相似度分数
     */
    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID)
    {
        // 1. 数据预处理
        std::wstring s1 = PreprocessString(realName);
        std::wstring s2 = PreprocessString(gameID);

        if (s1.empty() || s2.empty()) return 0;

        // 2. 计算最长公共子序列长度 (LCS)
        int lcsLen = GetLCSLength(s1, s2);

        // 3. 拦截绝对噪音
        if (lcsLen == 0) return 0;

        // ================= 【核心修复：防单字碰瓷】 =================
        // 如果只匹配上了 1 个字
        if (lcsLen == 1) {
            // 规则A：如果玩家名字是 3 个字及以上，只中 1 个字显然是错的
            if (s1.length() > 2) return 0;

            // 规则B：如果玩家名字是 2 个字，但 OCR 扫出了一长串字符（≥3个字，如“战斗法师”）
            // 这说明这个字纯属在一堆字里巧合碰上的，直接视为噪音归零！
            if (s2.length() >= 3) return 0;
        }
        // ============================================================

        // 4. 重写打分公式：以“真实名字长度”为主要分母
        float matchRate = static_cast<float>(lcsLen) / s1.length();

        // 5. 长度惩罚机制：如果 OCR 识别出了一大串很长的乱码，要扣分
        float lengthPenalty = 1.0f;
        // （微调惩罚触发线为 >=，更严格地遏制长串碰瓷短名）
        if (s2.length() >= s1.length() * 3) {
            lengthPenalty = 0.5f; // OCR长度是真名的3倍及以上，打5折
        }
        else if (s2.length() >= s1.length() * 2) {
            lengthPenalty = 0.8f; // OCR长度是真名的2倍及以上，打8折
        }

        int finalScore = static_cast<int>(matchRate * 100.0f * lengthPenalty);

        // 如果完全是一模一样，给满分
        if (s1 == s2) return 100;

        return (std::min)(100, finalScore);
    }

    /**
     * @brief 根据真实名字长度，获取动态及格线
     */
    static int GetDynamicThreshold(int realNameLen) {
        if (realNameLen <= 2) return 49; // 2字名字，至少要得50分
        if (realNameLen <= 4) return 35; // 3-4字名字，及格线35分
        return 25;                       // 5字及以上，及格线降低到25分
    }

private:
    std::unordered_map<wchar_t, wchar_t> m_confusableMap;

    /**
     * @brief 初始化易混淆字库
     */
    void InitConfusableMap() {
        // --- 1. 极高频 OCR 视觉误差（形近字） ---
        m_confusableMap[L'曰'] = L'日';
        m_confusableMap[L'目'] = L'且';
        m_confusableMap[L'犬'] = L'大';
        m_confusableMap[L'太'] = L'大';
        m_confusableMap[L'玉'] = L'王';
        m_confusableMap[L'干'] = L'千';
        m_confusableMap[L'于'] = L'千';
        m_confusableMap[L'未'] = L'末';
        m_confusableMap[L'乌'] = L'鸟';
        m_confusableMap[L'免'] = L'兔';
        m_confusableMap[L'找'] = L'我';
        m_confusableMap[L'狐'] = L'孤';
        m_confusableMap[L'徽'] = L'微';
        m_confusableMap[L'己'] = L'已';
        m_confusableMap[L'巳'] = L'已';
        m_confusableMap[L'入'] = L'人';
        m_confusableMap[L'八'] = L'人';
        m_confusableMap[L'土'] = L'士';
        m_confusableMap[L'拔'] = L'拨';
        m_confusableMap[L'拨'] = L'拔';
        m_confusableMap[L'幻'] = L'幼';
        m_confusableMap[L'幼'] = L'幻';
        m_confusableMap[L'待'] = L'侍';
        m_confusableMap[L'侍'] = L'待';

        // --- 2. 繁简转换（游戏 ID 高发） ---
        m_confusableMap[L'戰'] = L'战';
        m_confusableMap[L'劍'] = L'剑';
        m_confusableMap[L'龍'] = L'龙';
        m_confusableMap[L'殺'] = L'杀';
        m_confusableMap[L'無'] = L'无';
        m_confusableMap[L'愛'] = L'爱';
        m_confusableMap[L'夢'] = L'梦';
        m_confusableMap[L'亞'] = L'亚';
        m_confusableMap[L'區'] = L'区';
        m_confusableMap[L'網'] = L'网';
        m_confusableMap[L'雲'] = L'云';
        m_confusableMap[L'飛'] = L'飞';
        m_confusableMap[L'極'] = L'极';
        m_confusableMap[L'傷'] = L'伤';
        m_confusableMap[L'術'] = L'术';
        m_confusableMap[L'關'] = L'关';
        m_confusableMap[L'風'] = L'风';
        m_confusableMap[L'電'] = L'电';

        // --- 3. 拼音同音/输入法错字/网络热梗 ---
        m_confusableMap[L'平'] = L'苹';
        m_confusableMap[L'兰'] = L'蓝';
        m_confusableMap[L'蓝'] = L'兰';
        m_confusableMap[L'愉'] = L'偷';
        m_confusableMap[L'税'] = L'悦';
        m_confusableMap[L'垫'] = L'电';
        m_confusableMap[L'沈'] = L'神';
        m_confusableMap[L'度'] = L'渡';
        m_confusableMap[L'清'] = L'青';
        m_confusableMap[L'暗'] = L'岸';

        // --- 4. DNF 等游戏特有高频词纠错 ---
        m_confusableMap[L'苍'] = L'枪';
        m_confusableMap[L'抢'] = L'枪';
        m_confusableMap[L'虹'] = L'红';
        m_confusableMap[L'逛'] = L'狂';
        m_confusableMap[L'魄'] = L'魂';
        m_confusableMap[L'魁'] = L'鬼';
        m_confusableMap[L'泣'] = L'立';
        m_confusableMap[L'刀'] = L'刃';
        m_confusableMap[L'刃'] = L'刀';
        m_confusableMap[L'皇'] = L'星';
    }

    /**
     * @brief 计算最长公共子序列 (LCS)
     */
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

    /**
     * @brief 字符串预处理（清理标点、转小写、易错字映射）
     */
    std::wstring PreprocessString(const std::wstring& s) {
        std::wstring result = L"";

        for (wchar_t c : s) {
            wchar_t lowerC = std::towlower(c);

            // 过滤无意义标点
            if (lowerC == L' ' || lowerC == L'[' || lowerC == L']' || lowerC == L'(' || lowerC == L')') {
                continue;
            }

            // 字典纠错替换
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