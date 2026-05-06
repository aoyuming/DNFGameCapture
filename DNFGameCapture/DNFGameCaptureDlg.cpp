#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <shellapi.h>
#include <Gdiplus.h>
#include <string>
#include <mutex>
#include <ctime>
#include <deque>
#include <future>
#include <winhttp.h>
#include <wincrypt.h>
#include <wininet.h>
#include <tlhelp32.h> // 【新增】：用于遍历和杀掉后台残留进程

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Gdiplus.lib")

#include "json.hpp"
using json = nlohmann::json;
static CString s_backupAuthCode = L"";
using namespace Gdiplus;

// ============================================================================
// 全局变量与辅助结构
// ============================================================================
std::mutex g_bmpMutex;

struct VisualLogMsg {
    CString text;
    COLORREF color;
};

std::deque<VisualLogMsg> g_visualLogs;
std::mutex g_visualLogMutex;

float WINDOW_SCALE = 1.0f;
const int ID_BTN_START = 1005;
const int ID_BTN_APPLY = 1006;
const int ID_CHK_FLIP = 1007;
const int ID_BTN_RESET = 1008;
const int ID_BTN_BROWSE = 1013;
const int ID_EDIT_DIR = 1014;
const int ID_BTN_INPUT_KEY = 1020; // 输入授权码按钮ID
const CString PLACEHOLDER_TEXT = L"输入：主号(小号1)(小号2)...";

struct ScorePointF { float x; float y; };


// ========================================================
// 小号格式校验：真实 ID 少于 3 个字符时，必须带大区或 #职业。
// 规则说明：
//   - “上海1夏雫” -> 真实ID=夏雫，大区=上海1，合法
//   - “夏雫#气功师” -> 真实ID=夏雫，职业=气功师，合法
//   - “夏雫” -> 真实ID=夏雫，且无大区/职业，不合法
// 主号不参与 OCR 名称匹配，所以短 ID 必须补充上下文，避免两字/一字误判。
// ========================================================
static CString DnfTrimmedCopy(CString s)
{
    s.Trim();
    return s;
}

static CString DnfStripDeclaredArea(CString body, bool& hasArea)
{
    static const wchar_t* kAreas[] = {
        L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"四川", L"山东", L"河南", L"湖北", L"湖南",
        L"河北", L"辽宁", L"吉林", L"黑龙江", L"安徽", L"江西", L"广西", L"陕西", L"山西", L"重庆", L"天津",
        L"云南", L"贵州", L"新疆", L"西藏", L"青海", L"甘肃", L"宁夏", L"内蒙古", L"东北", L"西北", L"西南", L"跨"
    };

    hasArea = false;
    for (const wchar_t* area : kAreas) {
        for (int n = 1; n <= 9; ++n) {
            CString token;
            token.Format(L"%s%d", area, n);
            int pos = body.Find(token);
            if (pos >= 0) {
                CString left = body.Left(pos);
                CString right = body.Mid(pos + token.GetLength());
                body = left + right;
                body.Trim();
                hasArea = true;
                return body;
            }
        }
    }
    return body;
}

static CString DnfExtractAliasRealId(const CString& aliasRaw, bool& hasArea, bool& hasJob)
{
    hasArea = false;
    hasJob = false;

    CString body = aliasRaw;
    body.Trim();

    int sharp = body.Find(L'#');
    if (sharp >= 0) {
        CString job = body.Mid(sharp + 1);
        job.Trim();
        if (!job.IsEmpty()) hasJob = true;
        body = body.Left(sharp);
        body.Trim();
    }

    body = DnfStripDeclaredArea(body, hasArea);
    body.Trim();
    return body;
}


struct TDnfSimpleAliasMeta
{
    CString raw;
    CString realId;
    CString area;
    CString job;
    bool hasArea = false;
    bool hasJob = false;
};

static CString DnfNormalizeLooseText(CString s)
{
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

static bool DnfTryExtractAreaToken(CString& body, CString& areaOut)
{
    static const wchar_t* kAreas[] = {
        L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"四川", L"山东", L"河南", L"湖北", L"湖南",
        L"河北", L"辽宁", L"吉林", L"黑龙江", L"安徽", L"江西", L"广西", L"陕西", L"山西", L"重庆", L"天津",
        L"云南", L"贵州", L"新疆", L"西藏", L"青海", L"甘肃", L"宁夏", L"内蒙古", L"东北", L"西北", L"西南", L"跨"
    };

    CString compact = DnfNormalizeLooseText(body);
    for (const wchar_t* area : kAreas) {
        for (int n = 1; n <= 9; ++n) {
            CString token;
            token.Format(L"%s%d", area, n);
            CString ntoken = DnfNormalizeLooseText(token);
            int pos = compact.Find(ntoken);
            if (pos >= 0) {
                // 用原始 body 再查一次，保证能从原字符串中删除真实 token。
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

static TDnfSimpleAliasMeta DnfParseAliasMeta(const CString& rawText)
{
    TDnfSimpleAliasMeta meta;
    meta.raw = rawText;
    CString body = rawText;
    body.Trim();

    int sharp = body.Find(L'#');
    if (sharp >= 0) {
        meta.job = body.Mid(sharp + 1);
        meta.job.Trim();
        meta.hasJob = !meta.job.IsEmpty();
        body = body.Left(sharp);
        body.Trim();
    }

    meta.hasArea = DnfTryExtractAreaToken(body, meta.area);
    body.Trim();
    meta.realId = body;
    meta.realId.Trim();
    return meta;
}

static bool DnfAliasMetaExactSame(const TDnfSimpleAliasMeta& a, const TDnfSimpleAliasMeta& b)
{
    CString aid = DnfNormalizeLooseText(a.realId);
    CString bid = DnfNormalizeLooseText(b.realId);
    if (aid.IsEmpty() || bid.IsEmpty() || aid != bid) return false;

    if (a.hasArea && b.hasArea && a.area != b.area) return false;
    if (a.hasJob && b.hasJob && a.job.CompareNoCase(b.job) != 0) return false;
    return true;
}

static int DnfMetaContextScore(const TDnfSimpleAliasMeta& cand, const TDnfSimpleAliasMeta& ocr, int visibleIdLen, bool& strongContext)
{
    int score = 0;
    strongContext = false;

    if (cand.hasArea && ocr.hasArea) {
        if (cand.area == ocr.area) {
            score += (visibleIdLen <= 2) ? 28 : 16;
            strongContext = true;
        }
        else {
            score -= (visibleIdLen <= 2) ? 35 : 22;
        }
    }

    if (cand.hasJob && ocr.hasJob) {
        if (cand.job.CompareNoCase(ocr.job) == 0) {
            score += (visibleIdLen <= 2) ? 24 : 12;
            strongContext = true;
        }
        else {
            score -= (visibleIdLen <= 2) ? 28 : 14;
        }
    }

    return score;
}

static bool DnfIsLegacyShortAliasWithoutMeta(const CString& aliasRaw)
{
    CString alias = aliasRaw;
    alias.Trim();
    if (alias.IsEmpty()) return false;

    bool hasArea = false;
    bool hasJob = false;
    CString realId = DnfExtractAliasRealId(alias, hasArea, hasJob);
    realId.Trim();

    return (!realId.IsEmpty() && realId.GetLength() < 3 && !hasArea && !hasJob);
}

static CString DnfLegacyShortAliasDeleteReason(const CString& aliasRaw)
{
    CString alias = aliasRaw;
    alias.Trim();
    CString msg;
    msg.Format(L"小号【%s】是旧库短ID，真实ID少于3个字符且没有大区/#职业，容易误识别；已允许直接从小号列表和本地库删除。", (LPCTSTR)alias);
    return msg;
}

static bool DnfValidateAliasShortMeta(const CString& aliasRaw, CString& errorMsg)
{
    CString alias = aliasRaw;
    alias.Trim();
    if (alias.IsEmpty()) {
        errorMsg = L"小号不能为空";
        return false;
    }

    bool hasArea = false;
    bool hasJob = false;
    CString realId = DnfExtractAliasRealId(alias, hasArea, hasJob);

    if (realId.IsEmpty()) {
        errorMsg.Format(L"小号【%s】缺少真实ID，不能只填大区或职业。", (LPCTSTR)alias);
        return false;
    }

    if (realId.GetLength() < 3 && !hasArea && !hasJob) {
        errorMsg.Format(
            L"小号【%s】真实ID少于3个字符，必须加大区或 #职业，例如：上海1%s 或 %s#气功师。",
            (LPCTSTR)alias, (LPCTSTR)realId, (LPCTSTR)realId);
        return false;
    }

    return true;
}

static bool DnfValidateAliasListShortMeta(const std::vector<CString>& aliases, CString& errorMsg)
{
    for (const auto& alias : aliases) {
        if (!DnfValidateAliasShortMeta(alias, errorMsg)) return false;
    }
    return true;
}

// ========================================================
// 死亡 X 逻辑点：8 个最终判定点
// 索引约定：0-3 = 左侧，4-7 = 右侧
// 0 左主将，1 左下第1，2 左下第2，3 左下第3
// 4 右主将，5 右下第1，6 右下第2，7 右下第3
//
// v4：检测恢复使用旧版 40 点候选数据，但输出仍然是 8 个逻辑点。
// 每个逻辑点对应旧版 5 个候选点；每个候选点都执行 X 两条斜边射线检测。
// 无效/低分候选点自动丢弃，ROI 红色统计只辅助，不单独判死。
// ========================================================
constexpr int DEATH_POINT_COUNT = 8;
constexpr int DEATH_RAW_POINT_COUNT = 40;
constexpr int DP_LEFT_ACTIVE  = 0;
constexpr int DP_RIGHT_ACTIVE = 4;

// 回滚到旧版 40 点坐标。每 5 个一组，但真正参与死亡判定的只有每组第 0 个点：
// 右侧：0、5、10、15；左侧：20、25、30、35。
// 其余点保留只是兼容旧采点输出，不参与逻辑判断。
ScorePointF g_deathPts[DEATH_RAW_POINT_COUNT] = {
    // 右侧：0-19，顺序：右主将、右下第1、右下第2、右下第3
    { 0.8266f, 0.0366f },
    { 0.8297f, 0.0348f },
    { 0.8297f, 0.0422f },
    { 0.8245f, 0.0422f },
    { 0.8245f, 0.0330f },
    { 0.8111f, 0.1137f },
    { 0.8111f, 0.1137f },
    { 0.8111f, 0.1137f },
    { 0.8111f, 0.1137f },
    { 0.8111f, 0.1137f },
    { 0.7151f, 0.1137f },
    { 0.7151f, 0.1137f },
    { 0.7151f, 0.1137f },
    { 0.7151f, 0.1137f },
    { 0.7151f, 0.1137f },
    { 0.6181f, 0.1119f },
    { 0.6181f, 0.1119f },
    { 0.6181f, 0.1119f },
    { 0.6181f, 0.1119f },
    { 0.6181f, 0.1119f },

    // 左侧：20-39，顺序：左主将、左下第1、左下第2、左下第3
    { 0.1754f, 0.0366f },
    { 0.1754f, 0.0366f },
    { 0.1754f, 0.0366f },
    { 0.1754f, 0.0366f },
    { 0.1754f, 0.0366f },
    { 0.1867f, 0.1137f },
    { 0.1867f, 0.1137f },
    { 0.1867f, 0.1137f },
    { 0.1867f, 0.1137f },
    { 0.1867f, 0.1137f },
    { 0.2837f, 0.1119f },
    { 0.2837f, 0.1119f },
    { 0.2837f, 0.1119f },
    { 0.2837f, 0.1119f },
    { 0.2837f, 0.1119f },
    { 0.3808f, 0.1119f },
    { 0.3808f, 0.1119f },
    { 0.3808f, 0.1119f },
    { 0.3808f, 0.1119f },
    { 0.3808f, 0.1119f },
};

static int GetDeathRawIndex(int logicalIdx)
{
    static const int map[DEATH_POINT_COUNT] = {
        20, 25, 30, 35,  // 0-3：左侧
        0, 5, 10, 15     // 4-7：右侧
    };
    if (logicalIdx < 0 || logicalIdx >= DEATH_POINT_COUNT) return 0;
    return map[logicalIdx];
}

static ScorePointF GetDeathLogicPoint(int logicalIdx)
{
    return g_deathPts[GetDeathRawIndex(logicalIdx)];
}

// ========================================================
// 【实时调试】8 个死亡 X 的检测快照
// 说明：CheckColorTrigger() 每次刷新这里，Draw() 直接画到预览画面上。
// drawX/drawY 显示当前 8 个逻辑 X 的有效中心点，方便调参。
// ========================================================
struct DeathXDebugState {
    bool dead[DEATH_POINT_COUNT];
    int matchCount[DEATH_POINT_COUNT];
    int roiHits[DEATH_POINT_COUNT];
    COLORREF centerColor[DEATH_POINT_COUNT];
    float drawX[DEATH_POINT_COUNT];
    float drawY[DEATH_POINT_COUNT];
    bool centerGate[DEATH_POINT_COUNT];
    int hitCount[DEATH_POINT_COUNT];
    float hitX[DEATH_POINT_COUNT][16];
    float hitY[DEATH_POINT_COUNT][16];
    int hitDir[DEATH_POINT_COUNT][16];
    DWORD lastTick;
};

DeathXDebugState g_deathXDebug = {};
std::mutex g_deathXDebugMutex;

static const wchar_t* GetDeathPointName(int idx)
{
    switch (idx) {
    case 0: return L"左主";
    case 1: return L"左1";
    case 2: return L"左2";
    case 3: return L"左3";
    case 4: return L"右主";
    case 5: return L"右1";
    case 6: return L"右2";
    case 7: return L"右3";
    default: return L"?";
    }
}


// ========================================================
// 【新增】：完美的 MessageBox 强行居中钩子引擎
// ========================================================
HHOOK g_hMsgBoxHook = NULL;
HWND  g_hMsgBoxParent = NULL;
std::mutex g_msgBoxMutex;

LRESULT CALLBACK MsgBoxCBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 拦截窗口即将激活的瞬间
    if (nCode == HCBT_ACTIVATE) {
        HWND hMsgBox = (HWND)wParam;
        if (g_hMsgBoxParent && ::IsWindow(g_hMsgBoxParent)) {
            RECT pr, mr;
            ::GetWindowRect(g_hMsgBoxParent, &pr); // 获取软件主窗口坐标
            ::GetWindowRect(hMsgBox, &mr);         // 获取弹窗的坐标

            // 像素级计算正中心坐标
            int x = pr.left + (pr.right - pr.left) / 2 - (mr.right - mr.left) / 2;
            int y = pr.top + (pr.bottom - pr.top) / 2 - (mr.bottom - mr.top) / 2;

            // 强行挪过去
            ::SetWindowPos(hMsgBox, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        // 挪完就过河拆桥，卸载钩子，防止卡顿
        ::UnhookWindowsHookEx(g_hMsgBoxHook);
        g_hMsgBoxHook = NULL;
    }
    return CallNextHookEx(g_hMsgBoxHook, nCode, wParam, lParam);
}

int CDNFGameCaptureDlg::ShowCenteredMsgBox(LPCTSTR lpszText, LPCTSTR lpszCaption, UINT nType) {
    // 加锁防止多线程同时弹窗导致钩子冲突
    std::lock_guard<std::mutex> lock(g_msgBoxMutex);

    g_hMsgBoxParent = this->GetSafeHwnd();
    // 挂上只针对当前线程的拦截钩子
    g_hMsgBoxHook = SetWindowsHookEx(WH_CBT, MsgBoxCBTProc, NULL, GetCurrentThreadId());

    // 呼出系统的 MessageBox，它刚探出头就会被钩子按在正中间！
    return ::MessageBox(g_hMsgBoxParent, lpszText, lpszCaption, nType);
}


// ==========================================
// 【新增】：纯符号赛博幽灵检测器
// ==========================================
bool IsPureSymbol(const CString& str) {
    if (str.IsEmpty()) return false;
    for (int i = 0; i < str.GetLength(); i++) {
        wchar_t c = str[i];
        // 只要包含任何汉字、英文字母或数字，就说明它是正常的 ID
        if ((c >= L'a' && c <= L'z') ||
            (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') ||
            (c >= 0x4E00 && c <= 0x9FA5)) { // 基本汉字区间
            return false;
        }
    }
    return true; // 全是奇形怪状的符号
}

// 获取中英文混合字符串的视觉宽度
int GetVisualWidth(const CString& s) {
    int w = 0;
    for (int i = 0; i < s.GetLength(); i++) {
        w += (s[i] >= 0x4E00 && s[i] <= 0x9FFF) ? 2 : 1;
    }
    return w;
}

// 按语义版本号比较：返回 1 表示 a > b，-1 表示 a < b，0 表示相等
static int CompareVersion(const CString& a, const CString& b) {
    int ia = 0, ib = 0;
    int la = a.GetLength(), lb = b.GetLength();
    while (ia < la || ib < lb) {
        int numA = 0, numB = 0;
        while (ia < la && a[ia] != L'.') {
            if (a[ia] >= L'0' && a[ia] <= L'9')
                numA = numA * 10 + (a[ia] - L'0');
            ia++;
        }
        while (ib < lb && b[ib] != L'.') {
            if (b[ib] >= L'0' && b[ib] <= L'9')
                numB = numB * 10 + (b[ib] - L'0');
            ib++;
        }
        if (numA > numB) return 1;
        if (numA < numB) return -1;
        ia++; ib++; // 跳过 '.'
    }
    return 0;
}

// 时间戳转字符串
CString FormatTimeStamp(long long ts) {
    if (ts >= 0xFFFFFFF0) return L"永久有效";
    time_t t_ts = (time_t)ts;
    tm t;
    localtime_s(&t, &t_ts);
    CString res;
    res.Format(L"%04d-%02d-%02d %02d:%02d:%02d",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return res;
}


// 写入本地匹配日志。AppLog 需要提前声明，用来把被 UI 过滤的详细日志写入文件。
void WriteMatchLog(const CString& logLine);

// 【新增】：全局通用的 UI 日志输出助手，随处可用
void AppLog(const CString& msg, COLORREF color) {
    // 软件内不显示身份融合相关细节，只写入文件，避免 UI 刷屏。
    if (msg.Find(L"身份融合") >= 0) {
        WriteMatchLog(msg);
        return;
    }

    time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
    CString tStr; tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
    std::lock_guard<std::mutex> lk(g_visualLogMutex);
    g_visualLogs.push_back({ tStr, color });
}

// 写入本地匹配日志
void WriteMatchLog(const CString& logLine) {
    CFile file;
    if (file.Open(L"match_debug.log", CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite)) {
        if (file.GetLength() == 0) {
            unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            file.Write(bom, 3);
        }
        file.SeekToEnd();
        time_t now = time(0);
        tm t;
        localtime_s(&t, &now);
        CString fullLine;
        fullLine.Format(L"[%02d:%02d:%02d] %s\r\n", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)logLine);
        std::string utf8Line = CW2A(fullLine, CP_UTF8);
        file.Write(utf8Line.c_str(), (UINT)utf8Line.length());
        file.Close();
    }
}

// ============================================================================
// 消息映射与全局快捷键
// ============================================================================
BEGIN_MESSAGE_MAP(CDNFGameCaptureDlg, CWnd)
    ON_WM_TIMER()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CLOSE()
    ON_WM_LBUTTONDOWN()
    ON_BN_CLICKED(ID_BTN_START, OnBnClickedStart)
    ON_BN_CLICKED(ID_BTN_APPLY, OnBnClickedApply)
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip)
    ON_BN_CLICKED(ID_BTN_RESET, OnBnClickedReset)
    ON_BN_CLICKED(ID_BTN_BROWSE, OnBnClickedBrowseDir)
    ON_BN_CLICKED(ID_BTN_INPUT_KEY, OnBnClickedInputKey)
    ON_WM_SYSCOMMAND()
    ON_WM_HOTKEY()
    ON_EN_CHANGE(1025, &CDNFGameCaptureDlg::OnChangeEditNamesInput) // 1001是你输入框的ID
    ON_MESSAGE(WM_TRAY_MESSAGE, &CDNFGameCaptureDlg::OnTrayMessage)
    ON_MESSAGE(WM_UPDATE_OCR_DROPDOWNS, &CDNFGameCaptureDlg::OnUpdateOcrDropdowns)
    ON_CBN_SELCHANGE(1010, &CDNFGameCaptureDlg::OnCbnSelchangeLeft)
    ON_CBN_SELCHANGE(1009, &CDNFGameCaptureDlg::OnCbnSelchangeRight)
    ON_BN_CLICKED(1021, &CDNFGameCaptureDlg::OnBnClickedHelp) // 【新增】：绑定说明按钮
    ON_EN_SETFOCUS(1025, &CDNFGameCaptureDlg::OnEditSetFocus)   // 得到焦点
    ON_EN_KILLFOCUS(1025, &CDNFGameCaptureDlg::OnEditKillFocus) // 失去焦点
    // 绑定添加按钮和树控件的右键菜单
    ON_BN_CLICKED(1022, &CDNFGameCaptureDlg::OnBnClickedQuickAdd)
    ON_NOTIFY(NM_RCLICK, 1023, &CDNFGameCaptureDlg::OnRClickTree)
    // 1023 是树控件的 ID
    ON_NOTIFY(TVN_ENDLABELEDIT, 1023, &CDNFGameCaptureDlg::OnEndLabelEdit)
    // 找到 BEGIN_MESSAGE_MAP 区域，添加下面这一行
    ON_NOTIFY(NM_CUSTOMDRAW, 1023, &CDNFGameCaptureDlg::OnCustomDrawTree)
    ON_WM_CTLCOLOR() // 添加这一行，拦截所有的颜色请求
    ON_MESSAGE(WM_UPDATE_ALL_UI, &CDNFGameCaptureDlg::OnUpdateAllUI)// 【新增】：绑定自定义 UI 刷新消息
    ON_MESSAGE(WM_CLOUD_AUTH_FAIL, &CDNFGameCaptureDlg::OnCloudAuthFail) // 【新增】
    ON_CBN_SELCHANGE(1030, &CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine)
    ON_MESSAGE(WM_UPDATE_AUTH_TIME, &CDNFGameCaptureDlg::OnUpdateAuthTime)
    ON_CBN_DROPDOWN(1031, &CDNFGameCaptureDlg::OnCbnDropdownTargetWindow)
    ON_CBN_CLOSEUP(1031, &CDNFGameCaptureDlg::OnCbnCloseupTargetWindow)
    // ⬇️ 【新增】：绑定 1033 (我们给新列表框的ID) 的点击事件
    ON_LBN_SELCHANGE(1033, &CDNFGameCaptureDlg::OnLbnSelchangeRecentPlayers)
    ON_MESSAGE(WM_WEB_CMD_RECEIVED, &CDNFGameCaptureDlg::OnWebCmdReceived)
    ON_WM_MOUSEMOVE()
    ON_WM_RBUTTONDOWN()


END_MESSAGE_MAP()

void CDNFGameCaptureDlg::OnMouseMove(UINT nFlags, CPoint point) {
    // 鼠标在预览区移动时，高频重绘触发显微镜画面
    if (m_w > 0 && m_previewRect.PtInRect(point)) {
        InvalidateRect(&m_previewRect, FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CDNFGameCaptureDlg::OnRButtonDown(UINT nFlags, CPoint point) {
    // 【右键撤销功能】：点错了直接右键，删掉上一个点
    if (m_w > 0 && m_previewRect.PtInRect(point)) {
        if (!m_selectPts.empty()) {
            m_selectPts.pop_back();
            InvalidateRect(&m_previewRect, FALSE);
        }
    }
    CWnd::OnRButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= DEATH_RAW_POINT_COUNT) m_selectPts.clear(); // 改为 8 个关键点
        m_selectPts.push_back(CPoint(
            (int)(((float)(point.x - m_previewRect.left) / m_previewRect.Width()) * 10000.0f),
            (int)(((float)(point.y - m_previewRect.top) / m_previewRect.Height()) * 10000.0f)
        ));
        InvalidateRect(&m_previewRect, FALSE);

        // 凑齐 40 个点后，直接生成全新的数组代码
        if (m_selectPts.size() == DEATH_RAW_POINT_COUNT) {
            CString res;
            res.Format(L"ScorePointF g_deathPts[%d] = {\r\n", DEATH_RAW_POINT_COUNT);
            for (int i = 0; i < DEATH_RAW_POINT_COUNT; i++) {
                CString t; t.Format(L"    { %.4ff, %.4ff },\r\n", m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f); res += t;
            }
            m_editOcrResult.SetWindowText(res + L"};\r\n");
            MessageBox(L"🎉 40个大X坐标已采集完毕！\r\n请去右侧日志框复制代码。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2) {
    if (nHotKeyId == 8008) {
        ManualTriggerKill(0); // 触发红队
    }
    else if (nHotKeyId == 8009) {
        ManualTriggerKill(1); // 触发蓝队
    }
    CWnd::OnHotKey(nHotKeyId, nKey1, nKey2);
}

// ============================================================================
// 核心授权验证逻辑 (本地软拦截 + 云端强校验)
// ============================================================================
unsigned int CustomSimpleHash(const std::string& str) {
    unsigned int hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

CString CDNFGameCaptureDlg::GetMachineID() {
    DWORD volSerial = 0;
    GetVolumeInformation(L"C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0);
    CString hwid;
    hwid.Format(L"%08X", volSerial);
    return hwid;
}

// ==========================================
// 【新增】：精准击杀指定名称的后台进程
// ==========================================
void CDNFGameCaptureDlg::KillProcessByName(const CString& processName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnap, &pe)) {
        do {
            CString currentName(pe.szExeFile);
            if (currentName.CompareNoCase(processName) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
}


LRESULT CDNFGameCaptureDlg::OnCloudAuthFail(WPARAM wParam, LPARAM lParam) {
    CString* pCloudResult = (CString*)lParam;
    if (pCloudResult) {
        m_bIsAuthValid = false;
        m_cloudExpireTime = 0;

        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        OutputDebugAuthInfo();
        if (m_bIsRunning) OnBnClickedStart();

        // 🚨 还原旧的卡密到文件
        wchar_t exePath[MAX_PATH]; GetModuleFileName(NULL, exePath, MAX_PATH);
        CString path = exePath; path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";
        CFile fileWrite;
        if (fileWrite.Open(path, CFile::modeCreate | CFile::modeWrite)) {
            std::string ansiKey = CW2A(s_backupAuthCode, CP_UTF8);
            fileWrite.Write(ansiKey.c_str(), (UINT)ansiKey.length()); fileWrite.Close();
        }

        if (m_bIsManualAuthCheck) { // 🚨 只有手动点授权，才弹失败提示！
            json reply; reply["action"] = "auth_result"; reply["success"] = false;
            reply["message"] = std::string(CW2A(L"❌ 验证失败！\r\n卡密无效或已过期，已还原旧卡密。\r\n原因：" + *pCloudResult, CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
        m_bIsManualAuthCheck = false; // 重置标记

        CheckTrialAndLicense(); // 重新加载旧授权激活状态
        BroadcastStateToWeb();  // 通知网页刷新状态文字
        delete pCloudResult;
    }
    return 0;
}

// 找到 LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime
LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime(WPARAM wParam, LPARAM lParam) {
    long long cloudTime = (long long)lParam;
    m_cloudExpireTime = cloudTime;
    m_bIsAuthValid = (cloudTime > 1 || cloudTime == 0xFFFFFFFF);

    if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
    OutputDebugAuthInfo();
    if (m_bIsAuthValid) AppLog(L"✅ [云端验证] 授权已激活，欢迎使用！", RGB(0, 255, 100));

    if (m_bIsAuthValid) {
        if (m_bIsManualAuthCheck) { // 🚨 只有手动点授权，才弹成功提示！
            json reply; reply["action"] = "auth_result"; reply["success"] = true;
            reply["message"] = std::string(CW2A(L"✅ 授权验证成功！\r\n您已激活专业版。", CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
    }
    m_bIsManualAuthCheck = false; // 重置标记
    BroadcastStateToWeb();
    return 0;
}

bool CDNFGameCaptureDlg::VerifyKey(CString inputKey, CString machineID) {
    // ===================================
    // 纯血新版：只允许动态时长激活码 (CDK-开头)
    // ===================================
    if (inputKey.Left(4) == L"CDK-") {
        int firstDash = 3;
        int secondDash = inputKey.Find(L'-', firstDash + 1);
        int thirdDash = inputKey.Find(L'-', secondDash + 1);

        if (thirdDash != -1) {
            CString durStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
            CString nonceStr = inputKey.Mid(secondDash + 1, thirdDash - secondDash - 1);
            CString sigStr = inputKey.Mid(thirdDash + 1);

            long long duration = wcstoll(durStr, NULL, 16);
            unsigned int sig = wcstoul(sigStr, NULL, 16);

            CString signData; signData.Format(L"%llX-%s-MySuperSecretKey2026", duration, (LPCTSTR)nonceStr);
            if (sig != CustomSimpleHash(std::string(CW2A(signData, CP_UTF8)))) return false;

            m_keyDuration = duration;
            m_cloudExpireTime = -1;    // 设置为正在请求云端的状态

            // 【关键修改】：本地格式过关不代表授权有效！
            // 必须设为 false，等待子线程拿到云端的“OK”后再反转。
            m_bIsAuthValid = false;
            return true;
        }
    }

    // 如果是 DNF- 开头的老卡，或者乱输的字符，统统在这里直接拦截！
    return false;
}

CString CDNFGameCaptureDlg::CheckCloudBinding(CString key, CString hwid, long long duration, long long& outExpTime) {
    CString jsonStr;
    // 把 duration 一并传给 Node.js
    jsonStr.Format(L"{\"key\": \"%s\", \"hwid\": \"%s\", \"duration\": %lld}", key, hwid, duration);
    std::string jsonUtf8 = CW2A(jsonStr, CP_UTF8);

    HINTERNET hSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    // ==========================================
    // 【关键修复 1】：设置 3 秒超时！绝不允许后台线程无限卡死！
    // ==========================================
    if (hSession) {
        WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);
    }
    HINTERNET hConnect = WinHttpConnect(hSession, L"verifykey-thaovfpoib.cn-hangzhou.fcapp.run", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    CString resultMsg = L"未知请求异常";
    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        if (WinHttpSendRequest(hRequest, NULL, 0, (LPVOID)jsonUtf8.c_str(), (DWORD)jsonUtf8.length(), (DWORD)jsonUtf8.length(), 0) && WinHttpReceiveResponse(hRequest, NULL)) {
            std::string resp; DWORD sz = 0, dl = 0;
            while (WinHttpQueryDataAvailable(hRequest, &sz) && sz > 0) {
                std::vector<char> buf(sz + 1, 0);
                if (WinHttpReadData(hRequest, buf.data(), sz, &dl)) resp.append(buf.data(), dl);
            }

            if (resp.find("\"status\":\"ok\"") != std::string::npos) {
                resultMsg = L"OK";
                // 【提取云端返回的 expireTime】
                size_t pExp = resp.find("\"expireTime\":");
                if (pExp != std::string::npos) {
                    outExpTime = atoll(resp.c_str() + pExp + 13);
                }
            }
            else {
                size_t p1 = resp.find("\"msg\":\"");
                if (p1 != std::string::npos) {
                    size_t p2 = resp.find("\"", p1 + 7);
                    if (p2 != std::string::npos) resultMsg = CA2W(resp.substr(p1 + 7, p2 - p1 - 7).c_str(), CP_UTF8);
                }
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resultMsg;
}

// 授权检查函数：支持云端异步校验与环境隔离
void CDNFGameCaptureDlg::CheckTrialAndLicense() {
    // =========================================================
    // 【模式切换】：如果是云端测试模式，直接赋予上帝权限，跳过所有校验
    // =========================================================
#if ENABLE_CLOUD_TEST_MODE
    m_bIsAuthValid = true;   // 授权有效
    m_bIsTrial = false;      // 非试用（正式模式）
    m_trialEnd = 0;
    return;
#endif

    // 默认初始化状态：未授权
    m_bIsAuthValid = false;
    m_bIsTrial = false;
    m_trialEnd = 0;

    // 获取机器码
    CString hwid = GetMachineID();

    // 定位本地 license.txt 路径
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    // --- 第一阶段：尝试读取本地卡密 ---
    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        char buf[256] = { 0 };
        UINT nRead = file.Read(buf, 255);
        file.Close();

        CString inputKey(buf);
        inputKey.Trim();

        // 基础算法校验（这里现在只有 CDK 能通过了）
        if (!inputKey.IsEmpty() && VerifyKey(inputKey, hwid)) {
            // 【关键修复 2】：删掉 m_bIsAuthValid = true; 
            // 离线格式对了也没用，必须设为 false，等待云端判决！
            m_bIsTrial = false;
            long long duration = m_keyDuration;
            HWND hWnd = GetSafeHwnd();

            std::thread([this, hWnd, inputKey, hwid, duration]() {
                long long cloudExpTime = 0;
                CString cloudResult = CheckCloudBinding(inputKey, hwid, duration, cloudExpTime);

                if (cloudResult != L"OK" && ::IsWindow(hWnd)) {
                    CString* pResult = new CString(cloudResult);
                    ::PostMessage(hWnd, WM_CLOUD_AUTH_FAIL, 0, (LPARAM)pResult);
                }
                else if (cloudResult == L"OK" && ::IsWindow(hWnd)) {
                    ::PostMessage(hWnd, WM_UPDATE_AUTH_TIME, 0, (LPARAM)cloudExpTime);
                }
                }).detach();

            return;
        }
    }

    // --- 第三阶段：如果没有卡密，检查注册表试用期 ---
    HKEY hKey;
    time_t now = time(nullptr);
    // 打开注册表项：HKEY_CURRENT_USER\Software\DNFCapture
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD iT = 0, sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz) == ERROR_SUCCESS) {
            // 试用期设定为 7 天 (604800 秒)
            long long expireTime = (long long)iT + 604800;
            if (iT > 0 && (long long)now <= expireTime) {
                m_bIsAuthValid = true;
                m_bIsTrial = true;
                m_trialEnd = expireTime;
            }
        }
        RegCloseKey(hKey);
    }
}

void CDNFGameCaptureDlg::OutputDebugAuthInfo() {
    auto print = [&](const CString& t, COLORREF c) {
        if (!m_editVisualLogs.m_hWnd) return;
        int l = m_editVisualLogs.GetWindowTextLength();
        m_editVisualLogs.SetSel(l, l);
        CHARFORMAT cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = c;
        m_editVisualLogs.SetSelectionCharFormat(cf);
        m_editVisualLogs.ReplaceSel(t + L"\r\n");
        m_editVisualLogs.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);
        };

    print(L"====== [本机授权状态信息] ======", RGB(255, 215, 0));
    print(L"本机机器码: " + GetMachineID(), RGB(200, 200, 200));

    if (m_bIsAuthValid) {
        print(L"当前状态: [ ✔ 授权有效，可正常监控 ]", RGB(0, 255, 0));
    }
    else {
        print(L"当前状态: [ ❌ 授权无效，监控被锁定 ]", RGB(255, 80, 80));
    }

    HKEY hKey;
    time_t now = time(nullptr);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD iT = 0, sz = 4;
        if (RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz) == ERROR_SUCCESS) {
            long long expTime = (long long)iT + 604800;
            if (expTime > (long long)now) {
                print(L"试用期结束时间: " + FormatTimeStamp(expTime), RGB(0, 255, 255));
            }
            else {
                print(L"试用期状态: 已过期", RGB(150, 150, 150));
            }
        }
        RegCloseKey(hKey);
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        char buf[256] = { 0 };
        file.Read(buf, 255);
        file.Close();
        CString inputKey(buf);
        inputKey.Trim();

        // ==========================================
        // 【新增】：卡密脱敏处理 (数据打码)
        // ==========================================
        CString displayKey = inputKey;
        if (displayKey.GetLength() > 12) {
            // 保留前 8 个字符 (例如 CDK-278D 或 DNF-69F6)
            // 保留后 4 个字符 (例如 9A91)，中间全部用 **** 替换
            displayKey = displayKey.Left(8) + L"****-****-" + displayKey.Right(4);
        }

        print(L"本地卡密记录: " + displayKey, RGB(180, 180, 180));

        if (inputKey.Left(4) == L"CDK-") {
            // ... 下面保持不变
            if (!m_bIsAuthValid && m_cloudExpireTime == 0) {
                print(L"该卡密状态: ❌ 无效卡密 (格式错误或被篡改)", RGB(255, 80, 80));
            }
            else if (m_cloudExpireTime > 0) {
                print(L"该卡密到期时间: " + FormatTimeStamp(m_cloudExpireTime), RGB(200, 200, 200));
            }
            else if (m_cloudExpireTime == -1) {
                print(L"该卡密到期时间: 正在向云端同步激活信息...", RGB(255, 165, 0));
            }
            else {
                print(L"该卡密到期时间: 验证通过 (以云端记录为准)", RGB(0, 255, 100));
            }
        }
        else {
            // 【新增】：旧版卡密无情拒绝提示
            print(L"该卡密状态: ❌ 已淘汰的旧版卡密，请联系管理员更换新版 CDK！", RGB(255, 80, 80));
        }
    }
    print(L"==================================", RGB(255, 215, 0));
}

// ============================================================================
// 初始化与窗口过程
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    // 1. 🚨【关键修复】：在第一行初始化 COM 组件！这能直接解决 0x800401f0 闪退报错！
    CoInitialize(NULL);
    m_bIsAuthValid = false;
    m_pWebDlg = nullptr;
    m_bIsManualAuthCheck = false; // 初始设为 false

    m_hSingleInstanceMutex = CreateMutex(NULL, TRUE, L"Global\\DNFGameCapture_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(L"程序已经在运行中！\r\n请在右下角任务栏中查找。", L"提示", MB_ICONINFORMATION | MB_OK);
        exit(0);
    }

    m_bmp = NULL; m_w = 0; m_h = 0; m_bIsRunning = FALSE;
    m_bCanTrigger = TRUE; m_bCanTriggerTeamScore = TRUE;
    m_historyIdx = 0; m_bPendingTeamScoreWin = false;
    m_totalScoreRed = 0; m_totalScoreBlue = 0; m_lastKillerTeam = -1; m_bFlipSides = false;
    m_hDebugOcrBmp[0] = NULL; m_hDebugOcrBmp[1] = NULL;
    m_viewIndexLeft = -1; m_viewIndexRight = -1;

    GdiplusStartupInput gpi;
    GdiplusStartup(&m_gdiplusToken, &gpi, NULL);

    m_hHttpSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (m_hHttpSession) {
        WinHttpSetTimeouts(m_hHttpSession, 1500, 1500, 2500, 2500);
        m_hHttpConnect = WinHttpConnect(m_hHttpSession, L"127.0.0.1", 1224, 0);
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString appDir = exePath;
    int pos = appDir.ReverseFind(L'\\');
    if (pos != -1) appDir = appDir.Left(pos + 1);

    m_ocrExePath = appDir + L"Umi-OCR.exe";
    m_configPath = appDir + L"players_config.txt";
    m_iniPath = appDir + L"config.ini";

    wchar_t dirBuf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"OutputDir", appDir, dirBuf, MAX_PATH, m_iniPath);
    m_outputDir = dirBuf;
    if (m_outputDir.Right(1) == L"\\") {
        m_outputDir.TrimRight(L"\\");
    }

    AfxInitRichEdit2();
    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) { m_historyBmps[i] = NULL; }
    for (int i = 0; i < 8; i++) {
        m_players[i].kills = 0; m_players[i].deaths = 0;
        m_players[i].currentStreak = 0; m_players[i].akCount = 0;
        m_players[i].team = (i < 4 ? 0 : 1);
    }

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CString title;
    title.Format(L"DNF击杀统计(v%s)", CURRENT_VERSION);

    // 【新增】：分辨率自适应
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    if (screenY <= 1080) {
        WINDOW_SCALE = 1.2f; // 1080P或更低，缩小界面
    }
    else {
        WINDOW_SCALE = 1.6f; // 2K/4K屏幕，放大界面
    }

    CreateEx(0, cls, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100, (int)(750 * WINDOW_SCALE), (int)(760 * WINDOW_SCALE), NULL, NULL);

    // ========================================================
    // 🚨 终极架构修复：强制在后台提前初始化所有 UI 和数据库！
    // 彻底解决隐藏启动导致的断言崩溃与库文件被清空的问题！
    // ========================================================
    CRect r;
    GetClientRect(&r);
    int splitY = max(100, r.bottom - (int)(390 * WINDOW_SCALE));

    m_font.CreatePointFont(95, L"微软雅黑");
    int row1_Y = splitY + 5;
    m_chkFlip.Create(L"翻转红蓝", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(10, row1_Y, 95, row1_Y + 25), this, ID_CHK_FLIP); m_chkFlip.SetFont(&m_font);
    m_btnHelp.Create(L"说明", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(100, row1_Y, 150, row1_Y + 25), this, 1021); m_btnHelp.SetFont(&m_font);
    m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(155, row1_Y + 4, 215, row1_Y + 25), this, 1003); m_status.SetFont(&m_font);
    m_cmbCaptureEngine.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(220, row1_Y, 380, row1_Y + 200), this, 1030); m_cmbCaptureEngine.SetFont(&m_font);
    m_cmbCaptureEngine.AddString(L"🔄 自动选择引擎"); m_cmbCaptureEngine.AddString(L"🎮 WGC 硬件捕获"); m_cmbCaptureEngine.AddString(L"🖥️ PrintWindow");
    m_nCaptureEngineChoice = GetPrivateProfileInt(L"Settings", L"CaptureEngine", 0, m_iniPath); if (m_nCaptureEngineChoice < 0 || m_nCaptureEngineChoice > 2) m_nCaptureEngineChoice = 0;
    m_cmbCaptureEngine.SetCurSel(m_nCaptureEngineChoice);
    m_cmbTargetWindow.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(385, row1_Y, r.right - 100, row1_Y + 400), this, 1031); m_cmbTargetWindow.SetFont(&m_font);
    RefreshTargetList();
    m_chkCropTitle.Create(L"去标题栏", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(r.right - 95, row1_Y, r.right - 10, row1_Y + 25), this, 1032); m_chkCropTitle.SetFont(&m_font); m_chkCropTitle.SetCheck(BST_CHECKED);
    int row2_Y = row1_Y + 35; int halfW = (r.right - 30) / 2;
    m_cmbTeamSelect.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(10, row2_Y, 80, row2_Y + 200), this, 1024); m_cmbTeamSelect.SetFont(&m_font); m_cmbTeamSelect.AddString(L"[红队]"); m_cmbTeamSelect.AddString(L"[蓝队]"); m_cmbTeamSelect.SetCurSel(0);
    m_editQuickAdd.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL, CRect(85, row2_Y, halfW - 55, row2_Y + 30), this, 1025); m_editQuickAdd.SetFont(&m_font); m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT);
    m_btnQuickAdd.Create(L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(halfW - 50, row2_Y, 10 + halfW, row2_Y + 28), this, 1022); m_btnQuickAdd.SetFont(&m_font);
    int rightAreaW = (r.right - 10) - (20 + halfW); int trackerW = (rightAreaW - 10) / 2;
    m_cmbLeft.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(20 + halfW, row2_Y, 20 + halfW + trackerW, row2_Y + 300), this, 1010); m_cmbLeft.SetFont(&m_font); m_cmbLeft.AddString(L"[红] 左侧自动追踪"); m_cmbLeft.SetCurSel(0);
    m_cmbRight.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(20 + halfW + trackerW + 10, row2_Y, r.right - 10, row2_Y + 300), this, 1009); m_cmbRight.SetFont(&m_font); m_cmbRight.AddString(L"[蓝] 右侧自动追踪"); m_cmbRight.SetCurSel(0);
    int row3_Y = row2_Y + 35; int row2_Bottom = r.bottom - (int)(75 * WINDOW_SCALE); int treeHeight = (row2_Bottom - row3_Y) * 3 / 5;
    m_treePlayers.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS, CRect(10, row3_Y, 10 + halfW, row3_Y + treeHeight), this, 1023); m_treePlayers.SetFont(&m_font);
    m_listRecentPlayers.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS, CRect(10, row3_Y + treeHeight + 5, 10 + halfW, row2_Bottom), this, 1033);
    static CFont listFont; if (!listFont.m_hObject) listFont.CreatePointFont(110, L"微软雅黑"); m_listRecentPlayers.SetFont(&listFont);
    int scoreH = (int)(122 * WINDOW_SCALE);
    m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(20 + halfW, row3_Y, r.right - 10, row3_Y + scoreH), this, 1002); m_editOcrResult.SetFont(&m_font);
    m_editVisualLogs.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(20 + halfW, row3_Y + scoreH + 5, r.right - 10, row2_Bottom), this, 1011); m_editVisualLogs.SetFont(&m_font); m_editVisualLogs.SetBackgroundColor(FALSE, RGB(30, 30, 30)); m_editVisualLogs.LimitText(0);
    int btnY = row2_Bottom + 8; int btnH = (int)(28 * WINDOW_SCALE); int bW = (r.right - 40) / 3;
    m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, btnY, 10 + bW, btnY + btnH), this, ID_BTN_START); m_btnStart.SetFont(&m_font);
    m_btnApply.Create(L"应用修改", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(20 + bW, btnY, 20 + bW * 2, btnY + btnH), this, ID_BTN_APPLY); m_btnApply.SetFont(&m_font);
    m_btnReset.Create(L"战绩归零", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(30 + bW * 2, btnY, r.right - 10, btnY + btnH), this, ID_BTN_RESET); m_btnReset.SetFont(&m_font);
    int dirY = btnY + btnH + 5; int rightBtnW = 110;
    m_editOutDir.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, CRect(10, dirY, r.right - (rightBtnW * 2) - 30, dirY + btnH), this, ID_EDIT_DIR); m_editOutDir.SetFont(&m_font); m_editOutDir.SetWindowText(m_outputDir);
    m_btnBrowseDir.Create(L"更改目录", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(r.right - (rightBtnW * 2) - 20, dirY, r.right - rightBtnW - 20, dirY + btnH), this, ID_BTN_BROWSE); m_btnBrowseDir.SetFont(&m_font);
    m_btnInputKey.Create(L"输入授权码", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(r.right - rightBtnW - 10, dirY, r.right - 10, dirY + btnH), this, ID_BTN_INPUT_KEY); m_btnInputKey.SetFont(&m_font);

    // 加载配置
    LoadConfigFromFile();
    LoadAliasDB();
    SyncDataToTree();
    RefreshDisplay();
    WriteScoreToFile();

    CheckTrialAndLicense();
    OutputDebugAuthInfo();
    InitTrayIcon();

    ::RegisterHotKey(m_hWnd, 8008, MOD_CONTROL, VK_F8);
    ::RegisterHotKey(m_hWnd, 8009, MOD_CONTROL, VK_F9);

    SetTimer(5, 100, NULL);
    SetTimer(6, 1000, NULL);

    if (m_pWebDlg == nullptr) {
        m_pWebDlg = new CWebScoreDlg(nullptr);
        m_pWebDlg->Create(IDD_WEB_SCORE_DIALOG, GetDesktopWindow());
    }

    // 【终极解决隐藏】：先让 Web 窗口出来，主窗口直接深埋后台
    m_pWebDlg->ShowWindow(SW_SHOW);
    ShowWindow(SW_HIDE);

    // ==========================================
    // 🚨 恢复：开机自动在后台检查更新！
    // ==========================================
    std::thread([this]() {
        Sleep(2000); // 稍微延迟 2 秒，等软件 UI 完全加载完再去联网，防止开机卡顿
        CheckForUpdates(true); // true 代表静默检测模式
        }).detach();
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    ::UnregisterHotKey(m_hWnd, 8008);
    ::UnregisterHotKey(m_hWnd, 8009);

    if (m_hSingleInstanceMutex) CloseHandle(m_hSingleInstanceMutex);
    RemoveTrayIcon();

    if (m_bmp) ::DeleteObject(m_bmp);
    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) { if (m_historyBmps[i]) ::DeleteObject(m_historyBmps[i]); }
    if (m_hHttpConnect) WinHttpCloseHandle(m_hHttpConnect);
    if (m_hHttpSession) WinHttpCloseHandle(m_hHttpSession);

    GdiplusShutdown(m_gdiplusToken);

    // 3. 🚨【新增清理代码】：销毁新窗口并释放 COM
    if (m_pWebDlg) {
        m_pWebDlg->DestroyWindow();
        delete m_pWebDlg;
        m_pWebDlg = nullptr;
    }
    CoUninitialize();
}

// ============================================================================
// 托盘与退出
// ============================================================================
void CDNFGameCaptureDlg::InitTrayIcon() {
    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = GetSafeHwnd();
    m_nid.uID = 10001;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_MESSAGE;

    wchar_t p[MAX_PATH]; GetModuleFileName(NULL, p, MAX_PATH);
    m_nid.hIcon = ExtractIcon(AfxGetInstanceHandle(), p, 0);
    if (!m_nid.hIcon) m_nid.hIcon = AfxGetApp()->LoadStandardIcon(IDI_APPLICATION);
    wcscpy_s(m_nid.szTip, L"DNF击杀统计");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}
void CDNFGameCaptureDlg::RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &m_nid); }

void CDNFGameCaptureDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if ((nID & 0xFFF0) == SC_CLOSE) {
        ShowWindow(SW_HIDE);
        BroadcastStateToWeb(); // 👈 新增
        return;
    }
    if ((nID & 0xFFF0) == SC_MINIMIZE) {
        ShowWindow(SW_HIDE);
        BroadcastStateToWeb(); // 👈 新增
        return;
    }
    CWnd::OnSysCommand(nID, lParam);
}

void CDNFGameCaptureDlg::OnClose() {
    ShowWindow(SW_HIDE);
    BroadcastStateToWeb(); // 👈 新增
}

// ============================================================================
// 【修复清单】
//
//   1. RunOCR_Internal     — 修复 hScreenDC 泄漏 (死代码删除 + 资源统一释放)
//   2. DoRetryMatchingTask — 按需克隆历史帧，峰值内存从 160MB 降到 ~16MB
//   3. CheckColorTrigger   — 复用 DC，避免每 50ms 创建/销毁
//   4. Capture (PrintWindow 段) — 检测分辨率变化时重建 m_bmp
//   5. OnTimer             — Timer 6 预览间隔从 50ms 改为 200ms
//   6. OnBnClickedQuickAdd — 删除重复的树展开循环
//
// ============================================================================
// 【函数 1】RunOCR_Internal — 修复 hScreenDC 内存泄漏
//
// 原始问题：
//   - hScreenDC = ::GetDC(NULL) 之后，从未调用 ::ReleaseDC(NULL, hScreenDC)
//   - 函数末尾 return ret; 之后的释放代码是死代码，永远执行不到
//   - 每次 OCR 调用都泄漏一个屏幕 DC，长时间运行后 GDI 资源耗尽导致系统卡顿
// ============================================================================
OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex)
{
    OcrResultData result = { L"", NULL };

    if (!m_hHttpConnect)
        return result;

    // 关键修复：RunOCR_Internal 会被左右框并行调用。
    // Windows GDI 的同一个 HBITMAP 不能同时被选入多个 DC；如果左右 OCR 线程同时 SelectObject 同一张快照，
    // 其中一边的 StretchBlt 可能失败，最终只剩白底 padding，看起来就是“纯白图”。
    // 所以每次 OCR 先克隆一份私有位图，后续只操作这份本地副本。
    HBITMAP hLocalTargetBmp = (HBITMAP)::CopyImage(hTargetBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!hLocalTargetBmp)
        hLocalTargetBmp = hTargetBmp;

    // ---- 1. 计算截取区域 ----
    RECT cropRect;
    if (nAreaIndex == 0) {
        cropRect = {
            (long)(m_w * 0.190f), (long)(m_h * 0.004f),
            (long)(m_w * 0.360f), (long)(m_h * 0.040f)
        };
    }
    else {
        cropRect = {
            (long)(m_w * 0.655f), (long)(m_h * 0.004f),
            (long)(m_w * 0.815f), (long)(m_h * 0.040f)
        };
    }

    int srcW = cropRect.right - cropRect.left;
    int srcH = cropRect.bottom - cropRect.top;
    int scale = 2;
    int padding = 30;
    int dstW = srcW * scale + padding * 2;
    int dstH = srcH * scale + padding * 2;

    // ---- 2. 创建 GDI 资源（统一管理，统一释放） ----
    HDC hScreenDC = ::GetDC(NULL);
    HDC hSrcDC = ::CreateCompatibleDC(NULL);
    HDC hDstDC = ::CreateCompatibleDC(NULL);
    HBITMAP hWorkBmp = ::CreateCompatibleBitmap(hScreenDC, dstW, dstH);

    HGDIOBJ oldSrc = ::SelectObject(hSrcDC, hLocalTargetBmp);
    HGDIOBJ oldDst = ::SelectObject(hDstDC, hWorkBmp);

    // ---- 3. 白底填充 + 缩放拷贝 ----
    RECT bgRect = { 0, 0, dstW, dstH };
    HBRUSH whiteBrush = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(hDstDC, &bgRect, whiteBrush);
    ::DeleteObject(whiteBrush);

    ::SetStretchBltMode(hDstDC, HALFTONE);
    BOOL bltOk = ::StretchBlt(hDstDC, padding, padding, srcW * scale, srcH * scale,
        hSrcDC, cropRect.left, cropRect.top, srcW, srcH, SRCCOPY);

    // 如果 StretchBlt 偶发失败，至少留下明显提示色，避免误以为 OCR 内容是白图。
    // 正常情况下这里不会触发；真正的修复是上面的私有 HBITMAP 克隆。
    if (!bltOk) {
        RECT failRect = { padding, padding, padding + srcW * scale, padding + srcH * scale };
        HBRUSH failBrush = ::CreateSolidBrush(RGB(255, 220, 220));
        ::FillRect(hDstDC, &failRect, failBrush);
        ::DeleteObject(failBrush);
    }

    // ---- 4. OCR 原图直传：不做二值化/颜色过滤 ----
    // 说明：这里保留上面的“裁剪 + 2倍缩放 + 白边 padding”，
    // 但不再把图像转成黑白，也不再做颜色感知处理。
    // 目的：观察 Umi-OCR/PaddleOCR 对原始 HUD 字体的识别效果，
    // 避免二值化造成右侧文字虚线、断裂或整块变黑。

    // ---- 5. 保存预览用的副本 ----
    result.hBmp = (HBITMAP)::CopyImage(hWorkBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    // ---- 6. 编码为 PNG Base64 ----
    IStream* pStream = NULL;
    ::CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    {
        Gdiplus::Bitmap gBmp(hWorkBmp, NULL);
        CLSID pngClsid;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
        gBmp.Save(pStream, &pngClsid, NULL);
    }

    HGLOBAL hGlobal = NULL;
    ::GetHGlobalFromStream(pStream, &hGlobal);
    LPVOID pData = ::GlobalLock(hGlobal);
    SIZE_T dataSize = ::GlobalSize(hGlobal);

    DWORD base64Len = 0;
    ::CryptBinaryToStringA((const BYTE*)pData, (DWORD)dataSize,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);

    std::string base64Str(base64Len, '\0');
    ::CryptBinaryToStringA((const BYTE*)pData, (DWORD)dataSize,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &base64Str[0], &base64Len);

    ::GlobalUnlock(hGlobal);
    pStream->Release();

    // ---- 7. 【关键修复】：统一释放所有 GDI 资源 ----
    ::SelectObject(hSrcDC, oldSrc);
    ::SelectObject(hDstDC, oldDst);
    ::DeleteObject(hWorkBmp);
    ::DeleteDC(hSrcDC);
    ::DeleteDC(hDstDC);
    if (hLocalTargetBmp && hLocalTargetBmp != hTargetBmp)
        ::DeleteObject(hLocalTargetBmp);
    ::ReleaseDC(NULL, hScreenDC);  // ★ 原代码遗漏，导致 DC 泄漏

    // ---- 8. 去掉 Base64 尾部的空字符 ----
    if (!base64Str.empty() && base64Str.back() == '\0')
        base64Str.pop_back();

    // ---- 9. 构造 JSON 并发送 HTTP 请求 ----
    std::string jsonBody = "{\"base64\": \"" + base64Str + "\"}";
    CString ocrText = L"";

    HINTERNET hRequest = WinHttpOpenRequest(
        m_hHttpConnect, L"POST", L"/api/ocr",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        BOOL bSent = WinHttpSendRequest(
            hRequest, NULL, 0,
            (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.length(),
            (DWORD)jsonBody.length(), 0);

        if (bSent && WinHttpReceiveResponse(hRequest, NULL)) {
            // 读取响应体
            std::string responseStr;
            DWORD available = 0, downloaded = 0;
            while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
                std::vector<char> buf(available + 1, 0);
                if (WinHttpReadData(hRequest, buf.data(), available, &downloaded))
                    responseStr.append(buf.data(), downloaded);
            }

            // 解析所有 "text" 字段
            size_t searchPos = 0;
            while ((searchPos = responseStr.find("\"text\"", searchPos)) != std::string::npos) {
                size_t colonPos = responseStr.find(":", searchPos);
                size_t quoteOpen = responseStr.find("\"", colonPos);
                size_t quoteEnd = responseStr.find("\"", quoteOpen + 1);
                if (quoteEnd > quoteOpen) {
                    std::string textUtf8 = responseStr.substr(quoteOpen + 1, quoteEnd - quoteOpen - 1);
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, NULL, 0);
                    if (wideLen > 0) {
                        std::vector<wchar_t> wideBuf(wideLen);
                        MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, wideBuf.data(), wideLen);
                        ocrText += wideBuf.data();
                    }
                }
                searchPos = quoteEnd + 1;
            }
        }
        else {
            EnsureOcrRunning();
        }
        WinHttpCloseHandle(hRequest);
    }

    // ---- 10. 清洗 OCR 结果中的转义字符 ----
    ocrText.Replace(L"\\n", L"");
    ocrText.Replace(L"\\r", L"");

    // 处理 \uXXXX Unicode 转义
    int uPos = 0;
    while ((uPos = ocrText.Find(L"\\u", uPos)) != -1) {
        if (uPos + 5 < ocrText.GetLength()) {
            CString hexStr = ocrText.Mid(uPos + 2, 4);
            wchar_t wc = (wchar_t)wcstol(hexStr.GetString(), NULL, 16);
            ocrText.Delete(uPos, 6);
            ocrText.Insert(uPos, CString(wc));
            uPos += 1;
        }
        else {
            uPos += 2;
        }
    }

    ocrText.Replace(L"\\\"", L"");
    ocrText.Trim();

    result.text = ocrText;

    // ============================================================
    // 【身份融合补丁】把每次固定红框 OCR 结果喂给时间窗缓存。
    // 注意：这里不直接判定玩家，只记录“大区/ID帧/职业帧”证据。
    // nAreaIndex: 0=左框(ID+大区)，1=右框(大区+ID)。
    // ============================================================
    if (!ocrText.IsEmpty() && ocrText.Find(L"No text") == -1) {
        UpdateIdentityPanelCache(nAreaIndex, ocrText);
    }

    return result;
}


// ============================================================================
// 【函数 2】DoRetryMatchingTask — 按需克隆历史帧
//
// 原始问题：
//   - 一次性克隆全部 20 帧历史截图，1080P 下瞬时占用 160MB
//   - 每帧都单独 GetDC / CreateCompatibleDC / DeleteDC，GDI 调用爆炸
//
// 优化方案：
//   - 只在需要 OCR 时才克隆当前帧，用完立即释放
//   - 复用一对 DC，全程只创建/销毁一次
// ============================================================================
void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide)
{
    int killerArea = (triggerSide == 0) ? 1 : 0;
    int deadArea = triggerSide;
    bool killerIsLeft = (killerArea == 0);

    bool killerResolved = false, deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";
    int killerBestP = -1, killerBestA = -1;
    int deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    // 全局最优记录（用于二轮兜底）
    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999;
    CString globalKillerName;
    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999;
    CString globalDeadName;

    struct FrameData { CString text; int frameIdx; };
    std::vector<FrameData> historyKTexts;
    std::vector<FrameData> historyDTexts;

    // 日志输出辅助
    auto PushVisualLog = [&](const CString& msg, COLORREF color) {
        // 文件日志保留完整细节；软件内可视日志对“身份融合”做降噪，只显示更关键的结果级信息。
        WriteMatchLog(msg);

        // 软件内完全不显示身份融合相关日志；详细内容仍由上面的 WriteMatchLog(msg) 写入文件。
        if (msg.Find(L"身份融合") >= 0) {
            return;
        }

        time_t now_t = time(0);
        tm t;
        localtime_s(&t, &now_t);
        CString tStr;
        tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.push_back({ tStr, color });
        };


    // ========================================================
    // 【关键优化】：不再一次性克隆全部历史帧
    //   改为：记录有效帧的索引列表，需要时再单帧克隆
    // ========================================================
    struct HistorySlot {
        int ringIdx;  // 在 m_historyBmps 环形缓冲中的实际下标
    };
    std::vector<HistorySlot> validSlots;
    int snapshotW = 0, snapshotH = 0;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        snapshotW = m_w;
        snapshotH = m_h;
        for (int i = 1; i <= MAX_HISTORY_FRAMES; i++) {
            int idx = (m_historyIdx - i + MAX_HISTORY_FRAMES) % MAX_HISTORY_FRAMES;
            if (m_historyBmps[idx]) {
                validSlots.push_back({ idx });
            }
        }
    }

    if (validSlots.size() < 3) {
        PushVisualLog(L"⏳ [历史帧不足] 等待更多画面...", RGB(255, 165, 0));
        // 强制重置标志，以便下次继续尝试
        // 可以在这里主动将 m_bCanTrigger 提前恢复，或触发一次定时器重置
        return;
    }

    // 帧克隆辅助函数：从环形缓冲安全拷贝一帧出来
    auto CloneHistoryFrame = [&](int ringIdx) -> HBITMAP {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (!m_historyBmps[ringIdx])
            return nullptr;

        HDC hScreenDC = ::GetDC(NULL);
        HDC hSrcDC = ::CreateCompatibleDC(hScreenDC);
        HDC hDstDC = ::CreateCompatibleDC(hScreenDC);

        HBITMAP hClone = ::CreateCompatibleBitmap(hScreenDC, snapshotW, snapshotH);
        HGDIOBJ oldSrc = ::SelectObject(hSrcDC, m_historyBmps[ringIdx]);
        HGDIOBJ oldDst = ::SelectObject(hDstDC, hClone);

        ::BitBlt(hDstDC, 0, 0, snapshotW, snapshotH, hSrcDC, 0, 0, SRCCOPY);

        ::SelectObject(hSrcDC, oldSrc);
        ::SelectObject(hDstDC, oldDst);
        ::DeleteDC(hSrcDC);
        ::DeleteDC(hDstDC);
        ::ReleaseDC(NULL, hScreenDC);

        return hClone;
        };

    // 清空 OCR 下拉框历史
    {
        std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
        for (auto& r : m_ocrRecordsLeft)  if (r.hBmp) DeleteObject(r.hBmp);
        for (auto& r : m_ocrRecordsRight) if (r.hBmp) DeleteObject(r.hBmp);
        m_ocrRecordsLeft.clear();
        m_ocrRecordsRight.clear();
        m_viewIndexLeft = -1;
        m_viewIndexRight = -1;
    }
    PostMessage(WM_UPDATE_OCR_DROPDOWNS, 1, 0);

    auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName,
        bool isKiller, int& outBestP, int& outBestA,
        int& frameScore, bool isAggressive, int frameIdx) -> bool
        {
            frameScore = -2;
            if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1)
                return false;

            CString logMsg;
            logMsg.Format(L"▶ [%s] 第%d帧提取: \"%s\"",
                isKiller ? L"找杀手" : L"找死者", frameIdx, (LPCTSTR)ocrResult);
            PushVisualLog(logMsg, RGB(180, 180, 180));

            TDnfSimpleAliasMeta ocrMeta = DnfParseAliasMeta(ocrResult);

            // ====================================================
            // 精确小号命中检测（仅当唯一时才采纳）
            // 现在会先解析“大区/真实ID/#职业”，因此：
            //   上海1夏雫 == 夏雫上海1
            //   夏雫#气功师 也能按真实ID命中。
            // 主号仍然不参与名称匹配。
            // ====================================================
            int exactMatchCount = 0;
            int exactMatchP = -1, exactMatchA = -1;
            {
                m_dataMutex.lock();
                for (int p = 0; p < 8; p++) {
                    if (m_players[p].name.IsEmpty()) continue;
                    for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                        TDnfSimpleAliasMeta aliasMeta = DnfParseAliasMeta(m_players[p].aliases[a].name);
                        bool exactFull = (DnfNormalizeLooseText(m_players[p].aliases[a].name) == DnfNormalizeLooseText(ocrResult));
                        bool exactMeta = DnfAliasMetaExactSame(aliasMeta, ocrMeta);
                        if (exactFull || exactMeta) {
                            exactMatchCount++;
                            exactMatchP = p;
                            exactMatchA = (int)a;
                            break;
                        }
                    }
                    if (exactMatchCount > 1) break;
                }
                m_dataMutex.unlock();
            }

            if (exactMatchCount == 1) {
                // 唯一别名，直接锁定！安全且精确
                resolved = true;
                finalName = m_players[exactMatchP].name; // 🚨 【换成这行】：底层查重统一使用主号！
                outBestP = exactMatchP;
                outBestA = exactMatchA;

                CString successLog;
                successLog.Format(L"  └ [🎯别名命中] 职业/别名 [%s] 唯一指向: %s",
                    (LPCTSTR)ocrResult, (LPCTSTR)finalName);
                PushVisualLog(successLog, RGB(0, 255, 200));

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[exactMatchP].team;
                else          lockedDeadTeam = m_players[exactMatchP].team;
                m_dataMutex.unlock();
                return false;
            }
            else if (exactMatchCount > 1) {
                PushVisualLog(L"  └ [⚠️别名冲突] 多个玩家拥有相同别名，等待ID帧...", RGB(255, 165, 0));
                return false; // 本帧放弃
            }

            // ====================================================
            // 没有唯一别名命中，进入原有匹配逻辑
            // ====================================================
            int maxScore = -2, bestP = -1, bestA = -1, bestRealLen = 0;
            bool bestHasMetaContext = false;
            std::wstring bestName;

            m_dataMutex.lock();
            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                int teamPenalty = 0;
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam)
                    teamPenalty = 20;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam)
                    teamPenalty = 20;

                // 主号只作为归属 owner，不再参与 OCR 名称匹配。
                // 这样主号可以是备注名/真实主号，也允许与自己的小号重复；真正用于命中的只有小号列表。
                int curScore = -2;
                std::wstring curBestName;
                int curBestAlias = -1;
                int curRealLen = 0;
                bool curBestMetaContext = false;

                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    if (m_players[p].aliases[a].name.IsEmpty()) continue;

                    TDnfSimpleAliasMeta aliasMeta = DnfParseAliasMeta(m_players[p].aliases[a].name);
                    CString candId = aliasMeta.realId.IsEmpty() ? m_players[p].aliases[a].name : aliasMeta.realId;
                    CString ocrId = ocrMeta.realId.IsEmpty() ? ocrResult : ocrMeta.realId;

                    int aliasScore = m_matcher.GetMatchScore(candId.GetString(), ocrId.GetString(), isAggressive);
                    if (aliasScore == -1) {
                        // 如果 OCR 是纯职业帧，保持旧逻辑：本帧职业干扰，跳过。
                        maxScore = -1;
                        break;
                    }

                    bool strongMetaContext = false;
                    int metaScore = DnfMetaContextScore(aliasMeta, ocrMeta, candId.GetLength(), strongMetaContext);
                    aliasScore += metaScore;

                    // 如果 OCR 文本里包含大区，而候选小号也声明了同一大区，
                    // 大区在前/在后都等价加分，专门保护两字 ID 被 OCR 错一字的场景。
                    if (aliasScore > 160) aliasScore = 160;
                    aliasScore -= teamPenalty;

                    if (aliasScore > curScore) {
                        curScore = aliasScore;
                        curBestName = m_players[p].aliases[a].name.GetString();
                        curBestAlias = (int)a;
                        curRealLen = candId.GetLength();
                        curBestMetaContext = strongMetaContext;
                    }
                }
                if (curBestAlias == -1) continue;
                if (maxScore == -1) break;

                if (curScore > maxScore || (curScore == maxScore && maxScore > 0 && curRealLen > bestRealLen)) {
                    maxScore = curScore;
                    bestP = p;
                    bestA = curBestAlias;
                    bestName = curBestName;
                    bestRealLen = curRealLen;
                    bestHasMetaContext = curBestMetaContext;
                }
            }
            m_dataMutex.unlock();

            frameScore = maxScore;

            if (maxScore == -1) {
                PushVisualLog(L"  └ [⚠️职业干扰] 跳过本帧...", RGB(120, 120, 120));
                return true;
            }

            int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);

            // 更新全局最优记录
            if (bestP != -1) {
                if (isKiller && maxScore > globalKillerBestScore) {
                    globalKillerBestScore = maxScore;
                    globalKillerBestP = bestP;
                    globalKillerBestA = bestA;
                    globalKillerPassLine = passLine;
                    globalKillerName = bestName.c_str();
                }
                else if (!isKiller && maxScore > globalDeadBestScore) {
                    globalDeadBestScore = maxScore;
                    globalDeadBestP = bestP;
                    globalDeadBestA = bestA;
                    globalDeadPassLine = passLine;
                    globalDeadName = bestName.c_str();
                }
            }

            if (bestP != -1 && maxScore >= passLine) {
                // ============================================================
                // 【身份融合补丁】两字 ID 保护：
                // 旧算法对两字名的“包含命中”很容易过强，这里不再让两字名
                // 单帧直接锁定，而是交给固定红框时间窗融合做最终确认。
                // 精确唯一别名命中仍在上方提前通过。
                // ============================================================
                if (bestRealLen <= 2 && !bestHasMetaContext) {
                    CString guardLog;
                    guardLog.Format(L"  └ [🛡短名保护] 旧算法命中候选[%s] %d分/线%d，但长度=%d；缺少大区/#职业上下文，暂不单帧锁定",
                        bestName.c_str(), maxScore, passLine, bestRealLen);
                    PushVisualLog(guardLog, RGB(255, 210, 80));
                    return false;
                }

                resolved = true;
                finalName = m_players[bestP].name; // 🚨 这里才是 bestP！
                outBestP = bestP;
                outBestA = bestA;

                CString successLog;
                if (isAggressive)
                    successLog.Format(L"  └ [✨二轮匹配] 强行锁定: %s (%d分)", (LPCTSTR)finalName, maxScore);
                else
                    successLog.Format(L"  └ [✔首轮匹配] 成功指向: %s (%d分)", (LPCTSTR)finalName, maxScore);

                COLORREF teamColor = (m_players[bestP].team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
                PushVisualLog(successLog, teamColor);

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[bestP].team;
                else          lockedDeadTeam = m_players[bestP].team;
                m_dataMutex.unlock();
            }
            else {
                CString failLog;
                failLog.Format(L"  └ [✖未达标] 最高 %d 分 (及格线: %d)", maxScore, passLine);
                if (!isAggressive)
                    PushVisualLog(failLog, RGB(120, 120, 120));
            }

            return false;
        };

    // ============================================================
    // 【身份融合补丁】固定红框时间窗融合兜底
    // 说明：首轮逐帧 OCR 会不断调用 UpdateIdentityPanelCache() 写入缓存。
    // 首轮结束后，先用缓存做一次融合评分，再决定是否进入二轮降级。
    // ============================================================
    auto tryFusionMatch = [&](bool isKiller, int areaIndex, bool& resolved, CString& finalName,
        int& outBestP, int& outBestA, int& lockedTeam) -> bool
        {
            TDnfPanelSide side = (areaIndex == 0) ? TDnfPanelSide::LeftNameArea : TDnfPanelSide::RightAreaName;
            TDnfPanelMatchResult fusion = MatchIdentityPanel(side);

            CString tag = isKiller ? L"杀手" : L"死者";
            CString sideText = (side == TDnfPanelSide::LeftNameArea) ? L"左框" : L"右框";

            if (!fusion.ok) {
                CString fLog;
                fLog.Format(L"▶ [身份融合-%s] %s 未通过：best=%s final=%d gap=%d cacheInsufficient=%s",
                    (LPCTSTR)tag, (LPCTSTR)sideText,
                    fusion.best.candidate.name.IsEmpty() ? L"无" : fusion.best.candidate.name.GetString(),
                    fusion.best.finalScore, fusion.best.gapToSecond,
                    fusion.cacheInsufficient ? L"是" : L"否");
                PushVisualLog(fLog, RGB(140, 140, 140));
                return false;
            }

            int pIdx = -1;
            int aIdx = -1;
            int team = -1;
            CString owner = fusion.best.candidate.ownerName;
            CString hitName = fusion.best.candidate.name;

            {
                std::lock_guard<std::mutex> dataLock(m_dataMutex);
                for (int p = 0; p < 8; ++p) {
                    if (m_players[p].name == owner) {
                        pIdx = p;
                        team = m_players[p].team;
                        if (fusion.best.candidate.isAlias) {
                            for (size_t a = 0; a < m_players[p].aliases.size(); ++a) {
                                if (m_players[p].aliases[a].name == hitName) {
                                    aIdx = (int)a;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
            }

            if (pIdx < 0) {
                CString fLog;
                fLog.Format(L"▶ [身份融合-%s] %s 通过但无法回填到场上玩家：owner=%s hit=%s",
                    (LPCTSTR)tag, (LPCTSTR)sideText, (LPCTSTR)owner, (LPCTSTR)hitName);
                PushVisualLog(fLog, RGB(255, 120, 80));
                return false;
            }

            // 与另一侧已锁定队伍冲突时，拒绝本次融合结果。
            if (isKiller && lockedDeadTeam != -1 && team == lockedDeadTeam) {
                PushVisualLog(L"  └ [身份融合拒绝] 杀手候选与已锁死者同队，按队伍约束丢弃", RGB(255, 120, 80));
                return false;
            }
            if (!isKiller && lockedKillerTeam != -1 && team == lockedKillerTeam) {
                PushVisualLog(L"  └ [身份融合拒绝] 死者候选与已锁杀手同队，按队伍约束丢弃", RGB(255, 120, 80));
                return false;
            }

            if (!resolved) {
                resolved = true;
                finalName = owner;       // 底层战绩仍按主号归集
                outBestP = pIdx;
                outBestA = aIdx;
                lockedTeam = team;

                CString okLog;
                okLog.Format(L"  └ [🧩身份融合命中-%s] %s => 主号[%s] 命中[%s] final=%d id=%d gap=%d areaCtx=%+d jobCtx=%+d",
                    (LPCTSTR)tag, (LPCTSTR)sideText, (LPCTSTR)owner, (LPCTSTR)hitName,
                    fusion.best.finalScore, fusion.best.idScore, fusion.best.gapToSecond,
                    fusion.best.areaCtxScore, fusion.best.jobCtxScore);
                COLORREF teamColor = (team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
                PushVisualLog(okLog, teamColor);
            }
            else {
                CString learnLog;
                learnLog.Format(L"  └ [身份融合学习-%s] %s 已由旧算法锁定，本次融合仅用于学习缓存：%s final=%d",
                    (LPCTSTR)tag, (LPCTSTR)sideText, (LPCTSTR)hitName, fusion.best.finalScore);
                PushVisualLog(learnLog, RGB(120, 220, 255));
            }

            return true;
        };

    // ========================================================
    // 【核心优化】：逐帧克隆 → OCR → 匹配 → 释放
    // ========================================================
    for (size_t i = 0; i < validSlots.size(); i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved))
            break;

        // 按需克隆单帧
        HBITMAP hSnapshot = CloneHistoryFrame(validSlots[i].ringIdx);
        if (!hSnapshot)
            continue;

        // 并行 OCR
        std::future<OcrResultData> futKiller, futDead;
        if (!killerResolved)
            futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        if (!deadResolved)
            futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);

        OcrResultData resK = { L"", NULL };
        OcrResultData resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get();
        if (futDead.valid())   resD = futDead.get();

        // ★ 用完立即释放克隆帧，不累积内存
        ::DeleteObject(hSnapshot);

        // 记录有效文本用于二轮匹配
        if (!killerResolved && !resK.text.IsEmpty() && resK.text.Find(L"No text") == -1)
            historyKTexts.push_back({ resK.text, (int)(i + 1) });
        if (!deadResolved && !resD.text.IsEmpty() && resD.text.Find(L"No text") == -1)
            historyDTexts.push_back({ resD.text, (int)(i + 1) });

        // 首轮匹配
        int kScore = -2, dScore = -2;
        processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, false, (int)(i + 1));
        processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, false, (int)(i + 1));

        // 更新 OCR 下拉框
        OcrResultData& resL = killerIsLeft ? resK : resD;
        OcrResultData& resR = killerIsLeft ? resD : resK;
        {
            std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
            if (resL.hBmp) {
                CString lbl;
                lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resL.text);
                m_ocrRecordsLeft.push_back({ resL.hBmp, lbl });
            }
            if (resR.hBmp) {
                CString lbl;
                lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resR.text);
                m_ocrRecordsRight.push_back({ resR.hBmp, lbl });
            }
        }
        PostMessage(WM_UPDATE_OCR_DROPDOWNS, 0, 0);

        // 更新调试信息
        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult.Format(L"时光倒流 %d/%d | 杀:%s 亡:%s",
                (int)(i + 1), (int)validSlots.size(),
                killerResolved ? finalKillerName : L"未定",
                deadResolved ? finalDeadName : L"未定");
        }
        ::InvalidateRect(m_hWnd, &m_previewRect, FALSE);
    }

    // ============================================================
    // 【身份融合补丁】首轮 OCR 扫描结束后，先做一次固定红框融合。
    // 这一步特别保护两字 ID：旧算法短名命中不会直接锁，先到这里确认。
    // ============================================================
    PushVisualLog(L"▶ [身份融合] 首轮扫描结束，开始固定红框时间窗评分...", RGB(120, 220, 255));
    tryFusionMatch(true, killerArea, killerResolved, finalKillerName, killerBestP, killerBestA, lockedKillerTeam);
    tryFusionMatch(false, deadArea, deadResolved, finalDeadName, deadBestP, deadBestA, lockedDeadTeam);

    // ---- 二轮降级匹配（杀手） ----
    if (!killerResolved && !historyKTexts.empty()) {
        PushVisualLog(L"▶ [找杀手] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyKTexts) {
            int kScore = -2;
            processMatch(frame.text, killerResolved, finalKillerName, true,
                killerBestP, killerBestA, kScore, true, frame.frameIdx);
            if (killerResolved) break;
        }
    }

    // ---- 二轮降级匹配（死者） ----
    if (!deadResolved && !historyDTexts.empty()) {
        PushVisualLog(L"▶ [找死者] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyDTexts) {
            int dScore = -2;
            processMatch(frame.text, deadResolved, finalDeadName, false,
                deadBestP, deadBestA, dScore, true, frame.frameIdx);
            if (deadResolved) break;
        }
    }

    // 二轮后再尝试一次融合；如果二轮遇到职业帧/短名保护未锁定，这里仍可用缓存确认。
    if (!killerResolved || !deadResolved) {
        PushVisualLog(L"▶ [身份融合] 二轮结束后再次尝试融合确认...", RGB(120, 220, 255));
        tryFusionMatch(true, killerArea, killerResolved, finalKillerName, killerBestP, killerBestA, lockedKillerTeam);
        tryFusionMatch(false, deadArea, deadResolved, finalDeadName, deadBestP, deadBestA, lockedDeadTeam);
    }

    //// ---- 全局兜底 ----
    //if (!killerResolved && globalKillerBestP != -1
    //    && globalKillerBestScore >= (globalKillerPassLine - 20)
    //    && globalKillerBestScore >= 40)
    //{
    //    killerResolved = true;
    //    killerBestP = globalKillerBestP;
    //    killerBestA = globalKillerBestA;
    //    finalKillerName = globalKillerName;
    //}
    //if (!deadResolved && globalDeadBestP != -1
    //    && globalDeadBestScore >= (globalDeadPassLine - 20)
    //    && globalDeadBestScore >= 40)
    //{
    //    deadResolved = true;
    //    deadBestP = globalDeadBestP;
    //    deadBestA = globalDeadBestA;
    //    finalDeadName = globalDeadName;
    //}

    if (!killerResolved || !deadResolved) {
        // 匹配失败提示音：只响 2 下，避免实战时太吵。
        // 新开线程发声，不阻塞匹配逻辑和游戏画面。
        std::thread([]() {
            ::Beep(900, 150); // 高音 滴
            ::Beep(600, 150); // 低音 嘟
            }).detach();

        if (!killerResolved) {
            PushVisualLog(L"❌ [彻底失败] 无法识别【杀手】！请检查是否漏绑小号，或右键手动加分！", RGB(255, 80, 80));
        }
        if (!deadResolved) {
            PushVisualLog(L"❌ [彻底失败] 无法识别【死者】！请检查是否漏绑小号，或右键手动加分！", RGB(255, 80, 80));
        }
    }

    // ---- 战绩更新（与原版逻辑完全一致） ----
    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();

        bool isDup = false;
        CString conflictName = L"";
        CString conflictReason = L"";

        // ==========================================
        // ⬇️ 【修改点】：双重精准冷却法则 (60秒)
        // ==========================================
        for (const auto& ev : m_recentEvents) {
            if (now - ev.time < DUP_KILL_LIMIT_TIME) {

                // 规则 1：同一个 ID，短时间内绝对不能死两次！
                if (deadResolved && finalDeadName != L"待定") {
                    if (ev.dead == finalDeadName) {
                        isDup = true;
                        conflictName = finalDeadName;
                        conflictReason = L"极短时间内重复死亡";
                        break;
                    }
                }

                // 规则 2：同一个人，短时间内不能击杀同一个人两次！
                if (killerResolved && deadResolved && finalKillerName != L"待定" && finalDeadName != L"待定") {
                    if (ev.killer == finalKillerName && ev.dead == finalDeadName) {
                        isDup = true;
                        conflictName = finalKillerName + L" 击杀 " + finalDeadName;
                        conflictReason = L"极短时间内重复击杀同一人";
                        break;
                    }
                }

                // 🚨 【新增规则 3：专门绞杀结算画面幽灵击杀】
                // 如果在防抖时间内，只认出了杀手，但死者没认出来（被结算UI挡住）
                // 且这个杀手刚刚才拿过人头，这 100% 是大 X 闪烁重现！无情拦截！
                if (killerResolved && !deadResolved && finalKillerName != L"待定") {
                    if (ev.killer == finalKillerName) {
                        isDup = true;
                        conflictName = finalKillerName + L" (死者被遮挡)";
                        conflictReason = L"结算画面干扰，判定为大X重现";
                        break;
                    }
                }
            }
        }

        if (!isDup) {
            m_recentEvents.push_back({ finalKillerName, finalDeadName, now });
            m_recentEvents.erase(
                std::remove_if(m_recentEvents.begin(), m_recentEvents.end(),
                    [&](const RecentEvent& ev) { return now - ev.time > DUP_KILL_CLEAN_TIME; }), // 🚨 改用宏
                m_recentEvents.end());

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++)
                    if (p != killerBestP)
                        m_players[p].currentStreak = 0;

                m_players[killerBestP].kills++;
                m_players[killerBestP].currentStreak++;

                CString displayName = m_players[killerBestP].name;
                if (killerBestA != -1 && (size_t)killerBestA < m_players[killerBestP].aliases.size())
                    displayName = m_players[killerBestP].aliases[killerBestA].name;

                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                CString actionLog;
                actionLog.Format(L"⚔ [击杀成功] 玩家 [%s] 拿下一击！连杀: %d",
                    (LPCTSTR)displayName, m_players[killerBestP].currentStreak);
                PushVisualLog(actionLog, teamColor);

                if (m_players[killerBestP].currentStreak == 4) {
                    m_players[killerBestP].akCount++;
                    m_players[killerBestP].currentStreak = 0;
                    PushVisualLog(L"🌟 [AK宣告] 恐怖如斯！玩家 [" + displayName + L"] 完成一次 AK！",
                        RGB(255, 215, 0));
                }
                m_lastKillerTeam = m_players[killerBestP].team;
            }

            if (deadResolved && deadBestP != -1) {
                m_players[deadBestP].deaths++;
                // 【身份融合补丁】击杀成立后，只切死者侧身份段；杀手侧继续留场。
                NotifyIdentityKillConfirmed(m_players[deadBestP].team, finalDeadName);
            }

            if (m_bPendingTeamScoreWin) {
                m_bPendingTeamScoreWin = false;
                if (m_lastKillerTeam == 0)      m_totalScoreRed++;
                else if (m_lastKillerTeam == 1)  m_totalScoreBlue++;
                for (int p = 0; p < 8; p++)
                    m_players[p].currentStreak = 0;

                // 大比分真正落账时也强制启动 ROUND_END 冷却。
                // 原来只在“全队 X 视觉检测”阶段启动；如果比分是在击杀结算线程里落账，
                // 或视觉全队 X 未稳定触发，就可能只走普通击杀冷却，导致 COOLDOWN_ROUND_END 没生效。
                KillTimer(2);
                m_bCanTrigger = FALSE;
                SetTimer(2, COOLDOWN_ROUND_END, NULL);
                m_bCanTriggerTeamScore = FALSE;
                KillTimer(4);
                SetTimer(4, COOLDOWN_TEAM_SCORE, NULL);

                PushVisualLog(L"🏆 [结算] 局间大比分变动！所有人连击清零！ROUND_END 冷却已启动。", RGB(0, 255, 100));
                // 【身份融合补丁】新一局开始，两边固定框都可能换人，清空身份段和运行时学习。
                NotifyIdentityRoundReset(L"局间大比分变动/新一局开始");
            }

            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
        else {
            CString logMsg;
            logMsg.Format(L"⏳ [冷却拦截] 玩家 [%s] 在 %d 秒内已产生过战绩，本次忽略！", (LPCTSTR)conflictName, DUP_KILL_LIMIT_TIME / 1000);
            PushVisualLog(logMsg, RGB(255, 165, 0));
        }
    }

    // ★ 不再需要手动释放 historyClones，因为帧已在循环内逐个释放
}

// ==========================================
// 🚨 WGC 线程安全收尸器：防止 0xDDDDDDDD 越界崩溃
// ==========================================
void CDNFGameCaptureDlg::SafeDeleteWGC() {
    if (m_pWGC) {
        // 1. 先把指针据为己有，并从主程序剥离
        WGCCapture* pTemp = m_pWGC;
        m_pWGC = nullptr;
        m_bUseWGC = false;

        // 2. 告诉 WGC 停止捕获
        try {
            pTemp->StopCapture();
        }
        catch (...) {}

        // 3. 绝杀：开一个后台子线程，等 500 毫秒，让天上飞的 FrameArrived 回调全部落地后，再安全销毁！
        std::thread([pTemp]() {
            Sleep(500);
            delete pTemp;
            }).detach();
    }
}


// ============================================================================
// 【函数 3】CheckColorTrigger — 复用 DC，减少 GDI 创建/销毁开销
//
// 原始问题：
//   - 每 50ms 调用一次，每次都 CreateCompatibleDC + DeleteDC
//   - 高频率创建/销毁 DC 浪费 CPU，尤其在低端机上
// ============================================================================
void CDNFGameCaptureDlg::CheckColorTrigger()
{
    if (!m_bmp || !m_bIsRunning)
        return;

    bool isDeadArr[DEATH_POINT_COUNT] = { false };
    COLORREF colorDeath[DEATH_POINT_COUNT] = { 0 };
    int debugMatchCount[DEATH_POINT_COUNT] = { 0 };
    int debugRoiHits[DEATH_POINT_COUNT] = { 0 };
    float debugDrawX[DEATH_POINT_COUNT] = { 0 };
    float debugDrawY[DEATH_POINT_COUNT] = { 0 };
    bool debugCenterGate[DEATH_POINT_COUNT] = { false };
    int debugHitCount[DEATH_POINT_COUNT] = { 0 };
    float debugHitX[DEATH_POINT_COUNT][16] = { 0 };
    float debugHitY[DEATH_POINT_COUNT][16] = { 0 };
    int debugHitDir[DEATH_POINT_COUNT][16] = { 0 };

    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (!m_bmp) return;

        HDC hMemDC = ::CreateCompatibleDC(NULL);
        HGDIOBJ oldBmp = ::SelectObject(hMemDC, m_bmp);

        // 视频/投影兼容版：收紧后的软红橙检测。
        // 关键修正：红队血条/红色底板本身也是红色，不能再把“暗红/纯红背景”当作 X。
        // 现在只接受更像 X 笔画的“亮红/橙红/压缩后仍有亮度的红”，并过滤低亮度红底。
        auto isXRedLike = [](COLORREF c, bool isActive) -> bool {
            int r = GetRValue(c);
            int g = GetGValue(c);
            int b = GetBValue(c);

            // 死亡 X 的基准色来自实测：#D53000 = RGB(213, 48, 0)。
            // 这里按“接近这个橙红色”做容差，而不是泛泛地识别所有红色。
            // 目的：过滤红队血条/红底/头像红边，只保留更像死亡 X 笔画的橙红色。
            const int baseR = 0xD5;
            const int baseG = 0x30;
            const int baseB = 0x00;

            // 亮度下限：小 X 像素少，略放宽；主将大 X 稍稳，也保留一定容差。
            if (r < (isActive ? 120 : 125)) return false;

            // 死亡 X 是橙红：R 最高，G 有一定值，B 很低。
            // 纯深红血条常见特征是 G 太低；黄色/金边常见特征是 G 太高。
            if (g < 12 || g > 120) return false;
            if (b > 95) return false;
            if (r < g + 55) return false;
            if (r < b + 95) return false;

            int dr = abs(r - baseR);
            int dg = abs(g - baseG);
            int db = abs(b - baseB);

            // 加权距离：G/B 偏离对色相影响更大，权重更高。
            int weightedDist = dr + dg * 2 + db * 2;
            int distLimit = isActive ? 175 : 185;

            // 常规容差：覆盖 #D53000 在录屏/投影/缩放后的变暗、偏橙、轻微偏粉。
            if (weightedDist <= distLimit) {
                return true;
            }

            // 补充：非常亮的橙红高光。仍然要求 G 不能太低、B 不能太高，避免红底/白光误判。
            if (r >= 220 && g >= 22 && g <= 115 && b <= 80 &&
                r > g + 80 && r > b + 135) {
                return true;
            }

            return false;
        };

        // X 高光/前景色检测：真实 X 中心经常是白色/浅黄高光，而不是纯红色。
        // 中心门槛允许红/橙或高光；方向采样允许前景色，但最终至少要有 1 个红/橙方向证据。
        auto isXHighlightLike = [](COLORREF c, bool isActive) -> bool {
            int r = GetRValue(c);
            int g = GetGValue(c);
            int b = GetBValue(c);

            // 白色/浅黄高光：真实 X 的交叉中心和边缘高光经常落在这个范围。
            if (r >= (isActive ? 185 : 195) &&
                g >= (isActive ? 150 : 160) &&
                b >= (isActive ? 115 : 125) &&
                r >= g - 20 && g >= b - 30) {
                return true;
            }

            // 压缩/缩放后的灰白高光：三通道都很亮，避免中心点因为不是红色而失败。
            if (r >= (isActive ? 175 : 185) &&
                g >= (isActive ? 175 : 185) &&
                b >= (isActive ? 165 : 175)) {
                return true;
            }

            return false;
        };

        auto isXForegroundLike = [&](COLORREF c, bool isActive) -> bool {
            return isXRedLike(c, isActive) || isXHighlightLike(c, isActive);
        };

        auto hasLocalXRedColor = [&](int x, int y, int radius, bool isActive) -> bool {
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    if (isXRedLike(::GetPixel(hMemDC, sx, sy), isActive)) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto hasLocalXForegroundColor = [&](int x, int y, int radius, bool isActive) -> bool {
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    if (isXForegroundLike(::GetPixel(hMemDC, sx, sy), isActive)) {
                        return true;
                    }
                }
            }
            return false;
        };

        // 中心点强校验：中心必须有红色系证据。
        // 允许 5x5 小范围容错，但不再允许“白色/黄色高光”单独通过中心 gate。
        auto hasCenterXRed = [&](int x, int y, bool isActive) -> bool {
            const int radius = 2; // 5x5 容错范围
            int redHits = 0;

            // 正中心如果就是红色，直接认为中心通过。
            if (x >= 0 && x < m_w && y >= 0 && y < m_h &&
                isXRedLike(::GetPixel(hMemDC, x, y), isActive)) {
                return true;
            }

            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    if (isXRedLike(::GetPixel(hMemDC, sx, sy), isActive)) {
                        redHits++;
                    }
                }
            }

            // 不是要求单个像素完全精准，但中心附近必须真的有红/橙 X 笔画证据。
            return redHits >= 2;
        };

        // 红色中心 + X 四方向结构探测器：
        // 仍然只使用每组第 0 个有效点作为中心；中心必须有红色系证据，四方向至少 3 个方向命中红色系。
        auto checkDeadRaw = [&](int logicalIdx, int startIdx) -> bool {
            float cx = g_deathPts[startIdx].x;
            float cy = g_deathPts[startIdx].y;
            if (cx <= 0 || cy <= 0) return false;

            int centerX = (int)(cx * m_w);
            int centerY = (int)(cy * m_h);
            if (centerX < 0 || centerX >= m_w || centerY < 0 || centerY >= m_h)
                return false;

            const bool isActive = (logicalIdx == DP_LEFT_ACTIVE || logicalIdx == DP_RIGHT_ACTIVE);
            const int localRadius = isActive ? 3 : 3;
            const int requiredDirections = 3; // 中心点通过后，四个斜向射线方向中至少 3 个方向命中红色系

            int matchCount = 0;
            int diagDownHits = 0; // \ 方向：左上 <-> 右下，仅用于调试计数
            int diagUpHits = 0;   // / 方向：右上 <-> 左下，仅用于调试计数
            bool hitLeftUp = false;
            bool hitRightDown = false;
            bool hitRightUp = false;
            bool hitLeftDown = false;
            bool redLeftUp = false;
            bool redRightDown = false;
            bool redRightUp = false;
            bool redLeftDown = false;
            COLORREF centerColor = ::GetPixel(hMemDC, centerX, centerY);
            auto appendDebugHit = [&](float hx, float hy, int dirTag) {
                int idx = debugHitCount[logicalIdx];
                if (idx < 16) {
                    debugHitX[logicalIdx][idx] = hx;
                    debugHitY[logicalIdx][idx] = hy;
                    debugHitDir[logicalIdx][idx] = dirTag;
                    debugHitCount[logicalIdx]++;
                }
            };

            // 关键：中心点必须命中红色系 X 笔画。
            // 允许中心附近 5x5 容错，但不允许白色/黄色高光单独通过。
            bool centerGate = hasCenterXRed(centerX, centerY, isActive);
            debugCenterGate[logicalIdx] = centerGate;
            if (!centerGate) {
                colorDeath[logicalIdx] = centerColor;
                debugMatchCount[logicalIdx] = 0;
                debugRoiHits[logicalIdx] = 0;
                debugDrawX[logicalIdx] = cx;
                debugDrawY[logicalIdx] = cy;
                return false;
            }
            matchCount++;

            // 动态步长：主将大 X 使用原步长，替补小 X 步长减半。
            float stepX = 0.015f / 4.0f;
            float stepY = 0.025f / 4.0f;
            if (!isActive) {
                stepX /= 2.0f;
                stepY /= 2.0f;
            }

            // 沿 X 的两条斜边四个方向采样。每个方向必须命中红色系才算通过。
            for (int i = 1; i <= 4; i++) {
                // \ 方向
                float p1x = cx - i * stepX;
                float p1y = cy - i * stepY;
                float p2x = cx + i * stepX;
                float p2y = cy + i * stepY;

                int px = (int)(p1x * m_w);
                int py = (int)(p1y * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h) {
                    bool red = hasLocalXRedColor(px, py, localRadius, isActive);
                    if (red) {
                        diagDownHits++;
                        matchCount++;
                        hitLeftUp = true;
                        redLeftUp = true;
                        appendDebugHit(p1x, p1y, 0);
                    }
                }

                px = (int)(p2x * m_w);
                py = (int)(p2y * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h) {
                    bool red = hasLocalXRedColor(px, py, localRadius, isActive);
                    if (red) {
                        diagDownHits++;
                        matchCount++;
                        hitRightDown = true;
                        redRightDown = true;
                        appendDebugHit(p2x, p2y, 1);
                    }
                }

                // / 方向
                float p3x = cx + i * stepX;
                float p3y = cy - i * stepY;
                float p4x = cx - i * stepX;
                float p4y = cy + i * stepY;

                px = (int)(p3x * m_w);
                py = (int)(p3y * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h) {
                    bool red = hasLocalXRedColor(px, py, localRadius, isActive);
                    if (red) {
                        diagUpHits++;
                        matchCount++;
                        hitRightUp = true;
                        redRightUp = true;
                        appendDebugHit(p3x, p3y, 2);
                    }
                }

                px = (int)(p4x * m_w);
                py = (int)(p4y * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h) {
                    bool red = hasLocalXRedColor(px, py, localRadius, isActive);
                    if (red) {
                        diagUpHits++;
                        matchCount++;
                        hitLeftDown = true;
                        redLeftDown = true;
                        appendDebugHit(p4x, p4y, 3);
                    }
                }
            }

            // 水平/垂直方向统计只保留为调试参考，不参与死亡判定。
            // 判定只依赖中心点颜色 + 两条 X 斜边。
            int axisHits = 0;
            int axisRadius = max(1, localRadius - 1);
            for (int i = 1; i <= 4; i++) {
                float ax1 = cx - i * stepX;
                float ax2 = cx + i * stepX;
                float ay1 = cy - i * stepY;
                float ay2 = cy + i * stepY;

                int px = (int)(ax1 * m_w);
                int py = centerY;
                if (px >= 0 && px < m_w && py >= 0 && py < m_h && hasLocalXForegroundColor(px, py, axisRadius, isActive)) axisHits++;

                px = (int)(ax2 * m_w);
                py = centerY;
                if (px >= 0 && px < m_w && py >= 0 && py < m_h && hasLocalXForegroundColor(px, py, axisRadius, isActive)) axisHits++;

                px = centerX;
                py = (int)(ay1 * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h && hasLocalXForegroundColor(px, py, axisRadius, isActive)) axisHits++;

                px = centerX;
                py = (int)(ay2 * m_h);
                if (px >= 0 && px < m_w && py >= 0 && py < m_h && hasLocalXForegroundColor(px, py, axisRadius, isActive)) axisHits++;
            }

            // 只作调试参考：统计中心附近红橙像素数量，不单独判死。
            int roiHits = 0;
            int rx = isActive ? 18 : 14;
            int ry = isActive ? 16 : 12;
            for (int dy = -ry; dy <= ry; dy += 3) {
                for (int dx = -rx; dx <= rx; dx += 3) {
                    int sx = centerX + dx;
                    int sy = centerY + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    if (isXRedLike(::GetPixel(hMemDC, sx, sy), isActive)) roiHits++;
                }
            }

            colorDeath[logicalIdx] = centerColor;
            debugMatchCount[logicalIdx] = matchCount;
            debugRoiHits[logicalIdx] = roiHits;
            debugDrawX[logicalIdx] = cx;
            debugDrawY[logicalIdx] = cy;

            int directionHits = 0;
            if (hitLeftUp) directionHits++;
            if (hitRightDown) directionHits++;
            if (hitRightUp) directionHits++;
            if (hitLeftDown) directionHits++;

            int redDirectionHits = 0;
            if (redLeftUp) redDirectionHits++;
            if (redRightDown) redDirectionHits++;
            if (redRightUp) redDirectionHits++;
            if (redLeftDown) redDirectionHits++;

            // 新判定：中心点必须有红色系证据；四个斜向方向中至少 3 个方向有红色系证据。
            // 高光不再单独参与死亡判定，只用于必要时观察画面，不用于通过条件。
            debugMatchCount[logicalIdx] = directionHits * 10 + redDirectionHits;
            return directionHits >= requiredDirections;
        };

        // 8 个逻辑状态，使用旧 40 点中每组第 0 个有效点：
        // 0-3 左侧，4-7 右侧。
        for (int logicalIdx = 0; logicalIdx < DEATH_POINT_COUNT; logicalIdx++) {
            int startIdx = GetDeathRawIndex(logicalIdx);
            isDeadArr[logicalIdx] = checkDeadRaw(logicalIdx, startIdx);
        }

        // 实时调试快照：只显示 8 个逻辑状态，不参与判定。
        {
            std::lock_guard<std::mutex> dbgLock(g_deathXDebugMutex);
            for (int i = 0; i < DEATH_POINT_COUNT; i++) {
                g_deathXDebug.dead[i] = isDeadArr[i];
                g_deathXDebug.matchCount[i] = debugMatchCount[i];
                g_deathXDebug.roiHits[i] = debugRoiHits[i];
                g_deathXDebug.centerColor[i] = colorDeath[i];
                g_deathXDebug.drawX[i] = debugDrawX[i];
                g_deathXDebug.drawY[i] = debugDrawY[i];
                g_deathXDebug.centerGate[i] = debugCenterGate[i];
                g_deathXDebug.hitCount[i] = debugHitCount[i];
                for (int k = 0; k < 16; k++) {
                    g_deathXDebug.hitX[i][k] = debugHitX[i][k];
                    g_deathXDebug.hitY[i][k] = debugHitY[i][k];
                    g_deathXDebug.hitDir[i][k] = debugHitDir[i][k];
                }
            }
            g_deathXDebug.lastTick = GetTickCount();
        }

        ::SelectObject(hMemDC, oldBmp);
        ::DeleteDC(hMemDC);
    }

    // 让专业后台预览区实时刷新 8 个 X 状态，不刷日志、不打扰比赛。
    static DWORD s_lastDeathXDebugPaintTick = 0;
    DWORD debugNowTick = GetTickCount();
    if (debugNowTick - s_lastDeathXDebugPaintTick >= 150) {
        s_lastDeathXDebugPaintTick = debugNowTick;
        if (::IsWindow(m_hWnd) && m_previewRect.Width() > 0 && m_previewRect.Height() > 0) {
            ::InvalidateRect(m_hWnd, &m_previewRect, FALSE);
        }
    }

    // 3. 提取各位置的生死状态
    // 左侧：0-3
    bool leftActiveDead = isDeadArr[0];
    bool leftTeamDead = isDeadArr[0] && isDeadArr[1] && isDeadArr[2] && isDeadArr[3];

    // 右侧：4-7
    bool rightActiveDead = isDeadArr[4];
    bool rightTeamDead = isDeadArr[4] && isDeadArr[5] && isDeadArr[6] && isDeadArr[7];

    // ========================================================
    // 4. 状态机：跟踪【左侧/右侧】正在打的选手的生死，触发单局击杀
    // ========================================================
    static bool s_leftActiveWasDead = false;
    static bool s_rightActiveWasDead = false;

    // ========================================================
    // 日志输出：只提示主将 X 已识别但未触发，避免刷屏
    // ========================================================
    static DWORD s_lastDebugLogTime = 0;
    DWORD nowTick = GetTickCount();
    if (nowTick - s_lastDebugLogTime > 1000)
    {
        if (leftActiveDead && (!m_bCanTrigger || s_leftActiveWasDead))
        {
            CString reason = !m_bCanTrigger ? L"防抖冷却中" : L"状态已记录(未重置)";
            AppLog(L"🟡 左侧大X已识别但未触发匹配 (原因:" + reason + L")", RGB(255, 180, 0));
            s_lastDebugLogTime = nowTick;
        }

        if (rightActiveDead && (!m_bCanTrigger || s_rightActiveWasDead))
        {
            CString reason = !m_bCanTrigger ? L"防抖冷却中" : L"状态已记录(未重置)";
            AppLog(L"🟡 右侧大X已识别但未触发匹配 (原因:" + reason + L")", RGB(255, 180, 0));
            s_lastDebugLogTime = nowTick;
        }
    }

    // 🎯 左边正在打的死了 -> 右边赢了这一小局！(传入 0 代表左边被击杀)
    if (leftActiveDead && !s_leftActiveWasDead) {
        s_leftActiveWasDead = true;
        if (m_bCanTrigger) {
            m_bCanTrigger = FALSE;
            std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, 0).detach();
            SetTimer(2, COOLDOWN_KILL_TRIGGER, NULL);
        }
    }
    else if (!leftActiveDead && s_leftActiveWasDead) {
        s_leftActiveWasDead = false;
    }

    // 🎯 右边正在打的死了 -> 左边赢了这一小局！(传入 1 代表右边被击杀)
    if (rightActiveDead && !s_rightActiveWasDead) {
        s_rightActiveWasDead = true;
        if (m_bCanTrigger) {
            m_bCanTrigger = FALSE;
            std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, 1).detach();
            SetTimer(2, COOLDOWN_KILL_TRIGGER, NULL);
        }
    }
    else if (!rightActiveDead && s_rightActiveWasDead) {
        s_rightActiveWasDead = false;
    }

    // ========================================================
    // 5. 大比分检测
    // ========================================================
    if ((leftTeamDead || rightTeamDead) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;

        KillTimer(2);
        m_bCanTrigger = FALSE;
        SetTimer(2, COOLDOWN_ROUND_END, NULL);

        AppLog(L"🏆 局间大比分变动：已启动 35 秒深度结算防抖护盾！", RGB(0, 255, 255));

        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex);
            m_bPendingTeamScoreWin = true;
        }

        SetTimer(4, COOLDOWN_TEAM_SCORE, NULL);
    }
}

LRESULT CDNFGameCaptureDlg::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    // 🌟 左键单击：唤醒现代化 Web 计分板（因为现在它是主界面）
    if (lParam == WM_LBUTTONUP) {
        if (m_pWebDlg) {
            m_pWebDlg->ShowWindow(SW_SHOW);
            m_pWebDlg->ShowWindow(SW_RESTORE);
            m_pWebDlg->SetForegroundWindow();
        }
    }
    // 🌟 右键单击：弹出全能控制菜单
    else if (lParam == WM_RBUTTONUP) {
        CPoint pt;
        GetCursorPos(&pt);

        CMenu m;
        m.CreatePopupMenu();

        // 智能判断当前两个窗口的显示状态，动态改变菜单文字
        CString webText = (m_pWebDlg && m_pWebDlg->IsWindowVisible()) ? L"🙈 隐藏 Web 计分板" : L"💻 显示 Web 计分板";
        CString mfcText = IsWindowVisible() ? L"🙈 隐藏 专业后台" : L"⚙️ 显示 专业后台";

        m.AppendMenu(MF_STRING, 101, webText);
        m.AppendMenu(MF_STRING, 104, mfcText);
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING, 103, L"🔄 检查更新");
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING, 102, L"❌ 完全退出"); // 只有点这个才会死！

        SetForegroundWindow();
        int cmd = m.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);

        // 处理用户的点击
        if (cmd == 101) {
            if (m_pWebDlg) {
                if (m_pWebDlg->IsWindowVisible()) {
                    m_pWebDlg->ShowWindow(SW_HIDE);
                }
                else {
                    m_pWebDlg->ShowWindow(SW_SHOW);
                    m_pWebDlg->ShowWindow(SW_RESTORE);
                    m_pWebDlg->SetForegroundWindow();
                }
            }
        }
        else if (cmd == 104) {
            if (IsWindowVisible()) {
                ShowWindow(SW_HIDE);
            }
            else {
                ShowWindow(SW_SHOW);
                ShowWindow(SW_RESTORE);
                SetForegroundWindow();
            }
            BroadcastStateToWeb(); // 👈 新增：右键托盘隐藏后，通知网页按钮变色
        }
        else if (cmd == 103) {
            std::thread([this]() { CheckForUpdates(false); }).detach();
        }
        else if (cmd == 102) {
            DoRealExit(); // 真正的死神
        }
    }
    return 0;
}

void CDNFGameCaptureDlg::DoRealExit() {
    m_bIsRunning = FALSE;
    KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4);

    // ==========================================
    // 【新增】：主程序退出时，拉着 OCR 一起陪葬
    // 必须连底层的 PaddleOCR-json.exe 一起杀，防止内存泄漏泄漏！
    // ==========================================
    KillProcessByName(L"Umi-OCR.exe");
    KillProcessByName(L"PaddleOCR-json.exe");

    DestroyWindow();
    PostQuitMessage(0);
}

// ============================================================================
// UI 事件响应与授权软拦截
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedStart()
{
    if (m_cloudExpireTime == -1) {
        CString msg = L"正在与云端同步授权信息，请稍后...";
        if (!IsWindowVisible() && m_pWebDlg) { // 如果隐藏了主窗口，就把报错发给网页
            json reply; reply["action"] = "auth_result"; reply["success"] = false;
            reply["message"] = std::string(CW2A(msg, CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            m_pWebDlg->SendStateToWeb(jsonStr);
        }
        else {
            ShowCenteredMsgBox(msg, L"安全校验", MB_ICONINFORMATION);
        }
        BroadcastStateToWeb(); // 🚨 确保网页按钮状态立刻复位
        return;
    }

    if (!m_bIsAuthValid) {
        CString msg = L"❌ 您的授权无效或已过期，请检查卡密记录！";
        if (!IsWindowVisible() && m_pWebDlg) { // 如果隐藏了主窗口，就把报错发给网页
            json reply; reply["action"] = "auth_result"; reply["success"] = false;
            reply["message"] = std::string(CW2A(msg, CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            m_pWebDlg->SendStateToWeb(jsonStr);
        }
        else {
            ShowCenteredMsgBox(msg, L"需要授权", MB_ICONWARNING);
        }
        BroadcastStateToWeb(); // 🚨 确保网页按钮状态立刻复位
        return;
    }

    static bool once;
    if (!once) {
        if (m_bIsTrial && !m_bIsRunning) {
            once = true;
            if (IsWindowVisible()) { // 隐藏状态下不弹试用说明(网页上已经写了)
                CString trialMsg;
                trialMsg.Format(L"【欢迎试用 DNF 击杀统计工具】\r\n\r\n您当前处于免费试用阶段,试用结束时间:\r\n%s\r\n\r\n点击确定后将开启监控功能。", (LPCTSTR)FormatTimeStamp(m_trialEnd));
                ShowCenteredMsgBox(trialMsg, L"试用阶段", MB_ICONINFORMATION);
            }
        }
    }

    if (!m_bIsRunning) {
        m_bIsRunning = TRUE;
        m_btnStart.SetWindowText(L"停止监控");
        // 【身份融合补丁】开始监控时清空上一段录像/上一局残留缓存。
        NotifyIdentityRoundReset(L"开始监控，清空上一段身份缓存");
        // ==========================================
        // 【新增】：点击开始监控，立刻静默唤醒同目录下的 Umi-OCR
        // ==========================================
        EnsureOcrRunning();

        HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);

        // 如果引擎还没就绪，主动尝试激活一次
        bool shouldTryWGC = (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1);
        if (hGame && shouldTryWGC && !m_bUseWGC) {
            try {
                // 🚨 缓存支持状态，防止每次都去调用底层
                static int s_wgcSupported = -1;
                if (s_wgcSupported == -1) {
                    s_wgcSupported = WGCCapture::IsSupported() ? 1 : 0;
                }

                if (s_wgcSupported == 1) {
                    if (!m_pWGC) m_pWGC = new WGCCapture();
                    if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) {
                        m_bUseWGC = true;
                    }
                }
            }
            catch (...) {
                SafeDeleteWGC();
            }
        }

        m_nBlankFrameCount = 0;
        m_bAlreadyPrompted = false;

        // 打印相应的状态日志
        if (m_bUseWGC) {
            AppLog(L"✅ [监控已启动] 已启用 WGC 硬件加速捕获 (零闪屏)", RGB(0, 255, 100));
        }
        else if (!hGame) {
            // ★ 游戏没开，不管选了什么引擎，都只提示待命，不要说"降级"
            AppLog(L"⚠️ [监控已启动] 未检测到游戏窗口，待命中...", RGB(255, 165, 0));
        }
        else {
            // 游戏已开但 WGC 失败的情况，才算真正降级
            SafeDeleteWGC(); // 🚨 换成安全销毁

            if (m_nCaptureEngineChoice == 1) {
                AppLog(L"❌ [监控已启动] WGC 初始化失败，自动降级为 PrintWindow", RGB(255, 80, 80));
            }
            else if (m_nCaptureEngineChoice == 2) {
                AppLog(L"✅ [监控已启动] 用户选择 PrintWindow 兼容模式", RGB(0, 255, 100));
            }
            else {
                AppLog(L"⚠️ [监控已启动] WGC 不可用，已降级为 PrintWindow", RGB(255, 165, 0));
            }
        }

        SetTimer(1, 100, NULL);
        SetTimer(3, HISTORY_INTERVAL_MS, NULL);
        m_status.SetWindowText(L"监控中...");
    }
    else {
        m_bIsRunning = FALSE;
        KillTimer(1);
        KillTimer(3);

        // ==========================================
        // 【关键修复】：停止监控时，绝不能销毁 m_pWGC！
        // 把它留着，让 Timer 6 继续给画面提供实时预览！
        // ==========================================

        m_nBlankFrameCount = 0;
        m_bAlreadyPrompted = false;

        m_btnStart.SetWindowText(L"开始监控");
        m_status.SetWindowText(L"已停止");
        AppLog(L"🛑 [监控已停止] 战绩统计已暂停", RGB(255, 165, 0));
    }
    // 🚨 每次点击开始或停止，必须通知网页同步按钮状态
    BroadcastStateToWeb();
}

void CDNFGameCaptureDlg::OnBnClickedInputKey() {
    CString currentText;
    m_btnInputKey.GetWindowText(currentText);

    // ==========================================
    // 阶段一：点击“输入授权码”，打开记事本，并将按钮变身
    // ==========================================
    if (currentText == L"输入授权码") {
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        CString path = exePath;
        path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

        CFile file;
        if (file.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite)) {
            file.Close();
        }

        ShellExecute(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);

        // 【关键】：改变按钮文字
        m_btnInputKey.SetWindowText(L"应用授权码");

        // 更新弹窗提示语
        MessageBox(L"请在打开的 license.txt 中粘贴新卡密并保存。\r\n\r\n保存完成后，请点击软件上的【应用授权码】即可生效！", L"第一步：输入授权码", MB_ICONINFORMATION);
    }
    // ==========================================
    // 阶段二：点击“应用授权码”，校验卡密，并将按钮还原
    // ==========================================
    else {
        // 1. 重新读取卡密并执行静默检查
        CheckTrialAndLicense();

        // 2. 清空旧面板，强制打印最新的状态
        if (m_editVisualLogs.m_hWnd) {
            m_editVisualLogs.SetWindowText(L"");
        }
        OutputDebugAuthInfo();

        m_status.SetWindowText(L"授权码校验已触发");

        // 3. 【关键】：将按钮文字还原，完成闭环
        m_btnInputKey.SetWindowText(L"输入授权码");
    }
}

void CDNFGameCaptureDlg::OnBnClickedApply() {
    // ==========================================
    // 1. 纯粹的数据落地：保存所有战绩和配置
    // ==========================================
    SaveAliasDB();      // 保存小号自动补全库
    SaveConfigToFile(); // 保存战局人员信息
    WriteScoreToFile(); // 刷新输出给 OBS 用的直播 TXT 文本

    // ==========================================
    // 2. 纯粹的视觉刷新：同步左右界面的显示
    // ==========================================
    SyncDataToTree();   // 刷新左侧树状图
    // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
    BroadcastStateToWeb();
    RefreshDisplay();   // 刷新右侧红蓝阵营对比图

    // ==========================================
    // 3. 状态反馈
    // ==========================================
    m_status.SetWindowText(L"应用修改成功");
    AppLog(L"💾 [系统] 对局信息与战绩已手动保存", RGB(0, 255, 100));
}

void CDNFGameCaptureDlg::OnBnClickedFlip() {
    m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED);
    WriteScoreToFile();
    RefreshDisplay();
    BroadcastStateToWeb(); // 👈 新增：通知网页跟着翻转
}

void CDNFGameCaptureDlg::OnBnClickedReset() {
#ifdef _DEBUG
    if (GetKeyState(VK_CONTROL) < 0) {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            DWORD iT = 0, sz = 4;
            time_t now = time(nullptr);
            RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz);
            if ((long long)now > ((long long)iT + 604800)) {
                DWORD resetTime = (DWORD)now;
                RegSetValueEx(hKey, L"InstallTime", 0, REG_DWORD, (const BYTE*)&resetTime, sizeof(DWORD));
                RegSetValueEx(hKey, L"LastRun", 0, REG_DWORD, (const BYTE*)&resetTime, sizeof(DWORD));
                MessageBox(L"【后门】试用期已恢复！重启生效。", L"后门提示", MB_ICONINFORMATION);
            }
            else {
                DWORD expiredTime = (DWORD)(now - 800000);
                RegSetValueEx(hKey, L"InstallTime", 0, REG_DWORD, (const BYTE*)&expiredTime, sizeof(DWORD));
                MessageBox(L"【后门】试用期已强制熔断！重启测试。", L"后门提示", MB_ICONWARNING);
            }
            RegCloseKey(hKey);
        }
        return;
    }
#endif

    if (MessageBox(L"确定要将战绩全部归零吗？", L"确认", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        m_totalScoreRed = 0;
        m_totalScoreBlue = 0;
        for (int i = 0; i < 8; i++) {
            m_players[i].kills = 0; m_players[i].deaths = 0;
            m_players[i].currentStreak = 0; m_players[i].akCount = 0;
        }
        SyncDataToTree();
        // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
        BroadcastStateToWeb();
        SaveConfigToFile();
        RefreshDisplay();
        WriteScoreToFile();
        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        NotifyIdentityRoundReset(L"手动战绩归零/重置对局");
        OutputDebugAuthInfo();
        m_status.SetWindowText(L"战绩已归零！");
    }
}

void CDNFGameCaptureDlg::OnBnClickedBrowseDir() {
    bool wasRunning = m_bIsRunning;
    KillTimer(1);
    KillTimer(6);

    BROWSEINFO bi = { 0 };
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = L"请选择输出目录：";
    // 【终极绝杀】：只保留基础属性，绝对不能加 BIF_NEWDIALOGSTYLE！
    bi.ulFlags = BIF_RETURNONLYFSDIRS;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            m_outputDir = path;
            if (m_outputDir.Right(1) == L"\\") m_outputDir.TrimRight(L"\\");

            m_editOutDir.SetWindowText(m_outputDir);
            WritePrivateProfileString(L"Settings", L"OutputDir", m_outputDir, m_iniPath);
            WriteScoreToFile();
            m_status.SetWindowText(L"输出目录已更新");
        }
        CoTaskMemFree(pidl);
        // 🚨【新增】：修改完目录后，立刻广播给网页同步显示
        BroadcastStateToWeb();
    }

    if (wasRunning) SetTimer(1, 50, NULL);
    SetTimer(6, 200, NULL);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) { return TRUE; }

// ============================================================================
// OCR 下拉框与坐标选取
// ============================================================================
LRESULT CDNFGameCaptureDlg::OnUpdateOcrDropdowns(WPARAM wParam, LPARAM lParam) {
    std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
    if (wParam == 1) {
        m_cmbLeft.ResetContent(); m_cmbLeft.AddString(L"[红] 左侧自动追踪"); m_cmbLeft.SetCurSel(0);
        m_cmbRight.ResetContent(); m_cmbRight.AddString(L"[蓝] 右侧自动追踪"); m_cmbRight.SetCurSel(0);
        return 0;
    }
    while (m_cmbLeft.GetCount() - 1 < (int)m_ocrRecordsLeft.size()) {
        m_cmbLeft.AddString(m_ocrRecordsLeft[m_cmbLeft.GetCount() - 1].displayText);
    }
    while (m_cmbRight.GetCount() - 1 < (int)m_ocrRecordsRight.size()) {
        m_cmbRight.AddString(m_ocrRecordsRight[m_cmbRight.GetCount() - 1].displayText);
    }
    return 0;
}
void CDNFGameCaptureDlg::OnCbnSelchangeLeft() { m_viewIndexLeft = (m_cmbLeft.GetCurSel() == 0) ? -1 : (m_cmbLeft.GetCurSel() - 1); InvalidateRect(&m_previewRect, FALSE); }
void CDNFGameCaptureDlg::OnCbnSelchangeRight() { m_viewIndexRight = (m_cmbRight.GetCurSel() == 0) ? -1 : (m_cmbRight.GetCurSel() - 1); InvalidateRect(&m_previewRect, FALSE); }

// ============================================================================
// 手动测试与核心截图逻辑
// ============================================================================
void CDNFGameCaptureDlg::ManualTriggerKill(int killSide) {
    if (!m_bIsRunning || !m_bCanTrigger) return;

    Capture();
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            // ★ 同样用 CopyImage 替代
            if (m_historyBmps[m_historyIdx])
                ::DeleteObject(m_historyBmps[m_historyIdx]);
            m_historyBmps[m_historyIdx] = (HBITMAP)::CopyImage(
                m_bmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            m_historyIdx = (m_historyIdx + 1) % MAX_HISTORY_FRAMES;
        }
    }

    m_bCanTrigger = FALSE;
    CString sideName = (killSide == 0) ? L"【红队】" : L"【蓝队】";
    time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
    CString tStr; tStr.Format(L"[%02d:%02d:%02d] 🚀 全局快捷键触发: %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)sideName);

    {
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.push_back({ tStr, RGB(255, 165, 0) });
    }
    std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
    SetTimer(2, COOLDOWN_KILL_TRIGGER, NULL);
}


LRESULT CDNFGameCaptureDlg::OnWGCInitDone(WPARAM wParam, LPARAM lParam) {
    // 【已被废弃的异步回调，内容留空】
    return 0;
}

void CDNFGameCaptureDlg::Capture() {
    // ★ 如果既不是监控状态，主窗口也不可见，根本不需要画面，直接返回
    if (!m_bIsRunning && !IsWindowVisible())
        return;

    // ★★★ 下拉框打开期间，完全跳过捕获，防止滚动时反复初始化引擎 ★★★
    if (m_cmbTargetWindow.m_hWnd && m_cmbTargetWindow.GetDroppedState()) {
        return;
    }

    DWORD_PTR targetData = 0;
    if (m_cmbTargetWindow.m_hWnd && m_cmbTargetWindow.GetCurSel() != -1) {
        targetData = m_cmbTargetWindow.GetItemData(m_cmbTargetWindow.GetCurSel());
    }

    // ==========================================
    // 路线 A：【摄像头模式】
    // ==========================================
    if (targetData & 0x80000000) {
        int camIndex = targetData & 0x7FFFFFFF;
        if (!m_pCamera) {
            m_pCamera = new CameraCapture();
            if (m_pCamera->Initialize(camIndex)) {
                m_pCamera->StartCapture();
                AppLog(L"📹 [摄像头] 已成功连接，正在出流", RGB(0, 255, 100));
            }
            else {
                delete m_pCamera; m_pCamera = nullptr;
                AppLog(L"❌ [摄像头] 无法连接或被占用", RGB(255, 80, 80));
                return;
            }
        }

        int camW = 0, camH = 0;
        HBITMAP hCamFrame = m_pCamera->GetLatestFrame(camW, camH);
        if (hCamFrame && camW > 0 && camH > 0) {
            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (camW != m_w || camH != m_h) {
                for (int i = 0; i < MAX_HISTORY_FRAMES; i++) {
                    if (m_historyBmps[i]) { ::DeleteObject(m_historyBmps[i]); m_historyBmps[i] = nullptr; }
                }
                m_historyIdx = 0;
            }
            if (m_bmp) ::DeleteObject(m_bmp);
            m_bmp = hCamFrame;
            m_w = camW;
            m_h = camH;
        }
        else if (hCamFrame) {
            ::DeleteObject(hCamFrame);
        }
    }
    // ==========================================
    // 路线 B：【窗口模式】
    // ==========================================
    else {
        HWND hGame = NULL;
        if (targetData == 0) {
#if ENABLE_CLOUD_TEST_MODE
            hGame = ::GetDesktopWindow();
#else
            hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
#endif
        }
        else {
            hGame = (HWND)targetData;
            if (!::IsWindow(hGame)) {
                m_cmbTargetWindow.SetCurSel(0);
                return;
            }
        }

        if (!hGame) {
            if (m_pWGC && !m_bIsRunning) {
                // 🚨 换成安全销毁
                SafeDeleteWGC();
            }
            return;
        }

        // ==========================================
        // 2. 同步安全 WGC 初始化 (防假死装甲护体)
        // ==========================================
#if !ENABLE_CLOUD_TEST_MODE
        bool shouldTryWGC = (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1);

        // 🚨【终极死穴修复】：如果 WGC 已经在正常运行（m_bUseWGC == true），绝对不能再去初始化它！
        // 否则会导致它在后台捕获途中被主线程立刻 delete，触发 0xDDDDDDDD 越界崩溃！
        if (shouldTryWGC && !m_bUseWGC) {
            DWORD_PTR dwResult = 0;
            if (::SendMessageTimeout(hGame, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &dwResult) != 0) {
                try {
                    static int s_wgcSupported = -1;
                    if (s_wgcSupported == -1) {
                        s_wgcSupported = WGCCapture::IsSupported() ? 1 : 0;
                    }

                    if (s_wgcSupported == 1) {
                        if (!m_pWGC) m_pWGC = new WGCCapture();
                        if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) {
                            m_bUseWGC = true;
                        }
                        else {
                            // 这里删除是安全的，因为它根本没跑起来
                            delete m_pWGC; m_pWGC = nullptr;

                            // 🚨 如果自动选择模式下初始化失败，强制降级为兼容模式，防止下一帧再次触发死循环重试
                            if (m_nCaptureEngineChoice == 0) {
                                m_nCaptureEngineChoice = 2;
                                m_cmbCaptureEngine.SetCurSel(2); // 同步 UI
                            }
                        }
                    }
                }
                catch (...) {
                    SafeDeleteWGC();
                }
            }
        }
#endif

        bool bNeedBlankCheck = false;
        int  capturedW = 0, capturedH = 0;
        HBITMAP hCapturedBmp = nullptr;

        // 3. WGC 捕获
        if (m_bUseWGC && m_pWGC) {
            int w = 0, h = 0;
            HBITMAP hFrame = m_pWGC->GetLatestFrame(w, h);
            if (hFrame && w > 0 && h > 0) {

                // 【绝杀】：WGC 暴力切除标题栏与边框手术 (避开Win11幽灵阴影)
                // ==========================================
                if (m_chkCropTitle.m_hWnd && m_chkCropTitle.GetCheck() == BST_CHECKED && hGame) {
                    RECT cRect; ::GetClientRect(hGame, &cRect);
                    int cW = cRect.right - cRect.left;
                    int cH = cRect.bottom - cRect.top;

                    // 计算物理边框：(WGC截图宽度 - 实际客户区宽度) / 2 = 单侧边框厚度
                    int borderX = max(0, (w - cW) / 2);
                    // 标题栏高度：(WGC截图高度 - 实际客户区高度) - 底部边框
                    int borderY = max(0, h - cH - borderX);

                    // 只有当高度差确实存在（有标题栏），且尺寸合法时，才执行手术
                    if (borderY > 0 && cW > 0 && cH > 0 && (borderX + cW) <= w && (borderY + cH) <= h) {
                        HDC hdcScreen = ::GetDC(NULL);
                        HBITMAP hCropped = ::CreateCompatibleBitmap(hdcScreen, cW, cH);
                        HDC hMemDst = ::CreateCompatibleDC(hdcScreen);
                        HDC hMemSrc = ::CreateCompatibleDC(hdcScreen);
                        HGDIOBJ oldDst = ::SelectObject(hMemDst, hCropped);
                        HGDIOBJ oldSrc = ::SelectObject(hMemSrc, hFrame);

                        // 精准从 WGC 原图的 (borderX, borderY) 开始抠出纯净画面
                        ::BitBlt(hMemDst, 0, 0, cW, cH, hMemSrc, borderX, borderY, SRCCOPY);

                        ::SelectObject(hMemDst, oldDst); ::SelectObject(hMemSrc, oldSrc);
                        ::DeleteDC(hMemDst); ::DeleteDC(hMemSrc); ::ReleaseDC(NULL, hdcScreen);

                        ::DeleteObject(hFrame); // 销毁带标题栏的原图
                        hFrame = hCropped;      // 替换为纯净的图！
                        w = cW; h = cH;         // 更新全局分辨率
                    }
                }
                //if (!m_bAlreadyPrompted && m_nCaptureEngineChoice == 0 && IsBitmapBlank(hFrame, w, h)) {
                //    m_nBlankFrameCount++;
                //    if (m_nBlankFrameCount >= 5) {
                //        AppLog(L"⚠️ [捕获引擎] WGC 持续黑屏,自动降级为 PrintWindow", RGB(255, 165, 0));

                //        // 🚨 换成安全销毁
                //        SafeDeleteWGC();

                //        // 🚨 强行修改模式，防止下一帧再次触发 WGC 初始化死循环！
                //        m_nCaptureEngineChoice = 2;
                //        m_cmbCaptureEngine.SetCurSel(2);

                //        m_nBlankFrameCount = 0;
                //        DeleteObject(hFrame);
                //        goto fallback_printwindow;
                //    }
                //}
                //else {
                //    m_nBlankFrameCount = 0;
                //}

                std::lock_guard<std::mutex> lock(g_bmpMutex);
                if (w != m_w || h != m_h) {
                    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) {
                        if (m_historyBmps[i]) { ::DeleteObject(m_historyBmps[i]); m_historyBmps[i] = nullptr; }
                    }
                    m_historyIdx = 0;
                }

                if (m_bmp) DeleteObject(m_bmp);
                m_bmp = hFrame;
                m_w = w;
                m_h = h;
            }
            else if (hFrame) {
                ::DeleteObject(hFrame);
            }
        }
        // 4. PrintWindow / BitBlt 兼容模式
        else {
        fallback_printwindow:
            static DWORD s_lastPwTime = 0;
            DWORD now = GetTickCount();
            if (now - s_lastPwTime < 333) return;
            s_lastPwTime = now;

            {
                std::lock_guard<std::mutex> lock(g_bmpMutex);
#if ENABLE_CLOUD_TEST_MODE
                m_w = GetSystemMetrics(SM_CXSCREEN); m_h = GetSystemMetrics(SM_CYSCREEN);
                if (m_w > 0 && m_h > 0) {
                    HDC hdcScreen = ::GetDC(NULL);
                    if (!m_bmp) m_bmp = ::CreateCompatibleBitmap(hdcScreen, m_w, m_h);
                    HDC hMem = ::CreateCompatibleDC(hdcScreen);
                    HGDIOBJ old = ::SelectObject(hMem, m_bmp);
                    ::BitBlt(hMem, 0, 0, m_w, m_h, hdcScreen, 0, 0, SRCCOPY);
                    ::SelectObject(hMem, old); ::DeleteDC(hMem); ::ReleaseDC(NULL, hdcScreen);
                    capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp; bNeedBlankCheck = false;
                }
#else
                // ==========================================
                // 【绝杀修复】：完美适配去标题栏，不留黑边！
                // ==========================================
                bool isCrop = (m_chkCropTitle.m_hWnd && m_chkCropTitle.GetCheck() == BST_CHECKED);
                int newW = 0, newH = 0;

                // 核心：是否截取整个窗口(包含外部边框和标题栏)
                if (!isCrop) {
                    RECT wRect; ::GetWindowRect(hGame, &wRect);
                    newW = wRect.right - wRect.left;
                    newH = wRect.bottom - wRect.top;
                }
                else {
                    RECT cRect; ::GetClientRect(hGame, &cRect);
                    newW = cRect.right - cRect.left;
                    newH = cRect.bottom - cRect.top;
                }

                if (newW > 0 && newH > 0) {
                    if (!m_bmp || newW != m_w || newH != m_h) {
                        if (m_bmp) { ::DeleteObject(m_bmp); m_bmp = nullptr; }
                        HDC hdc = ::GetDC(hGame); m_bmp = ::CreateCompatibleBitmap(hdc, newW, newH); ::ReleaseDC(hGame, hdc);
                    }
                    m_w = newW; m_h = newH;

                    HDC hGameDC = ::GetDC(hGame);
                    HDC hMemDC = ::CreateCompatibleDC(hGameDC);
                    HGDIOBJ oldBmp = ::SelectObject(hMemDC, m_bmp);

                    // ==========================================
                    // 【极度关键】：刷入纯黑底漆！
                    // ==========================================
                    RECT fillRect = { 0, 0, newW, newH };
                    ::FillRect(hMemDC, &fillRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

                    // ==========================================
                    // 【终极合并】：废弃 BitBlt，全系启用 DWM 穿透捕获！
                    // 无论是 DNF 还是 OBS，只要没假死，统统能抓到最新的硬件加速实时画面！
                    // ==========================================
                    DWORD_PTR dwResult = 0;
                    if (::SendMessageTimeout(hGame, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &dwResult) != 0) {
                        // 参数 2: PW_RENDERFULLCONTENT (捕获带边框的硬件加速窗口)
                        // 参数 3: PW_CLIENTONLY | PW_RENDERFULLCONTENT (精准剔除标题栏)
                        ::PrintWindow(hGame, hMemDC, isCrop ? 3 : 2);
                    }

                    ::SelectObject(hMemDC, oldBmp); ::DeleteDC(hMemDC); ::ReleaseDC(hGame, hGameDC);
                    capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp; bNeedBlankCheck = !m_bAlreadyPrompted;
                }
#endif
            }
            //if (bNeedBlankCheck && IsBitmapBlank(hCapturedBmp, capturedW, capturedH)) {
            //    m_nBlankFrameCount++;
            //    if (m_nBlankFrameCount >= 5) {
            //        m_bAlreadyPrompted = true;
            //        if (!IsRunningAsAdmin()) {
            //            KillTimer(m_bIsRunning ? 1 : 6);
            //            int ret = ShowCenteredMsgBox(L"⚠️ 检测到画面连续黑屏\r\n请尝试以管理员身份运行软件。", L"权限不足", MB_ICONWARNING | MB_YESNO | MB_SYSTEMMODAL);
            //            if (ret == IDYES) {
            //                if (m_bIsRunning) { m_bIsRunning = FALSE; KillTimer(3); }
            //                if (!RelaunchAsAdmin()) MessageBox(L"自动提权失败，请手动管理员运行", L"错误", MB_ICONERROR);
            //                return;
            //            }
            //            if (m_bIsRunning) SetTimer(1, 50, NULL);
            //            else              SetTimer(6, 200, NULL);
            //        }
            //    }
            //}
            //else if (bNeedBlankCheck) {
            //    m_nBlankFrameCount = 0;
            //}
        }
    }

    // 5. 渲染预览图
    if (m_w <= 0 || m_h <= 0) return;
    CRect client; GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, client.right, splitY);
    float aspect = (float)m_w / (float)m_h;
    int drawW = topHalf.Width(); int drawH = (int)(drawW / aspect);
    if (drawH > topHalf.Height()) { drawH = topHalf.Height(); drawW = (int)(drawH * aspect); }
    int dX = topHalf.left + (topHalf.Width() - drawW) / 2; int dY = topHalf.top + (topHalf.Height() - drawH) / 2;
    m_previewRect = CRect(dX, dY, dX + drawW, dY + drawH);
    InvalidateRect(&topHalf, FALSE);
}



void CDNFGameCaptureDlg::EnsureOcrRunning() {
    std::lock_guard<std::mutex> lk(m_launchMutex); DWORD now = GetTickCount();
    if (now - m_lastLaunchOcrTime < 10000 || GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) return;
    m_lastLaunchOcrTime = now;
    SHELLEXECUTEINFO s = { sizeof(s) }; s.fMask = SEE_MASK_FLAG_NO_UI; s.lpVerb = L"open"; s.lpFile = m_ocrExePath; s.nShow = SW_SHOWMINNOACTIVE; ShellExecuteEx(&s);
}


void CDNFGameCaptureDlg::FilterLivePlatformPrefixes() {
    std::vector<CString> keywords = { L"FSN", L"TV", L"直播", L"抖音", L"快手", L"斗鱼", L"虎牙", L"B站", L"BILIBILI", L"企冲", L"熊猫", L"战旗" };
    for (const CString& kw : keywords) {
        int count = 0;
        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            bool foundInPlayer = false; CString upperName = m_players[i].name; upperName.MakeUpper(); CString upperKw = kw; upperKw.MakeUpper();
            if (upperName.Find(upperKw) != -1) { foundInPlayer = true; }
            else { for (const auto& a : m_players[i].aliases) { CString upperAlias = a.name; upperAlias.MakeUpper(); if (upperAlias.Find(upperKw) != -1) { foundInPlayer = true; break; } } }
            if (foundInPlayer) count++;
        }
        if (count >= 2) {
            CString upperKw = kw; upperKw.MakeUpper();
            for (int i = 0; i < 8; i++) {
                if (m_players[i].name.IsEmpty()) continue;
                CString upperName = m_players[i].name; upperName.MakeUpper(); int pos = upperName.Find(upperKw);
                while (pos != -1) { m_players[i].name.Delete(pos, kw.GetLength()); m_players[i].name.Trim(L"-_. "); upperName = m_players[i].name; upperName.MakeUpper(); pos = upperName.Find(upperKw); }
                for (auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name; upperAlias.MakeUpper(); int apos = upperAlias.Find(upperKw);
                    while (apos != -1) { a.name.Delete(apos, kw.GetLength()); a.name.Trim(L"-_. "); upperAlias = a.name; upperAlias.MakeUpper(); apos = upperAlias.Find(upperKw); }
                }
            }
        }
    }
}

void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) r.push_back(m_players[i]); else b.push_back(m_players[i]);
    }
    while (r.size() < 4) r.push_back({ L"",0,{},0,0,0,0 });
    while (b.size() < 4) b.push_back({ L"",1,{},0,0,0,0 });
    std::vector<PlayerData>& lT = m_bFlipSides ? b : r; std::vector<PlayerData>& rT = m_bFlipSides ? r : b;

    CString pathScore = m_outputDir + L"\\比分.txt"; CString pathLeft = m_outputDir + L"\\左侧人头.txt"; CString pathRight = m_outputDir + L"\\右侧人头.txt"; CString pathKill = m_outputDir + L"\\击杀.txt";
    FILE* fS = NULL;
    if (_wfopen_s(&fS, pathScore, L"wt, ccs=UTF-8") == 0 && fS) { fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue); fclose(fS); }

    auto gs_full = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L""); CString s; s.Format(L"%s%02d/%02d", p.name.GetString(), p.kills, p.deaths);
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L" A%d", p.akCount);else  s += L" -"; return s;
        };

    FILE* fKL = NULL; if (_wfopen_s(&fKL, pathLeft, L"wt, ccs=UTF-8") == 0 && fKL) { for (int i = 0; i < 4; i++) { CString ls = gs_full(lT[i]); if (!ls.IsEmpty()) fwprintf(fKL, L"%s\n", ls.GetString()); } fclose(fKL); }
    FILE* fKR = NULL; if (_wfopen_s(&fKR, pathRight, L"wt, ccs=UTF-8") == 0 && fKR) { for (int i = 0; i < 4; i++) { CString rs = gs_full(rT[i]); if (!rs.IsEmpty()) fwprintf(fKR, L"%s\n", rs.GetString()); } fclose(fKR); }

    auto gs_kill_only = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L""); CString s; s.Format(L"%s %02d", p.name.GetString(), p.kills);
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L"A%d", p.akCount);else  s += L" -"; return s;
        };
    FILE* fKill = NULL;
    if (_wfopen_s(&fKill, pathKill, L"wt, ccs=UTF-8") == 0 && fKill) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_kill_only(lT[i]); CString rs = gs_kill_only(rT[i]);
            if (ls.IsEmpty() && rs.IsEmpty()) continue;
            int pad = max(1, 11 - GetVisualWidth(ls)); CString spaces(L' ', pad); fwprintf(fKill, L"%s%s%s\n", ls.GetString(), spaces.GetString(), rs.GetString());
        }
        fclose(fKill);
    }
}

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");
    CString sS; sS.Format(L"========= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
    auto ap = [&](const CString& t, COLORREF c) {
        int l = m_editOcrResult.GetWindowTextLength(); m_editOcrResult.SetSel(l, l);
        CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = c;
        m_editOcrResult.SetSelectionCharFormat(cf); m_editOcrResult.ReplaceSel(t);
        };

    ap(sS, RGB(0, 100, 0)); ap(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0)); ap(L"------------------------------------------\r\n", RGB(150, 150, 150));
    std::vector<int> rI, bI;
    for (int i = 0; i < 8; i++) { if (m_players[i].name.IsEmpty()) continue; if (m_players[i].team == 0) rI.push_back(i); else bI.push_back(i); }
    std::vector<int>& lIdx = m_bFlipSides ? bI : rI; std::vector<int>& rIdx = m_bFlipSides ? rI : bI;
    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0); COLORREF rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    for (size_t i = 0; i < (std::max)(lIdx.size(), rIdx.size()); i++) {
        CString lT = L"";
        if (i < lIdx.size()) {
            int p = lIdx[i]; lT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) lT += L" A"; else if (m_players[p].akCount > 1) lT.AppendFormat(L" A%d", m_players[p].akCount);
        }
        ap(lT, lC); int curW = GetVisualWidth(lT); for (int s = 0; s < (32 - curW); s++) ap(L" ", 0);
        CString rT = L"";
        if (i < rIdx.size()) {
            int p = rIdx[i]; rT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) rT += L" A"; else if (m_players[p].akCount > 1) rT.AppendFormat(L" A%d", m_players[p].akCount); rT += L"\r\n";
        }
        else { rT = L"\r\n"; }
        ap(rT, rC);
    }
}

// ============================================================================
// 绘制模块与 UI 排版
// ============================================================================
void CDNFGameCaptureDlg::OnPaint() {
    static bool s_bTimer7Started = false;
    if (!s_bTimer7Started) { SetTimer(7, 1000, NULL); s_bTimer7Started = true; }

    CPaintDC dc(this);
    CRect r; GetClientRect(&r);
    int splitY = max(100, r.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, r.right, splitY);
    CRect uiRect(0, splitY, r.right, r.bottom);

    dc.FillSolidRect(&uiRect, GetSysColor(COLOR_BTNFACE));
    CDC memDC; memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp; memBmp.CreateCompatibleBitmap(&dc, topHalf.Width(), topHalf.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);
    memDC.FillSolidRect(0, 0, topHalf.Width(), topHalf.Height(), RGB(15, 15, 15));

    if (m_w > 0 && m_h > 0 && IsWindowVisible()) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);
            memDC.SetStretchBltMode(HALFTONE);
            memDC.StretchBlt(m_previewRect.left, m_previewRect.top, m_previewRect.Width(), m_previewRect.Height(), CDC::FromHandle(hBmpDC), 0, 0, m_w, m_h, SRCCOPY);
            ::SelectObject(hBmpDC, oldBmp); ::DeleteDC(hBmpDC);
        }
    }
    Draw(memDC);
    dc.BitBlt(0, 0, topHalf.Width(), topHalf.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
}

// ==========================================
// 【新增】：鼠标点击列表框某一项时触发自动填入
// ==========================================
void CDNFGameCaptureDlg::OnLbnSelchangeRecentPlayers() {
    int curSel = m_listRecentPlayers.GetCurSel();

    // ==========================================
    // 【拦截】：如果点的是第一行的标题 (索引0) 或者空白处
    // 直接取消高亮状态，不执行任何操作
    // ==========================================
    if (curSel <= 0 || curSel == LB_ERR) {
        m_listRecentPlayers.SetCurSel(-1);
        return;
    }

    std::lock_guard<std::mutex> lk(m_recentRecordsMutex);

    // ==========================================
    // 【修正】：因为第0项是标题，所以真实的数据索引需要 减 1
    // ==========================================
    int realIndex = curSel - 1;

    if (realIndex >= 0 && realIndex < (int)m_recentPlayerRecords.size()) {
        const RecentPlayerRecord& record = m_recentPlayerRecords[realIndex];

        // 拼装格式填入输入框
        CString fillText = record.mainName;
        for (const CString& alias : record.aliases) {
            if (!alias.IsEmpty()) fillText += L"(" + alias + L")";
        }

        m_editQuickAdd.SetWindowText(fillText);
        m_editQuickAdd.SetFocus();
        m_editQuickAdd.SetSel(fillText.GetLength(), fillText.GetLength());

        // (注：沉底逻辑依然在点击“添加”按钮里执行，这里保持原样不移动)
    }
}

// ==========================================
// 【新增】：更新并刷新左下角的常用选手名单
// ==========================================
void CDNFGameCaptureDlg::UpdateAndRefreshRecentList() {
    std::lock_guard<std::mutex> lk(m_recentRecordsMutex);

    // 1. 提取当前在场的 8 个人，他们是最活跃的，优先插到最前面
    for (int i = 7; i >= 0; i--) {
        if (m_players[i].name.IsEmpty()) {
            continue;
        }

        // 查找是否已存在，如果存在先删掉，以便稍后重新插到最前面
        for (auto it = m_recentPlayerRecords.begin(); it != m_recentPlayerRecords.end(); ) {
            if (it->mainName == m_players[i].name) {
                it = m_recentPlayerRecords.erase(it);
            }
            else {
                ++it;
            }
        }

        RecentPlayerRecord r;
        r.mainName = m_players[i].name;
        for (const auto& a : m_players[i].aliases) {
            r.aliases.push_back(a.name);
        }
        m_recentPlayerRecords.push_front(r);
    }

    // 2. 把本地库 (m_aliasDB) 里的其他人也加载进去作为候选项
    for (auto it = m_aliasDB.begin(); it != m_aliasDB.end(); ++it) {
        bool exists = false;
        for (const auto& r : m_recentPlayerRecords) {
            if (r.mainName == it->first) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            RecentPlayerRecord r;
            r.mainName = it->first;

            // 将长字符串 "(小号1)(小号2)" 切割装入数组
            int curPos = 0;
            CString token = it->second.Tokenize(L" ()（）", curPos);
            while (token != L"") {
                r.aliases.push_back(token);
                token = it->second.Tokenize(L" ()（）", curPos);
            }
            m_recentPlayerRecords.push_back(r);
        }
    }

    // 3. 将最终排序好的数据渲染到 UI 列表框中
    if (m_listRecentPlayers.m_hWnd) {
        int lastTopIndex = m_listRecentPlayers.GetTopIndex();
        m_listRecentPlayers.ResetContent();

        // ==========================================
        // 【新增】：直接把标题作为列表的 第0项 固定塞进去！
        // ==========================================
        m_listRecentPlayers.AddString(L"📋 === 选手库信息 (点击填入) ===");

        // 真实选手数据从 第1项 开始往下排
        for (const auto& record : m_recentPlayerRecords) {
            m_listRecentPlayers.AddString(record.mainName);
        }

        // 恢复之前的滚动条位置
        if (lastTopIndex != LB_ERR && lastTopIndex < m_listRecentPlayers.GetCount()) {
            m_listRecentPlayers.SetTopIndex(lastTopIndex);
        }
    }
}

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;

    // 调试文字显示
    CString h;
    {
        std::lock_guard<std::mutex> lk(m_debugMutex);
        h = m_debugOcrResult;
    }
    if (!h.IsEmpty()) {
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 255, 0));
        CFont f;
        f.CreatePointFont(105, L"黑体");
        CFont* of = dc.SelectObject(&f);
        CRect tR(0, 0, 0, 0);
        dc.DrawText(h, &tR, DT_LEFT | DT_TOP | DT_CALCRECT);
        CRect cr(m_previewRect.left + 15,
            m_previewRect.bottom - 25 - tR.Height(),
            m_previewRect.left + 15 + tR.Width(),
            m_previewRect.bottom - 25);
        cr.InflateRect(8, 8);
        dc.FillSolidRect(&cr, RGB(25, 25, 25));
        dc.DrawText(h, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(of);
    }

    // 左右 OCR 预处理的缩略图显示
    HBITMAP hL = NULL, hR = NULL;
    {
        std::lock_guard<std::mutex> lkBmp(m_ocrRecordMutex);
        if (m_viewIndexLeft >= 0 && m_viewIndexLeft < (int)m_ocrRecordsLeft.size())
            hL = m_ocrRecordsLeft[m_viewIndexLeft].hBmp;
        else if (!m_ocrRecordsLeft.empty())
            hL = m_ocrRecordsLeft.back().hBmp;

        if (m_viewIndexRight >= 0 && m_viewIndexRight < (int)m_ocrRecordsRight.size())
            hR = m_ocrRecordsRight[m_viewIndexRight].hBmp;
        else if (!m_ocrRecordsRight.empty())
            hR = m_ocrRecordsRight.back().hBmp;
    }
    HBITMAP arr[2] = { hL, hR };
    int cY = m_previewRect.bottom - 20;
    int tW = max(180, m_previewRect.Width() / 4);
    for (int i = 1; i >= 0; i--) {
        if (arr[i]) {
            BITMAP bm;
            GetObject(arr[i], sizeof(BITMAP), &bm);
            int sW = (int)(bm.bmWidth * 0.70);
            int sH = bm.bmHeight;
            int sX = (i == 0) ? 0 : (bm.bmWidth - sW);
            int dW = tW;
            int dH = (int)((float)sH / sW * dW);
            cY -= dH;
            int iX = m_previewRect.right - 15 - dW;
            HDC hM = CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oB = SelectObject(hM, arr[i]);
            COLORREF bC = (i == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
            dc.FillSolidRect(iX - 2, cY - 2, dW + 4, dH + 4, bC);
            dc.SetStretchBltMode(HALFTONE);
            dc.StretchBlt(iX, cY, dW, dH, CDC::FromHandle(hM), sX, 0, sW, sH, SRCCOPY);
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(bC);
            CFont fM;
            fM.CreatePointFont(90, L"微软雅黑");
            CFont* oM = dc.SelectObject(&fM);
            dc.TextOut(iX, cY - 18, i == 0 ? L"左侧提取区" : L"右侧提取区");
            dc.SelectObject(oM);
            cY -= 25;
            SelectObject(hM, oB);
            DeleteDC(hM);
        }
    }

    // ----- 绘制鼠标坐标采集的绿点（手动采集模式） -----
    CPen pPoint(PS_SOLID, 2, RGB(0, 255, 0));
    dc.SelectStockObject(NULL_BRUSH);
    CPen* pOldPointPen = dc.SelectObject(&pPoint);
    for (size_t i = 0; i < m_selectPts.size(); i++) {
        int px = m_previewRect.left + (int)((m_selectPts[i].x / 10000.0f) * m_previewRect.Width());
        int py = m_previewRect.top + (int)((m_selectPts[i].y / 10000.0f) * m_previewRect.Height());
        dc.Ellipse(px - 3, py - 3, px + 3, py + 3);
    }
    dc.SelectObject(pOldPointPen);

    // ===================================================
    // 极简死亡 X 调试显示
    // 未检测到死亡：沿用原来的青蓝色 X；检测到死亡：同一个 X 改成红色。
    // 不再额外绘制圆点、文字、总览面板，避免遮挡游戏画面。
    // ===================================================
    if (m_previewRect.Width() > 0 && m_previewRect.Height() > 0) {
        DeathXDebugState snap = {};
        {
            std::lock_guard<std::mutex> dbgLock(g_deathXDebugMutex);
            snap = g_deathXDebug;
        }

        dc.SelectStockObject(NULL_BRUSH);

        // 遍历 8 个关键死亡 X 中心点
        for (int i = 0; i < DEATH_POINT_COUNT; i++) {
            ScorePointF logicPt = GetDeathLogicPoint(i);
            float cx = logicPt.x;
            float cy = logicPt.y;

            // 基础射线的伸展步长，与底层检测代码保持同步
            float stepX = 0.015f / 4.0f;
            float stepY = 0.025f / 4.0f;

            // 替补席小 X 缩小采样范围
            if (i != DP_LEFT_ACTIVE && i != DP_RIGHT_ACTIVE) {
                stepX /= 2.0f;
                stepY /= 2.0f;
            }

            COLORREF xColor = snap.dead[i] ? RGB(255, 0, 0) : RGB(0, 255, 255); // 上方 8 个检测位：触发画红色，未触发画蓝色
            CPen centerPen(PS_SOLID, 2, xColor);
            CPen* pOldPen = dc.SelectObject(&centerPen);

            // 绘制: 左上 (\) 到 右下 (\)
            int tl_x = m_previewRect.left + (int)((cx - 4 * stepX) * m_previewRect.Width());
            int tl_y = m_previewRect.top + (int)((cy - 4 * stepY) * m_previewRect.Height());
            int br_x = m_previewRect.left + (int)((cx + 4 * stepX) * m_previewRect.Width());
            int br_y = m_previewRect.top + (int)((cy + 4 * stepY) * m_previewRect.Height());
            dc.MoveTo(tl_x, tl_y);
            dc.LineTo(br_x, br_y);

            // 绘制: 右上 (/) 到 左下 (/)
            int tr_x = m_previewRect.left + (int)((cx + 4 * stepX) * m_previewRect.Width());
            int tr_y = m_previewRect.top + (int)((cy - 4 * stepY) * m_previewRect.Height());
            int bl_x = m_previewRect.left + (int)((cx - 4 * stepX) * m_previewRect.Width());
            int bl_y = m_previewRect.top + (int)((cy + 4 * stepY) * m_previewRect.Height());
            dc.MoveTo(tr_x, tr_y);
            dc.LineTo(bl_x, bl_y);

            for (int k = 0; k < snap.hitCount[i] && k < 16; k++) {
                int dirTag = snap.hitDir[i][k];
                COLORREF hitColor = RGB(255, 255, 0);
                if (dirTag == 1) hitColor = RGB(0, 255, 0);
                else if (dirTag == 2) hitColor = RGB(255, 0, 255);
                else if (dirTag == 3) hitColor = RGB(255, 128, 0);

                int hx = m_previewRect.left + (int)(snap.hitX[i][k] * m_previewRect.Width());
                int hy = m_previewRect.top + (int)(snap.hitY[i][k] * m_previewRect.Height());

                CPen hitPen(PS_SOLID, 1, RGB(0, 0, 0));
                CBrush hitBrush(hitColor);
                CPen* pOldHitPen = dc.SelectObject(&hitPen);
                CBrush* pOldHitBrush = dc.SelectObject(&hitBrush);
                dc.Ellipse(hx - 4, hy - 4, hx + 4, hy + 4);
                dc.SelectObject(pOldHitBrush);
                dc.SelectObject(pOldHitPen);
            }

            if (snap.centerGate[i]) {
                int ccx = m_previewRect.left + (int)(cx * m_previewRect.Width());
                int ccy = m_previewRect.top + (int)(cy * m_previewRect.Height());
                CPen centerGatePen(PS_SOLID, 1, RGB(255, 255, 255));
                CPen* pOldGatePen = dc.SelectObject(&centerGatePen);
                dc.MoveTo(ccx - 4, ccy - 4); dc.LineTo(ccx + 4, ccy - 4);
                dc.LineTo(ccx + 4, ccy + 4); dc.LineTo(ccx - 4, ccy + 4);
                dc.LineTo(ccx - 4, ccy - 4);
                dc.SelectObject(pOldGatePen);
            }
            dc.SelectObject(pOldPen);
        }
    }

    // ===================================================
    // 左右放大框：把每个 X 的检查区域放大显示，方便观察中心点和射线命中位置。
    // 左框显示左侧 4 个 X，蓝框显示右侧 4 个 X。
    // ===================================================
    {
        DeathXDebugState snap = {};
        {
            std::lock_guard<std::mutex> dbgLock(g_deathXDebugMutex);
            snap = g_deathXDebug;
        }
        const wchar_t* slotNames[4] = { L"主", L"1", L"2", L"3" };
        CRect teamPanels[2];

        int panelW = max(300, m_previewRect.Width() / 4);
        int panelH = max(250, m_previewRect.Height() / 3);
        int panelY = m_previewRect.top + max(90, m_previewRect.Height() / 5);

        teamPanels[0] = CRect(m_previewRect.left + 10, panelY,
            m_previewRect.left + 10 + panelW, panelY + panelH);
        teamPanels[1] = CRect(m_previewRect.left + m_previewRect.Width() / 2 - panelW / 2, panelY,
            m_previewRect.left + m_previewRect.Width() / 2 + panelW / 2, panelY + panelH);

        std::lock_guard<std::mutex> bmpLock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);

            for (int team = 0; team < 2; ++team) {
                COLORREF panelColor = (team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
                CRect panel = teamPanels[team];
                dc.FillSolidRect(&panel, RGB(16, 16, 16));
                dc.FillSolidRect(panel.left - 2, panel.top - 2, panel.Width() + 4, 2, panelColor);
                dc.FillSolidRect(panel.left - 2, panel.bottom, panel.Width() + 4, 2, panelColor);
                dc.FillSolidRect(panel.left - 2, panel.top - 2, 2, panel.Height() + 4, panelColor);
                dc.FillSolidRect(panel.right, panel.top - 2, 2, panel.Height() + 4, panelColor);

                CString title = (team == 0) ? L"左侧X检测放大区" : L"右侧X检测放大区";
                dc.SetBkMode(TRANSPARENT);
                dc.SetTextColor(panelColor);
                CFont fTitle;
                fTitle.CreatePointFont(95, L"微软雅黑");
                CFont* oldTitleFont = dc.SelectObject(&fTitle);
                dc.TextOut(panel.left + 8, panel.top - 22, title);
                dc.SelectObject(oldTitleFont);

                int innerPad = 8;
                int titlePad = 8;
                int gridTop = panel.top + innerPad + titlePad;
                int cellGap = 8;

                // “凸”字布局：
                // 上面居中放正在打的玩家（主位 / 当前在场位），
                // 下面一排放还没上场或已经死亡的 3 个位置。
                int usableW = panel.Width() - innerPad * 2;
                int usableH = panel.Height() - innerPad * 2 - titlePad;
                int topCellH = max(70, usableH * 5 / 11);
                int bottomCellH = max(58, usableH - topCellH - cellGap);
                int topCellW = max(96, usableW * 5 / 9);
                int bottomCellW = max(54, (usableW - cellGap * 2) / 3);
                int topCellX = panel.left + innerPad + (usableW - topCellW) / 2;
                int bottomY = gridTop + topCellH + cellGap;

                for (int local = 0; local < 4; ++local) {
                    // 放大格位置 local：0=上方主位，1/2/3=下方左/中/右。
                    // 右侧 HUD 的下方 3 个位置在画面上是从中间向右排列，和左侧方向相反；
                    // 因此右侧下方显示时把两端调换为 3/2/1，保证和上方实际 HUD 顺序一致。
                    int sourceLocal = local;
                    if (team == 1 && local > 0) {
                        sourceLocal = 4 - local;
                    }
                    int idx = team * 4 + sourceLocal;
                    CRect cell;
                    if (local == 0) {
                        cell = CRect(topCellX,
                            gridTop,
                            topCellX + topCellW,
                            gridTop + topCellH);
                    }
                    else {
                        int bottomCol = local - 1;
                        int x = panel.left + innerPad + bottomCol * (bottomCellW + cellGap);
                        cell = CRect(x,
                            bottomY,
                            x + bottomCellW,
                            bottomY + bottomCellH);
                    }

                    ScorePointF logicPt = GetDeathLogicPoint(idx);
                    int srcCX = (int)(logicPt.x * m_w);
                    int srcCY = (int)(logicPt.y * m_h);
                    bool isActive = (idx == DP_LEFT_ACTIVE || idx == DP_RIGHT_ACTIVE);

                    // 这里只放大“蓝色 X 实际检查范围”，不再带大量周边无效画面。
                    float stepX = 0.015f / 4.0f;
                    float stepY = 0.025f / 4.0f;
                    if (!isActive) {
                        stepX /= 2.0f;
                        stepY /= 2.0f;
                    }

                    int rayHalfW = max(10, (int)(4.0f * stepX * m_w));
                    int rayHalfH = max(10, (int)(4.0f * stepY * m_h));
                    int padW = isActive ? 12 : 8;
                    int padH = isActive ? 12 : 8;
                    int cropHalfW = rayHalfW + padW;
                    int cropHalfH = rayHalfH + padH;
                    int cropW = max(28, cropHalfW * 2);
                    int cropH = max(28, cropHalfH * 2);
                    int sx = max(0, min(m_w - cropW, srcCX - cropHalfW));
                    int sy = max(0, min(m_h - cropH, srcCY - cropHalfH));

                    dc.FillSolidRect(&cell, RGB(8, 8, 8));
                    int oldMode = dc.SetStretchBltMode(HALFTONE);
                    dc.StretchBlt(cell.left + 1, cell.top + 1, cell.Width() - 2, cell.Height() - 2,
                        CDC::FromHandle(hBmpDC), sx, sy, cropW, cropH, SRCCOPY);
                    dc.SetStretchBltMode(oldMode);

                    COLORREF cellBorder = snap.dead[idx] ? RGB(255, 0, 0) : panelColor;
                    dc.FillSolidRect(cell.left, cell.top, cell.Width(), 1, cellBorder);
                    dc.FillSolidRect(cell.left, cell.bottom - 1, cell.Width(), 1, cellBorder);
                    dc.FillSolidRect(cell.left, cell.top, 1, cell.Height(), cellBorder);
                    dc.FillSolidRect(cell.right - 1, cell.top, 1, cell.Height(), cellBorder);

                    CString cellTitle;
                    cellTitle.Format(L"%s%s", slotNames[sourceLocal], snap.dead[idx] ? L" 死" : L" 活");
                    dc.SetTextColor(cellBorder);
                    CFont fCell;
                    fCell.CreatePointFont(82, L"微软雅黑");
                    CFont* oldCellFont = dc.SelectObject(&fCell);
                    dc.TextOut(cell.left + 4, cell.top + 2, cellTitle);
                    dc.SelectObject(oldCellFont);

                    auto mapToCellX = [&](float nx) -> int {
                        return cell.left + 1 + (int)(((nx * m_w) - sx) / (float)cropW * (cell.Width() - 2));
                    };
                    auto mapToCellY = [&](float ny) -> int {
                        return cell.top + 1 + (int)(((ny * m_h) - sy) / (float)cropH * (cell.Height() - 2));
                    };

                    int tlx = mapToCellX(logicPt.x - 4 * stepX);
                    int tly = mapToCellY(logicPt.y - 4 * stepY);
                    int brx = mapToCellX(logicPt.x + 4 * stepX);
                    int bry = mapToCellY(logicPt.y + 4 * stepY);
                    int trx = mapToCellX(logicPt.x + 4 * stepX);
                    int try_ = mapToCellY(logicPt.y - 4 * stepY);
                    int blx = mapToCellX(logicPt.x - 4 * stepX);
                    int bly = mapToCellY(logicPt.y + 4 * stepY);

                    // 下方放大框 8 个检测位：未触发时都不画蓝色 X，只在触发死亡时画红色 X。
                    bool drawCellX = snap.dead[idx];
                    if (drawCellX) {
                        COLORREF xColor = RGB(255, 0, 0);
                        CPen cellXPen(PS_SOLID, 2, xColor);
                        CPen* oldXP = dc.SelectObject(&cellXPen);
                        dc.MoveTo(tlx, tly); dc.LineTo(brx, bry);
                        dc.MoveTo(trx, try_); dc.LineTo(blx, bly);
                        dc.SelectObject(oldXP);
                    }

                    if (snap.centerGate[idx]) {
                        int ccx = mapToCellX(logicPt.x);
                        int ccy = mapToCellY(logicPt.y);
                        CPen gatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        CPen* oldGatePen = dc.SelectObject(&gatePen);
                        dc.MoveTo(ccx - 4, ccy - 4); dc.LineTo(ccx + 4, ccy - 4);
                        dc.LineTo(ccx + 4, ccy + 4); dc.LineTo(ccx - 4, ccy + 4);
                        dc.LineTo(ccx - 4, ccy - 4);
                        dc.SelectObject(oldGatePen);
                    }

                    for (int k = 0; k < snap.hitCount[idx] && k < 16; ++k) {
                        int dirTag = snap.hitDir[idx][k];
                        COLORREF hitColor = RGB(255, 255, 0);
                        if (dirTag == 1) hitColor = RGB(0, 255, 0);
                        else if (dirTag == 2) hitColor = RGB(255, 0, 255);
                        else if (dirTag == 3) hitColor = RGB(255, 128, 0);

                        int hx = mapToCellX(snap.hitX[idx][k]);
                        int hy = mapToCellY(snap.hitY[idx][k]);
                        CPen hitPen(PS_SOLID, 1, RGB(0, 0, 0));
                        CBrush hitBrush(hitColor);
                        CPen* oldHitPen = dc.SelectObject(&hitPen);
                        CBrush* oldHitBrush = dc.SelectObject(&hitBrush);
                        dc.Ellipse(hx - 3, hy - 3, hx + 4, hy + 4);
                        dc.SelectObject(oldHitBrush);
                        dc.SelectObject(oldHitPen);
                    }
                }
            }

            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }
    }


    // ===================================================
    // 绘制 10 倍像素级显微镜
    // ===================================================
    CPoint pt;
    GetCursorPos(&pt);
    ScreenToClient(&pt);
    if (m_previewRect.PtInRect(pt)) {
        int origX = (int)(((float)(pt.x - m_previewRect.left) / m_previewRect.Width()) * m_w);
        int origY = (int)(((float)(pt.y - m_previewRect.top) / m_previewRect.Height()) * m_h);

        int magW = 160, magH = 160, srcSize = 16;

        int drawX = m_previewRect.left + 10;
        int drawY = m_previewRect.top + 10;
        if (pt.x < m_previewRect.left + m_previewRect.Width() / 2 &&
            pt.y < m_previewRect.top + m_previewRect.Height() / 2) {
            drawX = m_previewRect.right - magW - 10;
        }

        dc.FillSolidRect(drawX - 2, drawY - 2, magW + 4, magH + 4, RGB(255, 255, 255));

        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);

            int oldMode = dc.SetStretchBltMode(COLORONCOLOR);
            dc.StretchBlt(drawX, drawY, magW, magH,
                CDC::FromHandle(hBmpDC),
                origX - srcSize / 2, origY - srcSize / 2,
                srcSize, srcSize, SRCCOPY);
            dc.SetStretchBltMode(oldMode);

            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }

        // 红色准星
        CPen crossPen(PS_SOLID, 1, RGB(255, 0, 0));
        CPen* pOldPen = dc.SelectObject(&crossPen);
        dc.MoveTo(drawX + magW / 2, drawY);
        dc.LineTo(drawX + magW / 2, drawY + magH);
        dc.MoveTo(drawX, drawY + magH / 2);
        dc.LineTo(drawX + magW, drawY + magH / 2);
        dc.SelectObject(pOldPen);

        // 进度提示
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 255, 0));
        CString tip;
        tip.Format(L"已采: %d/%d (右键撤销)", (int)m_selectPts.size(), DEATH_POINT_COUNT);
        dc.TextOut(drawX + 5, drawY + magH - 25, tip);
    }
}

// 【修改】：点击说明按钮弹出的消息框，详细更新功能手册
void CDNFGameCaptureDlg::OnBnClickedHelp() {
    CString msg = L"💡 DNF击杀统计 - 终极使用说明书\r\n\r\n"
        L"【一、 智能录入 (顶部输入框)】\r\n"
        L"1. 批量添加：支持“主号(小号1)(小号2)”格式，按回车或点击[添加]解析入库。\r\n"
        L"2. 智能补全：输入主号后打出左括号“(”，系统会自动去“历史数据库”里检索并秒补齐小号。\r\n"
        L"3. 队伍防呆：如果一侧队伍满员，打字时会自动将人员分配到对面未满队伍。\r\n\r\n"
        L"【二、 树状图左键操作 (双击直接修改)】\r\n"
        L"1. 改比分：左键慢速双击【红队/蓝队】根节点，直接输入数字即可修改大比分。\r\n"
        L"2. 改人名/战绩：左键慢速双击任意【主号/小号】，像重命名文件一样修改名字或“击杀/死亡/AK”数值。系统防重名，且会自动绑定数据库。\r\n"
        L"3. 展开折叠：点击 [+] / [-] 可以自由隐藏或显示小号，让界面更清爽。\r\n\r\n"
        L"【三、 树状图右键菜单 (全能管理)】\r\n"
        L"1. 队伍管理：在【红队/蓝队】右键，可 +1/-1/归零大比分，或一键清空该队。\r\n"
        L"2. 战绩容错：在【主号】右键，可手动对“击杀、死亡、AK”进行加减(+1/-1)操作。\r\n"
        L"3. 一键换边：在【主号】右键，可将该玩家及旗下所有小号【移动】到对面阵营（如果对面满员，自动触发位置互换）。\r\n"
        L"4. 智能删除：在【小号】右键，可以选择仅从本局移除，或者【彻底删除】（连同自动补全记忆一并抹除）。\r\n\r\n"
        L"【四、 OBS 与直播防卡死同步】\r\n"
        L"无论你是添加小号、还是修改了人头、或是系统自动识图抓取了击杀，软件都会【在毫秒内】自动更新输出目录下的 TXT 文件！OBS 即可实现零延迟自动跳分！\r\n\r\n"
        L"------------------------------------\r\n"
        L"🛠️ 调试快捷键：(用于无比赛时测试画图)\r\n"
        L"Ctrl+F8 : 强制触发【红队】击杀一次\r\n"
        L"Ctrl+F9 : 强制触发【蓝队】击杀一次";

    MessageBox(msg, L"最新操作逻辑与指南", MB_ICONINFORMATION);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nID) {
    if (nID == 1 && m_bIsRunning) {
        Capture();

        // ★ 颜色检测降频：每 240ms 检测一次，不是每 50ms
        static DWORD s_lastColorCheck = 0;
        DWORD now = GetTickCount();
        if (now - s_lastColorCheck >= POLL_COLOR_INTERVAL) {
            s_lastColorCheck = now;
            CheckColorTrigger();
        }
    }
    else if (nID == 2) {
        m_bCanTrigger = TRUE;
        KillTimer(2);
    }
    else if (nID == 4) {
        m_bCanTriggerTeamScore = TRUE;
        KillTimer(4);
    }
    else if (nID == 3 && m_bIsRunning) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp && m_w > 0 && m_h > 0) {
            // ★ 旧帧先释放
            if (m_historyBmps[m_historyIdx]) {
                ::DeleteObject(m_historyBmps[m_historyIdx]);
                m_historyBmps[m_historyIdx] = nullptr;
            }
            // ★ 一句话搞定，不用手动创建/销毁 DC
            m_historyBmps[m_historyIdx] = (HBITMAP)::CopyImage(
                m_bmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            m_historyIdx = (m_historyIdx + 1) % MAX_HISTORY_FRAMES;
        }
    }
    else if (nID == 5) {
        std::lock_guard<std::mutex> lkLog(g_visualLogMutex);
        if (!g_visualLogs.empty() && m_editVisualLogs.m_hWnd) {
            // ★ 超过 300 行时砍掉前半，防止再次撞上限
            int lineCount = m_editVisualLogs.GetLineCount();
            if (lineCount > 300) {
                int charIdx = m_editVisualLogs.LineIndex(lineCount - 150);
                m_editVisualLogs.SetSel(0, charIdx);
                m_editVisualLogs.ReplaceSel(L"");
            }

            for (const auto& log : g_visualLogs) {
                int len = m_editVisualLogs.GetWindowTextLength();
                m_editVisualLogs.SetSel(len, len);
                CHARFORMAT cf;
                ZeroMemory(&cf, sizeof(cf));
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR;
                cf.crTextColor = log.color;
                m_editVisualLogs.SetSelectionCharFormat(cf);
                m_editVisualLogs.ReplaceSel(log.text + L"\r\n");
            }
            m_editVisualLogs.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);
            g_visualLogs.clear();
        }
    }
    // ==========================================
    // 【Timer 6】:独立预览定时器 + 终极隐藏保护装甲
    // ==========================================
    else if (nID == 6) {
        // 🚨 强行隐藏装甲：开机 200 毫秒后，管你系统怎么唤醒，直接把黑框按下去隐藏！
        static bool s_bFirstHide = true;
        if (s_bFirstHide) {
            ShowWindow(SW_HIDE);
            s_bFirstHide = false;
        }

        if (!m_bIsRunning) {
            Capture();
        }
    }
    // ==========================================
    // 【Timer 7】: 终极系统级轮询 (无视任何消息屏蔽)
    // ==========================================
    else if (nID == 7) {
        static int s_idleSeconds = 0;          // 闲置秒数
        static bool s_hasFolded = true;        // 默认 true，防止刚开软件还没动就乱折叠

        // 1. 问系统：现在屏幕最前面的是不是咱们的软件？
        HWND hForeground = ::GetForegroundWindow();
        bool bIsOurAppFocused = (hForeground == m_hWnd || ::IsChild(m_hWnd, hForeground));

        // 2. 问系统：用户最近一次摸鼠标或【敲键盘】距离现在多少毫秒？
        LASTINPUTINFO lii;
        lii.cbSize = sizeof(LASTINPUTINFO);
        ::GetLastInputInfo(&lii);
        DWORD idleMs = ::GetTickCount() - lii.dwTime;

        // 3. 判断是否“正在操作”：焦点在咱们软件上，且最近 1.5 秒内敲过键盘或动过鼠标
        if (bIsOurAppFocused && idleMs < 1500) {
            s_idleSeconds = 0;
            s_hasFolded = false;
        }
        else {
            s_idleSeconds++;
        }

        // 4. 满 10 秒闲置或失去焦点执行动作
        if (s_idleSeconds >= 10) {
            if (!s_hasFolded && m_treePlayers.m_hWnd) {
                bool actuallyFoldedSomething = false;

                HTREEITEM hRoot = m_treePlayers.GetRootItem();
                while (hRoot) {
                    HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
                    while (hChild) {
                        if (m_treePlayers.GetItemState(hChild, TVIS_EXPANDED) & TVIS_EXPANDED) {
                            m_treePlayers.Expand(hChild, TVE_COLLAPSE);
                            actuallyFoldedSomething = true;
                        }
                        hChild = m_treePlayers.GetNextSiblingItem(hChild);
                    }
                    hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
                }

                s_hasFolded = true;

                if (actuallyFoldedSomething) {
                    AppLog(L"💤 [界面收起] 失去焦点或闲置10秒，已自动折叠", RGB(150, 150, 150));
                }
            }
            s_idleSeconds = 10;
        }
    }
}

// ============================================================================
// 自动更新系统 (后台静默检测)
// 自动更新系统 (修复中文乱码 + 支持网页手动更新)
// 自动更新系统 (适配按行读取格式)
// ============================================================================
void CDNFGameCaptureDlg::CheckForUpdates(bool bSilent) {
    CString strCheckUrlV2 = UPDATE_CHECK_URL_V2;
    CString currentVersion = CURRENT_VERSION;

    wchar_t tempPath[MAX_PATH];
    GetTempPath(MAX_PATH, tempPath);
    CString tempFile;
    tempFile.Format(L"%supdate_check.txt", tempPath);

    ::DeleteUrlCacheEntry(strCheckUrlV2);

    // ==========================================
    // 拦截 1：下载失败，提前返回
    // ==========================================
    if (URLDownloadToFile(NULL, strCheckUrlV2, tempFile, 0, NULL) != S_OK) {
        if (!bSilent) MessageBox(L"连接更新服务器失败！", L"错误", MB_ICONERROR);
        return;
    }

    // ==========================================
    // 拦截 2：文件打开失败，提前返回
    // ==========================================
    CFile file;
    if (!file.Open(tempFile, CFile::modeRead)) {
        if (!bSilent) MessageBox(L"无法读取更新配置文件！", L"错误", MB_ICONERROR);
        return;
    }

    // --- 读取并转换 UTF-8 内容 ---
    ULONGLONG dwLength = file.GetLength();
    char* pBuf = new char[(size_t)dwLength + 1];  // 加上 (size_t) 消除警告
    memset(pBuf, 0, (size_t)dwLength + 1);        // 加上 (size_t) 消除警告
    file.Read(pBuf, (UINT)dwLength);
    file.Close();
    ::DeleteFile(tempFile);

    int nLen = MultiByteToWideChar(CP_UTF8, 0, pBuf, -1, NULL, 0);
    wchar_t* pWBuf = new wchar_t[nLen + 1];
    memset(pWBuf, 0, (nLen + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, pBuf, -1, pWBuf, nLen);

    CString content(pWBuf);
    delete[] pBuf;
    delete[] pWBuf;

    // ==========================================
    // 拦截 3：格式解析失败，提前返回
    // ==========================================
    content.Replace(L"\r\n", L"\n");
    int pos1 = content.Find(L'\n');
    int pos2 = content.Find(L'\n', pos1 + 1);

    if (pos1 == -1 || pos2 == -1) {
        if (!bSilent) MessageBox(L"更新文件格式解析失败！", L"错误", MB_ICONERROR);
        return;
    }

    CString serverVersion = content.Left(pos1);
    CString downloadUrl = content.Mid(pos1 + 1, pos2 - pos1 - 1);
    CString updateLog = content.Mid(pos2 + 1);

    serverVersion.Trim();
    downloadUrl.Trim();
    updateLog.Trim();

    // ==========================================
    // 拦截 4：没有新版本，提前返回
    // ==========================================
    // 原来的：
    // bool bHasUpdate = (serverVersion != currentVersion && !serverVersion.IsEmpty());

    int cmp = CompareVersion(serverVersion, currentVersion);
    bool bHasUpdate = (!serverVersion.IsEmpty() && cmp > 0);

    if (!bHasUpdate) {
        if (!bSilent) {
            if (cmp < 0)
                MessageBox(L"当前为测试版本，已高于线上正式版。", L"检查更新", MB_OK);
            else
                MessageBox(L"当前已是最新版本！", L"检查更新", MB_OK);
        }
        return;
    }

    // ==========================================
    // 终点：真正的更新处理逻辑
    // ==========================================
    if (currentVersion == BRIDGE_VERSION) {
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        AppLog(L"🔄 [桥接升级] 检测到这是过渡版本", RGB(255, 215, 0));
        AppLog(L"   正在自动升级到最新正式版,请稍候...", RGB(255, 215, 0));
        AppLog(L"   升级完成后软件会自动重启", RGB(255, 215, 0));
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        Sleep(800);  // 让用户有时间看到提示
        DownloadAndApplyUpdate(downloadUrl);
    }
    //else if (bSilent) {
    //    // 【普通版 - 后台静默检测】：不打扰用户,只在日志里提示有新版本
    //    CString logMsg;
    //    logMsg.Format(L"💡 [发现新版本] 服务器版本 %s,点击菜单可手动更新", serverVersion.GetString());
    //    AppLog(logMsg, RGB(100, 200, 255));
    //    // 注意:这里不调用 DownloadAndApplyUpdate,等用户主动点"检查更新"
    //}
    //else {
        // 【普通版】：无论是后台检测还是手动检测，只要有新版本，一律弹窗！
            CString msg;
            msg.Format(L"发现新版本: %s\n\n更新内容:\n%s\n\n是否立即更新？", serverVersion, updateLog);

            // ⬇️ 【修改点】：把 MessageBox 换成 ShowCenteredMsgBox
            if (ShowCenteredMsgBox(msg, L"发现新版本", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES) {
                DownloadAndApplyUpdate(downloadUrl);
            }
            else {
                CString logMsg;
                logMsg.Format(L"💡 [发现新版本] 服务器版本 %s，您已取消更新。右键托盘可随时更新。", serverVersion.GetString());
                AppLog(logMsg, RGB(100, 200, 255));
            }
    //}
}

void CDNFGameCaptureDlg::DownloadAndApplyUpdate(CString url) {
    if (m_status.m_hWnd) 
        ::SetWindowText(m_status.GetSafeHwnd(), L"正在下载更新包...");// 加上全局作用域和安全的句柄

    wchar_t p[MAX_PATH];
    GetModuleFileName(NULL, p, MAX_PATH);
    CString cp(p);
    CString d = cp.Left(cp.ReverseFind(L'\\') + 1);

    CString t = d + L"update_temp.zip";
    CString b = d + L"update.bat";
    CString engine = d + L"7za.exe";

    // 1. 强制清理引擎和更新包的本地缓存
    CString engineUrl = L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/7z/7za.exe"; // 【请修改这里】
    ::DeleteUrlCacheEntry(engineUrl);
    ::DeleteUrlCacheEntry(url);

    // 2. 先下解压引擎，再下真实的 ZIP 压缩包
    URLDownloadToFile(NULL, engineUrl, engine, 0, NULL);
    if (URLDownloadToFile(NULL, url, t, 0, NULL) != S_OK) {
        MessageBox(L"下载更新包失败，请检查网络！", L"更新失败", MB_ICONERROR);
        if (m_status.m_hWnd) m_status.SetWindowText(L"就绪");
        return;
    }

    // 3. 生成强力解压替换脚本 (兼容 Win7~Win11)
    CFile f;
    if (f.Open(b, CFile::modeCreate | CFile::modeWrite)) {
        CString s;
        s.Format(L"@echo off\r\n"
            L":Retry\r\n"
            L"ping 127.0.0.1 -n 2 > nul\r\n"
            L"del \"%s\" 2>nul\r\n"
            L"if exist \"%s\" goto Retry\r\n"
            L"\"%s\" x \"update_temp.zip\" -y > nul\r\n"
            L"del \"update_temp.zip\"\r\n"
            L"ping 127.0.0.1 -n 2 > nul\r\n"                       // 等 1 秒让文件系统稳定
            L"start \"\" \"%s\"\r\n"
            L"ping 127.0.0.1 -n 3 > nul\r\n"                       // 等新程序启动窗口
            L"powershell -NoProfile -Command \""
            L"$w=(Get-Process -Name 'DNFGameCapture' -ErrorAction SilentlyContinue | "
            L"Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1).MainWindowHandle; "
            L"if($w){ "
            L"Add-Type '[DllImport(\\\"user32.dll\\\")]public static extern bool SetForegroundWindow(IntPtr h);' "
            L"-Name W -Namespace N; "
            L"[N.W]::SetForegroundWindow($w) }\"\r\n"
            L"del \"%%~f0\"\r\n",
            cp.GetString(), cp.GetString(), engine.GetString(), cp.GetString());

        std::string a = CW2A(s, CP_OEMCP);
        f.Write(a.c_str(), (UINT)a.length());
        f.Close();
    }

    // 4. 释放多开锁并执行换包脚本
    if (m_hSingleInstanceMutex) {
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = NULL;
    }

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = b;
    sei.nShow = SW_HIDE;

    if (ShellExecuteEx(&sei)) {
        ShowWindow(SW_HIDE);
        exit(0);
    }
}

void CDNFGameCaptureDlg::LoadAliasDB() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_db.ini";

    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        int len = (int)file.GetLength();
        if (len > 0) {
            char* buf = new char[len + 1];
            file.Read(buf, len); buf[len] = 0;
            CString content = CA2W(buf, CP_UTF8);
            delete[] buf;

            m_aliasDB.clear(); // 先清空内存

            int pos = 0;
            while (pos < content.GetLength()) {
                int nl = content.Find(L'\n', pos);
                CString line = (nl != -1) ? content.Mid(pos, nl - pos) : content.Mid(pos);
                pos = (nl != -1) ? nl + 1 : content.GetLength();
                line.Remove(L'\r'); line.Trim();

                int eq = line.Find(L'=');
                if (eq != -1) {
                    CString mainName = line.Left(eq);
                    CString aliases = line.Mid(eq + 1);

                    mainName.Trim();
                    aliases.Trim();

                    if (!mainName.IsEmpty()) {
                        // 🚨 纯净读取：不管库里有没有重复，直接装载进内存，不弹窗不管它
                        m_aliasDB[mainName] = aliases;
                    }
                }
            }
        }
        file.Close();
    }

    // 加载完后刷新左下角列表
    UpdateAndRefreshRecentList();
}

void CDNFGameCaptureDlg::SaveAliasDB() {
    for (int i = 0; i < 8; i++) {
        CString mName = m_players[i].name;
        mName.Trim();

        if (!mName.IsEmpty() && !m_players[i].aliases.empty()) {
            // 获取数据库中已有的该主号的小号字符串（如果没有则为空）
            CString existingAliases = m_aliasDB[mName];

            for (const auto& a : m_players[i].aliases) {
                CString aName = a.name;
                aName.Trim();
                CString target1 = L"(" + aName + L")";
                CString target2 = L"（" + aName + L"）"; // 兼容中文括号

                // 【核心：增量合并过滤】如果数据库里没有这个小号，才追加进去
                if (existingAliases.Find(target1) == -1 && existingAliases.Find(target2) == -1) {
                    existingAliases += target1;
                }
            }
            // 更新内存中的数据库
            m_aliasDB[mName] = existingAliases;
        }
    }

    // 写入 ini 文件
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_db.ini";

    CString content;
    for (const auto& pair : m_aliasDB) {
        if (!pair.second.IsEmpty()) { // 防止写入空主号
            content += pair.first + L"=" + pair.second + L"\r\n";
        }
    }

    CFile file;
    if (file.Open(path, CFile::modeCreate | CFile::modeWrite)) {
        std::string utf8 = CW2A(content, CP_UTF8);
        file.Write(utf8.c_str(), (UINT)utf8.length());
        file.Close();
    }

    // 【新增】：保存数据库后，顺便刷新常用选手列表
    UpdateAndRefreshRecentList();
}

void CDNFGameCaptureDlg::OnChangeEditNamesInput() {
    static int s_prevLen = 0;
    static CString s_lastAutoExpandedName = L"";

    int curLen = m_editQuickAdd.GetWindowTextLength();
    bool isBackspace = (curLen < s_prevLen);
    s_prevLen = curLen;

    int nStart, nEnd;
    m_editQuickAdd.GetSel(nStart, nEnd);

    CString fullText;
    m_editQuickAdd.GetWindowText(fullText);

    int lineStart = 0;
    for (int i = nStart - 1; i >= 0; i--) {
        if (fullText[i] == L'\n') { lineStart = i + 1; break; }
    }
    int lineEnd = fullText.GetLength();
    for (int i = nStart; i < fullText.GetLength(); i++) {
        if (fullText[i] == L'\r' || fullText[i] == L'\n') { lineEnd = i; break; }
    }
    CString currentLine = fullText.Mid(lineStart, lineEnd - lineStart);

    // 解析出正在输入的主名 (遇到空格或括号即截断)
    int fP = -1;
    for (int i = 0; i < currentLine.GetLength(); i++) {
        wchar_t c = currentLine[i];
        if (c == L' ' || c == L'(' || c == L'（') { fP = i; break; }
    }
    CString typingMainName = (fP != -1) ? currentLine.Left(fP) : currentLine;
    // 强制洗掉可能粘连的非法字符
    typingMainName.Remove(L' '); typingMainName.Remove(L'('); typingMainName.Remove(L')'); typingMainName.Remove(L'（'); typingMainName.Remove(L'）');
    typingMainName.Trim();

    int redCount = 0, blueCount = 0;
    m_dataMutex.lock();
    for (int i = 0; i < 4; i++) if (!m_players[i].name.IsEmpty()) redCount++;
    for (int i = 4; i < 8; i++) if (!m_players[i].name.IsEmpty()) blueCount++;
    m_dataMutex.unlock();

    if (redCount >= 4 && blueCount < 4) {
        m_cmbTeamSelect.SetCurSel(1);
    }
    else if (blueCount >= 4 && redCount < 4) {
        m_cmbTeamSelect.SetCurSel(0);
    }

    auto ToggleTreeNode = [&](CString targetName, UINT action) {
        if (!m_treePlayers.m_hWnd || targetName.IsEmpty()) return;
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                CString text = m_treePlayers.GetItemText(hChild);
                int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                CString nodeName = (eqPos != -1) ? text.Left(eqPos) : text;
                nodeName.Trim();
                if (nodeName == targetName) { m_treePlayers.Expand(hChild, action); return; }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
        };

    int foundTeam = -1;
    m_dataMutex.lock();
    for (int i = 0; i < 8; i++) {
        if (!m_players[i].name.IsEmpty() && m_players[i].name == typingMainName) {
            foundTeam = m_players[i].team;
            break;
        }
    }
    m_dataMutex.unlock();

    if (foundTeam != -1) {
        m_cmbTeamSelect.SetCurSel(foundTeam);
        if (s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(typingMainName, TVE_EXPAND);
            s_lastAutoExpandedName = typingMainName;
        }
    }
    else {
        if (!s_lastAutoExpandedName.IsEmpty() && s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(s_lastAutoExpandedName, TVE_COLLAPSE);
            s_lastAutoExpandedName = L"";
        }
    }

    if (isBackspace || nStart == 0 || nStart > fullText.GetLength()) return;

    // 没打空格也没打括号，立即拦截
    wchar_t lastChar = fullText.GetAt(nStart - 1);
    if (lastChar != L'(' && lastChar != L'（' && lastChar != L' ') return;

    if (typingMainName.IsEmpty() || m_aliasDB.find(typingMainName) == m_aliasDB.end()) return;

    CString dbAliases = m_aliasDB[typingMainName];
    std::vector<CString> existAliases;

    m_dataMutex.lock();
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name == typingMainName) {
            for (const auto& a : m_players[i].aliases) existAliases.push_back(a.name);
            break;
        }
    }
    m_dataMutex.unlock();

    CString aliasesToInsert = L"";
    int c = 0;
    while (true) {
        CString tS = dbAliases.Mid(c);
        int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（');
        int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
        if (Lr == -1 || Rr == -1 || Rr <= Lr) break;

        CString aN = tS.Mid(Lr + 1, Rr - Lr - 1); aN.Trim();

        bool exists = false;
        for (const auto& ea : existAliases) {
            if (ea == aN) { exists = true; break; }
        }

        if (!exists && !aN.IsEmpty()) aliasesToInsert += L"(" + aN + L")";
        c += Rr + 1;
    }

    if (!aliasesToInsert.IsEmpty()) {
        m_editQuickAdd.SetSel(nStart - 1, nStart);
        // 如果用户打了空格触发了联想，我们顺手把那个空格替换掉，保证格式完美
        if (lastChar == L' ') {
            m_editQuickAdd.ReplaceSel(aliasesToInsert);
        }
        else {
            // 如果是打了括号触发的，把左括号覆盖掉
            m_editQuickAdd.ReplaceSel(aliasesToInsert);
        }
        s_prevLen = m_editQuickAdd.GetWindowTextLength();
    }
}

// ============================================================================
// 新版 GUI 核心逻辑：添加、树状渲染、右键菜单、存取配置
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedQuickAdd()
{
    CString text;
    m_editQuickAdd.GetWindowText(text);
    text.Trim();

    if (text.IsEmpty() || text == PLACEHOLDER_TEXT) return;

    int currentTeam = m_cmbTeamSelect.GetCurSel();
    if (currentTeam == CB_ERR) currentTeam = 0;

    int addMainCount = 0;
    int addAliasCount = 0;
    CString strTeamFullAlert = L"";
    CString strDupAliasAlert = L"";

    int curPos = 0;
    CString line = text.Tokenize(L"\r\n ", curPos);

    while (line != L"") {
        line.Trim();
        if (!line.IsEmpty()) {
            CString mainName = line;
            std::vector<CString> parsedAliases;

            int p1 = line.Find(L'(');
            int p2 = line.Find(L'（');
            int firstP = -1;
            if (p1 != -1 && p2 != -1) firstP = min(p1, p2);
            else if (p1 != -1) firstP = p1;
            else if (p2 != -1) firstP = p2;

            if (firstP != -1) {
                mainName = line.Left(firstP);
                CString aliasStr = line.Mid(firstP);
                int aPos = 0;
                CString aToken = aliasStr.Tokenize(L" ()（）", aPos);
                while (aToken != L"") {
                    parsedAliases.push_back(aToken);
                    aToken = aliasStr.Tokenize(L" ()（）", aPos);
                }
            }
            mainName.Trim();

            if (mainName.IsEmpty()) {
                line = text.Tokenize(L"\r\n ", curPos); continue;
            }

            CString aliasRuleError;
            if (!DnfValidateAliasListShortMeta(parsedAliases, aliasRuleError)) {
                strDupAliasAlert += L"【" + mainName + L"】小号格式不合格 -> " + aliasRuleError + L"\n";
                line = text.Tokenize(L"\r\n ", curPos);
                continue;
            }

            int targetIdx = -1;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { targetIdx = i; break; } }

            // 主号不参与名称匹配，所以新上场选手必须至少绑定 1 个小号。
            if (targetIdx == -1 && parsedAliases.empty()) {
                strDupAliasAlert += L"【" + mainName + L"】无法上场 -> 必须至少添加一个小号，格式：主号(小号)\n";
                line = text.Tokenize(L"\r\n ", curPos);
                continue;
            }

            // ================== 尝试添加到场上新位置 ==================
            if (targetIdx == -1) {
                // 🚨 新人上场前，进行严格碰撞检测！
                CString conflictInfo = CheckFieldConflict(mainName, parsedAliases, -1);
                if (!conflictInfo.IsEmpty()) {
                    strDupAliasAlert += L"【" + mainName + L"】无法上场 -> 冲突对象: " + conflictInfo + L"\n";
                    line = text.Tokenize(L"\r\n ", curPos);
                    continue;
                }

                int sI = (currentTeam == 0) ? 0 : 4, eI = (currentTeam == 0) ? 4 : 8;
                for (int i = sI; i < eI; i++) {
                    if (m_players[i].name.IsEmpty()) {
                        targetIdx = i;
                        m_players[i].name = mainName;
                        m_players[i].team = currentTeam;
                        addMainCount++;
                        AppLog(L"👤 [新增主号] [" + mainName + L"]", RGB(80, 180, 255));
                        break;
                    }
                }
                if (targetIdx == -1) strTeamFullAlert += L"[" + mainName + L"]\n";
            }

            // ================== 给场上已有选手追加小号 ==================
            if (targetIdx != -1) {
                // 🚨 即使是补小号，也要查重，防止串台
                CString conflictInfo = CheckFieldConflict(mainName, parsedAliases, targetIdx);
                if (!conflictInfo.IsEmpty()) {
                    strDupAliasAlert += L"【" + mainName + L"】追加小号失败 -> 冲突对象: " + conflictInfo + L"\n";
                    line = text.Tokenize(L"\r\n ", curPos);
                    continue;
                }

                for (const auto& aN : parsedAliases) {
                    bool exist = false;
                    for (const auto& oa : m_players[targetIdx].aliases) { if (oa.name == aN) { exist = true; break; } }
                    if (!exist) {
                        m_players[targetIdx].aliases.push_back({ aN });
                        addAliasCount++;
                        AppLog(L" ├ ➕追加小号: [" + aN + L"]", RGB(100, 255, 100));
                    }
                }
            }
        }
        line = text.Tokenize(L"\r\n ", curPos);
    }

    m_editQuickAdd.SetWindowText(L"");

    if (!strTeamFullAlert.IsEmpty() || !strDupAliasAlert.IsEmpty()) {
        CString msg = L"";
        if (!strTeamFullAlert.IsEmpty()) msg += L"【队伍已满】:\n" + strTeamFullAlert + L"\n";
        if (!strDupAliasAlert.IsEmpty()) msg += L"【撞名拦截】:\n" + strDupAliasAlert;
        MessageBox(msg, L"添加拦截报告", MB_ICONWARNING | MB_OK);
    }

    if (addMainCount > 0 || addAliasCount > 0) {
        SaveAliasDB();
        SyncDataToTree();
        RefreshDisplay();
        BroadcastStateToWeb();
    }
}

LRESULT CDNFGameCaptureDlg::OnWebCmdReceived(WPARAM wParam, LPARAM lParam)
{
    CString* pJsonStr = (CString*)lParam;
    if (!pJsonStr) return 0;

    try {
        std::string utf8Str = CW2A(*pJsonStr, CP_UTF8);
        json j = json::parse(utf8Str);
        std::string action = j["action"].get<std::string>();

        // 🚨 接收前端的心跳，立马给它推送全部数据！
        if (action == "page_ready") {
            BroadcastStateToWeb();
        }
        else if (action == "update_state") {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            auto& data = j["data"];

            m_totalScoreBlue = data["blueScore"].get<int>();
            m_totalScoreRed = data["redScore"].get<int>();

            auto& players = data["players"];
            if (players.is_array() && players.size() == 8) {
                // 🚨 Web端前4个是红队，写回 MFC 的 0-3
                for (int i = 0; i < 4; i++) {
                    int mfcIdx = i;
                    auto& p = players[i];
                    m_players[mfcIdx].name = CA2W(p["name"].get<std::string>().c_str(), CP_UTF8);
                    m_players[mfcIdx].team = 0;
                    m_players[mfcIdx].kills = p["kills"].get<int>();
                    m_players[mfcIdx].deaths = p["deaths"].get<int>();
                    m_players[mfcIdx].akCount = p["akCount"].get<int>();

                    m_players[mfcIdx].aliases.clear();
                    bool aliasFormatInvalid = false;
                    CString aliasFormatError;
                    for (auto& a : p["aliases"]) {
                        AliasData ad;
                        ad.name = CA2W(a.get<std::string>().c_str(), CP_UTF8);
                        ad.name.Trim();
                        if (!ad.name.IsEmpty()) {
                            CString oneAliasError;
                            if (!DnfValidateAliasShortMeta(ad.name, oneAliasError)) {
                                aliasFormatInvalid = true;
                                aliasFormatError = oneAliasError;
                                break;
                            }
                            m_players[mfcIdx].aliases.push_back(ad);
                        }
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && aliasFormatInvalid) {
                        AppLog(L"❌ [Web同步拦截] [" + m_players[mfcIdx].name + L"] 小号格式不合格：" + aliasFormatError, RGB(255, 120, 80));
                        m_players[mfcIdx].name.Empty();
                        m_players[mfcIdx].aliases.clear();
                        m_players[mfcIdx].kills = m_players[mfcIdx].deaths = m_players[mfcIdx].akCount = 0;
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && m_players[mfcIdx].aliases.empty()) {
                        AppLog(L"❌ [Web同步拦截] [" + m_players[mfcIdx].name + L"] 缺少小号，已拒绝上场。", RGB(255, 120, 80));
                        m_players[mfcIdx].name.Empty();
                        m_players[mfcIdx].kills = m_players[mfcIdx].deaths = m_players[mfcIdx].akCount = 0;
                    }
                }
                // 🚨 Web端后4个是蓝队，写回 MFC 的 4-7
                for (int i = 4; i < 8; i++) {
                    int mfcIdx = i;
                    auto& p = players[i];
                    m_players[mfcIdx].name = CA2W(p["name"].get<std::string>().c_str(), CP_UTF8);
                    m_players[mfcIdx].team = 1;
                    m_players[mfcIdx].kills = p["kills"].get<int>();
                    m_players[mfcIdx].deaths = p["deaths"].get<int>();
                    m_players[mfcIdx].akCount = p["akCount"].get<int>();

                    m_players[mfcIdx].aliases.clear();
                    bool aliasFormatInvalid = false;
                    CString aliasFormatError;
                    for (auto& a : p["aliases"]) {
                        AliasData ad;
                        ad.name = CA2W(a.get<std::string>().c_str(), CP_UTF8);
                        ad.name.Trim();
                        if (!ad.name.IsEmpty()) {
                            CString oneAliasError;
                            if (!DnfValidateAliasShortMeta(ad.name, oneAliasError)) {
                                aliasFormatInvalid = true;
                                aliasFormatError = oneAliasError;
                                break;
                            }
                            m_players[mfcIdx].aliases.push_back(ad);
                        }
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && aliasFormatInvalid) {
                        AppLog(L"❌ [Web同步拦截] [" + m_players[mfcIdx].name + L"] 小号格式不合格：" + aliasFormatError, RGB(255, 120, 80));
                        m_players[mfcIdx].name.Empty();
                        m_players[mfcIdx].aliases.clear();
                        m_players[mfcIdx].kills = m_players[mfcIdx].deaths = m_players[mfcIdx].akCount = 0;
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && m_players[mfcIdx].aliases.empty()) {
                        AppLog(L"❌ [Web同步拦截] [" + m_players[mfcIdx].name + L"] 缺少小号，已拒绝上场。", RGB(255, 120, 80));
                        m_players[mfcIdx].name.Empty();
                        m_players[mfcIdx].kills = m_players[mfcIdx].deaths = m_players[mfcIdx].akCount = 0;
                    }
                }
            }
            else {
                MessageBox(L"Web端发来的数据长度不对！", L"同步异常", MB_ICONWARNING);
            }

            SaveAliasDB();
            SaveConfigToFile();
            WriteScoreToFile();
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
        else if (action == "cmd_swap") {
            m_chkFlip.SetCheck(m_chkFlip.GetCheck() == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
            OnBnClickedFlip();
        }
        else if (action == "cmd_monitor") {
            bool state = j["state"].get<bool>();
            if ((m_bIsRunning == TRUE) != state) { // 🚨 加上 == TRUE，解决 BOOL 和 bool 混合不安全的警告
                OnBnClickedStart();
            }
        }
        else if (action == "cmd_auth") {
            std::string codeStr = j["code"].get<std::string>();
            CString newAuthCode = CA2W(codeStr.c_str(), CP_UTF8);
            // 🚨 【新增】：标记本次云端校验是用户手动触发的！
            m_bIsManualAuthCheck = true;

            wchar_t exePath[MAX_PATH]; GetModuleFileName(NULL, exePath, MAX_PATH);
            CString path = exePath; path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

            CFile fileRead;
            if (fileRead.Open(path, CFile::modeRead)) {
                char buf[256] = { 0 }; fileRead.Read(buf, 255);
                s_backupAuthCode = CA2W(buf, CP_UTF8); fileRead.Close();
            }

            CFile fileWrite;
            if (fileWrite.Open(path, CFile::modeCreate | CFile::modeWrite)) {
                std::string ansiKey = CW2A(newAuthCode, CP_UTF8);
                fileWrite.Write(ansiKey.c_str(), (UINT)ansiKey.length()); fileWrite.Close();
            }

            CheckTrialAndLicense();

            json reply; reply["action"] = "auth_result"; reply["success"] = true;
            // 🚨【关键防崩溃修复】：必须强制转为 UTF-8！
            reply["message"] = std::string(CW2A(L"🔄 已提交卡密，正在云端验证中，请稍候...", CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
        // 🚨【新增】：处理网页发来的“专业模式”隐藏/显示指令
        else if (action == "cmd_toggle_mfc") {
            bool bShow = j["show"].get<bool>();
            if (bShow) {
                ShowWindow(SW_SHOW);          // 显示主窗口
                SetForegroundWindow();        // 提到最前面
            }
            else {
                ShowWindow(SW_HIDE);          // 隐藏主窗口
            }
            BroadcastStateToWeb(); // 👈 新增：执行完命令立刻把最新状态弹回去
        }
        // 🚨【新增】：处理网页发来的更改目录指令
        else if (action == "cmd_browse_dir") {
            OnBnClickedBrowseDir(); // 直接调用 MFC 原本的浏览目录函数
        }
        // 🚨【新增】：处理网页发来的“彻底删除小号”指令
        else if (action == "cmd_delete_alias") {
            std::string mNameStr = j["mainName"].get<std::string>();
            std::string aNameStr = j["aliasName"].get<std::string>();
            CString mainName = CA2W(mNameStr.c_str(), CP_UTF8);
            CString aliasName = CA2W(aNameStr.c_str(), CP_UTF8);

            std::lock_guard<std::mutex> lock(m_dataMutex);

            bool blockedDelete = false;
            bool legacyShortAlias = DnfIsLegacyShortAliasWithoutMeta(aliasName);
            // 主号不参与名称匹配，场上选手必须至少保留 1 个小号。
            // 例外：旧库中已经存在的短 ID 小号可以直接删除，避免脏数据卡住用户。
            for (int i = 0; i < 8; i++) {
                if (m_players[i].name == mainName && m_players[i].aliases.size() <= 1 && !legacyShortAlias) {
                    AppLog(L"❌ [删除小号失败] [" + mainName + L"] 至少要保留一个小号。", RGB(255, 120, 80));
                    blockedDelete = true;
                    break;
                }
            }

            if (!blockedDelete) {
                if (legacyShortAlias) {
                    AppLog(L"⚠️ [旧库短ID清理] [" + mainName + L"] " + DnfLegacyShortAliasDeleteReason(aliasName), RGB(255, 180, 0));
                }

                // 1. 从场上活跃选手 (m_players) 中剥离
                for (int i = 0; i < 8; i++) {
                    if (m_players[i].name == mainName) {
                        for (auto it = m_players[i].aliases.begin(); it != m_players[i].aliases.end(); ) {
                            if (it->name == aliasName) {
                                it = m_players[i].aliases.erase(it);
                            }
                            else {
                                ++it;
                            }
                        }
                    }
                }

                // 2. 从底层数据库 (m_aliasDB) 中连根拔起
                if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
                    CString& dbAliases = m_aliasDB[mainName];
                    dbAliases.Replace(L"(" + aliasName + L")", L"");
                    dbAliases.Replace(L"（" + aliasName + L"）", L"");
                    if (dbAliases.IsEmpty()) {
                        m_aliasDB.erase(mainName);
                    }
                }

                // 3. 落地保存并刷新所有界面（这会触发 BroadcastStateToWeb 告诉网页更新成功）
                SaveAliasDB();
                SaveConfigToFile();
                PostMessage(WM_UPDATE_ALL_UI, 0, 0);
            }
        }
    }
    // 🚨 增加了显式报错：如果解析出错，直接弹窗告诉你到底哪里写错了！
    catch (json::exception& e) {
        CString errMsg;
        errMsg.Format(L"JSON 数据同步失败: %S", e.what());
        MessageBox(errMsg, L"同步报错", MB_ICONERROR);
    }
    catch (...) {}

    delete pJsonStr;
    return 0;
}

void CDNFGameCaptureDlg::BroadcastStateToWeb()
{
    if (m_pWebDlg == nullptr) return;

    try {
        json j;
        j["action"] = "sync_state";
        j["data"]["blueScore"] = m_totalScoreBlue;
        j["data"]["redScore"] = m_totalScoreRed;

        // 🚨 【新增 1】：同步监控运行状态
        j["data"]["isMonitoring"] = (m_bIsRunning == TRUE);
        j["data"]["isFlipped"] = (m_bFlipSides == true);         // 👈 新增
        j["data"]["isMfcVisible"] = (IsWindowVisible() == TRUE); // 👈 新增

        // 🚨 【新增 2】：计算并同步授权时间文字
        j["data"]["isAuthValid"] = (m_bIsAuthValid == true);
        CString expStr = L"";
        if (m_bIsTrial) {
            expStr.Format(L"试用至: %s", FormatTimeStamp(m_trialEnd));
        }
        else if (m_bIsAuthValid) {
            if (m_cloudExpireTime == -1) expStr = L"验证中...";
            else if (m_cloudExpireTime > 0) expStr.Format(L"到期: %s", FormatTimeStamp(m_cloudExpireTime));
            else expStr = L"永久有效";
        }
        else {
            expStr = L"未激活";
        }
        j["data"]["authText"] = std::string(CW2A(expStr, CP_UTF8));
        // 🚨【新增】：同步当前输出目录给网页
        j["data"]["outputDir"] = std::string(CW2A(m_outputDir, CP_UTF8));

        json playersArray = json::array();

        // 🚨 先打包：红队 (MFC 0-3)
        for (int i = 0; i < 4; i++) {
            json p;
            p["team"] = 0;
            p["name"] = std::string(CW2A(m_players[i].name, CP_UTF8));
            p["kills"] = m_players[i].kills;
            p["deaths"] = m_players[i].deaths;
            p["akCount"] = m_players[i].akCount;
            json aliases = json::array();
            for (auto& a : m_players[i].aliases) {
                aliases.push_back(std::string(CW2A(a.name, CP_UTF8)));
            }
            p["aliases"] = aliases;
            playersArray.push_back(p);
        }

        // 🚨 后打包：蓝队 (MFC 4-7)
        for (int i = 4; i < 8; i++) {
            json p;
            p["team"] = 1;
            p["name"] = std::string(CW2A(m_players[i].name, CP_UTF8));
            p["kills"] = m_players[i].kills;
            p["deaths"] = m_players[i].deaths;
            p["akCount"] = m_players[i].akCount;
            json aliases = json::array();
            for (auto& a : m_players[i].aliases) {
                aliases.push_back(std::string(CW2A(a.name, CP_UTF8)));
            }
            p["aliases"] = aliases;
            playersArray.push_back(p);
        }

        j["data"]["players"] = playersArray;

        // --- 2. 打包整个小号库，供前端补全使用 ---
        json dbJson = json::object();
        for (auto const& [name, aliases] : m_aliasDB) {
            // 🚨 必须显式套上一层 std::string()，否则 JSON 库会因为类型不匹配而报错！
            std::string utf8Name = std::string(CW2A(name, CP_UTF8));
            std::string utf8Aliases = std::string(CW2A(aliases, CP_UTF8));
            dbJson[utf8Name] = utf8Aliases;
        }
        j["data"]["fullAliasDB"] = dbJson;

        CString jsonStr = CA2W(j.dump().c_str(), CP_UTF8);
        m_pWebDlg->SendStateToWeb(jsonStr);
    }
    catch (...) {}
}

// 将数据同步到树状控件（带视觉状态记忆）
void CDNFGameCaptureDlg::SyncDataToTree() {

    // 1. 【核心新增】：重绘前，先记住当前用户已经展开了哪些主号
    std::vector<CString> userExpandedNames;
    if (m_treePlayers.m_hWnd) { // 确保控件已创建
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                // 如果这个主号目前是展开状态，记下它的名字
                if (m_treePlayers.GetItemState(hChild, TVIS_EXPANDED) & TVIS_EXPANDED) {
                    CString text = m_treePlayers.GetItemText(hChild);
                    int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                    CString name = (eqPos != -1) ? text.Left(eqPos) : text;
                    name.Trim();
                    userExpandedNames.push_back(name);
                }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
    }

    m_treePlayers.DeleteAllItems();

    CString redTitle; redTitle.Format(L"【红队】- %d 分", m_totalScoreRed);
    CString blueTitle; blueTitle.Format(L"【蓝队】- %d 分", m_totalScoreBlue);

    HTREEITEM hRed = m_treePlayers.InsertItem(redTitle);
    HTREEITEM hBlue = m_treePlayers.InsertItem(blueTitle);

    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        HTREEITEM hTeam = (m_players[i].team == 0) ? hRed : hBlue;

        CString mainText;
        mainText.Format(L"%s = %d/%d", m_players[i].name, m_players[i].kills, m_players[i].deaths);
        if (m_players[i].akCount > 0) mainText.AppendFormat(L" A%d", m_players[i].akCount);

        HTREEITEM hMain = m_treePlayers.InsertItem(mainText, hTeam);
        m_treePlayers.SetItemData(hMain, i);

        for (size_t j = 0; j < m_players[i].aliases.size(); j++) {
            HTREEITEM hAlias = m_treePlayers.InsertItem(m_players[i].aliases[j].name, hMain);
            m_treePlayers.SetItemData(hAlias, (i << 16) | j | 0x80000000);
        }

        // 2. 【核心新增】：按记忆恢复展开状态
        bool shouldExpand = false;
        for (const auto& en : userExpandedNames) {
            if (m_players[i].name == en) { shouldExpand = true; break; }
        }
        if (shouldExpand) {
            m_treePlayers.Expand(hMain, TVE_EXPAND);
        }
    }
    // 仅展开队伍根节点
    m_treePlayers.Expand(hRed, TVE_EXPAND);
    m_treePlayers.Expand(hBlue, TVE_EXPAND);
}

void CDNFGameCaptureDlg::OnRClickTree(NMHDR* pNMHDR, LRESULT* pResult) {
    CPoint pt;
    GetCursorPos(&pt);
    CPoint ptTree = pt;
    m_treePlayers.ScreenToClient(&ptTree);
    UINT uFlags;
    HTREEITEM hItem = m_treePlayers.HitTest(ptTree, &uFlags);

    if (hItem && (uFlags & TVHT_ONITEM)) {
        m_treePlayers.SelectItem(hItem);
        DWORD_PTR data = m_treePlayers.GetItemData(hItem);
        HTREEITEM hParent = m_treePlayers.GetParentItem(hItem);

        CMenu menu;
        menu.CreatePopupMenu();
        CMenu subMenu;

        if (hParent == NULL) {
            // ==========================================
            // 【新增】：根节点添加“添加主号”选项
            // ==========================================
            menu.AppendMenu(MF_STRING, 16, L"➕ 添加主号 (自动切换队伍)");
            menu.AppendMenu(MF_SEPARATOR);

            menu.AppendMenu(MF_STRING, 7, L"🏆 该队大比分 +1");
            menu.AppendMenu(MF_STRING, 9, L"🔽 该队大比分 -1");
            menu.AppendMenu(MF_STRING, 8, L"❌ 该队大比分归零");
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 10, L"✏️ 自定义比分 (在上方输入框修改)");
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 11, L"🗑️ 一键清空该队所有成员");
        }
        else if (data & 0x80000000) {
            menu.AppendMenu(MF_STRING, 1, L"🗑️ 从当前战局列表中移除");
            menu.AppendMenu(MF_STRING, 14, L"💥 删除（同时从自动补齐库中彻底删除）");
        }
        else if (data >= 0 && data < 8) {
            // 为主号添加小号快捷选项
            menu.AppendMenu(MF_STRING, 15, L"➕ 为该主号添加小号...");
            menu.AppendMenu(MF_STRING, 2, L"🗑️ 删除该主号 (及所有小号)");
            menu.AppendMenu(MF_SEPARATOR);

            // ==========================================
            // 【关键视觉优化】：使用高辨识度专属图标，彻底告别点错
            // ==========================================
            menu.AppendMenu(MF_STRING, 3, L"⚔️ 战绩：击杀 +1");
            menu.AppendMenu(MF_STRING, 31, L"⚔️ 战绩：击杀 -1  (撤销)");
            menu.AppendMenu(MF_STRING, 4, L"💀 战绩：死亡 +1");
            menu.AppendMenu(MF_STRING, 32, L"💀 战绩：死亡 -1  (撤销)");
            menu.AppendMenu(MF_STRING, 5, L"🌟 战绩：AK +1");
            menu.AppendMenu(MF_STRING, 33, L"🌟 战绩：AK -1  (撤销)");
            menu.AppendMenu(MF_STRING, 6, L"🔄 该主号战绩清零");
            menu.AppendMenu(MF_SEPARATOR);

            int curTeam = m_players[data].team;
            int targetTeam = (curTeam == 0) ? 1 : 0;
            int sI = (targetTeam == 0) ? 0 : 4, eI = (targetTeam == 0) ? 4 : 8;

            std::vector<int> targetOccupied;
            for (int i = sI; i < eI; i++) {
                CString checkName = m_players[i].name;
                checkName.Trim();
                if (!checkName.IsEmpty()) {
                    targetOccupied.push_back(i);
                }
            }

            if (targetOccupied.size() < 4) {
                if (curTeam == 0) menu.AppendMenu(MF_STRING, 12, L"➡️ 一键移动到【蓝队】");
                else menu.AppendMenu(MF_STRING, 13, L"⬅️ 一键移动到【红队】");
            }
            else {
                subMenu.CreatePopupMenu();
                for (size_t i = 0; i < targetOccupied.size(); i++) {
                    int tIdx = targetOccupied[i];
                    CString swapTxt;
                    swapTxt.Format(L"🔄 与 [%s] 互换位置", m_players[tIdx].name);
                    subMenu.AppendMenu(MF_STRING, 20 + tIdx, swapTxt);
                }
                if (curTeam == 0) menu.AppendMenu(MF_POPUP, (UINT_PTR)subMenu.GetSafeHmenu(), L"➡️ 【蓝队】已满，请选择互换目标...");
                else menu.AppendMenu(MF_POPUP, (UINT_PTR)subMenu.GetSafeHmenu(), L"⬅️ 【红队】已满，请选择互换目标...");
            }
        }

        if (menu.GetMenuItemCount() > 0) {
            int cmd = menu.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);
            if (cmd <= 0) {
                *pResult = 0;
                return;
            }

            // ==========================================
            // 【新增】：处理根节点点击“添加主号”的联动逻辑
            // ==========================================
            if (cmd == 16) {
                // 1. 判断点的是红队还是蓝队，自动切换下拉框
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) {
                    m_cmbTeamSelect.SetCurSel(0); // 设置为红队
                }
                else {
                    m_cmbTeamSelect.SetCurSel(1); // 设置为蓝队
                }

                // 2. 将光标焦点移至录入输入框，方便直接打字
                m_editQuickAdd.SetFocus();

                // 3. (可选) 全选当前输入框的内容，这样用户一打字就会覆盖掉旧内容或提示词
                m_editQuickAdd.SetSel(0, -1);

                AppLog(L"💡 [操作提示] 已自动切换队伍，请在输入框录入新主号！", RGB(0, 255, 255));
                return; // 直接返回，不用走后面的保存刷新逻辑
            }

            if (cmd == 7) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed++;
                else m_totalScoreBlue++;
            }
            else if (cmd == 9) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed--;
                else m_totalScoreBlue--;
            }
            else if (cmd == 8) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed = 0;
                else m_totalScoreBlue = 0;
            }
            else if (cmd == 10) {
                CString scoreStr;
                scoreStr.Format(L"红 %d : %d 蓝", m_totalScoreRed, m_totalScoreBlue);
                m_editQuickAdd.SetWindowText(scoreStr);
                m_editQuickAdd.SetFocus();
                m_editQuickAdd.SetSel(0, -1);
            }
            else if (cmd == 11) {
                int teamToClear = (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) ? 0 : 1;
                CString teamName = (teamToClear == 0) ? L"红队" : L"蓝队";
                int sI = (teamToClear == 0) ? 0 : 4;
                int eI = (teamToClear == 0) ? 4 : 8;
                for (int i = sI; i < eI; i++) {
                    m_players[i].name = L"";
                    m_players[i].aliases.clear();
                    m_players[i].kills = 0;
                    m_players[i].deaths = 0;
                    m_players[i].akCount = 0;
                }
                AppLog(L"🗑️ [清空队伍] 一键清空了【" + teamName + L"】的所有成员！", RGB(255, 80, 80));
            }
            else if (cmd == 15) {
                int pIdx = (int)data;

                // ==========================================
                // 【新增】：获取该主号所在的队伍，并自动切换下拉框
                // ==========================================
                int curTeam = m_players[pIdx].team;
                m_cmbTeamSelect.SetCurSel(curTeam);

                CString mainName = m_players[pIdx].name;
                CString templateText;

                // 帮你把输入框填充好模板： 主号()
                templateText.Format(L"%s()", mainName.GetString());
                m_editQuickAdd.SetWindowText(templateText);
                m_editQuickAdd.SetFocus();

                // 精准将光标移动到左右括号的中间，直接打字即可
                int pos = templateText.GetLength() - 1;
                m_editQuickAdd.SetSel(pos, pos);

                CString teamNameStr = (curTeam == 0) ? L"红队" : L"蓝队";
                AppLog(L"💡 [操作提示] 已自动切换至【" + teamNameStr + L"】，请在括号内填入小号名称！", RGB(0, 255, 255));
            }
            else if (cmd == 12 || cmd == 13) {
                int pIdx = (int)data;
                int targetTeam = (cmd == 12) ? 1 : 0;
                int targetIdx = -1;
                int sI = (targetTeam == 0) ? 0 : 4;
                int eI = (targetTeam == 0) ? 4 : 8;

                for (int i = sI; i < eI; i++) {
                    CString checkName = m_players[i].name;
                    checkName.Trim();
                    if (checkName.IsEmpty()) {
                        targetIdx = i;
                        break;
                    }
                }
                if (targetIdx != -1) {
                    CString moveName = m_players[pIdx].name;
                    m_players[targetIdx] = m_players[pIdx];
                    m_players[targetIdx].team = targetTeam;
                    m_players[pIdx].name = L"";
                    m_players[pIdx].aliases.clear();
                    m_players[pIdx].kills = 0;
                    m_players[pIdx].deaths = 0;
                    m_players[pIdx].akCount = 0;
                    AppLog(L"➡️ [移动换边] 玩家 [" + moveName + L"] 已移动至对面阵营", RGB(80, 180, 255));
                }
            }
            else if (cmd >= 20 && cmd <= 27) {
                int pIdx = (int)data;
                int targetIdx = cmd - 20;

                int curTeam = m_players[pIdx].team;
                int targetTeam = m_players[targetIdx].team;
                CString myName = m_players[pIdx].name;
                CString targetName = m_players[targetIdx].name;

                PlayerData temp = m_players[targetIdx];
                m_players[targetIdx] = m_players[pIdx];
                m_players[targetIdx].team = targetTeam;

                m_players[pIdx] = temp;
                m_players[pIdx].team = curTeam;

                AppLog(L"🔄 [位置互换] [" + myName + L"] 与 [" + targetName + L"] 互换了位置", RGB(255, 215, 0));
            }
            else if (cmd == 1) {
                int pIdx = (data & 0x7FFFFFFF) >> 16;
                int aIdx = (data & 0xFFFF);
                CString subName = m_players[pIdx].aliases[aIdx].name;
                m_players[pIdx].aliases.erase(m_players[pIdx].aliases.begin() + aIdx);
                AppLog(L"✂️ [战局移除] 小号 [" + subName + L"] 已从当前战局剥离（保留在库中）", RGB(200, 200, 200));
            }
            else if (cmd == 14) {
                int pIdx = (data & 0x7FFFFFFF) >> 16;
                int aIdx = (data & 0xFFFF);
                CString mainName = m_players[pIdx].name;
                CString subName = m_players[pIdx].aliases[aIdx].name;

                bool legacyShortAlias = DnfIsLegacyShortAliasWithoutMeta(subName);
                if (m_players[pIdx].aliases.size() <= 1 && !legacyShortAlias) {
                    AppLog(L"❌ [删除小号失败] [" + mainName + L"] 至少要保留一个小号。", RGB(255, 120, 80));
                }
                if (legacyShortAlias) {
                    AppLog(L"⚠️ [旧库短ID清理] [" + mainName + L"] " + DnfLegacyShortAliasDeleteReason(subName), RGB(255, 180, 0));
                }

                if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
                    CString& dbAliases = m_aliasDB[mainName];
                    dbAliases.Replace(L"(" + subName + L")", L"");
                    dbAliases.Replace(L"（" + subName + L"）", L"");
                    if (dbAliases.IsEmpty()) {
                        m_aliasDB.erase(mainName);
                    }
                }

                m_players[pIdx].aliases.erase(m_players[pIdx].aliases.begin() + aIdx);
                AppLog(L"💥 [双重抹除] 小号 [" + subName + L"] 已从战局及自动补齐数据库中彻底删除！", RGB(255, 80, 80));
            }
            else if (cmd == 2) {
                int pIdx = (int)data;
                CString mainName = m_players[pIdx].name;
                m_players[pIdx].name = L"";
                m_players[pIdx].aliases.clear();
                m_players[pIdx].kills = 0;
                m_players[pIdx].deaths = 0;
                m_players[pIdx].akCount = 0;
                AppLog(L"🗑️ [删除主号] 玩家 [" + mainName + L"] 及其旗下小号已被全盘清空", RGB(255, 80, 80));
            }
            else if (cmd == 3) { m_players[data].kills++; }
            else if (cmd == 31) { if (m_players[data].kills > 0) m_players[data].kills--; }
            else if (cmd == 4) { m_players[data].deaths++; }
            else if (cmd == 32) { if (m_players[data].deaths > 0) m_players[data].deaths--; }
            else if (cmd == 5) { m_players[data].akCount++; }
            else if (cmd == 33) { if (m_players[data].akCount > 0) m_players[data].akCount--; }
            else if (cmd == 6) {
                m_players[data].kills = 0;
                m_players[data].deaths = 0;
                m_players[data].akCount = 0;
            }

            SaveAliasDB();
            SyncDataToTree();
            // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
            BroadcastStateToWeb();
            WriteScoreToFile();
            RefreshDisplay();
            SaveConfigToFile();
        }
    }
    *pResult = 0;
}

// 🚨 C++版 战场级查重：严格防止场上 8 个人发生任何主/小号交叉
CString CDNFGameCaptureDlg::CheckFieldConflict(const CString& newMain, const std::vector<CString>& extraAliases, int excludeIdx) {
    if (newMain.IsEmpty()) return L"";

    // 汇总即将上场的所有小号（文本框解析带的 + 库里本身带的）
    std::vector<CString> allAliases = extraAliases;
    auto it = m_aliasDB.find(newMain);
    if (it != m_aliasDB.end()) {
        int curPos = 0;
        CString token = it->second.Tokenize(L" ()（）", curPos);
        while (token != L"") {
            if (std::find(allAliases.begin(), allAliases.end(), token) == allAliases.end()) allAliases.push_back(token);
            token = it->second.Tokenize(L" ()（）", curPos);
        }
    }

    // 遍历场上 8 个人比对
    for (int i = 0; i < 8; i++) {
        if (i == excludeIdx || m_players[i].name.IsEmpty()) continue;

        CString otherMain = m_players[i].name;

        if (otherMain == newMain) return otherMain + L" (主号冲突)";
        for (const auto& a : allAliases) {
            if (otherMain == a) return otherMain + L" (小号包含了对方主号)";
        }
        for (const auto& oa : m_players[i].aliases) {
            if (oa.name == newMain) return otherMain + L" (名字是对方的小号)";
            for (const auto& na : allAliases) {
                if (oa.name == na) return otherMain + L" (小号互斥: " + na + L")";
            }
        }
    }
    return L""; // 返回空代表绝对安全
}

// 序列化保存新版配置文件
void CDNFGameCaptureDlg::SaveConfigToFile() {
    CFile file;
    if (file.Open(m_configPath, CFile::modeCreate | CFile::modeWrite)) {
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF }; file.Write(bom, 3);
        CString text;
        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            text.AppendFormat(L"%d|%s|%d|%d|%d", m_players[i].team, m_players[i].name, m_players[i].kills, m_players[i].deaths, m_players[i].akCount);
            for (auto& a : m_players[i].aliases) text.AppendFormat(L"|%s", a.name);
            text += L"\r\n";
        }
        std::string utf8 = CW2A(text, CP_UTF8); file.Write(utf8.c_str(), (UINT)utf8.length()); file.Close();
    }
}

// 反序列化读取配置文件（支持三种历史格式，自带脏数据清洗，附带说明文案过滤）
void CDNFGameCaptureDlg::LoadConfigFromFile() {
    CFile file;
    if (!file.Open(m_configPath, CFile::modeRead)) return;

    int len = (int)file.GetLength();
    if (len <= 0) {
        file.Close();
        return;
    }

    char* buf = new char[len + 1];
    file.Read(buf, len);
    buf[len] = 0;

    char* start = buf;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        start += 3;
    }

    CString content = CA2W(start, CP_UTF8);
    delete[] buf;

    int pos = 0;
    int currentTeamContext = 0;

    while (pos < content.GetLength()) {
        int nl = content.Find(L'\n', pos);
        CString line = (nl != -1) ? content.Mid(pos, nl - pos) : content.Mid(pos);
        pos = (nl != -1) ? nl + 1 : content.GetLength();

        line.Remove(L'\r');
        line.Trim();

        if (line.IsEmpty()) continue;

        if (line.Find(L"操作说明") != -1 || line.Find(L"分队：") != -1 ||
            line.Find(L"绑定小号：") != -1 || line.Find(L"手动改分") != -1 ||
            line.Find(L"手动改AK") != -1 || line.Find(L"💡") != -1) {
            continue;
        }

        if (line.Find(L"【红队】") != -1) { currentTeamContext = 0; continue; }
        if (line.Find(L"【蓝队】") != -1) { currentTeamContext = 1; continue; }

        if (line.Find(L"|") != -1) {
            std::vector<CString> tokens;
            int splitPos = 0; CString token;
            while (AfxExtractSubString(token, line, splitPos, L'|')) {
                tokens.push_back(token); splitPos++;
            }
            if (tokens.size() < 4) continue;

            int team = _wtoi(tokens[0]);
            CString mainName = tokens[1]; mainName.Trim();

            bool isDup = false;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { isDup = true; break; } }
            if (isDup) continue;

            int targetIdx = -1; int sIdx = (team == 0) ? 0 : 4; int eIdx = (team == 0) ? 4 : 8;
            for (int i = sIdx; i < eIdx; i++) { if (m_players[i].name.IsEmpty()) { targetIdx = i; break; } }
            if (targetIdx == -1) continue;

            m_players[targetIdx].team = team;
            m_players[targetIdx].name = mainName;
            m_players[targetIdx].kills = _wtoi(tokens[2]);
            m_players[targetIdx].deaths = _wtoi(tokens[3]);
            m_players[targetIdx].akCount = 0;
            int aliasStartIndex = 4;

            if (tokens.size() >= 5) {
                CString t4 = tokens[4]; t4.Trim();
                bool isNumeric = !t4.IsEmpty();
                for (int i = 0; i < t4.GetLength(); i++) { if (t4[i] < L'0' || t4[i] > L'9') { isNumeric = false; break; } }
                if (isNumeric && t4.GetLength() <= 3) {
                    m_players[targetIdx].akCount = _wtoi(t4);
                    aliasStartIndex = 5;
                }
            }

            for (size_t i = aliasStartIndex; i < tokens.size(); i++) {
                CString aName = tokens[i]; aName.Trim();
                bool aDup = false;
                for (int k = 0; k < 8; k++) {
                    if (m_players[k].name == aName && k != targetIdx) { aDup = true; break; }
                    for (auto& ea : m_players[k].aliases) { if (ea.name == aName) { aDup = true; break; } }
                }
                if (!aDup && !aName.IsEmpty()) m_players[targetIdx].aliases.push_back({ aName });
            }
        }
        else if (line.Find(L"=") != -1 || line.Find(L"＝") != -1) {
            int eqPos = line.Find(L'=');
            if (eqPos == -1) eqPos = line.Find(L'＝');

            CString leftPart = line.Left(eqPos);
            CString rightPart = line.Mid(eqPos + 1);
            leftPart.Trim(); rightPart.Trim();

            // 用切词引擎加载以前的文本配置
            CString mainName = L"";
            std::vector<CString> parsedAliases;
            int curPos = 0;
            CString token = leftPart.Tokenize(L" ()（）", curPos);
            if (token != L"") {
                mainName = token;
                token = leftPart.Tokenize(L" ()（）", curPos);
                while (token != L"") {
                    parsedAliases.push_back(token);
                    token = leftPart.Tokenize(L" ()（）", curPos);
                }
            }

            if (mainName.IsEmpty()) continue;

            bool isDup = false;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { isDup = true; break; } }
            if (isDup) continue;

            int targetIdx = -1;
            int sIdx = (currentTeamContext == 0) ? 0 : 4;
            int eIdx = (currentTeamContext == 0) ? 4 : 8;
            for (int i = sIdx; i < eIdx; i++) {
                if (m_players[i].name.IsEmpty()) { targetIdx = i; break; }
            }
            if (targetIdx == -1) continue;

            m_players[targetIdx].name = mainName;
            m_players[targetIdx].team = currentTeamContext;

            int aP = rightPart.Find(L'A');
            if (aP != -1) {
                m_players[targetIdx].akCount = _wtoi(rightPart.Mid(aP + 1));
                if (m_players[targetIdx].akCount == 0 && rightPart.Mid(aP + 1) != L"0") m_players[targetIdx].akCount = 1;
                rightPart = rightPart.Left(aP);
            }
            else {
                m_players[targetIdx].akCount = 0;
            }

            int slash = rightPart.Find(L'/');
            if (slash == -1) slash = rightPart.Find(L'-');

            if (slash != -1) {
                m_players[targetIdx].kills = _wtoi(rightPart.Left(slash));
                m_players[targetIdx].deaths = _wtoi(rightPart.Mid(slash + 1));
            }
            else {
                m_players[targetIdx].kills = 0; m_players[targetIdx].deaths = 0;
            }

            // 极简加载小号
            for (const auto& aN : parsedAliases) {
                bool aDup = false;
                for (int k = 0; k < 8; k++) {
                    if (m_players[k].name == aN && k != targetIdx) { aDup = true; break; }
                    for (auto& ea : m_players[k].aliases) {
                        if (ea.name == aN) { aDup = true; break; }
                    }
                }
                if (!aDup && !aN.IsEmpty()) m_players[targetIdx].aliases.push_back({ aN });
            }
        }
    }
    file.Close();
}

// 当鼠标点进输入框
void CDNFGameCaptureDlg::OnEditSetFocus() {
    CString content;
    m_editQuickAdd.GetWindowText(content);
    // 如果当前内容是水印提示，则清空并改变颜色
    if (content == PLACEHOLDER_TEXT) {
        m_editQuickAdd.SetWindowText(L"");
        // 这里可以根据需要微调文字颜色，CEdit默认是黑色
    }
}

// 当鼠标切出输入框
void CDNFGameCaptureDlg::OnEditKillFocus() {
    CString content;
    m_editQuickAdd.GetWindowText(content);
    content.Trim(); // 【关键】：去除空格

    // 如果内容为空或用户只打了空格，恢复水印提示
    if (content.IsEmpty()) {
        m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT);
    }
}

void CDNFGameCaptureDlg::OnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMTVDISPINFO pTVDispInfo = (LPNMTVDISPINFO)pNMHDR;
    *pResult = FALSE;

    if (pTVDispInfo->item.pszText == NULL) return;

    CString line = pTVDispInfo->item.pszText;
    line.Trim();
    if (line.IsEmpty()) return;

    HTREEITEM hItem = pTVDispInfo->item.hItem;
    HTREEITEM hParent = m_treePlayers.GetParentItem(hItem);

    if (hParent == NULL) {
        CString oldText = m_treePlayers.GetItemText(hItem);
        int newScore = 0;
        CString numStr = L"";

        for (int i = 0; i < line.GetLength(); i++) {
            if (line[i] >= L'0' && line[i] <= L'9') numStr += line[i];
        }

        if (!numStr.IsEmpty()) {
            newScore = _wtoi(numStr);
            if (oldText.Find(L"红队") != -1) {
                m_totalScoreRed = newScore;
            }
            else {
                m_totalScoreBlue = newScore;
            }
            AppLog(L"✏️ [大比分修改] 队伍比分已更新为：" + numStr, RGB(0, 255, 100));
        }

        SaveConfigToFile();
        WriteScoreToFile();
        SyncDataToTree();
        // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
        BroadcastStateToWeb();
        RefreshDisplay();
        return;
    }

    DWORD_PTR data = m_treePlayers.GetItemData(hItem);
    std::lock_guard<std::mutex> lk(m_dataMutex);

    CString newNameOnly = line;
    if (!(data & 0x80000000)) {
        int eP = line.Find(L'=');
        if (eP == -1) eP = line.Find(L'＝');
        if (eP != -1) {
            newNameOnly = line.Left(eP);
        }
    }

    // 强力剥离非法字符
    newNameOnly.Remove(L' '); newNameOnly.Remove(L'('); newNameOnly.Remove(L')'); newNameOnly.Remove(L'（'); newNameOnly.Remove(L'）');
    newNameOnly.Trim();

    int curPIdx = (data & 0x80000000) ? ((data & 0x7FFFFFFF) >> 16) : (int)data;
    int curAIdx = (data & 0x80000000) ? (data & 0xFFFF) : -1;

    bool isDup = false;
    for (int i = 0; i < 8 && !isDup; i++) {
        if (m_players[i].name.IsEmpty()) continue;

        if (i != curPIdx) {
            if (m_players[i].name == newNameOnly) { isDup = true; break; }
            for (int j = 0; j < (int)m_players[i].aliases.size(); j++) {
                if (m_players[i].aliases[j].name == newNameOnly) { isDup = true; break; }
            }
        }
        else {
            // 同一名选手内部允许：主号名称 == 自己的小号名称。
            // 但仍然禁止同一名选手的小号之间互相重名。
            if (curAIdx != -1) {
                for (int j = 0; j < (int)m_players[i].aliases.size(); j++) {
                    if (j != curAIdx && m_players[i].aliases[j].name == newNameOnly) { isDup = true; break; }
                }
            }
        }
    }

    if (isDup) {
        AppLog(L"❌ [重命名失败] 名称 [" + newNameOnly + L"] 已被占用！", RGB(255, 100, 100));
        MessageBox(L"修改失败！该名称已经被其他主号或小号占用，请使用唯一名称。", L"命名冲突", MB_ICONWARNING);
        return;
    }

    if (data & 0x80000000) {
        // 防止小号名字带脏字符
        line.Remove(L' '); line.Remove(L'('); line.Remove(L')'); line.Remove(L'（'); line.Remove(L'）');
        CString oldAliasName = m_players[curPIdx].aliases[curAIdx].name;
        CString mainName = m_players[curPIdx].name;

        if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
            CString& dbAliases = m_aliasDB[mainName];
            dbAliases.Replace(L"(" + oldAliasName + L")", L"(" + line + L")");
            dbAliases.Replace(L"（" + oldAliasName + L"）", L"（" + line + L"）");
        }
        m_players[curPIdx].aliases[curAIdx].name = line;
    }
    else {
        CString oldMainName = m_players[data].name;
        CString newMainName = line;

        int eP = line.Find(L'=');
        if (eP == -1) eP = line.Find(L'＝');

        if (eP != -1) {
            newMainName = line.Left(eP);
            // 保护主名剥离脏字符
            newMainName.Remove(L' '); newMainName.Remove(L'('); newMainName.Remove(L')'); newMainName.Remove(L'（'); newMainName.Remove(L'）');
            newMainName.Trim();

            CString scorePart = line.Mid(eP + 1);
            scorePart.Trim();

            int aPos = scorePart.Find(L'A');
            if (aPos != -1) {
                m_players[data].akCount = _wtoi(scorePart.Mid(aPos + 1));
                if (m_players[data].akCount == 0 && scorePart.Mid(aPos + 1) != L"0") m_players[data].akCount = 1;
                scorePart = scorePart.Left(aPos);
            }

            int slash = scorePart.Find(L'/');
            if (slash == -1) slash = scorePart.Find(L'-');
            if (slash != -1) {
                m_players[data].kills = _wtoi(scorePart.Left(slash));
                m_players[data].deaths = _wtoi(scorePart.Mid(slash + 1));
            }
        }
        else {
            // 如果只有名字没有等号战绩，同样执行净化
            newMainName.Remove(L' '); newMainName.Remove(L'('); newMainName.Remove(L')'); newMainName.Remove(L'（'); newMainName.Remove(L'）');
            newMainName.Trim();
        }

        if (oldMainName != newMainName && !oldMainName.IsEmpty()) {
            if (m_aliasDB.find(oldMainName) != m_aliasDB.end()) {
                m_aliasDB[newMainName] = m_aliasDB[oldMainName];
                m_aliasDB.erase(oldMainName);
            }
        }
        m_players[data].name = newMainName;
    }

    AppLog(L"✏️ [信息修改] 成功保存更新: " + line, RGB(0, 255, 100));

    SaveAliasDB();
    SaveConfigToFile();
    WriteScoreToFile();
    SyncDataToTree();
    // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
    BroadcastStateToWeb();
    RefreshDisplay();
}

void CDNFGameCaptureDlg::OnCustomDrawTree(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMTVCUSTOMDRAW pCustomDraw = (LPNMTVCUSTOMDRAW)pNMHDR;
    *pResult = CDRF_DODEFAULT;

    if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        *pResult = CDRF_NOTIFYITEMDRAW;
        return;
    }

    if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        HTREEITEM hItem = (HTREEITEM)pCustomDraw->nmcd.dwItemSpec;
        CString text = m_treePlayers.GetItemText(hItem);
        DWORD_PTR data = m_treePlayers.GetItemData(hItem);

        if (text.Find(L"【红队】") != -1) {
            pCustomDraw->clrText = RGB(220, 20, 60);
            *pResult = CDRF_NEWFONT;
            return;
        }
        if (text.Find(L"【蓝队】") != -1) {
            pCustomDraw->clrText = RGB(30, 144, 255);
            *pResult = CDRF_NEWFONT;
            return;
        }

        // --- 【新增】：判断是小号，直接变灰 ---
        if (data & 0x80000000) {
            pCustomDraw->clrText = RGB(150, 150, 150); // 灰色
            *pResult = CDRF_NEWFONT;
            return;
        }

        // --- 下面是主号的颜色 ---
        int playerIdx = (int)data;
        if (playerIdx >= 0 && playerIdx < 4) {
            pCustomDraw->clrText = RGB(255, 80, 80); // 红队主号
        }
        else if (playerIdx >= 4 && playerIdx < 8) {
            pCustomDraw->clrText = RGB(80, 120, 255); // 蓝队主号
        }
        *pResult = CDRF_NEWFONT;
    }
}

// DNFGameCaptureDlg.cpp
HBRUSH CDNFGameCaptureDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
    HBRUSH hbr = CWnd::OnCtlColor(pDC, pWnd, nCtlColor);

    // 1. 处理“快速添加框”的水印颜色
    if (pWnd->GetDlgCtrlID() == 1025) {
        CString txt;
        pWnd->GetWindowText(txt);
        if (txt == PLACEHOLDER_TEXT) {
            pDC->SetTextColor(RGB(160, 160, 160));
        }
        else {
            pDC->SetTextColor(RGB(0, 0, 0));
        }
    }
    // ==========================================
    // 2. 【新增】：将选手列表框 (1033) 的文字统一设为灰色
    // ==========================================
    else if (pWnd->GetDlgCtrlID() == 1033) {
        pDC->SetTextColor(RGB(150, 150, 150)); // 设定为灰色
        // 注意：这里不需要改变背景色，直接返回默认的 hbr 即可
    }

    return hbr;
}

// 【核心修复】：专门接收子线程消息，在安全的主线程中刷新树状图和看板
LRESULT CDNFGameCaptureDlg::OnUpdateAllUI(WPARAM wParam, LPARAM lParam) {
    // 1. 刷新软件界面的视觉显示
    SyncDataToTree();
    // 【加入这行】：自动识图抓到击杀后，立刻通知网页闪电跳分！
    BroadcastStateToWeb();
    RefreshDisplay();

    // ==========================================
    // 【关键修复】：自动识图拿到人头后，必须立刻将数据写入本地 TXT 文件！
    // 这样 OBS 才能瞬间读取到最新比分，实现真正的零延迟自动跟进！
    // ==========================================
    WriteScoreToFile(); // 实时更新发给 OBS 用的 TXT 文件
    SaveConfigToFile(); // 实时保存对局进度，防止崩溃丢失战绩

    return 0;
}

// ============================================================================
// 系统版本与权限检测
// ============================================================================
bool CDNFGameCaptureDlg::IsWindows10OrGreater() {
    // 使用 RtlGetVersion 获取真实系统版本（不受兼容性清单影响）
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
    if (!hNtdll) return false;

    auto pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
    if (!pRtlGetVersion) return false;

    RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
    if (pRtlGetVersion(&osvi) != 0) return false;

    // Win10 = 10.0, Win8.1 = 6.3, Win7 = 6.1
    return (osvi.dwMajorVersion >= 10);
}

// ============================================================================
// 权限检测与自动提权
// ============================================================================
bool CDNFGameCaptureDlg::IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool CDNFGameCaptureDlg::RelaunchAsAdmin() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteEx(&sei)) {
        if (m_hSingleInstanceMutex) {
            CloseHandle(m_hSingleInstanceMutex);
            m_hSingleInstanceMutex = NULL;
        }
        exit(0);
        return true;
    }
    return false; // 用户拒绝了 UAC
}

// ========================================================
// 【终极黑屏检测】：专门免疫 DNF“失明”状态与暗黑图特效
// ========================================================
bool CDNFGameCaptureDlg::IsBitmapBlank(HBITMAP hBmp, int w, int h) {
    if (!hBmp || w <= 0 || h <= 0) return true;

    HDC hDC = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(hDC, hBmp);

    bool isAllBlack = true; // 假设它是真黑屏

    // 沿画面的【主对角线】和【副对角线】扫射 40 个点
    // 这种扫射方式必定会穿过 DNF 的血条、技能栏、决斗场比分板或连击数区域
    for (int i = 1; i < 20; i++) {
        // 1. 测主对角线 (左上到右下)
        COLORREF c1 = GetPixel(hDC, w * i / 20, h * i / 20);
        if (c1 != RGB(0, 0, 0)) {
            isAllBlack = false; // 发现任意非纯黑像素，立刻洗清嫌疑！
            break;
        }

        // 2. 测副对角线 (左下到右上)
        COLORREF c2 = GetPixel(hDC, w * i / 20, h - (h * i / 20));
        if (c2 != RGB(0, 0, 0)) {
            isAllBlack = false; // 发现任意非纯黑像素，立刻洗清嫌疑！
            break;
        }
    }

    SelectObject(hDC, old);
    DeleteDC(hDC);

    // 只有这 40 个点全部是 100% 绝对的纯黑，才会被判定为捕获失败
    return isAllBlack;
}

// =====================================================================
// 【函数 2】OnCbnSelchangeCaptureEngine —— 替换原函数（同样的问题）
// =====================================================================
void CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine() {
    m_nCaptureEngineChoice = m_cmbCaptureEngine.GetCurSel();

    CString val;
    val.Format(L"%d", m_nCaptureEngineChoice);
    WritePrivateProfileString(L"Settings", L"CaptureEngine", val, m_iniPath);

    CString engineNames[] = { L"自动选择", L"WGC 硬件加速", L"PrintWindow 兼容模式" };
    AppLog(L"⚙️ [设置] 捕获引擎已切换为: " + engineNames[m_nCaptureEngineChoice], RGB(0, 255, 255));

    ClearPreview();

    // 🚨 换成安全销毁
    SafeDeleteWGC();

    m_nBlankFrameCount = 0;
    m_bAlreadyPrompted = false;
}


void CDNFGameCaptureDlg::ClearPreview() 
{
    // 清空位图数据
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            DeleteObject(m_bmp);
            m_bmp = nullptr;
        }
        m_w = 0;
        m_h = 0;
    }

    // 强制重绘预览区域，让画面变黑
    CRect client;
    GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, client.right, splitY);
    InvalidateRect(&topHalf, TRUE);
}

BOOL CALLBACK CDNFGameCaptureDlg::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!::IsWindowVisible(hwnd)) return TRUE;
    if (::GetWindowTextLength(hwnd) == 0) return TRUE;

    // 过滤掉系统杂项窗口
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    wchar_t title[256];
    ::GetWindowText(hwnd, title, 256);
    CString strTitle(title);

    if (strTitle == L"Program Manager" || strTitle.Find(L"DNF击杀统计") != -1) return TRUE;

    CComboBox* pCmb = (CComboBox*)lParam;
    int idx = pCmb->AddString(L"[窗口] " + strTitle);
    pCmb->SetItemData(idx, (DWORD_PTR)hwnd); // 藏入 HWND

    return TRUE;
}

void CDNFGameCaptureDlg::RefreshTargetList() {
    int curSelData = -1;
    if (m_cmbTargetWindow.GetCurSel() != -1) {
        curSelData = (int)m_cmbTargetWindow.GetItemData(m_cmbTargetWindow.GetCurSel());
    }

    m_cmbTargetWindow.ResetContent();

    // 1. 默认 DNF 游戏
    int dnfIdx = m_cmbTargetWindow.AddString(L"[默认] " DNF_WINDOW_NAME);
    m_cmbTargetWindow.SetItemData(dnfIdx, 0); // 0 代表使用老逻辑寻找DNF

    // 2. 枚举摄像头
    std::vector<std::wstring> cameras = CameraCapture::GetAvailableCameras();
    for (size_t i = 0; i < cameras.size(); i++) {
        int idx = m_cmbTargetWindow.AddString(CString(L"[摄像头] ") + cameras[i].c_str());
        // 最高位打个标记 0x80000000，表示这是摄像头，低位存索引
        m_cmbTargetWindow.SetItemData(idx, 0x80000000 | (DWORD_PTR)i);
    }

    // 3. 枚举其他窗口
    EnumWindows(EnumWindowsProc, (LPARAM)&m_cmbTargetWindow);

    // 尝试恢复之前的选择
    bool restored = false;
    for (int i = 0; i < m_cmbTargetWindow.GetCount(); i++) {
        if ((int)m_cmbTargetWindow.GetItemData(i) == curSelData) {
            m_cmbTargetWindow.SetCurSel(i);
            restored = true; break;
        }
    }
    if (!restored) m_cmbTargetWindow.SetCurSel(0);
}

void CDNFGameCaptureDlg::OnCbnDropdownTargetWindow() {
    RefreshTargetList(); // 每次点开下拉框，实时刷新最新的窗口列表
}

// 只在用户"确认选择并关闭下拉框"时触发，滚动期间不触发
// =============================================================
void CDNFGameCaptureDlg::OnCbnCloseupTargetWindow() {
    ClearPreview();

    // 🚨 换成安全销毁
    SafeDeleteWGC();

    if (m_pCamera) {
        m_pCamera->StopCapture();
        delete m_pCamera;
        m_pCamera = nullptr;
    }

    AppLog(L"🎯 [设置] 已切换捕获目标", RGB(0, 255, 255));
}
