#pragma once

// 禁用Windows系统自带的max/min宏，避免与C++标准库冲突
#define NOMINMAX

/**
 * @file NameMatcher.hpp
 * @brief 游戏ID名字匹配工具类（单文件头文件）
 * @details 专为DNF PK圈定制，支持过滤通用前缀(抖音/FSN等)、简写互匹配、分数计算、精准区分用户
 * @author 定制开发
 * @date 2026-03-29
 * @note 支持控制台/MFC调用，宽字符(UTF-16)编码，无第三方依赖
 */

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <utility>

 /**
  * @class CNameMatcher
  * @brief 名字匹配核心类
  * @功能 1. 自动过滤抖音/FSN等通用前缀 2. 支持简写/全称互相匹配 3. 0-100分匹配度计算 4. 精准区分不同用户
  */
class CNameMatcher
{
public:
    /**
     * @brief 构造函数
     * @details 初始化默认过滤前缀列表，按长度排序保证长前缀优先匹配
     */
    CNameMatcher()
    {
        // 默认内置圈子通用前缀（按长度从长到短初始化）
        m_commonPrefixes = {
            L"fsn抖音", L"抖音", L"fsn", L"douyin",
            L"虎牙", L"B站", L"斗鱼", L"战队", L"DNF"
        };
        // 对前缀列表排序，确保长前缀优先匹配
        SortPrefixes();
    }

    /**
     * @brief 移动构造函数（优化性能，避免拷贝）
     * @param other 右值引用对象
     */
    CNameMatcher(CNameMatcher&& other) noexcept
        : m_commonPrefixes(std::move(other.m_commonPrefixes)) {
    }

    /**
     * @brief 移动赋值运算符重载
     * @param other 右值引用对象
     * @return 自身引用
     */
    CNameMatcher& operator=(CNameMatcher&& other) noexcept
    {
        if (this != &other)
        {
            m_commonPrefixes = std::move(other.m_commonPrefixes);
        }
        return *this;
    }

    // 禁用拷贝构造/赋值（避免不必要的内存拷贝，提升性能）
    CNameMatcher(const CNameMatcher&) = delete;
    CNameMatcher& operator=(const CNameMatcher&) = delete;

    /**
     * @brief 析构函数（默认）
     */
    ~CNameMatcher() = default;

    /**
     * @brief 添加自定义需要过滤的前缀
     * @param prefix 要过滤的前缀字符串（宽字符）
     * @example AddCommonPrefix(L"PK赛")
     */
    void AddCommonPrefix(const std::wstring& prefix)
    {
        m_commonPrefixes.push_back(prefix);
        // 添加后重新排序，保证长前缀优先匹配
        SortPrefixes();
    }

    /**
     * @brief 清空所有需要过滤的前缀
     */
    void ClearCommonPrefixes()
    {
        m_commonPrefixes.clear();
    }

    /**
     * @brief 获取两个名字的匹配分数（核心接口）
     * @param realName 真实姓名/核心名字
     * @param gameID 游戏ID（可带前缀）
     * @return int 匹配分数 0-100，分数越高匹配度越高
     */
    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID)
    {
        // 1. 预处理：过滤前缀 + 保留有效字符 + 转小写
        std::wstring realClean = PreprocessString(realName);
        std::wstring gameClean = PreprocessString(gameID);

        // 空字符串直接返回0分
        if (realClean.empty() || gameClean.empty())
            return 0;

        // 区分长短字符串：短串=核心名/简写，长串=全称/带前缀ID
        const std::wstring& shortStr = (realClean.size() <= gameClean.size()) ? realClean : gameClean;
        const std::wstring& longStr = (realClean.size() >= gameClean.size()) ? realClean : gameClean;

        // 双向子序列匹配：支持 真实名包含ID / ID包含真实名
        bool realIsSubOfGame = IsSubsequence(realClean, gameClean);
        bool gameIsSubOfReal = IsSubsequence(gameClean, realClean);
        bool isSubseq = realIsSubOfGame || gameIsSubOfReal;

        // 计算最长公共子序列长度
        int lcsLen = GetLCSLength(realClean, gameClean);
        size_t shortLen = shortStr.size();
        size_t longLen = longStr.size();

        // 字符重合度：短串的匹配比例（核心判断依据）
        float charMatchRate = (shortLen == 0) ? 0.0f : static_cast<float>(lcsLen) / static_cast<float>(shortLen);
        // 长度占比：短串/长串，防止过度泛化匹配
        float lengthRate = (longLen == 0) ? 0.0f : static_cast<float>(shortLen) / static_cast<float>(longLen);

        // ===================== 分数权重（贴合人类直觉）=====================
        // 子序列匹配：50分（最高优先级，简写/包含核心逻辑）
        int subseqScore = isSubseq ? 50 : 0;
        // 字符重合度：40分（核心字符匹配比例）
        int charScore = static_cast<int>(charMatchRate * 40.0f);
        // 长度占比：10分（长度匹配度）
        int lengthScore = static_cast<int>(lengthRate * 10.0f);

        // 计算总分并限制在0-100范围内
        int score = subseqScore + charScore + lengthScore;
        score = score < 0 ? 0 : (score > 100 ? 100 : score);
        return score;
    }

    /**
     * @brief 直接判断是否为同一个人
     * @param realName 真实姓名/核心名字
     * @param gameID 游戏ID（可带前缀）
     * @param threshold 匹配阈值（默认80分，超过即判定为同一人）
     * @return bool true=同一人，false=不同人
     */
    bool IsSamePerson(const std::wstring& realName, const std::wstring& gameID, int threshold = 80)
    {
        int score = GetMatchScore(realName, gameID);
        return score >= threshold;
    }

    /**
     * @brief 获取预处理后的核心名字（调试/日志专用）
     * @param name 原始名字
     * @return std::wstring 过滤前缀+清理后的纯核心名
     */
    std::wstring GetProcessedName(const std::wstring& name)
    {
        return PreprocessString(name);
    }

private:
    /**
     * @brief 对前缀列表按长度降序排序
     * @details 保证长前缀优先匹配（例如先匹配"fsn抖音"，再匹配"抖音"）
     */
    void SortPrefixes()
    {
        std::sort(m_commonPrefixes.begin(), m_commonPrefixes.end(),
            [](const std::wstring& a, const std::wstring& b) {
                return a.size() > b.size();
            });
    }

    /**
     * @brief 过滤字符串开头的通用前缀
     * @param str 原始字符串
     * @return std::wstring 过滤前缀后的字符串
     */
    std::wstring FilterCommonPrefix(const std::wstring& str) const
    {
        std::wstring lowerStr = str;
        // 统一转小写，不区分大小写匹配前缀
        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), towlower);

        // 遍历所有前缀，匹配成功则截取后续字符串
        for (const auto& prefix : m_commonPrefixes)
        {
            std::wstring lowerPrefix = prefix;
            std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), towlower);

            if (lowerStr.find(lowerPrefix) == 0)
            {
                return str.substr(prefix.size());
            }
        }
        // 无匹配前缀，返回原字符串
        return str;
    }

    /**
     * @brief 判断字符是否为中文字符
     * @param c 宽字符
     * @return bool true=中文字符
     */
    bool IsChinese(wchar_t c) const
    {
        // 常用汉字Unicode编码范围
        return (c >= 0x4e00 && c <= 0x9fa5);
    }

    /**
     * @brief 字符串预处理（核心清洗逻辑）
     * @param str 原始字符串
     * @return std::wstring 清洗后的标准字符串
     * @步骤 1.过滤前缀 2.保留中文/字母/数字 3.转小写
     */
    std::wstring PreprocessString(const std::wstring& str) const
    {
        // 第一步：过滤通用前缀
        std::wstring filtered = FilterCommonPrefix(str);
        std::wstring res;

        // 第二步：仅保留中文、字母、数字，其余字符全部过滤
        for (wchar_t c : filtered)
        {
            if (IsChinese(c) || iswalnum(static_cast<wint_t>(c)))
            {
                res += towlower(static_cast<wint_t>(c));
            }
        }
        return res;
    }

    /**
     * @brief 判断子串是否为主串的子序列（字符顺序一致即可）
     * @param subStr 子串（简写/核心名）
     * @param mainStr 主串（全称/带前缀）
     * @return bool true=是子序列
     */
    bool IsSubsequence(const std::wstring& subStr, const std::wstring& mainStr) const
    {
        if (subStr.empty() || mainStr.empty())
            return false;

        size_t i = 0, j = 0;
        // 双指针匹配字符顺序
        while (i < subStr.size() && j < mainStr.size())
        {
            if (subStr[i] == mainStr[j])
                i++;
            j++;
        }
        // 子串全部匹配成功
        return i == subStr.size();
    }

    /**
     * @brief 计算两个字符串的最长公共子序列(LCS)长度
     * @param s1 字符串1
     * @param s2 字符串2
     * @return int LCS长度
     */
    int GetLCSLength(const std::wstring& s1, const std::wstring& s2) const
    {
        if (s1.empty() || s2.empty())
            return 0;

        size_t m = s1.size(), n = s2.size();
        // 动态规划数组
        std::vector<std::vector<int>> dp(static_cast<int>(m) + 1,
            std::vector<int>(static_cast<int>(n) + 1, 0));

        // 填充DP数组
        for (int i = 1; i <= static_cast<int>(m); i++)
        {
            for (int j = 1; j <= static_cast<int>(n); j++)
            {
                if (s1[static_cast<size_t>(i) - 1] == s2[static_cast<size_t>(j) - 1])
                {
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                }
                else
                {
                    // 手动取最大值，避免宏冲突
                    dp[i][j] = dp[i - 1][j] > dp[i][j - 1] ? dp[i - 1][j] : dp[i][j - 1];
                }
            }
        }
        return dp[static_cast<int>(m)][static_cast<int>(n)];
    }

private:
    /// @brief 通用前缀列表（需要自动过滤的前缀）
    std::vector<std::wstring> m_commonPrefixes;
};