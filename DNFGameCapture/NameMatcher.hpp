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

    int GetMatchScore(const std::wstring& realName, const std::wstring& gameID, bool aggressive = false)
    {
        std::wstring s1 = PreprocessString(realName);
        std::wstring s2 = PreprocessString(gameID);

        if (s1.empty() || s2.empty()) return 0;

        // ==========================================
        // 1. 双向强力清洗（解决带有前缀的玩家名无法满分匹配的问题）
        // ==========================================
        std::wstring s1_clean = StripNoise(s1);
        std::wstring s2_clean = StripNoise(s2);

        // 如果玩家自己录入的名字全是屏蔽词（极少见），就保留原名，否则用清洗后的干净名字
        if (!s1_clean.empty()) s1 = s1_clean;

        if (s2_clean.empty()) return 0;
        s2 = s2_clean;

        // ==========================================
        // 2. 绝对防线：精准职业隔离 (修复散打猪猪被误杀)
        // ==========================================
        bool isJob = false;
        for (const auto& job : m_professions) {
            if (s2.find(job) != std::wstring::npos) {
                // 【核心修复】：职业名字长度 + 1。
                // 如果 OCR 是 "散打猪猪"(长度4)，职业是"散打"(长度2)。 4 <= 2+1 为假，放行！
                // 只有当 OCR 真正是 "散打" 或带个符号的 "·散打"(长度3) 时，才拦截！
                if (s2.length() <= job.length() + 1) {
                    isJob = true;
                    break;
                }
            }
        }
        // 职业帧是硬防线：二轮降级也不能把纯职业当成玩家 ID。
        // 新增的 TemporalIdentityMatcher 会把职业作为上下文证据缓存，
        // 但 GetMatchScore 本身绝不直接把职业帧匹配成玩家。
        if (isJob) return -1; // 纯职业帧，直接打回，去查下一帧

        // ==========================================
        // 3. 完美包含秒杀
        // ==========================================
        if (s2.find(s1) != std::wstring::npos) {
            return 100;
        }

        // ==========================================
        // 4. 容错计算：应对错字和残缺
        // ==========================================
        int lcs = GetLCSLength(s1, s2);
        if (lcs == 0) return 0;

        double score1 = (double)lcs / s1.length();
        double score2 = (double)lcs / s2.length();

        // 更偏向于“玩家真名”是否被大量包含
        double baseScore = (score1 * 0.6 + score2 * 0.4) * 100.0;

        // 5. 长度惩罚
        int lenDiff = abs((int)s1.length() - (int)s2.length());
        double penalty = 0;

        if (aggressive) {
            penalty = lenDiff * 2.0; // 二轮降级：微弱惩罚
        }
        else {
            penalty = lenDiff * 6.0; // 首轮匹配：重罚多余杂字
        }

        baseScore -= penalty;

        if (baseScore < 0) baseScore = 0;
        if (baseScore > 100) baseScore = 100;
        return (int)baseScore;
    }

    static int GetDynamicThreshold(int nameLen) {
        if (nameLen <= 2) return 55;
        if (nameLen <= 4) return 45;
        if (nameLen <= 6) return 35;
        return 30;
    }

    // ================================================================
    // 【新增】给固定红框身份融合算法使用的只读/工具接口
    // 说明：不改动原有 ID 匹配主流程，只把职业表、归一化和 LCS 暴露给外层。
    // ================================================================
    const std::vector<std::wstring>& GetProfessions() const {
        return m_professions;
    }

    std::wstring NormalizeForOcr(const std::wstring& s) {
        return PreprocessString(s);
    }

    std::wstring StripNoisePublic(std::wstring s) {
        return StripNoise(s);
    }

    int GetRawLCSLength(const std::wstring& a, const std::wstring& b) {
        return GetLCSLength(a, b);
    }

private:
    std::unordered_map<wchar_t, wchar_t> m_confusableMap;
    std::vector<std::wstring> m_professions;

    void InitProfessionList() {
        m_professions = {
            // [男鬼剑士]
            L"鬼剑士", L"剑魂", L"剑圣", L"剑神", L"极诣·剑魂",
            L"鬼泣", L"弑魂", L"黑暗君主", L"极诣·鬼泣",
            L"狂战士", L"狱血魔神", L"帝血弑天", L"极诣·狂战士",
            L"阿修罗", L"大暗黑天", L"天帝", L"极诣·阿修罗",
            L"剑影", L"夜见罗刹", L"夜刀神", L"极诣·剑影",
            L"红眼", L"白手", L"瞎子",

            // [女鬼剑士]
            L"女鬼剑", L"驭剑士", L"剑宗", L"剑皇", L"极诣·驭剑士",
            L"契魔者", L"剑魔", L"弑神者", L"极诣·契魔者",
            L"暗殿骑士", L"暗帝", L"裁决女神", L"极诣·暗殿骑士",
            L"流浪武士", L"剑帝", L"剑豪", L"极诣·流浪武士",
            L"刃影", L"斩夜", L"孤星", L"极诣·刃影",

            // [男/女格斗家]
            L"格斗家", L"男格斗", L"女格斗",
            L"气功师", L"男气功", L"女气功", L"狂虎帝", L"念皇·光风霁月", L"极诣·气功师", L"百花缭乱", L"念帝", L"归元·气功师",
            L"散打", L"武极", L"极武皇", L"极诣·散打", L"武神", L"极武圣", L"归元·散打",
            L"街霸", L"千手罗汉", L"暗街之王", L"极诣·街霸", L"毒王", L"毒神绝", L"归元·街霸",
            L"柔道家", L"柔道", L"宗师", L"傲之最", L"极诣·柔道家", L"暴风眼", L"风暴女皇", L"归元·柔道家",

            // [男/女神枪手]
            L"神枪手",
            L"漫游枪手", L"漫游", L"男漫", L"女漫", L"枪神", L"掠天之翼", L"重霄·漫游枪手", L"沾血玫瑰", L"绯红玫瑰",
            L"枪炮师",  L"狂暴者", L"毁灭者", L"重霄·枪炮师", L"重炮掌控者", L"风暴骑兵",
            L"机械师", L"机械战神", L"机械元首", L"重霄·机械师", L"机械之心", L"机械之光",
            L"弹药专家", L"大将军", L"战场统治者", L"重霄·弹药专家", L"战争女神", L"芙蕾雅",
            L"合金战士", L"食金战士", L"台金战士", L"钢铁之心", L"超能终结者", L"重霄·合金战士",

            // [男/女魔法师]
            L"魔法师", L"男法", L"女法",
            L"元素爆破师", L"魔皇", L"湮灭之瞳", L"知源·元素爆破师",
            L"冰结师", L"冰冻之心", L"刹那碎星", L"知源·冰结师",
            L"猩红法师", L"血法", L"血狱伯爵", L"血狱君主", L"知源·猩红法师",
            L"逐风者", L"风法", L"御风者", L"风神", L"知源·逐风者",
            L"次元行者", L"次元星神", L"混沌行者", L"知源·次元行者",
            L"元素师", L"大魔导师", L"元素圣灵", L"知源·元素师",
            L"召唤师", L"月之女皇", L"月蚀", L"知源·召唤师",
            L"战斗法师", L"贝亚娜斗神", L"伊丝塔战尊", L"知源·战斗法师",
            L"魔道学者", L"魔术师", L"古灵精怪", L"知源·魔道学者",
            L"小魔女", L"奶萝", L"暗黑少女", L"冥月女神", L"知源·小魔女",

            // [男/女圣职者]
            L"圣职者", L"男圣职", L"女圣职",
            L"圣骑士", L"光明骑士", L"奶爸", L"奶妈", L"天启者", L"神思者", L"光启·圣骑士", L"福音传道者", L"炽天使",
            L"蓝拳圣使", L"蓝拳", L"蓝拳使者", L"神之手", L"正义仲裁者", L"光启·蓝拳圣使",
            L"驱魔师", L"龙斗士", L"真龙星君", L"光启·驱魔师",
            L"复仇者", L"四叔", L"末日审判者", L"永生者", L"光启·复仇者",
            L"异端审判者", L"团长", L"神焰处刑官", L"炎狱裁决者", L"光启·异端审判者",
            L"巫女", L"神龙天女", L"神龙星主", L"光启·巫女",
            L"诱魔者", L"四姨", L"断罪者", L"救世主", L"光启·诱魔者",

            // [暗夜使者]
            L"暗夜使者",
            L"刺客", L"银月", L"月影星劫", L"隐夜·刺客",
            L"死灵术士", L"灵魂收割者", L"亡魂主宰", L"隐夜·死灵术士",
            L"忍者", L"毕方之炎", L"不知火", L"隐夜·忍者",
            L"影舞者", L"梦魇", L"幽冥", L"隐夜·影舞者",

            // [守护者]
            L"守护者",
            L"精灵骑士", L"星辰之光", L"大地女神", L"皓曦·精灵骑士",
            L"混沌魔灵", L"黑魔后", L"黑曜神", L"皓曦·混沌魔灵",
            L"帕拉丁", L"曙光", L"破晓女神", L"皓曦·帕拉丁",
            L"龙骑士", L"龙灵", L"龙神", L"皓曦·龙骑士",

            // [魔枪士]
            L"魔枪士",
            L"征战者", L"关羽", L"战魂", L"不灭战神", L"千魂·征战者",
            L"决战者", L"赵云", L"无双魂", L"圣武枪魂", L"千魂·决战者",
            L"狩猎者", L"光枪", L"灭魔者", L"歼灭者", L"千魂·狩猎者",
            L"暗枪士", L"暗枪", L"狂怒恶鬼", L"幽影夜神", L"千魂·暗枪士",

            // [枪剑士]
            L"枪剑士",
            L"暗刃", L"统御者", L"铁血统帅", L"苍暮·暗刃",
            L"特工", L"绝命谍影", L"弑心镇魂者", L"苍暮·特工",
            L"战线佣兵", L"战场王牌", L"巅峰狂徒", L"苍暮·战线佣兵",
            L"源能专家", L"源力掌控者", L"未来开拓者", L"苍暮·源能专家",

            // [弓箭手]
            L"弓箭手",
            L"缪斯", L"奶弓", L"调音师", L"流行之神", L"聆风·缪斯",
            L"旅人", L"梦游者", L"巡星者", L"聆风·旅人",
            L"猎人", L"鹰眼", L"天罗", L"聆风·猎人",
            L"妖护使", L"幻妖", L"绝影", L"聆风·妖护使",

            // [外传职业及其他补充]
            L"黑暗武士", L"黑武", L"自觉", L"极诣·黑暗武士",
            L"缔造者", L"鼠标妹", L"知源·缔造者"
        };
    }

    void InitConfusableMap() {
        m_confusableMap[L'I'] = L'l'; m_confusableMap[L'1'] = L'l';
        m_confusableMap[L'0'] = L'O'; m_confusableMap[L'o'] = L'O';
        m_confusableMap[L'Z'] = L'2'; m_confusableMap[L'z'] = L'2';
        m_confusableMap[L'S'] = L'5'; m_confusableMap[L's'] = L'5';

        // --- 专属补充：根据你的实战日志加入 OCR 常见错字纠正 ---
        m_confusableMap[L'浸'] = L'漫';
        m_confusableMap[L'支'] = L'士'; // 光明骑支 -> 光明骑士
        m_confusableMap[L'楚'] = L'猪'; // 猪楚越云 -> 猪猪越云
        m_confusableMap[L'今'] = L'闪'; // 一局3今 -> 一局3闪
        m_confusableMap[L'渠'] = L'银'; // 渠太焕 -> 银太焕
        m_confusableMap[L'遵'] = L'道'; // 柔遵家 -> 柔道家
        m_confusableMap[L'捉'] = L'打'; // 散捉 -> 散打
        m_confusableMap[L'绘'] = L'枪'; // 漫游绘手 -> 漫游枪手
        m_confusableMap[L'惑'] = L'辨'; // 无心分惑 -> 无心分辨
        m_confusableMap[L'恬'] = L'师'; // 猩红法恬 -> 猩红法师
        m_confusableMap[L'工'] = L'丁'; // 帅工 -> 帅丁
        m_confusableMap[L'汀'] = L'丁'; // 帅汀 -> 帅丁
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
            // 大区+数字 (长词优先，防截断)
            L"广东1", L"广东2", L"广东3", L"广东4", L"广东5", L"广东6",
            L"北京1", L"北京2", L"北京3", L"北京4",
            L"上海1", L"上海2", L"上海3", L"上海4",
            L"江苏1", L"江苏2", L"江苏3", L"江苏4",
            L"浙江1", L"浙江2", L"浙江3", L"浙江4",
            L"福建1", L"福建2", L"福建3",
            L"山东1", L"山东2", L"山东3", L"山东4",
            L"四川1", L"四川2", L"四川3", L"四川4",
            L"湖北1", L"湖北2", L"湖北3",
            L"湖南1", L"湖南2", L"湖南3",
            L"河南1", L"河南2", L"河南3",
            L"河北1", L"河北2", L"河北3",
            L"东北1", L"东北2", L"东北3",
            L"西北1", L"西北2", L"西北3",
            L"西南1", L"西南2", L"西南3",

            // 纯跨区/省份
            L"跨1", L"跨2", L"跨3", L"跨4", L"跨5", L"跨6", L"跨7", L"跨8",
            L"跨一", L"跨二", L"跨三", L"跨四", L"跨五", L"跨六", L"跨七", L"跨八",
            L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"山东", L"四川", L"湖北", L"湖南", L"河南", L"河北", L"东北", L"西北", L"西南",
            L"一区", L"二区", L"三区", L"四区", L"五区", L"六区", L"七区", L"八区",

            // 直播前缀和公会 (首上海1抖音散打猪猪 -> 处理首)
            L"首", L"斗鱼", L"虎牙", L"虎多", L"抖音", L"科音", L"快手", L"B站", L"哔哩", L"企鹅", L"TV", L"FSN", L"直播", L"JK"
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