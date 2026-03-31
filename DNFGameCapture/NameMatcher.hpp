#pragma once
#define NOMINMAX
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <windows.h>

/**
 * @brief 游戏玩家名称匹配类
 * 采用 LCS (最长公共子序列) 算法，针对 OCR 识别模糊的特性进行加权匹配
 */
class CNameMatcher
{
public:
    CNameMatcher() {}

    /**
     * @brief 计算 OCR 识别名与名单原名的相似度分数
     * @param realName 名单中的标准名字
     * @param gameID   OCR 识别到的原始字符串
     * @return int     匹配分数 (0-100)
     */
    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID)
    {
        // 1. 数据预处理（统一转小写、去除特殊符号、修正易混淆字符）
        std::wstring s1 = PreprocessString(realName);
        std::wstring s2 = PreprocessString(gameID);

        if (s1.empty() || s2.empty()) return 0;

        // 2. 快速匹配：若存在直接包含关系，直接判定为完全匹配
        if (s2.find(s1) != std::wstring::npos || s1.find(s2) != std::wstring::npos) {
            return 100;
        }

        // 3. 模糊匹配：计算最长公共子序列长度
        int lcsLen = GetLCSLength(s1, s2);

        // 【策略】：若匹配到的有效字数 <= 1，视为干扰项（如只中一个“枪”字），直接舍弃
        if (lcsLen <= 1) return 0;

        // 4. 分数加权：以两者中最短的名字长度为基准计算百分比
        size_t minLen = (std::min)(s1.length(), s2.length());
        float matchRate = static_cast<float>(lcsLen) / (minLen == 0 ? 1 : minLen);

        return static_cast<int>(matchRate * 100);
    }

private:
    /**
     * @brief 动态规划实现最长公共子序列
     */
    int GetLCSLength(const std::wstring& s1, const std::wstring& s2) {
        int n = (int)s1.length(), m = (int)s2.length();
        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
        for (int i = 1; i <= n; i++) {
            for (int j = 1; j <= m; j++) {
                if (s1[i - 1] == s2[j - 1])
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                else
                    dp[i][j] = (std::max)(dp[i - 1][j], dp[i][j - 1]);
            }
        }
        return dp[n][m];
    }

    /**
     * @brief 字符串预处理：去噪音、纠错
     */
    std::wstring PreprocessString(const std::wstring& s) {
        // ... (此处保留你原有的过滤 LV、区服及字符纠错逻辑) ...
        return s;
    }
};