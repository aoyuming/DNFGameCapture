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
#include <cwctype>
#include <set>
#include <fstream>
#include <sstream>
#include <climits>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Ws2_32.lib")

#include "json.hpp"
using json = nlohmann::json;
static CString s_backupAuthCode = L"";
static CString s_pendingAuthCode = L"";
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
const int ID_BTN_DEATH_X_CALIBRATE = 1036;
const int ID_BTN_DEATH_X_SAVE = 1037;
const int ID_BTN_DEATH_X_CANCEL = 1038;
const int ID_BTN_DEATH_X_DEFAULT = 1039;
const CString PLACEHOLDER_TEXT = L"输入：主号(小号1)(小号2)...";

constexpr int DEATH_X_ALGO_COLOR = 0;      // 当前颜色采样算法
constexpr int DEATH_X_ALGO_PATCH = 1;      // 打补丁红蓝点算法
constexpr int DEATH_X_COLOR_SAMPLE_COUNT = 16;
constexpr int DEATH_X_PATCH_SAMPLE_COUNT = 4;
constexpr int DEATH_X_PATCH_COLOR_TOL = 30;
constexpr int DEATH_X_COLOR_TOL = 40;       // 当前算法：#D53000 三通道上下容差
constexpr int DEATH_X_PATCH_REQUIRED_HITS = 3;       // 打补丁红蓝判断：左右一红一蓝 + 上/下至少一个红蓝点
constexpr int DEATH_X_PATCH_SEARCH_RADIUS = 2;        // 打补丁算法：固定理论点周围的小容错窗口
constexpr int DEATH_X_STABLE_ON_FRAMES = 2;           // 连续命中 2 次才认为死亡，避免录像压缩/运动单帧跳点误判
constexpr int DEATH_X_STABLE_OFF_FRAMES = 2;          // 连续消失 2 次才认为复活，避免残留 X 抖动造成重复边沿
constexpr float DEATH_X_PATCH_MUL_ACTIVE = 4.6f;     // 主将大 X 的检测点外移倍数
constexpr float DEATH_X_PATCH_MUL_NORMAL = 5.4f;     // 下方小 X 的检测点外移倍数
static const wchar_t* DEATH_X_PATCH_FILE_NAME = L"sprite(击杀大XX).NPK";
static const int ID_CHK_AUTO_CROP_BLACK_BARS = 1040;
static const int DNF_BLACK_BAR_PIXEL_MAX = 18;
static const int DNF_BLACK_BAR_MIN_EDGE = 4;
static const double DNF_BLACK_BAR_ROW_RATIO = 0.965;

// 打补丁文件路径检查辅助函数在文件后部实现；这里提前声明，方便同步给 Web。
static bool DnfFileExists(const CString& path);
static CString DnfJoinPath(const CString& a, const CString& b);
static void DnfSendWebToast(CWebScoreDlg* webDlg, const CString& action, const CString& message);
static bool DnfPostCloudJson(const std::string& jsonUtf8, std::string& responseUtf8, CString& errorMsg, int timeoutMs = 8000);
static CString DnfReadLocalLicenseKey();
static bool DnfWriteLocalLicenseKey(const CString& key);
static CString DnfReadLicenseFromFile();
static json DnfBuildScoreboardTextStylesJson(const CString& iniPath);
static void DnfSaveScoreboardTextStylesJson(const CString& iniPath, const json& styles);
static json DnfBuildKillDisplaySettingsJson(const CString& iniPath);
static void DnfSaveKillDisplaySettingsJson(const CString& iniPath, const json& settings);
static json DnfBuildInstalledFontListJson();
static bool DnfStartKillDisplayHttpServer(CDNFGameCaptureDlg* host, const CString& webDir, CString& errorMsg);
static void DnfStopKillDisplayHttpServer();
void WriteMatchLog(const CString& logLine);
void AppLog(const CString& msg, COLORREF color);

static const wchar_t* SCOREBOARD_STYLE_SECTION = L"ScoreboardTextStyles";
static const wchar_t* KILL_DISPLAY_LAYOUT_SECTION = L"KillDisplay";
static const wchar_t* KILL_DISPLAY_STYLE_SECTION = L"KillDisplayTextStyles";
static constexpr int KILL_DISPLAY_HTTP_PORT = 18777;
static constexpr wchar_t KILL_DISPLAY_OBS_URL_W[] = L"http://127.0.0.1:18777/kill.html";
static constexpr char KILL_DISPLAY_OBS_URL_UTF8[] = "http://127.0.0.1:18777/kill.html";

struct DnfScoreboardStyleDefault {
    const char* key;
    const wchar_t* fontFamily;
    int fontSize;
    const wchar_t* colorMode;
    const wchar_t* color;
    const wchar_t* strokeColor;
    int strokeWidth;
    int glow;
    int letterSpacing;
    bool allowTeamColor;
};

static const DnfScoreboardStyleDefault SCOREBOARD_STYLE_DEFAULTS[] = {
    { "teamName", L"Microsoft YaHei", 38, L"team",   L"#ffffff", L"#000000", 0, 8,  0, true },
    { "score",    L"Arial Black",     39, L"team",   L"#ffffff", L"#000000", 0, 12, 0, true },
    { "header",   L"Microsoft YaHei", 22, L"custom", L"#8b8b9f", L"#000000", 0, 0,  0, false },
    { "pickLabel",L"Microsoft YaHei", 18, L"custom", L"#a6b7bf", L"#000000", 1, 0,  0, false },
    { "playerName",L"Arial Black",    22, L"custom", L"#ffffff", L"#000000", 1, 2,  0, false },
    { "statNumber",L"Microsoft YaHei",25, L"custom", L"#ffffff", L"#000000", 1, 0,  0, false },
};

struct DnfKillDisplayLayoutDefault {
    const char* key;
    int value;
    int minValue;
    int maxValue;
};

static const DnfKillDisplayLayoutDefault KILL_DISPLAY_LAYOUT_DEFAULTS[] = {
    { "showDeathNumber", 0, 0,     1 },
    { "bgAlpha",          0, 0,   100 },
    { "panelAlpha",      49, 0,   100 },
    { "rowAlpha",         0, 0,   100 },
    { "canvasPadding",    0, 0,    40 },
    { "panelPadding",    14, 0,    40 },
    { "teamGap",          0, 0,    40 },
    { "rowGap",           0, 0,    20 },
    { "rowHeight",       48, 32,   90 },
    { "panelRadius",      0, 0,    28 },
    { "rowRadius",        0, 0,    22 },
    { "boardBorder",      0, 0,     6 },
    { "shadow",           0, 0,    48 },
    { "pickColumnWidth", 54, 36,  110 },
    { "statColumnWidth", 61, 28,   90 },
    { "akColumnWidth",   24, 24,   80 },
    { "pageScale",      100, 60,  180 },
    { "teamNameOffsetX", -9, -180, 180 },
    { "teamNameOffsetY", -9, -120, 120 },
    { "pickLabelOffsetX",-7, -180, 180 },
    { "pickLabelOffsetY", 0, -120, 120 },
    { "playerNameOffsetX",0, -180, 180 },
    { "playerNameOffsetY",0, -120, 120 },
    { "killNumberOffsetX",-7, -180, 180 },
    { "killNumberOffsetY",0, -120, 120 },
    { "deathNumberOffsetX",-11, -180, 180 },
    { "deathNumberOffsetY",0, -120, 120 },
    { "akMarkOffsetX",    2, -180, 180 },
    { "akMarkOffsetY",    0, -120, 120 },
    { "akCountBadgeOffsetX", 12, -80, 80 },
    { "akCountBadgeOffsetY",-26, -80, 80 },
};

static const DnfScoreboardStyleDefault KILL_DISPLAY_TEXT_STYLE_DEFAULTS[] = {
    { "teamName",    L"Microsoft YaHei", 54, L"team",   L"#ffffff", L"#000000", 4, 0, 0, true },
    { "score",       L"Arial Black",     70, L"team",   L"#ffffff", L"#000000", 3, 2, 0, true },
    { "header",      L"Microsoft YaHei", 31, L"custom", L"#a9abb9", L"#000000", 2, 0, 0, false },
    { "pickLabel",   L"Arial Black",     27, L"custom", L"#6fc8b9", L"#000000", 3, 0, 0, false },
    { "playerName",  L"Arial",           43, L"custom", L"#f7ca69", L"#000000", 5, 2, 0, false },
    { "killNumber",  L"FZXS24",          50, L"custom", L"#f7ca69", L"#000000", 4, 0, 0, false },
    { "deathNumber", L"FZXS24",          50, L"custom", L"#ab986d", L"#000000", 4, 0, 0, false },
    { "akMark",      L"FZXS24",          40, L"custom", L"#f7d67e", L"#000000", 3, 0, 0, false },
    { "akCountBadge",L"Microsoft YaHei", 30, L"custom", L"#f7d67e", L"#000000", 1, 0, 0, false },
};

static CString DnfMakeScoreboardStyleIniKey(const char* styleKey, const char* field)
{
    CString key = CA2W(styleKey, CP_UTF8);
    key += L".";
    key += CA2W(field, CP_UTF8);
    return key;
}

static int DnfClampScoreboardInt(int value, int minValue, int maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static bool DnfIsHexColorChar(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') ||
        (ch >= L'a' && ch <= L'f') ||
        (ch >= L'A' && ch <= L'F');
}

static CString DnfNormalizeScoreboardColor(CString value, const wchar_t* fallback)
{
    value.Trim();
    if ((value.GetLength() == 4 || value.GetLength() == 7) && value[0] == L'#') {
        bool valid = true;
        for (int i = 1; i < value.GetLength(); ++i) {
            if (!DnfIsHexColorChar(value[i])) {
                valid = false;
                break;
            }
        }
        if (valid) {
            value.MakeLower();
            return value;
        }
    }
    return CString(fallback);
}

static CString DnfNormalizeScoreboardFontFamily(CString value, const wchar_t* fallback)
{
    value.Replace(L"\r", L"");
    value.Replace(L"\n", L"");
    value.Replace(L"\"", L"");
    value.Replace(L"'", L"");
    value.Trim();
    if (value.GetLength() > 80) value = value.Left(80);
    if (value.IsEmpty()) value = fallback;
    return value;
}

static CString DnfNormalizeScoreboardColorMode(CString value, const DnfScoreboardStyleDefault& def)
{
    value.Trim();
    value.MakeLower();
    if (def.allowTeamColor && value == L"team") return L"team";
    if (value == L"custom") return L"custom";
    return CString(def.colorMode);
}

static CString DnfReadScoreboardStyleString(const CString& iniPath, const DnfScoreboardStyleDefault& def, const char* field, const wchar_t* fallback)
{
    if (iniPath.IsEmpty()) return CString(fallback);
    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    wchar_t buf[256] = { 0 };
    ::GetPrivateProfileString(SCOREBOARD_STYLE_SECTION, key, fallback, buf, 256, iniPath);
    return CString(buf);
}

static std::string DnfJsonUtf8(const CString& value)
{
    return std::string(CW2A(value, CP_UTF8));
}

static json DnfBuildScoreboardStyleJson(const CString& iniPath, const DnfScoreboardStyleDefault& def)
{
    CString fontFamily = DnfNormalizeScoreboardFontFamily(
        DnfReadScoreboardStyleString(iniPath, def, "fontFamily", def.fontFamily),
        def.fontFamily);
    CString colorMode = DnfNormalizeScoreboardColorMode(
        DnfReadScoreboardStyleString(iniPath, def, "colorMode", def.colorMode),
        def);
    CString color = DnfNormalizeScoreboardColor(
        DnfReadScoreboardStyleString(iniPath, def, "color", def.color),
        def.color);
    CString strokeColor = DnfNormalizeScoreboardColor(
        DnfReadScoreboardStyleString(iniPath, def, "strokeColor", def.strokeColor),
        def.strokeColor);

    int fontSize = def.fontSize;
    int strokeWidth = def.strokeWidth;
    int glow = def.glow;
    if (!iniPath.IsEmpty()) {
        CString fontSizeKey = DnfMakeScoreboardStyleIniKey(def.key, "fontSize");
        CString strokeWidthKey = DnfMakeScoreboardStyleIniKey(def.key, "strokeWidth");
        CString glowKey = DnfMakeScoreboardStyleIniKey(def.key, "glow");
        fontSize = ::GetPrivateProfileInt(SCOREBOARD_STYLE_SECTION, fontSizeKey, def.fontSize, iniPath);
        strokeWidth = ::GetPrivateProfileInt(SCOREBOARD_STYLE_SECTION, strokeWidthKey, def.strokeWidth, iniPath);
        glow = ::GetPrivateProfileInt(SCOREBOARD_STYLE_SECTION, glowKey, def.glow, iniPath);
    }

    json style;
    style["fontFamily"] = DnfJsonUtf8(fontFamily);
    style["fontSize"] = DnfClampScoreboardInt(fontSize, 10, 48);
    style["colorMode"] = DnfJsonUtf8(colorMode);
    style["color"] = DnfJsonUtf8(color);
    style["strokeColor"] = DnfJsonUtf8(strokeColor);
    style["strokeWidth"] = DnfClampScoreboardInt(strokeWidth, 0, 4);
    style["glow"] = DnfClampScoreboardInt(glow, 0, 24);
    return style;
}

static json DnfBuildScoreboardTextStylesJson(const CString& iniPath)
{
    json styles = json::object();
    for (const auto& def : SCOREBOARD_STYLE_DEFAULTS) {
        styles[def.key] = DnfBuildScoreboardStyleJson(iniPath, def);
    }
    return styles;
}

static CString DnfJsonScoreboardString(const json& style, const char* field, const wchar_t* fallback)
{
    if (style.is_object() && style.contains(field) && style[field].is_string()) {
        CString converted;
        converted = CA2W(style[field].get<std::string>().c_str(), CP_UTF8);
        return converted;
    }
    return CString(fallback);
}

static int DnfJsonScoreboardInt(const json& style, const char* field, int fallback)
{
    if (style.is_object() && style.contains(field) && style[field].is_number_integer()) {
        return style[field].get<int>();
    }
    if (style.is_object() && style.contains(field) && style[field].is_number()) {
        return (int)style[field].get<double>();
    }
    return fallback;
}

static void DnfWriteScoreboardStyleString(const CString& iniPath, const DnfScoreboardStyleDefault& def, const char* field, const CString& value)
{
    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    ::WritePrivateProfileString(SCOREBOARD_STYLE_SECTION, key, value, iniPath);
}

static void DnfWriteScoreboardStyleInt(const CString& iniPath, const DnfScoreboardStyleDefault& def, const char* field, int value)
{
    CString text;
    text.Format(L"%d", value);
    DnfWriteScoreboardStyleString(iniPath, def, field, text);
}

static void DnfSaveScoreboardTextStylesJson(const CString& iniPath, const json& styles)
{
    if (iniPath.IsEmpty() || !styles.is_object()) return;

    for (const auto& def : SCOREBOARD_STYLE_DEFAULTS) {
        json style = json::object();
        if (styles.contains(def.key) && styles[def.key].is_object()) {
            style = styles[def.key];
        }

        CString fontFamily = DnfNormalizeScoreboardFontFamily(
            DnfJsonScoreboardString(style, "fontFamily", def.fontFamily),
            def.fontFamily);
        CString colorMode = DnfNormalizeScoreboardColorMode(
            DnfJsonScoreboardString(style, "colorMode", def.colorMode),
            def);
        CString color = DnfNormalizeScoreboardColor(
            DnfJsonScoreboardString(style, "color", def.color),
            def.color);
        CString strokeColor = DnfNormalizeScoreboardColor(
            DnfJsonScoreboardString(style, "strokeColor", def.strokeColor),
            def.strokeColor);
        int fontSize = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "fontSize", def.fontSize), 10, 48);
        int strokeWidth = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "strokeWidth", def.strokeWidth), 0, 4);
        int glow = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "glow", def.glow), 0, 24);

        DnfWriteScoreboardStyleString(iniPath, def, "fontFamily", fontFamily);
        DnfWriteScoreboardStyleInt(iniPath, def, "fontSize", fontSize);
        DnfWriteScoreboardStyleString(iniPath, def, "colorMode", colorMode);
        DnfWriteScoreboardStyleString(iniPath, def, "color", color);
        DnfWriteScoreboardStyleString(iniPath, def, "strokeColor", strokeColor);
        DnfWriteScoreboardStyleInt(iniPath, def, "strokeWidth", strokeWidth);
        DnfWriteScoreboardStyleInt(iniPath, def, "glow", glow);
    }
}

static CString DnfReadStyleStringFromSection(const CString& iniPath, const wchar_t* section, const DnfScoreboardStyleDefault& def, const char* field, const wchar_t* fallback)
{
    if (iniPath.IsEmpty()) return CString(fallback);
    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    wchar_t buf[256] = { 0 };
    ::GetPrivateProfileString(section, key, fallback, buf, 256, iniPath);
    return CString(buf);
}

static bool DnfIsSplitKillStatStyle(const char* key)
{
    return strcmp(key, "killNumber") == 0 || strcmp(key, "deathNumber") == 0;
}

static CString DnfReadKillDisplayStyleString(const CString& iniPath, const DnfScoreboardStyleDefault& def, const char* field, const wchar_t* fallback)
{
    if (iniPath.IsEmpty()) return CString(fallback);

    static const wchar_t* missing = L"__DNF_MISSING_STYLE__";
    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    wchar_t buf[256] = { 0 };
    ::GetPrivateProfileString(KILL_DISPLAY_STYLE_SECTION, key, missing, buf, 256, iniPath);
    CString value(buf);
    if (value != missing) return value;

    if (DnfIsSplitKillStatStyle(def.key)) {
        CString legacyKey = DnfMakeScoreboardStyleIniKey("statNumber", field);
        ::GetPrivateProfileString(KILL_DISPLAY_STYLE_SECTION, legacyKey, fallback, buf, 256, iniPath);
        return CString(buf);
    }
    return CString(fallback);
}

static int DnfReadKillDisplayStyleInt(const CString& iniPath, const DnfScoreboardStyleDefault& def, const char* field, int fallback)
{
    if (iniPath.IsEmpty()) return fallback;

    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    int value = ::GetPrivateProfileInt(KILL_DISPLAY_STYLE_SECTION, key, INT_MIN, iniPath);
    if (value != INT_MIN) return value;

    if (DnfIsSplitKillStatStyle(def.key)) {
        CString legacyKey = DnfMakeScoreboardStyleIniKey("statNumber", field);
        return ::GetPrivateProfileInt(KILL_DISPLAY_STYLE_SECTION, legacyKey, fallback, iniPath);
    }
    return fallback;
}

static void DnfWriteStyleStringToSection(const CString& iniPath, const wchar_t* section, const DnfScoreboardStyleDefault& def, const char* field, const CString& value)
{
    CString key = DnfMakeScoreboardStyleIniKey(def.key, field);
    ::WritePrivateProfileString(section, key, value, iniPath);
}

static void DnfWriteStyleIntToSection(const CString& iniPath, const wchar_t* section, const DnfScoreboardStyleDefault& def, const char* field, int value)
{
    CString text;
    text.Format(L"%d", value);
    DnfWriteStyleStringToSection(iniPath, section, def, field, text);
}

static json DnfBuildKillDisplayStyleJson(const CString& iniPath, const DnfScoreboardStyleDefault& def)
{
    CString fontFamily = DnfNormalizeScoreboardFontFamily(
        DnfReadKillDisplayStyleString(iniPath, def, "fontFamily", def.fontFamily),
        def.fontFamily);
    CString colorMode = DnfNormalizeScoreboardColorMode(
        DnfReadKillDisplayStyleString(iniPath, def, "colorMode", def.colorMode),
        def);
    CString color = DnfNormalizeScoreboardColor(
        DnfReadKillDisplayStyleString(iniPath, def, "color", def.color),
        def.color);
    CString strokeColor = DnfNormalizeScoreboardColor(
        DnfReadKillDisplayStyleString(iniPath, def, "strokeColor", def.strokeColor),
        def.strokeColor);

    int fontSize = def.fontSize;
    int strokeWidth = def.strokeWidth;
    int glow = def.glow;
    int letterSpacing = def.letterSpacing;
    if (!iniPath.IsEmpty()) {
        fontSize = DnfReadKillDisplayStyleInt(iniPath, def, "fontSize", def.fontSize);
        strokeWidth = DnfReadKillDisplayStyleInt(iniPath, def, "strokeWidth", def.strokeWidth);
        glow = DnfReadKillDisplayStyleInt(iniPath, def, "glow", def.glow);
        letterSpacing = DnfReadKillDisplayStyleInt(iniPath, def, "letterSpacing", def.letterSpacing);
    }

    json style;
    style["fontFamily"] = DnfJsonUtf8(fontFamily);
    style["fontSize"] = DnfClampScoreboardInt(fontSize, 10, 76);
    style["colorMode"] = DnfJsonUtf8(colorMode);
    style["color"] = DnfJsonUtf8(color);
    style["strokeColor"] = DnfJsonUtf8(strokeColor);
    style["strokeWidth"] = DnfClampScoreboardInt(strokeWidth, 0, 8);
    style["glow"] = DnfClampScoreboardInt(glow, 0, 36);
    style["letterSpacing"] = DnfClampScoreboardInt(letterSpacing, -4, 16);
    return style;
}

static json DnfBuildKillDisplayTextStylesJson(const CString& iniPath)
{
    json styles = json::object();
    for (const auto& def : KILL_DISPLAY_TEXT_STYLE_DEFAULTS) {
        styles[def.key] = DnfBuildKillDisplayStyleJson(iniPath, def);
    }
    return styles;
}

static json DnfBuildKillDisplayLayoutJson(const CString& iniPath)
{
    json layout = json::object();
    for (const auto& def : KILL_DISPLAY_LAYOUT_DEFAULTS) {
        int value = def.value;
        if (!iniPath.IsEmpty()) {
            CString key = CA2W(def.key, CP_UTF8);
            value = ::GetPrivateProfileInt(KILL_DISPLAY_LAYOUT_SECTION, key, def.value, iniPath);
        }
        layout[def.key] = DnfClampScoreboardInt(value, def.minValue, def.maxValue);
    }
    return layout;
}

static json DnfBuildKillDisplaySettingsJson(const CString& iniPath)
{
    json settings;
    settings["obsUrl"] = KILL_DISPLAY_OBS_URL_UTF8;
    settings["layout"] = DnfBuildKillDisplayLayoutJson(iniPath);
    settings["textStyles"] = DnfBuildKillDisplayTextStylesJson(iniPath);
    return settings;
}

static void DnfSaveKillDisplaySettingsJson(const CString& iniPath, const json& settings)
{
    if (iniPath.IsEmpty() || !settings.is_object()) return;

    json layout = json::object();
    if (settings.contains("layout") && settings["layout"].is_object()) {
        layout = settings["layout"];
    }
    for (const auto& def : KILL_DISPLAY_LAYOUT_DEFAULTS) {
        int value = DnfClampScoreboardInt(DnfJsonScoreboardInt(layout, def.key, def.value), def.minValue, def.maxValue);
        CString key = CA2W(def.key, CP_UTF8);
        CString text;
        text.Format(L"%d", value);
        ::WritePrivateProfileString(KILL_DISPLAY_LAYOUT_SECTION, key, text, iniPath);
    }

    json styles = json::object();
    if (settings.contains("textStyles") && settings["textStyles"].is_object()) {
        styles = settings["textStyles"];
    }

    for (const auto& def : KILL_DISPLAY_TEXT_STYLE_DEFAULTS) {
        json style = json::object();
        if (styles.contains(def.key) && styles[def.key].is_object()) {
            style = styles[def.key];
        }

        CString fontFamily = DnfNormalizeScoreboardFontFamily(
            DnfJsonScoreboardString(style, "fontFamily", def.fontFamily),
            def.fontFamily);
        CString colorMode = DnfNormalizeScoreboardColorMode(
            DnfJsonScoreboardString(style, "colorMode", def.colorMode),
            def);
        CString color = DnfNormalizeScoreboardColor(
            DnfJsonScoreboardString(style, "color", def.color),
            def.color);
        CString strokeColor = DnfNormalizeScoreboardColor(
            DnfJsonScoreboardString(style, "strokeColor", def.strokeColor),
            def.strokeColor);
        int fontSize = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "fontSize", def.fontSize), 10, 76);
        int strokeWidth = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "strokeWidth", def.strokeWidth), 0, 8);
        int glow = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "glow", def.glow), 0, 36);
        int letterSpacing = DnfClampScoreboardInt(DnfJsonScoreboardInt(style, "letterSpacing", def.letterSpacing), -4, 16);

        DnfWriteStyleStringToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "fontFamily", fontFamily);
        DnfWriteStyleIntToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "fontSize", fontSize);
        DnfWriteStyleStringToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "colorMode", colorMode);
        DnfWriteStyleStringToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "color", color);
        DnfWriteStyleStringToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "strokeColor", strokeColor);
        DnfWriteStyleIntToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "strokeWidth", strokeWidth);
        DnfWriteStyleIntToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "glow", glow);
        DnfWriteStyleIntToSection(iniPath, KILL_DISPLAY_STYLE_SECTION, def, "letterSpacing", letterSpacing);
    }
}

struct DnfFontEnumContext {
    std::set<std::wstring> names;
};

static void DnfAppendFontName(DnfFontEnumContext& ctx, const wchar_t* name)
{
    CString fontName = name ? name : L"";
    fontName.Trim();
    if (fontName.IsEmpty() || fontName[0] == L'@') return;
    ctx.names.insert(std::wstring(fontName.GetString()));
}

static int CALLBACK DnfEnumFontFamExProc(const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM lParam)
{
    DnfFontEnumContext* ctx = reinterpret_cast<DnfFontEnumContext*>(lParam);
    if (ctx && lpelfe) DnfAppendFontName(*ctx, lpelfe->lfFaceName);
    return 1;
}

static json DnfBuildInstalledFontListJson()
{
    static json s_cachedFonts;
    static bool s_loaded = false;
    if (s_loaded) return s_cachedFonts;

    DnfFontEnumContext ctx;
    HDC hdc = ::GetDC(NULL);
    if (hdc) {
        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        ::EnumFontFamiliesExW(hdc, &lf, DnfEnumFontFamExProc, reinterpret_cast<LPARAM>(&ctx), 0);
        ::ReleaseDC(NULL, hdc);
    }

    DnfAppendFontName(ctx, L"Microsoft YaHei");
    DnfAppendFontName(ctx, L"SimHei");
    DnfAppendFontName(ctx, L"Arial");
    DnfAppendFontName(ctx, L"Arial Black");

    s_cachedFonts = json::array();
    for (const auto& name : ctx.names) {
        CString fontName(name.c_str());
        s_cachedFonts.push_back(DnfJsonUtf8(fontName));
    }
    s_loaded = true;
    return s_cachedFonts;
}

namespace {
    struct DnfKillDisplayHttpServerState {
        std::mutex mutex;
        std::thread worker;
        std::atomic<bool> stop{ false };
        std::atomic<bool> running{ false };
        SOCKET listenSocket = INVALID_SOCKET;
        CDNFGameCaptureDlg* host = nullptr;
        CString webDir;
    };

    DnfKillDisplayHttpServerState g_killDisplayHttpServer;
}

static bool DnfHttpSendAll(SOCKET client, const char* data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = ::send(client, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static void DnfHttpSendResponse(SOCKET client, int status, const char* reason, const char* contentType, const std::string& body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << reason << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        << "Connection: close\r\n\r\n";
    const std::string header = oss.str();
    DnfHttpSendAll(client, header.data(), (int)header.size());
    if (!body.empty()) {
        DnfHttpSendAll(client, body.data(), (int)body.size());
    }
}

static std::string DnfHttpContentTypeForPath(const std::string& path)
{
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript; charset=utf-8";
    return "application/octet-stream";
}

static bool DnfHttpReadStaticFile(const CString& path, std::string& body)
{
    CFile file;
    if (!file.Open(path, CFile::modeRead | CFile::shareDenyNone)) return false;
    ULONGLONG length = file.GetLength();
    if (length > 8ull * 1024ull * 1024ull) return false;

    body.resize((size_t)length);
    if (length > 0) {
        UINT read = file.Read(&body[0], (UINT)length);
        if (read != (UINT)length) return false;
    }
    return true;
}

static std::string DnfHttpParsePath(const std::string& request)
{
    const size_t lineEnd = request.find("\r\n");
    const std::string firstLine = request.substr(0, lineEnd == std::string::npos ? request.size() : lineEnd);
    const size_t firstSpace = firstLine.find(' ');
    if (firstSpace == std::string::npos) return "/";
    const size_t secondSpace = firstLine.find(' ', firstSpace + 1);
    std::string path = firstLine.substr(firstSpace + 1, secondSpace == std::string::npos ? std::string::npos : secondSpace - firstSpace - 1);
    const size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) path = path.substr(0, queryPos);
    if (path.empty()) path = "/";
    return path;
}

static std::string DnfHttpParseMethod(const std::string& request)
{
    const size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) return "";
    return request.substr(0, firstSpace);
}

static size_t DnfHttpHeaderEnd(const std::string& request)
{
    const size_t headerEnd = request.find("\r\n\r\n");
    return headerEnd == std::string::npos ? std::string::npos : headerEnd + 4;
}

static int DnfHttpContentLength(const std::string& request)
{
    const std::string needle = "\r\nContent-Length:";
    size_t pos = request.find(needle);
    if (pos == std::string::npos) {
        const std::string lowerNeedle = "\r\ncontent-length:";
        pos = request.find(lowerNeedle);
        if (pos == std::string::npos) return 0;
    }

    pos = request.find(':', pos);
    if (pos == std::string::npos) return 0;
    const size_t end = request.find("\r\n", pos);
    const std::string raw = request.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
    int value = atoi(raw.c_str());
    return value > 0 && value < 1024 * 1024 ? value : 0;
}

static void DnfHttpReadBodyIfNeeded(SOCKET client, std::string& request)
{
    const size_t headerEnd = DnfHttpHeaderEnd(request);
    if (headerEnd == std::string::npos) return;

    const int contentLength = DnfHttpContentLength(request);
    if (contentLength <= 0) return;

    while ((int)(request.size() - headerEnd) < contentLength && request.size() < headerEnd + (size_t)contentLength) {
        char buf[2048] = {};
        int n = ::recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, buf + n);
    }
}

static std::string DnfHttpRequestBody(const std::string& request)
{
    const size_t headerEnd = DnfHttpHeaderEnd(request);
    if (headerEnd == std::string::npos || headerEnd >= request.size()) return "";
    const int contentLength = DnfHttpContentLength(request);
    if (contentLength <= 0) return "";
    const size_t available = request.size() - headerEnd;
    const size_t bodyLength = available < (size_t)contentLength ? available : (size_t)contentLength;
    return request.substr(headerEnd, bodyLength);
}

static bool DnfKillDisplayStaticPathForRoute(const CString& webDir, const std::string& route, CString& outPath)
{
    std::string name;
    if (route == "/" || route == "/kill.html") name = "kill.html";
    else if (route == "/kill.css") name = "kill.css";
    else if (route == "/kill.js") name = "kill.js";
    else return false;

    CString fileName = CA2W(name.c_str(), CP_UTF8);
    outPath = DnfJoinPath(webDir, fileName);
    return true;
}

static void DnfHandleKillDisplayHttpClient(SOCKET client)
{
    DWORD timeout = 900;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::string request;
    request.reserve(4096);
    char buf[2048] = {};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        int n = ::recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, buf + n);
    }
    DnfHttpReadBodyIfNeeded(client, request);

    const std::string method = DnfHttpParseMethod(request);
    if (method == "OPTIONS") {
        DnfHttpSendResponse(client, 204, "No Content", "text/plain; charset=utf-8", "");
        return;
    }
    if (method != "GET" && method != "POST") {
        DnfHttpSendResponse(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", "GET/POST only");
        return;
    }

    const std::string route = DnfHttpParsePath(request);
    if (route == "/api/state") {
        if (method != "GET") {
            DnfHttpSendResponse(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", "GET only");
            return;
        }
        CDNFGameCaptureDlg* host = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
            host = g_killDisplayHttpServer.host;
        }
        const std::string body = host ? host->BuildKillDisplayStatePayload() : "{\"action\":\"sync_state\",\"data\":{}}";
        DnfHttpSendResponse(client, 200, "OK", "application/json; charset=utf-8", body);
        return;
    }

    if (route == "/api/kill-display-settings") {
        if (method != "POST") {
            DnfHttpSendResponse(client, 405, "Method Not Allowed", "text/plain; charset=utf-8", "POST only");
            return;
        }
        CDNFGameCaptureDlg* host = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
            host = g_killDisplayHttpServer.host;
        }
        std::string responseBody;
        if (!host || !host->SaveKillDisplaySettingsPayload(DnfHttpRequestBody(request), responseBody)) {
            if (responseBody.empty()) responseBody = "{\"ok\":false}";
            DnfHttpSendResponse(client, 400, "Bad Request", "application/json; charset=utf-8", responseBody);
            return;
        }
        DnfHttpSendResponse(client, 200, "OK", "application/json; charset=utf-8", responseBody);
        return;
    }

    CString staticPath;
    CString webDir;
    {
        std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
        webDir = g_killDisplayHttpServer.webDir;
    }
    if (!DnfKillDisplayStaticPathForRoute(webDir, route, staticPath)) {
        DnfHttpSendResponse(client, 404, "Not Found", "text/plain; charset=utf-8", "Not found");
        return;
    }

    std::string body;
    if (!DnfHttpReadStaticFile(staticPath, body)) {
        DnfHttpSendResponse(client, 404, "Not Found", "text/plain; charset=utf-8", "Static file missing");
        return;
    }
    DnfHttpSendResponse(client, 200, "OK", DnfHttpContentTypeForPath(route).c_str(), body);
}

static void DnfKillDisplayHttpWorker(SOCKET listenSocket)
{
    g_killDisplayHttpServer.running = true;
    while (!g_killDisplayHttpServer.stop.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);
        timeval tv = {};
        tv.tv_sec = 0;
        tv.tv_usec = 250000;
        int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        SOCKET client = ::accept(listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        DnfHandleKillDisplayHttpClient(client);
        ::shutdown(client, SD_BOTH);
        ::closesocket(client);
    }

    ::closesocket(listenSocket);
    {
        std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
        g_killDisplayHttpServer.listenSocket = INVALID_SOCKET;
        g_killDisplayHttpServer.host = nullptr;
    }
    g_killDisplayHttpServer.running = false;
    WSACleanup();
}

static bool DnfStartKillDisplayHttpServer(CDNFGameCaptureDlg* host, const CString& webDir, CString& errorMsg)
{
    errorMsg.Empty();

    if (host == nullptr) {
        errorMsg = L"展示页服务启动失败：主程序对象为空。";
        return false;
    }

    if (!DnfFileExists(DnfJoinPath(webDir, L"kill.html")) ||
        !DnfFileExists(DnfJoinPath(webDir, L"kill.css")) ||
        !DnfFileExists(DnfJoinPath(webDir, L"kill.js"))) {
        errorMsg = L"展示页服务启动失败：缺少 kill.html / kill.css / kill.js。";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
        if (g_killDisplayHttpServer.running.load()) {
            g_killDisplayHttpServer.host = host;
            g_killDisplayHttpServer.webDir = webDir;
            return true;
        }
    }

    WSADATA wsaData = {};
    int wsa = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa != 0) {
        errorMsg.Format(L"展示页服务启动失败：WSAStartup=%d。", wsa);
        return false;
    }

    SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        int err = WSAGetLastError();
        WSACleanup();
        errorMsg.Format(L"展示页服务启动失败：socket=%d。", err);
        return false;
    }

    BOOL reuseAddr = TRUE;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KILL_DISPLAY_HTTP_PORT);
    InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        ::closesocket(listenSocket);
        WSACleanup();
        errorMsg.Format(L"展示页服务启动失败：127.0.0.1:%d 被占用或不可用，错误=%d。", KILL_DISPLAY_HTTP_PORT, err);
        return false;
    }

    if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        ::closesocket(listenSocket);
        WSACleanup();
        errorMsg.Format(L"展示页服务启动失败：listen=%d。", err);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
        g_killDisplayHttpServer.stop = false;
        g_killDisplayHttpServer.host = host;
        g_killDisplayHttpServer.webDir = webDir;
        g_killDisplayHttpServer.listenSocket = listenSocket;
        g_killDisplayHttpServer.worker = std::thread(DnfKillDisplayHttpWorker, listenSocket);
    }

    return true;
}

static void DnfStopKillDisplayHttpServer()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_killDisplayHttpServer.mutex);
        g_killDisplayHttpServer.stop = true;
        if (g_killDisplayHttpServer.worker.joinable()) {
            worker = std::move(g_killDisplayHttpServer.worker);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
}

static bool DnfCopyUnicodeTextToClipboard(HWND owner, const CString& text)
{
    if (!::OpenClipboard(owner)) return false;
    ::EmptyClipboard();

    const SIZE_T bytes = (text.GetLength() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        ::CloseClipboard();
        return false;
    }

    void* ptr = ::GlobalLock(hMem);
    if (!ptr) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }
    memcpy(ptr, text.GetString(), bytes);
    ::GlobalUnlock(hMem);

    if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }

    ::CloseClipboard();
    return true;
}

static bool DnfIsProcessRunningByName(const CString& processName)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    bool found = false;
    if (Process32First(hSnap, &pe)) {
        do {
            CString currentName(pe.szExeFile);
            if (currentName.CompareNoCase(processName) == 0) {
                found = true;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return found;
}

static bool DnfGetProcessImagePathByName(const CString& processName, CString& pathOut)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    bool found = false;
    if (Process32First(hSnap, &pe)) {
        do {
            CString currentName(pe.szExeFile);
            if (currentName.CompareNoCase(processName) != 0) continue;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProcess) continue;

            wchar_t buf[MAX_PATH] = { 0 };
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, buf, &size)) {
                pathOut = buf;
                pathOut.Trim();
                found = !pathOut.IsEmpty();
            }
            CloseHandle(hProcess);
            if (found) break;
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return found;
}

static const wchar_t* DNF_CLOUD_API_HOST = L"verifykey-thaovfpoib.cn-hangzhou.fcapp.run";
static const wchar_t* DNF_LICENSE_REG_PATH = L"Software\\DNFCapture";
static const wchar_t* DNF_LICENSE_REG_VALUE = L"LicenseKey";
static CString g_lastLicenseSource = L"未读取";
static CString g_lastLicenseRepair = L"";

static bool DnfIsBlackBarPixel(BYTE r, BYTE g, BYTE b)
{
    return r <= DNF_BLACK_BAR_PIXEL_MAX &&
           g <= DNF_BLACK_BAR_PIXEL_MAX &&
           b <= DNF_BLACK_BAR_PIXEL_MAX;
}

static bool DnfIsBlackBarRow(const std::vector<BYTE>& pixels, int w, int h, int y)
{
    if (w <= 0 || h <= 0 || y < 0 || y >= h) return false;

    int blackCount = 0;
    const BYTE* row = pixels.data() + (size_t)y * w * 4;
    for (int x = 0; x < w; ++x) {
        const BYTE* px = row + (size_t)x * 4;
        if (DnfIsBlackBarPixel(px[2], px[1], px[0])) ++blackCount;
    }
    return blackCount >= (int)(w * DNF_BLACK_BAR_ROW_RATIO + 0.5);
}

static bool DnfIsBlackBarColumn(const std::vector<BYTE>& pixels, int w, int h, int x)
{
    if (w <= 0 || h <= 0 || x < 0 || x >= w) return false;

    int blackCount = 0;
    for (int y = 0; y < h; ++y) {
        const BYTE* px = pixels.data() + ((size_t)y * w + x) * 4;
        if (DnfIsBlackBarPixel(px[2], px[1], px[0])) ++blackCount;
    }
    return blackCount >= (int)(h * DNF_BLACK_BAR_ROW_RATIO + 0.5);
}

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

static int DnfFindAliasSharp(const CString& aliasRaw)
{
    int halfSharp = aliasRaw.Find(L'#');
    int fullSharp = aliasRaw.Find(L'＃');
    if (halfSharp >= 0 && fullSharp >= 0) return halfSharp < fullSharp ? halfSharp : fullSharp;
    return halfSharp >= 0 ? halfSharp : fullSharp;
}

static CString DnfExtractAliasRealId(const CString& aliasRaw, bool& hasArea, bool& hasJob)
{
    hasArea = false;
    hasJob = false;

    CString body = aliasRaw;
    body.Trim();

    int sharp = DnfFindAliasSharp(body);
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

static CString DnfAliasDuplicateKey(const CString& aliasRaw)
{
    CString key = aliasRaw;
    key.Trim();
    if (key.IsEmpty()) return key;

    int sharp = DnfFindAliasSharp(key);
    if (sharp >= 0) key = key.Left(sharp);
    key.Trim();
    return key;
}

static CString DnfAliasDisplayName(const CString& aliasRaw, bool showDeclaredJob)
{
    CString alias = aliasRaw;
    alias.Trim();
    if (alias.IsEmpty() || showDeclaredJob) return alias;
    CString display = DnfAliasDuplicateKey(alias);
    return display.IsEmpty() ? alias : display;
}

static bool DnfAliasSameDuplicateId(const CString& a, const CString& b)
{
    CString ka = DnfAliasDuplicateKey(a);
    CString kb = DnfAliasDuplicateKey(b);
    return !ka.IsEmpty() && !kb.IsEmpty() && ka == kb;
}

static CString DnfAliasJobKey(const CString& aliasRaw)
{
    CString alias = aliasRaw;
    alias.Trim();
    int sharp = DnfFindAliasSharp(alias);
    if (sharp < 0) return L"";

    CString job = alias.Mid(sharp + 1);
    job.Trim();
    return job;
}

static bool DnfAliasHasDeclaredJob(const CString& aliasRaw)
{
    return !DnfAliasJobKey(aliasRaw).IsEmpty();
}

static bool DnfAliasSameStorageEntry(const CString& a, const CString& b)
{
    CString aa = a;
    CString bb = b;
    aa.Trim();
    bb.Trim();
    if (aa.IsEmpty() || bb.IsEmpty()) return false;
    if (aa == bb) return true;
    if (!DnfAliasSameDuplicateId(aa, bb)) return false;

    CString aj = DnfAliasJobKey(aa);
    CString bj = DnfAliasJobKey(bb);
    bool ah = !aj.IsEmpty();
    bool bh = !bj.IsEmpty();
    if (ah || bh) return ah && bh && aj == bj;
    return true;
}

static bool DnfAliasCloudContainsNakedAlias(const CString& cloudAliasRaw, const CString& localAliasRaw)
{
    CString localAlias = localAliasRaw;
    CString cloudAlias = cloudAliasRaw;
    localAlias.Trim();
    cloudAlias.Trim();
    if (localAlias.IsEmpty() || cloudAlias.IsEmpty()) return false;
    if (DnfAliasHasDeclaredJob(localAlias)) return false;
    if (DnfAliasHasDeclaredJob(cloudAlias) && DnfAliasSameStorageEntry(cloudAlias, localAlias)) return true;
    return cloudAlias.Find(localAlias) >= 0 && cloudAlias != localAlias;
}

static bool DnfAliasBlocksSamePlayerAlias(const CString& existingAliasRaw, const CString& candidateAliasRaw)
{
    CString existingAlias = existingAliasRaw;
    CString candidateAlias = candidateAliasRaw;
    existingAlias.Trim();
    candidateAlias.Trim();
    if (existingAlias.IsEmpty() || candidateAlias.IsEmpty()) return false;
    if (DnfAliasSameStorageEntry(existingAlias, candidateAlias)) return true;
    if (!DnfAliasHasDeclaredJob(candidateAlias)) {
        if (DnfAliasHasDeclaredJob(existingAlias) && DnfAliasSameDuplicateId(existingAlias, candidateAlias)) return true;
        if (existingAlias != candidateAlias && existingAlias.Find(candidateAlias) >= 0) return true;
    }
    return false;
}

enum DnfAliasMergeResult
{
    DnfAliasMergeNone = 0,
    DnfAliasMergeAdded = 1,
    DnfAliasMergeUpgraded = 2
};

static DnfAliasMergeResult DnfMergeAliasIntoList(std::vector<CString>& aliases, const CString& aliasRaw)
{
    CString alias = aliasRaw;
    alias.Trim();
    if (alias.IsEmpty()) return DnfAliasMergeNone;

    bool aliasHasJob = DnfAliasHasDeclaredJob(alias);
    for (auto& oldAlias : aliases) {
        oldAlias.Trim();
        if (oldAlias.IsEmpty()) continue;

        bool oldHasJob = DnfAliasHasDeclaredJob(oldAlias);
        if (DnfAliasSameStorageEntry(oldAlias, alias)) {
            if (!oldHasJob && aliasHasJob) {
                oldAlias = alias;
                return DnfAliasMergeUpgraded;
            }
            return DnfAliasMergeNone;
        }

        if (DnfAliasSameDuplicateId(oldAlias, alias)) {
            if (!oldHasJob && aliasHasJob) {
                oldAlias = alias;
                return DnfAliasMergeUpgraded;
            }
            if (oldHasJob && !aliasHasJob) {
                return DnfAliasMergeNone;
            }
        }
        if (!aliasHasJob && oldAlias != alias && oldAlias.Find(alias) >= 0) {
            return DnfAliasMergeNone;
        }
    }

    aliases.push_back(alias);
    return DnfAliasMergeAdded;
}

static DnfAliasMergeResult DnfMergeAliasIntoAliasDataList(std::vector<AliasData>& aliases, const CString& aliasRaw)
{
    CString alias = aliasRaw;
    alias.Trim();
    if (alias.IsEmpty()) return DnfAliasMergeNone;

    bool aliasHasJob = DnfAliasHasDeclaredJob(alias);
    for (auto& oldAlias : aliases) {
        oldAlias.name.Trim();
        if (oldAlias.name.IsEmpty()) continue;

        bool oldHasJob = DnfAliasHasDeclaredJob(oldAlias.name);
        if (DnfAliasSameStorageEntry(oldAlias.name, alias)) {
            if (!oldHasJob && aliasHasJob) {
                oldAlias.name = alias;
                return DnfAliasMergeUpgraded;
            }
            return DnfAliasMergeNone;
        }

        if (DnfAliasSameDuplicateId(oldAlias.name, alias)) {
            if (!oldHasJob && aliasHasJob) {
                oldAlias.name = alias;
                return DnfAliasMergeUpgraded;
            }
            if (oldHasJob && !aliasHasJob) {
                return DnfAliasMergeNone;
            }
        }
        if (!aliasHasJob && oldAlias.name != alias && oldAlias.name.Find(alias) >= 0) {
            return DnfAliasMergeNone;
        }
    }

    AliasData ad;
    ad.name = alias;
    aliases.push_back(ad);
    return DnfAliasMergeAdded;
}

static void DnfNormalizeAliasDataList(std::vector<AliasData>& aliases)
{
    std::vector<AliasData> normalized;
    for (const auto& alias : aliases) {
        DnfMergeAliasIntoAliasDataList(normalized, alias.name);
    }
    aliases.swap(normalized);
}

static CString DnfSerializeAliasDataListRaw(const std::vector<AliasData>& aliases)
{
    CString out;
    for (const auto& alias : aliases) {
        CString name = alias.name;
        name.Trim();
        if (!name.IsEmpty()) out += L"(" + name + L")";
    }
    return out;
}

static CString DnfFormatClockNow()
{
    time_t now_t = time(0);
    tm t;
    localtime_s(&t, &now_t);
    CString s;
    s.Format(L"%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return s;
}

struct TDnfSimpleAliasMeta
{
    CString raw;
    CString matchId;
    CString realId;
    CString area;
    CString job;
    bool hasArea = false;
    bool hasJob = false;
};

struct TDnfPreparedIdMatch
{
    CString candidateId;
    CString ocrId;
    CString mode;
    int score = 0;
    int realIdLen = 0;
    int ocrRealIdLen = 0;
    int realIdLcs = 0;
    bool candidateHasArea = false;
    bool ocrHasArea = false;
    bool bothHaveArea = false;
    bool usedRealId = false;
    bool oneCharRealId = false;
    bool allowStrongIdLock = false;
    bool areaShortIdAssist = false;
    CString note;
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


// ------------------------------------------------------------
// 模糊匹配辅助：ID / 大区 / 职业都不能再用“完全相等”判断。
// OCR 经常把“柔道家”识别成“柔道 / 柔道室”，把大区数字漏掉，
// 所以这里统一做归一化 + 包含 + 公共连续片段的模糊分。
// ------------------------------------------------------------
static int DnfLongestCommonSubstringLen(const CString& a, const CString& b)
{
    CString x = DnfNormalizeLooseText(a);
    CString y = DnfNormalizeLooseText(b);
    if (x.IsEmpty() || y.IsEmpty()) return 0;
    int best = 0;
    for (int i = 0; i < x.GetLength(); ++i) {
        for (int j = 0; j < y.GetLength(); ++j) {
            int k = 0;
            while (i + k < x.GetLength() && j + k < y.GetLength() && x[i + k] == y[j + k]) ++k;
            if (k > best) best = k;
        }
    }
    return best;
}

static int DnfFuzzyTextScore(const CString& a, const CString& b)
{
    CString x = DnfNormalizeLooseText(a);
    CString y = DnfNormalizeLooseText(b);
    if (x.IsEmpty() || y.IsEmpty()) return 0;
    if (x == y) return 100;
    if (x.Find(y) >= 0 || y.Find(x) >= 0) {
        int mn = min(x.GetLength(), y.GetLength());
        if (mn >= 2) return 88;
        return 70;
    }

    // 两字短 ID 的 OCR 经常错一个字，例如“夏雫”识别成“夏乘”。
    // 这种不能当成强命中，但在职业/大区辅助存在时应该给到可用的弱 ID 分。
    if (x.GetLength() == 2 && y.GetLength() == 2) {
        int samePos = 0;
        if (x[0] == y[0]) samePos++;
        if (x[1] == y[1]) samePos++;
        if (samePos == 1) return 62;
    }

    int lcs = DnfLongestCommonSubstringLen(x, y);
    int mx = max(x.GetLength(), y.GetLength());
    int mn = min(x.GetLength(), y.GetLength());
    if (mx <= 0) return 0;
    int score = (lcs * 100) / mx;
    if (lcs >= 2 && mn <= 3) score = max(score, 76); // 柔道室 vs 柔道家、夏乘 vs 夏雫这类短文本
    return score;
}

static int DnfNgramCoverageScore(const CString& candidateText, const CString& ocrText, int n)
{
    CString x = DnfNormalizeLooseText(candidateText);
    CString y = DnfNormalizeLooseText(ocrText);
    if (x.IsEmpty() || y.IsEmpty() || n <= 0 || x.GetLength() < n) return 0;

    int total = x.GetLength() - n + 1;
    int hits = 0;
    for (int i = 0; i <= x.GetLength() - n; ++i) {
        CString part = x.Mid(i, n);
        if (!part.IsEmpty() && y.Find(part) >= 0) hits++;
    }
    if (total <= 0) return 0;
    return (hits * 100) / total;
}

static int DnfFallbackIdPartScore(const CString& candidateText, const CString& ocrText)
{
    CString x = DnfNormalizeLooseText(candidateText);
    CString y = DnfNormalizeLooseText(ocrText);
    if (x.IsEmpty() || y.IsEmpty()) return 0;

    int candLen = x.GetLength();
    if (x == y) return candLen <= 1 ? 35 : 100;
    if (y.Find(x) >= 0) return candLen <= 1 ? 28 : 100;

    int lcs = DnfLongestCommonSubstringLen(x, y);
    if (lcs <= 0) return 0;
    if (candLen <= 1) return min(28, lcs * 28);
    if (candLen == 2 && lcs == 1) return 28;

    int bigramScore = DnfNgramCoverageScore(x, y, 2);
    int trigramScore = DnfNgramCoverageScore(x, y, 3);
    int score = max(bigramScore, trigramScore);

    // n-gram 分数能避免“同大区”公共片段独自把所有候选都抬到高分；
    // LCS 只在候选很短、且确实出现连续两字以上时兜底补一点相似度。
    if (lcs >= 2 && candLen <= 4) {
        score = max(score, 55);
    }

    int lenDiff = abs(candLen - y.GetLength());
    if (lenDiff > candLen + 2) {
        score -= min(25, (lenDiff - candLen - 2) * 3);
    }
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return score;
}

static int DnfFallbackIdScore(const CString& candidateId, const CString& ocrText)
{
    CString x = DnfNormalizeLooseText(candidateId);
    CString y = DnfNormalizeLooseText(ocrText);
    if (x.IsEmpty() || y.IsEmpty()) return 0;
    return DnfFallbackIdPartScore(candidateId, ocrText);
}

static int DnfExactFullIdWithAreaScore(const CString& candArea, const CString& candReal,
    const CString& ocrArea, const CString& ocrReal)
{
    CString candForms[2] = { candArea + candReal, candReal + candArea };
    CString ocrForms[2] = { ocrArea + ocrReal, ocrReal + ocrArea };
    int best = 0;
    for (const CString& c : candForms) {
        for (const CString& o : ocrForms) {
            best = max(best, DnfFallbackIdScore(c, o));
        }
    }
    return best;
}

static CString DnfNormalizeJobAlias(CString s)
{
    CString n = DnfNormalizeLooseText(s);
    if (n.IsEmpty()) return n;

    // 常见职业/OCR别名归一化。这里不是强匹配，只是把明显同义词拉到同一名字，后面仍会走模糊分。
    if (n.Find(L"柔道") >= 0) return L"柔道家";
    if (n.Find(L"枪炮") >= 0 || n.Find(L"大枪") >= 0) return L"枪炮师";
    if (n.Find(L"漫游") >= 0) return L"漫游枪手";
    if (n.Find(L"蓝拳") >= 0) return L"蓝拳使者";
    if (n.Find(L"次元") >= 0) return L"次元行者";
    if (n.Find(L"魔道") >= 0) return L"魔道学者";
    if (n.Find(L"驱魔") >= 0) return L"驱魔师";
    if (n.Find(L"气功") >= 0) return L"气功师";
    if (n.Find(L"合金") >= 0) return L"合金战士";
    if (n.Find(L"剑魂") >= 0) return L"剑魂";
    if (n.Find(L"散打") >= 0) return L"散打";
    return n;
}


static CString DnfStripJobWordsFromName(CString s)
{
    s.Trim();
    if (s.IsEmpty()) return s;

    // OCR 经常把职业和ID粘在一起，例如“上海1柔道夏乘”。
    // 第一轮可以按整体名称匹配；二轮职业兜底时，需要把职业词剥掉，留下“夏乘”再去和“夏雫”模糊比。
    static const wchar_t* kJobWords[] = {
        L"柔道家", L"柔道室", L"柔道",
        L"枪炮师", L"枪炮", L"大枪",
        L"漫游枪手", L"漫游枪丢", L"漫游",
        L"蓝拳使者", L"蓝拳圣使", L"蓝拳",
        L"次元行者", L"次元",
        L"魔道学者", L"魔道",
        L"驱魔师", L"驱魔",
        L"气功师", L"气功",
        L"合金战士", L"合金",
        L"剑魂", L"散打"
    };

    for (const wchar_t* w : kJobWords) {
        s.Replace(w, L"");
    }
    s.Trim();
    return s;
}

static int DnfFuzzyJobScore(const CString& declaredJob, const CString& ocrJob)
{
    CString a = DnfNormalizeJobAlias(declaredJob);
    CString b = DnfNormalizeJobAlias(ocrJob);
    if (a.IsEmpty() || b.IsEmpty()) return 0;
    return DnfFuzzyTextScore(a, b);
}

static bool DnfFuzzyJobSame(const CString& declaredJob, const CString& ocrJob, int* outScore = nullptr)
{
    int s = DnfFuzzyJobScore(declaredJob, ocrJob);
    if (outScore) *outScore = s;
    return s >= 72;
}

static int DnfAreaDigit(CString s)
{
    s.Trim();
    for (int i = s.GetLength() - 1; i >= 0; --i) {
        wchar_t ch = s[i];
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
    }
    return -1;
}

static bool DnfAreaWeakSame(const CString& a, const CString& b);

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

    meta.matchId = body;
    meta.matchId.Trim();
    meta.hasArea = DnfTryExtractAreaToken(body, meta.area);
    body.Trim();
    meta.realId = body;
    meta.realId.Trim();
    return meta;
}

static TDnfPreparedIdMatch DnfPrepareIdMatch(const TDnfSimpleAliasMeta& candMeta, const TDnfSimpleAliasMeta& ocrMeta)
{
    TDnfPreparedIdMatch out;
    out.candidateHasArea = candMeta.hasArea;
    out.ocrHasArea = ocrMeta.hasArea;
    out.bothHaveArea = candMeta.hasArea && ocrMeta.hasArea;

    CString candFull = candMeta.matchId.IsEmpty() ? candMeta.raw : candMeta.matchId;
    CString ocrFull = ocrMeta.matchId.IsEmpty() ? ocrMeta.raw : ocrMeta.matchId;
    CString candReal = candMeta.realId.IsEmpty() ? candFull : candMeta.realId;
    CString ocrReal = ocrMeta.realId.IsEmpty() ? ocrFull : ocrMeta.realId;
    candFull.Trim(); ocrFull.Trim(); candReal.Trim(); ocrReal.Trim();

    if (out.bothHaveArea) {
        out.candidateId = candFull;
        out.ocrId = ocrFull;
        out.mode = L"完整ID";
        out.usedRealId = false;
    }
    else {
        out.candidateId = candReal;
        out.ocrId = ocrReal;
        out.mode = L"真实ID";
        out.usedRealId = true;
    }

    CString nCandReal = DnfNormalizeLooseText(candReal);
    out.oneCharRealId = (nCandReal.GetLength() <= 1);
    CString nOcrReal = DnfNormalizeLooseText(ocrReal);
    out.realIdLen = nCandReal.GetLength();
    out.ocrRealIdLen = nOcrReal.GetLength();
    out.realIdLcs = DnfLongestCommonSubstringLen(candReal, ocrReal);

    int realScore = DnfFallbackIdScore(candReal, ocrReal);
    int exactFullScore = out.bothHaveArea
        ? DnfExactFullIdWithAreaScore(candMeta.area, candReal, ocrMeta.area, ocrReal)
        : 0;

    bool oneCharExactRealId = out.oneCharRealId && !nCandReal.IsEmpty() && nCandReal == nOcrReal;
    bool twoCharAreaAssist = out.bothHaveArea && out.realIdLen == 2 && out.realIdLcs >= 1 && realScore > 0;
    out.areaShortIdAssist = twoCharAreaAssist;

    if (out.bothHaveArea && exactFullScore >= 95) {
        out.score = exactFullScore;
    }
    else if (twoCharAreaAssist) {
        out.score = max(realScore, 55);
    }
    else {
        out.score = realScore;
    }

    if (out.bothHaveArea && exactFullScore >= 95 && (!out.oneCharRealId || oneCharExactRealId)) {
        out.allowStrongIdLock = true;
        out.note = out.oneCharRealId ? L"两边都有大区，且1字真实ID精确一致" : L"两边都有大区，完整ID高置信";
    }
    else if (twoCharAreaAssist) {
        out.allowStrongIdLock = true;
        out.note = L"2字真实ID命中1字，且两边大区一致，仅作为短ID辅助";
    }
    else if (!out.oneCharRealId) {
        out.allowStrongIdLock = true;
        out.note = out.usedRealId ? L"按真实ID匹配" : L"按完整ID匹配";
    }
    else {
        out.allowStrongIdLock = false;
        out.note = L"真实ID仅1字，不作为强ID锁定依据";
    }

    return out;
}

static TDnfPreparedIdMatch DnfPrepareIdMatch(const CString& candidateId, const CString& ocrText)
{
    TDnfSimpleAliasMeta candMeta = DnfParseAliasMeta(candidateId);
    TDnfSimpleAliasMeta ocrMeta = DnfParseAliasMeta(ocrText);
    return DnfPrepareIdMatch(candMeta, ocrMeta);
}

static bool DnfAliasMetaExactSame(const TDnfSimpleAliasMeta& a, const TDnfSimpleAliasMeta& b)
{
    CString aid = DnfNormalizeLooseText(a.realId);
    CString bid = DnfNormalizeLooseText(b.realId);
    if (aid.IsEmpty() || bid.IsEmpty() || aid != bid) return false;

    if (a.hasArea && b.hasArea && !DnfAreaWeakSame(a.area, b.area)) return false;
    if (a.hasJob && b.hasJob && !DnfFuzzyJobSame(a.job, b.job)) return false;
    return true;
}

static int DnfMetaContextScore(const TDnfSimpleAliasMeta& cand, const TDnfSimpleAliasMeta& ocr, int visibleIdLen, bool& strongContext)
{
    int score = 0;
    strongContext = false;

    if (cand.hasArea && ocr.hasArea) {
        // 大区不再作为普通上下文加分或放行条件。
        // 它只在 DnfPrepareIdMatch() 中用于决定完整ID/真实ID打分，以及 2 字短 ID 的窄辅助。
        if (!DnfAreaWeakSame(cand.area, ocr.area)) score -= (visibleIdLen <= 2) ? 6 : 3;
    }

    if (cand.hasJob && ocr.hasJob) {
        int jobScore = 0;
        if (DnfFuzzyJobSame(cand.job, ocr.job, &jobScore)) {
            score += (visibleIdLen <= 2) ? 28 : 14;
            strongContext = true;
        }
        else {
            score -= (visibleIdLen <= 2) ? 10 : 5;
        }
    }

    return score;
}


// ============================================================
// 【简化匹配辅助】
// 普通长 ID 仍走旧算法；这里只服务“两字短ID / 纯符号ID”的职业、大区、同队唯一兜底。
// ============================================================
static bool DnfIsSymbolLikeRealId(const CString& rawId)
{
    CString s = rawId;
    s.Trim();
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

    // 纯符号，或“有效字很少且夹杂符号”的 ID，都按弱 ID 处理。
    if (meaningful == 0) return true;
    if (meaningful <= 1 && symbol >= 1) return true;
    if (meaningful <= 2 && symbol >= meaningful) return true;
    return false;
}

static CString DnfAreaBaseName(const CString& area)
{
    CString s = area;
    s.Trim();
    while (!s.IsEmpty()) {
        wchar_t ch = s[s.GetLength() - 1];
        if (ch >= L'0' && ch <= L'9') s.Delete(s.GetLength() - 1, 1);
        else break;
    }
    return s;
}

static bool DnfAreaWeakSame(const CString& a, const CString& b)
{
    CString aa = DnfAreaBaseName(a);
    CString bb = DnfAreaBaseName(b);
    if (aa.IsEmpty() || bb.IsEmpty()) return false;
    // 大区也做模糊：上海1 / 上海、浙江3 / 浙江8 这类算弱一致。
    return DnfFuzzyTextScore(aa, bb) >= 70;
}

static bool DnfRawContainsWeakArea(const CString& raw, CString& weakAreaOut)
{
    static const wchar_t* kAreas[] = {
        L"广东", L"北京", L"上海", L"江苏", L"浙江", L"福建", L"四川", L"山东", L"河南", L"湖北", L"湖南",
        L"河北", L"辽宁", L"吉林", L"黑龙江", L"安徽", L"江西", L"广西", L"陕西", L"山西", L"重庆", L"天津",
        L"云南", L"贵州", L"新疆", L"西藏", L"青海", L"甘肃", L"宁夏", L"内蒙古", L"东北", L"西北", L"西南", L"跨"
    };
    CString norm = DnfNormalizeLooseText(raw);
    for (const wchar_t* area : kAreas) {
        CString a(area);
        CString na = DnfNormalizeLooseText(a);
        if (norm.Find(na) >= 0 || DnfFuzzyTextScore(norm, na) >= 72) {
            weakAreaOut = a;
            return true;
        }
    }
    return false;
}

static CString DnfBoolCN(bool v) { return v ? L"是" : L"否"; }

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
    msg.Format(L"小号【%s】是2字短ID，真实ID少于3个字符且没有大区/#职业，容易误识别；不会自动删除，但开始监控前需要补充大区或 #职业。", (LPCTSTR)alias);
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

    // 允许小号列表保留 2 字短ID；短ID只在“开始监控”时拦截，不在 Web 同步阶段删除/清空。
    return true;
}

static bool DnfValidateAliasListShortMeta(const std::vector<CString>& aliases, CString& errorMsg)
{
    for (const auto& alias : aliases) {
        if (!DnfValidateAliasShortMeta(alias, errorMsg)) return false;
    }
    return true;
}

static std::vector<CString> DnfParseAliasListString(const CString& aliasesRaw)
{
    std::vector<CString> aliases;
    CString text = aliasesRaw;
    int curPos = 0;
    CString token = text.Tokenize(L"()（） \t\r\n", curPos);
    while (token != L"") {
        DnfMergeAliasIntoList(aliases, token);
        token = text.Tokenize(L"()（） \t\r\n", curPos);
    }
    return aliases;
}

static CString DnfFormatAliasListString(const std::vector<CString>& aliases)
{
    CString out;
    std::vector<CString> uniqueAliases;
    for (const auto& alias : aliases) {
        DnfMergeAliasIntoList(uniqueAliases, alias);
    }
    for (const auto& alias : uniqueAliases) {
        out += L"(" + alias + L")";
    }
    return out;
}

static bool DnfAliasListContainsExact(const std::vector<CString>& aliases, const CString& targetRaw)
{
    CString target = targetRaw;
    target.Trim();
    if (target.IsEmpty()) return false;
    for (auto alias : aliases) {
        alias.Trim();
        if (alias == target) return true;
    }
    return false;
}

static CString DnfJoinAliasNames(const std::vector<CString>& aliases, int maxItems = 12)
{
    CString out;
    int count = 0;
    for (auto alias : aliases) {
        alias.Trim();
        if (alias.IsEmpty()) continue;
        if (!out.IsEmpty()) out += L" / ";
        out += alias;
        count++;
        if (count >= maxItems) break;
    }
    if ((int)aliases.size() > maxItems) {
        CString more;
        more.Format(L" / ...等%d个", (int)aliases.size());
        out += more;
    }
    return out;
}

static bool DnfAliasListSameSet(const std::vector<CString>& a, const std::vector<CString>& b)
{
    if (a.size() != b.size()) return false;
    for (const auto& alias : a) {
        if (!DnfAliasListContainsExact(b, alias)) return false;
    }
    return true;
}

static std::vector<CString> DnfAliasListSubtractExact(const std::vector<CString>& left, const std::vector<CString>& right)
{
    std::vector<CString> out;
    for (const auto& alias : left) {
        if (!DnfAliasListContainsExact(right, alias)) out.push_back(alias);
    }
    return out;
}

static std::vector<CString> DnfAliasesFromJsonValue(const json& value)
{
    std::vector<CString> aliases;
    if (value.is_string()) {
        CString aliasListText = CA2W(value.get<std::string>().c_str(), CP_UTF8);
        aliases = DnfParseAliasListString(aliasListText);
    }
    else if (value.is_array()) {
        for (const auto& item : value) {
            if (!item.is_string()) continue;
            CString aliasText = CA2W(item.get<std::string>().c_str(), CP_UTF8);
            DnfMergeAliasIntoList(aliases, aliasText);
        }
    }
    return aliases;
}

static std::map<CString, std::vector<CString>> DnfBuildAliasSnapshotFromPayload(const std::string& aliasDbPayload)
{
    std::map<CString, std::vector<CString>> out;
    if (aliasDbPayload.empty()) return out;

    json root = json::parse(aliasDbPayload);
    if (!root.is_object()) return out;

    for (auto it = root.begin(); it != root.end(); ++it) {
        CString mainName = CA2W(it.key().c_str(), CP_UTF8);
        mainName.Trim();
        if (mainName.IsEmpty()) continue;

        std::vector<CString> aliases = DnfAliasesFromJsonValue(it.value());
        if (!aliases.empty()) out[mainName] = aliases;
    }
    return out;
}

static std::map<CString, std::vector<CString>> DnfBuildAliasSnapshotFromBaseline(const std::map<CString, CString>& baselinePlayers)
{
    std::map<CString, std::vector<CString>> out;
    for (const auto& pair : baselinePlayers) {
        CString mainName = pair.first;
        mainName.Trim();
        if (mainName.IsEmpty()) continue;

        std::vector<CString> aliases = DnfParseAliasListString(pair.second);
        if (!aliases.empty()) out[mainName] = aliases;
    }
    return out;
}

static CString DnfFormatMaybeEmptyAliasList(const std::vector<CString>& aliases)
{
    CString text = DnfJoinAliasNames(aliases);
    return text.IsEmpty() ? L"无小号" : text;
}

static void DnfLogAliasPushDiff(const std::map<CString, CString>& baselinePlayers, const std::string& filteredAliasDbPayload, int containedNakedAliasCount)
{
    std::map<CString, std::vector<CString>> before = DnfBuildAliasSnapshotFromBaseline(baselinePlayers);
    std::map<CString, std::vector<CString>> after = DnfBuildAliasSnapshotFromPayload(filteredAliasDbPayload);

    std::vector<CString> addedMains;
    std::vector<CString> removedMains;
    std::vector<CString> changedMains;

    for (const auto& pair : after) {
        if (before.find(pair.first) == before.end()) {
            addedMains.push_back(pair.first);
        }
        else if (!DnfAliasListSameSet(before[pair.first], pair.second)) {
            changedMains.push_back(pair.first);
        }
    }

    for (const auto& pair : before) {
        if (after.find(pair.first) == after.end()) removedMains.push_back(pair.first);
    }

    CString renamedMainOld;
    CString renamedMainNew;
    bool hasMainRename = false;
    if (addedMains.size() == 1 && removedMains.size() == 1 && DnfAliasListSameSet(before[removedMains[0]], after[addedMains[0]])) {
        hasMainRename = true;
        renamedMainOld = removedMains[0];
        renamedMainNew = addedMains[0];
    }

    int addedAliasCount = 0;
    int removedAliasCount = 0;
    int renameCount = hasMainRename ? 1 : 0;
    int changedMainCount = 0;

    struct AliasChangeLog {
        CString mainName;
        std::vector<CString> added;
        std::vector<CString> removed;
        CString renameOld;
        CString renameNew;
    };
    std::vector<AliasChangeLog> aliasLogs;

    for (const CString& mainName : changedMains) {
        std::vector<CString> addedAliases = DnfAliasListSubtractExact(after[mainName], before[mainName]);
        std::vector<CString> removedAliases = DnfAliasListSubtractExact(before[mainName], after[mainName]);
        if (addedAliases.empty() && removedAliases.empty()) continue;

        AliasChangeLog log;
        log.mainName = mainName;
        if (addedAliases.size() == 1 && removedAliases.size() == 1) {
            log.renameOld = removedAliases[0];
            log.renameNew = addedAliases[0];
            renameCount++;
        }
        else {
            log.added = addedAliases;
            log.removed = removedAliases;
            addedAliasCount += (int)addedAliases.size();
            removedAliasCount += (int)removedAliases.size();
        }
        aliasLogs.push_back(log);
        changedMainCount++;
    }

    int visibleAddedMainCount = (int)addedMains.size() - (hasMainRename ? 1 : 0);
    int visibleRemovedMainCount = (int)removedMains.size() - (hasMainRename ? 1 : 0);
    for (const CString& mainName : addedMains) {
        if (hasMainRename && mainName == renamedMainNew) continue;
        addedAliasCount += (int)after[mainName].size();
    }
    for (const CString& mainName : removedMains) {
        if (hasMainRename && mainName == renamedMainOld) continue;
        removedAliasCount += (int)before[mainName].size();
    }

    CString summary;
    summary.Format(L"☁️ [推送明细] 本次推送：新增主号 %d、删除主号 %d、修改主号 %d、新增小号 %d、删除小号 %d、改名 %d",
        visibleAddedMainCount,
        visibleRemovedMainCount,
        changedMainCount,
        addedAliasCount,
        removedAliasCount,
        renameCount);
    AppLog(summary, RGB(80, 220, 180));

    if (containedNakedAliasCount > 0) {
        CString filtered;
        filtered.Format(L"☁️ [推送预过滤] 本地预过滤裸ID %d 个，未作为待审差异提交。", containedNakedAliasCount);
        AppLog(filtered, RGB(255, 200, 90));
    }

    if (hasMainRename) {
        AppLog(L"☁️ [推送主号改名] " + renamedMainOld + L" => " + renamedMainNew + L" -> " + DnfFormatMaybeEmptyAliasList(after[renamedMainNew]), RGB(255, 220, 120));
    }

    for (const CString& mainName : addedMains) {
        if (hasMainRename && mainName == renamedMainNew) continue;
        AppLog(L"☁️ [推送新增主号] " + mainName + L" -> " + DnfFormatMaybeEmptyAliasList(after[mainName]), RGB(100, 255, 140));
    }

    for (const CString& mainName : removedMains) {
        if (hasMainRename && mainName == renamedMainOld) continue;
        AppLog(L"☁️ [推送删除主号] " + mainName + L" -> " + DnfFormatMaybeEmptyAliasList(before[mainName]), RGB(255, 120, 100));
    }

    for (const auto& log : aliasLogs) {
        if (!log.renameOld.IsEmpty() || !log.renameNew.IsEmpty()) {
            AppLog(L"☁️ [推送小号改名] " + log.mainName + L" -> " + log.renameOld + L" => " + log.renameNew, RGB(255, 220, 120));
        }
        if (!log.added.empty()) {
            AppLog(L"☁️ [推送新增小号] " + log.mainName + L" -> " + DnfJoinAliasNames(log.added), RGB(100, 255, 140));
        }
        if (!log.removed.empty()) {
            AppLog(L"☁️ [推送删除小号] " + log.mainName + L" -> " + DnfJoinAliasNames(log.removed), RGB(255, 190, 90));
        }
    }

    if (visibleAddedMainCount == 0 && visibleRemovedMainCount == 0 && changedMainCount == 0 && !hasMainRename) {
        AppLog(L"☁️ [推送明细] 本次没有发现需要提交审核的具体差异。", RGB(160, 170, 180));
    }
}

static CString DnfNormalizeAliasListString(const CString& aliasesRaw)
{
    return DnfFormatAliasListString(DnfParseAliasListString(aliasesRaw));
}

static CString DnfRemoveAliasFromAliasListString(const CString& aliasesRaw, const CString& aliasToRemove)
{
    CString target = aliasToRemove;
    target.Trim();
    std::vector<CString> kept;
    for (auto alias : DnfParseAliasListString(aliasesRaw)) {
        alias.Trim();
        if (!target.IsEmpty() && DnfAliasSameStorageEntry(alias, target)) continue;
        DnfMergeAliasIntoList(kept, alias);
    }
    return DnfFormatAliasListString(kept);
}

static CString DnfRenameAliasInAliasListString(const CString& aliasesRaw, const CString& oldAlias, const CString& newAlias)
{
    CString oldClean = oldAlias;
    CString newClean = newAlias;
    oldClean.Trim();
    newClean.Trim();

    bool renamed = false;
    std::vector<CString> out;
    for (auto alias : DnfParseAliasListString(aliasesRaw)) {
        alias.Trim();
        if (!oldClean.IsEmpty() && DnfAliasSameStorageEntry(alias, oldClean)) {
            renamed = true;
            DnfMergeAliasIntoList(out, newClean);
        }
        else {
            DnfMergeAliasIntoList(out, alias);
        }
    }
    if (!renamed && !newClean.IsEmpty()) DnfMergeAliasIntoList(out, newClean);
    return DnfFormatAliasListString(out);
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

static bool IsValidDeathPoint(ScorePointF pt)
{
    return pt.x > 0.0f && pt.x < 1.0f && pt.y > 0.0f && pt.y < 1.0f;
}

static bool IsActiveDeathPoint(int logicalIdx)
{
    return logicalIdx == DP_LEFT_ACTIVE || logicalIdx == DP_RIGHT_ACTIVE;
}

static void GetDeathXColorStep(int logicalIdx, float& stepX, float& stepY)
{
    stepX = 0.015f / 4.0f;
    stepY = 0.025f / 4.0f;
    if (!IsActiveDeathPoint(logicalIdx)) {
        stepX /= 2.0f;
        stepY /= 2.0f;
    }
}

static int BuildDeathXColorSamples(int logicalIdx, ScorePointF center, float outX[DEATH_X_COLOR_SAMPLE_COUNT], float outY[DEATH_X_COLOR_SAMPLE_COUNT])
{
    float stepX = 0.0f;
    float stepY = 0.0f;
    GetDeathXColorStep(logicalIdx, stepX, stepY);

    int count = 0;
    for (int i = 1; i <= 4 && count + 3 < DEATH_X_COLOR_SAMPLE_COUNT; ++i) {
        outX[count] = center.x - i * stepX; outY[count] = center.y - i * stepY; ++count; // 左上
        outX[count] = center.x + i * stepX; outY[count] = center.y + i * stepY; ++count; // 右下
        outX[count] = center.x + i * stepX; outY[count] = center.y - i * stepY; ++count; // 右上
        outX[count] = center.x - i * stepX; outY[count] = center.y + i * stepY; ++count; // 左下
    }
    return count;
}

static void GetDeathXPatchStep(int logicalIdx, float& stepX, float& stepY, float& patchMul)
{
    stepX = 0.015f / 4.0f;
    stepY = 0.015f / 4.0f;
    if (!IsActiveDeathPoint(logicalIdx)) {
        stepX /= 2.0f;
        stepY /= 2.0f;
    }
    patchMul = IsActiveDeathPoint(logicalIdx) ? DEATH_X_PATCH_MUL_ACTIVE : DEATH_X_PATCH_MUL_NORMAL;
}

static int BuildDeathXPatchSamples(int logicalIdx, ScorePointF center, float outX[DEATH_X_PATCH_SAMPLE_COUNT], float outY[DEATH_X_PATCH_SAMPLE_COUNT])
{
    float stepX = 0.0f;
    float stepY = 0.0f;
    float patchMul = 1.0f;
    GetDeathXPatchStep(logicalIdx, stepX, stepY, patchMul);

    outX[0] = center.x;                    outY[0] = center.y - patchMul * stepY; // 上
    outX[1] = center.x;                    outY[1] = center.y + patchMul * stepY; // 下
    outX[2] = center.x - patchMul * stepX; outY[2] = center.y;                    // 左
    outX[3] = center.x + patchMul * stepX; outY[3] = center.y;                    // 右
    return DEATH_X_PATCH_SAMPLE_COUNT;
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
    COLORREF hitColor[DEATH_POINT_COUNT][16];       // 命中点实际像素颜色
    int colorSampleCount[DEATH_POINT_COUNT];
    float colorSampleX[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT];     // 大X颜色算法全部理论采样点
    float colorSampleY[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT];
    bool colorSampleHit[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT];    // 当前帧该理论点是否命中红橙
    COLORREF colorSampleColor[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT];
    int patchPointCount[DEATH_POINT_COUNT];
    float patchX[DEATH_POINT_COUNT][4];             // 打补丁红蓝判断 4 个检测点
    float patchY[DEATH_POINT_COUNT][4];
    COLORREF patchColor[DEATH_POINT_COUNT][4];      // 4 点实际像素颜色
    int patchClass[DEATH_POINT_COUNT][4];           // 0=不合格，1=红，2=蓝
    bool patchPass[DEATH_POINT_COUNT];
    DWORD lastTick;
};

DeathXDebugState g_deathXDebug = {};
std::mutex g_deathXDebugMutex;

// Timer(2) 当前含义：0=无，1=普通击杀冷却，2=局间/大比分冷却。
// 只在普通击杀冷却里允许记录“待触发X”；局间冷却不补触发，避免残留X重复计分。
static int g_triggerCooldownKind = 0;
static bool g_leftActiveWasDead = false;
static bool g_rightActiveWasDead = false;
static int g_pendingActiveDeathSide = -1; // 0=左侧，1=右侧，-1=无
static DWORD g_pendingActiveDeathTick = 0;
static CString g_pendingActiveDeathReason;
static DWORD g_lastDeathXBlockedLogTick = 0;

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

void CDNFGameCaptureDlg::ResetDeathXStableState()
{
    memset(m_deathXStableState, 0, sizeof(m_deathXStableState));
    memset(m_deathXStableOn, 0, sizeof(m_deathXStableOn));
    memset(m_deathXStableOff, 0, sizeof(m_deathXStableOff));
}

void CDNFGameCaptureDlg::ResetMatchCooldownState(const CString& reason)
{
    KillTimer(2);
    KillTimer(4);
    m_bCanTrigger = TRUE;
    m_bCanTriggerTeamScore = TRUE;
    m_bPendingTeamScoreWin = false;
    m_lastKillerTeam = -1;
    g_triggerCooldownKind = 0;
    ResetDeathXStableState();

    g_leftActiveWasDead = false;
    g_rightActiveWasDead = false;
    g_pendingActiveDeathSide = -1;
    g_pendingActiveDeathTick = 0;
    g_pendingActiveDeathReason.Empty();
    g_lastDeathXBlockedLogTick = 0;

    CString line;
    line.Format(L"[冷却重置] 来源=%s；已清空击杀冷却、局间冷却、队伍分冷却、待触发X和死亡X稳定状态。",
        reason.GetString());
    WriteMatchLog(line);
}

ScorePointF CDNFGameCaptureDlg::GetDeathXPoint(int logicalIdx) const
{
    if (logicalIdx < 0 || logicalIdx >= DEATH_POINT_COUNT) return GetDeathLogicPoint(0);
    return m_deathXPoints[logicalIdx];
}

void CDNFGameCaptureDlg::SetDeathXPoint(int logicalIdx, ScorePointF pt)
{
    if (logicalIdx < 0 || logicalIdx >= DEATH_POINT_COUNT) return;
    pt.x = max(0.0f, min(1.0f, pt.x));
    pt.y = max(0.0f, min(1.0f, pt.y));
    m_deathXPoints[logicalIdx] = pt;
    ResetDeathXStableState();
}

void CDNFGameCaptureDlg::SelectDeathXPoint(int logicalIdx)
{
    if (logicalIdx < 0 || logicalIdx >= DEATH_POINT_COUNT) return;
    m_selectedDeathXPoint = logicalIdx;
    InvalidateRect(&m_previewRect, FALSE);
}

bool CDNFGameCaptureDlg::MoveSelectedDeathXPointByPixels(int dx, int dy)
{
    if (!m_bDeathXCalibrationMode ||
        m_selectedDeathXPoint < 0 ||
        m_selectedDeathXPoint >= DEATH_POINT_COUNT ||
        m_previewRect.Width() <= 0 ||
        m_previewRect.Height() <= 0) {
        return false;
    }

    ScorePointF pt = GetDeathXPoint(m_selectedDeathXPoint);
    pt.x += (float)dx / (float)max(1, m_previewRect.Width());
    pt.y += (float)dy / (float)max(1, m_previewRect.Height());
    SetDeathXPoint(m_selectedDeathXPoint, pt);
    InvalidateRect(&m_previewRect, FALSE);
    return true;
}

void CDNFGameCaptureDlg::ApplyDefaultDeathXPoints()
{
    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        m_deathXPoints[i] = GetDeathLogicPoint(i);
    }
    m_bDeathXCustomPoints = false;
    ResetDeathXStableState();
}

void CDNFGameCaptureDlg::SnapshotDeathXCalibration()
{
    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        m_deathXSnapshotPoints[i] = m_deathXPoints[i];
    }
}

CPoint CDNFGameCaptureDlg::DeathXPointToClient(ScorePointF pt) const
{
    return CPoint(
        m_previewRect.left + (int)(pt.x * m_previewRect.Width()),
        m_previewRect.top + (int)(pt.y * m_previewRect.Height()));
}

ScorePointF CDNFGameCaptureDlg::ClientToDeathXPoint(CPoint point) const
{
    ScorePointF pt;
    pt.x = (float)(point.x - m_previewRect.left) / (float)max(1, m_previewRect.Width());
    pt.y = (float)(point.y - m_previewRect.top) / (float)max(1, m_previewRect.Height());
    pt.x = max(0.0f, min(1.0f, pt.x));
    pt.y = max(0.0f, min(1.0f, pt.y));
    return pt;
}

int CDNFGameCaptureDlg::HitTestDeathXPoint(CPoint point) const
{
    if (!m_bDeathXCalibrationMode || m_previewRect.Width() <= 0 || m_previewRect.Height() <= 0) return -1;
    int bestIdx = -1;
    int bestDist2 = 999999;
    const int hitRadius = 18;
    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        CPoint p = DeathXPointToClient(m_deathXPoints[i]);
        int dx = point.x - p.x;
        int dy = point.y - p.y;
        int d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestIdx = i;
        }
    }
    return bestDist2 <= hitRadius * hitRadius ? bestIdx : -1;
}

void CDNFGameCaptureDlg::LoadDeathXCalibrationFromIni()
{
    ApplyDefaultDeathXPoints();
    if (m_iniPath.IsEmpty()) return;
    if (GetPrivateProfileInt(L"Settings", L"DeathXCustomEnabled", 0, m_iniPath) != 1) return;

    ScorePointF loaded[DEATH_POINT_COUNT] = {};
    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        CString key;
        key.Format(L"DeathXPoint%d", i);
        wchar_t buf[64] = {};
        ::GetPrivateProfileString(L"Settings", key, L"", buf, 64, m_iniPath);
        float x = -1.0f, y = -1.0f;
        if (swscanf_s(buf, L"%f,%f", &x, &y) != 2) {
            ApplyDefaultDeathXPoints();
            return;
        }
        loaded[i] = { x, y };
        if (!IsValidDeathPoint(loaded[i])) {
            ApplyDefaultDeathXPoints();
            return;
        }
    }

    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        m_deathXPoints[i] = loaded[i];
    }
    m_bDeathXCustomPoints = true;
    ResetDeathXStableState();
}

void CDNFGameCaptureDlg::SaveDeathXCalibrationToIni()
{
    bool sameAsDefault = true;
    for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
        ScorePointF defPt = GetDeathLogicPoint(i);
        if (fabs(m_deathXPoints[i].x - defPt.x) > 0.00005f ||
            fabs(m_deathXPoints[i].y - defPt.y) > 0.00005f) {
            sameAsDefault = false;
            break;
        }
    }

    if (sameAsDefault) {
        ::WritePrivateProfileString(L"Settings", L"DeathXCustomEnabled", L"0", m_iniPath);
        for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
            CString key;
            key.Format(L"DeathXPoint%d", i);
            ::WritePrivateProfileString(L"Settings", key, NULL, m_iniPath);
        }
        m_bDeathXCustomPoints = false;
    }
    else {
        ::WritePrivateProfileString(L"Settings", L"DeathXCustomEnabled", L"1", m_iniPath);
        for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
            CString key, val;
            key.Format(L"DeathXPoint%d", i);
            val.Format(L"%.6f,%.6f", m_deathXPoints[i].x, m_deathXPoints[i].y);
            ::WritePrivateProfileString(L"Settings", key, val, m_iniPath);
        }
        m_bDeathXCustomPoints = true;
    }
    ResetDeathXStableState();
}

void CDNFGameCaptureDlg::UpdateDeathXCalibrationButtons()
{
    if (!m_btnDeathXSave.m_hWnd || !m_btnDeathXCancel.m_hWnd || !m_btnDeathXDefault.m_hWnd) return;
    bool show = m_bDeathXCalibrationMode && m_previewRect.Width() > 0 && m_previewRect.Height() > 0;
    if (!show) {
        m_btnDeathXSave.ShowWindow(SW_HIDE);
        m_btnDeathXCancel.ShowWindow(SW_HIDE);
        m_btnDeathXDefault.ShowWindow(SW_HIDE);
        return;
    }

    const int footerGap = 8;
    const int footerH = 54;
    const int btnH = 28;
    const int btnYGap = 22;
    const int footerTop = max(m_previewRect.top + 10, m_previewRect.bottom - footerGap - footerH);
    const int x = m_previewRect.left + 10;
    const int y = footerTop + btnYGap;
    m_btnDeathXSave.MoveWindow(x, y, 90, btnH);
    m_btnDeathXCancel.MoveWindow(x + 96, y, 70, btnH);
    m_btnDeathXDefault.MoveWindow(x + 172, y, 90, btnH);
    m_btnDeathXSave.ShowWindow(show ? SW_SHOW : SW_HIDE);
    m_btnDeathXCancel.ShowWindow(show ? SW_SHOW : SW_HIDE);
    m_btnDeathXDefault.ShowWindow(show ? SW_SHOW : SW_HIDE);
}

void CDNFGameCaptureDlg::EnterDeathXCalibrationMode()
{
    if (m_bDeathXCalibrationMode) return;
    SnapshotDeathXCalibration();
    m_bDeathXCalibrationMode = true;
    m_selectedDeathXPoint = 0;
    m_dragDeathXPoint = -1;
    m_bDraggingDeathXPoint = false;
    if (m_btnDeathXCalibrate.m_hWnd) m_btnDeathXCalibrate.SetWindowText(L"校准中");
    UpdateDeathXCalibrationButtons();
    ResetDeathXStableState();
    InvalidateRect(&m_previewRect, FALSE);
    SetFocus();
    AppLog(L"🎯 [X校准] 已进入死亡X拖拽校准模式，拖动预览上的 8 个点实时调整。", RGB(0, 255, 255));
}

void CDNFGameCaptureDlg::ExitDeathXCalibrationMode(bool restoreSnapshot)
{
    if (restoreSnapshot) {
        for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
            m_deathXPoints[i] = m_deathXSnapshotPoints[i];
        }
    }
    m_bDeathXCalibrationMode = false;
    m_selectedDeathXPoint = -1;
    m_dragDeathXPoint = -1;
    m_bDraggingDeathXPoint = false;
    if (GetCapture() == this) ReleaseCapture();
    if (m_btnDeathXCalibrate.m_hWnd) m_btnDeathXCalibrate.SetWindowText(L"X校准");
    UpdateDeathXCalibrationButtons();
    ResetDeathXStableState();
    InvalidateRect(&m_previewRect, FALSE);
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

    // 如果专业后台窗口被隐藏，就把原生对话框挂到 Web 计分板上，并居中到 Web 窗口。
    // 这样从 Web 端操作时，确认/提醒框不会跑到隐藏的 C++ 后台窗口中心。
    HWND hOwner = this->GetSafeHwnd();
    if (!IsWindowVisible() && m_pWebDlg && ::IsWindow(m_pWebDlg->GetSafeHwnd()) && m_pWebDlg->IsWindowVisible()) {
        hOwner = m_pWebDlg->GetSafeHwnd();
    }

    g_hMsgBoxParent = hOwner;
    // 挂上只针对当前线程的拦截钩子
    g_hMsgBoxHook = SetWindowsHookEx(WH_CBT, MsgBoxCBTProc, NULL, GetCurrentThreadId());

    // 呼出系统的 MessageBox，它刚探出头就会被钩子按在当前可见主界面正中间！
    return ::MessageBox(hOwner, lpszText, lpszCaption, nType);
}


// ========================================================
// 更新弹窗：用只读多行文本框显示更新内容，保留 update_v2.txt 原始换行/缩进。
// MessageBox 会自动换行，导致更新日志格式看起来乱；这里改为等宽字体 + 横向滚动。
// ========================================================
struct TDnfUpdateDialogState {
    CString headerText;
    CString updateLogText;
    int result = IDNO;
    HFONT hUiFont = NULL;
    HFONT hMonoFont = NULL;
    HWND hHeader = NULL;
    HWND hEdit = NULL;
    HWND hQuestion = NULL;
    HWND hYes = NULL;
    HWND hNo = NULL;
};

static LRESULT CALLBACK DnfUpdateDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TDnfUpdateDialogState* st = (TDnfUpdateDialogState*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        st = (TDnfUpdateDialogState*)cs->lpCreateParams;
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)st);

        st->hUiFont = ::CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        // 更新日志里有中文，Consolas 在部分系统/EDIT 控件里不会做中文字体回退，
        // 会显示成方块；改用新宋体，既能显示中文，又尽量保留等宽排版。
        st->hMonoFont = ::CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"NSimSun");

        st->hHeader = ::CreateWindowExW(0, L"STATIC", st->headerText,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 18, 720, 52, hWnd, NULL, AfxGetInstanceHandle(), NULL);

        st->hEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->updateLogText,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_HSCROLL | WS_VSCROLL,
            24, 78, 720, 330, hWnd, NULL, AfxGetInstanceHandle(), NULL);

        st->hQuestion = ::CreateWindowExW(0, L"STATIC", L"是否立即更新？",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 424, 260, 28, hWnd, NULL, AfxGetInstanceHandle(), NULL);

        st->hYes = ::CreateWindowExW(0, L"BUTTON", L"是(&Y)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            530, 456, 96, 34, hWnd, (HMENU)IDYES, AfxGetInstanceHandle(), NULL);
        st->hNo = ::CreateWindowExW(0, L"BUTTON", L"否(&N)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            648, 456, 96, 34, hWnd, (HMENU)IDNO, AfxGetInstanceHandle(), NULL);

        HWND ctrls[] = { st->hHeader, st->hEdit, st->hQuestion, st->hYes, st->hNo };
        for (HWND h : ctrls) {
            if (h) ::SendMessage(h, WM_SETFONT, (WPARAM)st->hUiFont, TRUE);
        }
        if (st->hEdit) ::SendMessage(st->hEdit, WM_SETFONT, (WPARAM)st->hMonoFont, TRUE);

        ::SetFocus(st->hYes);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDYES || LOWORD(wParam) == IDNO) {
            if (st) st->result = LOWORD(wParam);
            ::DestroyWindow(hWnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (st) st->result = IDNO;
        ::DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (st) {
            if (st->hUiFont) { ::DeleteObject(st->hUiFont); st->hUiFont = NULL; }
            if (st->hMonoFont) { ::DeleteObject(st->hMonoFont); st->hMonoFont = NULL; }
        }
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

static CString DnfNormalizeDialogCrlf(CString s)
{
    s.Replace(L"\r\n", L"\n");
    s.Replace(L"\r", L"\n");
    s.Replace(L"\n", L"\r\n");
    return s;
}

int CDNFGameCaptureDlg::ShowUpdateConfirmDialog(const CString& serverVersion, const CString& currentVersion, const CString& visibleUpdateLog)
{
    HWND hOwner = this->GetSafeHwnd();
    if (!IsWindowVisible() && m_pWebDlg && ::IsWindow(m_pWebDlg->GetSafeHwnd()) && m_pWebDlg->IsWindowVisible()) {
        hOwner = m_pWebDlg->GetSafeHwnd();
    }

    TDnfUpdateDialogState st;
    st.headerText.Format(L"发现新版本: %s\r\n当前版本: %s\r\n更新内容：以下按服务器 update_v2.txt 排版显示",
        serverVersion.GetString(), currentVersion.GetString());
    st.updateLogText = DnfNormalizeDialogCrlf(visibleUpdateLog);

    static bool s_registered = false;
    const wchar_t* kClassName = L"DNF_UpdateConfirmDialog";
    if (!s_registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DnfUpdateDialogProc;
        wc.hInstance = AfxGetInstanceHandle();
        wc.lpszClassName = kClassName;
        wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        ::RegisterClassW(&wc);
        s_registered = true;
    }

    const int dlgW = 790;
    const int dlgH = 540;
    int x = 100, y = 100;
    RECT pr = {};
    if (hOwner && ::IsWindow(hOwner)) {
        ::GetWindowRect(hOwner, &pr);
        x = pr.left + ((pr.right - pr.left) - dlgW) / 2;
        y = pr.top + ((pr.bottom - pr.top) - dlgH) / 2;
    }
    else {
        x = (::GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
        y = (::GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;
    }
    x = max(0, x);
    y = max(0, y);

    bool ownerWasEnabled = false;
    if (hOwner && ::IsWindow(hOwner)) {
        ownerWasEnabled = ::IsWindowEnabled(hOwner) != FALSE;
        if (ownerWasEnabled) ::EnableWindow(hOwner, FALSE);
    }

    HWND hDlg = ::CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kClassName, L"发现新版本",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hOwner, NULL, AfxGetInstanceHandle(), &st);

    if (!hDlg) {
        if (hOwner && ownerWasEnabled) ::EnableWindow(hOwner, TRUE);
        CString fallback;
        fallback.Format(L"发现新版本: %s\r\n当前版本: %s\r\n\r\n更新内容:\r\n%s\r\n\r\n是否立即更新？",
            serverVersion.GetString(), currentVersion.GetString(), visibleUpdateLog.GetString());
        return ShowCenteredMsgBox(fallback, L"发现新版本", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
    }

    ::ShowWindow(hDlg, SW_SHOW);
    ::UpdateWindow(hDlg);

    MSG msg;
    while (::IsWindow(hDlg) && ::GetMessage(&msg, NULL, 0, 0)) {
        if (!::IsDialogMessage(hDlg, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    if (hOwner && ownerWasEnabled) {
        ::EnableWindow(hOwner, TRUE);
        ::SetForegroundWindow(hOwner);
    }
    return st.result;
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


// 从一行更新日志中提取开头版本号，例如："3.2.3 修复xxx" -> "3.2.3"
static bool ExtractUpdateLogVersionPrefix(const CString& line, CString& outVersion) {
    CString s = line;
    s.TrimLeft();
    outVersion.Empty();

    if (s.IsEmpty() || s[0] < L'0' || s[0] > L'9')
        return false;

    int i = 0;
    bool hasDot = false;
    while (i < s.GetLength()) {
        wchar_t ch = s[i];
        if (ch >= L'0' && ch <= L'9') {
            i++;
            continue;
        }
        if (ch == L'.') {
            hasDot = true;
            i++;
            continue;
        }
        break;
    }

    if (!hasDot || i <= 0)
        return false;

    // 版本号后面必须是空白或行尾，避免把普通文本误判为版本号
    if (i < s.GetLength()) {
        wchar_t next = s[i];
        if (next != L' ' && next != L'\t' && next != L'\r' && next != L'\n')
            return false;
    }

    outVersion = s.Left(i);
    outVersion.Trim();
    return !outVersion.IsEmpty();
}

// update_v2.txt 格式：
// 第1行：服务器最新版本
// 第2行：下载地址
// 第3行开始：每个版本一段更新说明，版本行以 3.2.3 这种版本号开头，缩进行属于上一版本。
// 这里只返回“大于当前版本”的更新内容，避免老版本说明一直显示给用户。
static CString FilterUpdateLogGreaterThanCurrent(const CString& rawUpdateLog, const CString& currentVersion) {
    CString normalized = rawUpdateLog;
    normalized.Replace(L"\r\n", L"\n");
    normalized.Replace(L"\r", L"\n");

    std::vector<CString> visibleEntries;
    CString currentEntry;
    CString currentEntryVersion;
    bool sawVersionEntry = false;

    auto FlushEntry = [&]() {
        CString entry = currentEntry;
        entry.Trim();
        CString ver = currentEntryVersion;
        ver.Trim();
        if (!entry.IsEmpty() && !ver.IsEmpty() && CompareVersion(ver, currentVersion) > 0) {
            visibleEntries.push_back(entry);
        }
        currentEntry.Empty();
        currentEntryVersion.Empty();
    };

    int pos = 0;
    while (pos <= normalized.GetLength()) {
        int next = normalized.Find(L'\n', pos);
        CString line;
        if (next < 0) {
            line = normalized.Mid(pos);
            pos = normalized.GetLength() + 1;
        }
        else {
            line = normalized.Mid(pos, next - pos);
            pos = next + 1;
        }

        CString ver;
        if (ExtractUpdateLogVersionPrefix(line, ver)) {
            sawVersionEntry = true;
            FlushEntry();
            currentEntryVersion = ver;
            currentEntry = line;
        }
        else {
            // 缩进行/说明行挂到上一条版本日志下
            if (!currentEntry.IsEmpty()) {
                currentEntry += L"\r\n";
                currentEntry += line;
            }
        }
    }
    FlushEntry();

    // 兼容旧格式：如果后续日志完全没有版本号，就沿用原文，避免用户看不到更新说明。
    if (!sawVersionEntry) {
        CString legacy = rawUpdateLog;
        legacy.Trim();
        return legacy;
    }

    CString result;
    for (size_t i = 0; i < visibleEntries.size(); ++i) {
        if (i > 0) result += L"\r\n\r\n";
        result += visibleEntries[i];
    }
    result.Trim();
    return result;
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
    ON_WM_KEYDOWN()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_BN_CLICKED(ID_BTN_START, OnBnClickedStart)
    ON_BN_CLICKED(ID_BTN_APPLY, OnBnClickedApply)
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip)
    ON_BN_CLICKED(ID_BTN_RESET, OnBnClickedReset)
    ON_BN_CLICKED(ID_BTN_BROWSE, OnBnClickedBrowseDir)
    ON_BN_CLICKED(ID_BTN_INPUT_KEY, OnBnClickedInputKey)
    ON_BN_CLICKED(ID_BTN_DEATH_X_CALIBRATE, OnBnClickedDeathXCalibrate)
    ON_BN_CLICKED(ID_BTN_DEATH_X_SAVE, OnBnClickedDeathXSave)
    ON_BN_CLICKED(ID_BTN_DEATH_X_CANCEL, OnBnClickedDeathXCancel)
    ON_BN_CLICKED(ID_BTN_DEATH_X_DEFAULT, OnBnClickedDeathXDefault)
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
    ON_CBN_SELCHANGE(1034, &CDNFGameCaptureDlg::OnCbnSelchangeDeathAlgorithm)
    ON_MESSAGE(WM_UPDATE_AUTH_TIME, &CDNFGameCaptureDlg::OnUpdateAuthTime)
    ON_MESSAGE(WM_OCR_SERVICE_FAIL, &CDNFGameCaptureDlg::OnOcrServiceFail)
    ON_MESSAGE(WM_OCR_START_RESULT, &CDNFGameCaptureDlg::OnOcrStartResult)
    ON_MESSAGE(WM_OCR_RECOVER_RESULT, &CDNFGameCaptureDlg::OnOcrRecoverResult)
    ON_MESSAGE(WM_KILL_DISPLAY_VISIBILITY_CHANGED, &CDNFGameCaptureDlg::OnKillDisplayVisibilityChanged)
    ON_CBN_DROPDOWN(1031, &CDNFGameCaptureDlg::OnCbnDropdownTargetWindow)
    ON_CBN_CLOSEUP(1031, &CDNFGameCaptureDlg::OnCbnCloseupTargetWindow)
    ON_BN_CLICKED(ID_CHK_AUTO_CROP_BLACK_BARS, &CDNFGameCaptureDlg::OnBnClickedAutoCropBlackBars)
    // ⬇️ 【新增】：绑定 1033 (我们给新列表框的ID) 的点击事件
    ON_LBN_SELCHANGE(1033, &CDNFGameCaptureDlg::OnLbnSelchangeRecentPlayers)
    ON_MESSAGE(WM_WEB_CMD_RECEIVED, &CDNFGameCaptureDlg::OnWebCmdReceived)
    ON_WM_MOUSEMOVE()
    ON_WM_RBUTTONDOWN()


END_MESSAGE_MAP()

bool CDNFGameCaptureDlg::HandleDeathXCalibrationKey(UINT vk)
{
    if (!m_bDeathXCalibrationMode) return false;

    if (vk >= '1' && vk <= '8') {
        SelectDeathXPoint((int)(vk - '1'));
        return true;
    }

    int step = (::GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
    int dx = 0;
    int dy = 0;
    switch (vk) {
    case VK_LEFT:
        dx = -step;
        break;
    case VK_RIGHT:
        dx = step;
        break;
    case VK_UP:
        dy = -step;
        break;
    case VK_DOWN:
        dy = step;
        break;
    default:
        break;
    }

    return (dx != 0 || dy != 0) && MoveSelectedDeathXPointByPixels(dx, dy);
}

BOOL CDNFGameCaptureDlg::PreTranslateMessage(MSG* pMsg)
{
    if (m_bDeathXCalibrationMode && pMsg && pMsg->message == WM_KEYDOWN) {
        if (HandleDeathXCalibrationKey((UINT)pMsg->wParam)) return TRUE;
    }

    return CWnd::PreTranslateMessage(pMsg);
}

void CDNFGameCaptureDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (HandleDeathXCalibrationKey(nChar)) return;
    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDNFGameCaptureDlg::OnMouseMove(UINT nFlags, CPoint point) {
    if (m_bDeathXCalibrationMode && m_bDraggingDeathXPoint && m_dragDeathXPoint >= 0) {
        if (m_previewRect.PtInRect(point)) {
            m_selectedDeathXPoint = m_dragDeathXPoint;
            SetDeathXPoint(m_dragDeathXPoint, ClientToDeathXPoint(point));
            InvalidateRect(&m_previewRect, FALSE);
        }
        CWnd::OnMouseMove(nFlags, point);
        return;
    }

    // 鼠标在预览区移动时，高频重绘触发显微镜画面
    if (m_w > 0 && m_previewRect.PtInRect(point)) {
        InvalidateRect(&m_previewRect, FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CDNFGameCaptureDlg::OnRButtonDown(UINT nFlags, CPoint point) {
    if (m_bDeathXCalibrationMode) {
        CWnd::OnRButtonDown(nFlags, point);
        return;
    }
    CWnd::OnRButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_bDeathXCalibrationMode) {
        int idx = HitTestDeathXPoint(point);
        if (idx >= 0) {
            SelectDeathXPoint(idx);
            m_dragDeathXPoint = idx;
            m_bDraggingDeathXPoint = true;
            SetCapture();
            SetDeathXPoint(idx, ClientToDeathXPoint(point));
            InvalidateRect(&m_previewRect, FALSE);
            CString msg;
            msg.Format(L"🎯 [X校准] 正在拖动 %s：%.4f, %.4f",
                GetDeathPointName(idx), m_deathXPoints[idx].x, m_deathXPoints[idx].y);
            AppLog(msg, RGB(0, 255, 255));
        }
        CWnd::OnLButtonDown(nFlags, point);
        return;
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnLButtonUp(UINT nFlags, CPoint point) {
    if (m_bDeathXCalibrationMode && m_bDraggingDeathXPoint) {
        if (m_previewRect.PtInRect(point) && m_dragDeathXPoint >= 0) {
            SetDeathXPoint(m_dragDeathXPoint, ClientToDeathXPoint(point));
        }
        m_bDraggingDeathXPoint = false;
        m_dragDeathXPoint = -1;
        if (GetCapture() == this) ReleaseCapture();
        InvalidateRect(&m_previewRect, FALSE);
        CWnd::OnLButtonUp(nFlags, point);
        return;
    }
    CWnd::OnLButtonUp(nFlags, point);
}

void CDNFGameCaptureDlg::OnBnClickedDeathXCalibrate() {
    if (m_bDeathXCalibrationMode) {
        ExitDeathXCalibrationMode(true);
        AppLog(L"↩️ [X校准] 已取消校准，恢复进入校准前的点位。", RGB(255, 180, 0));
    }
    else {
        EnterDeathXCalibrationMode();
    }
}

void CDNFGameCaptureDlg::OnBnClickedDeathXSave() {
    SaveDeathXCalibrationToIni();
    SnapshotDeathXCalibration();
    ExitDeathXCalibrationMode(false);
    AppLog(L"✅ [X校准] 死亡X点位已保存到 config.ini。", RGB(0, 255, 100));
}

void CDNFGameCaptureDlg::OnBnClickedDeathXCancel() {
    ExitDeathXCalibrationMode(true);
    AppLog(L"↩️ [X校准] 已取消校准，恢复进入校准前的点位。", RGB(255, 180, 0));
}

void CDNFGameCaptureDlg::OnBnClickedDeathXDefault() {
    ApplyDefaultDeathXPoints();
    if (m_bDeathXCalibrationMode && m_selectedDeathXPoint < 0) {
        m_selectedDeathXPoint = 0;
    }
    InvalidateRect(&m_previewRect, FALSE);
    AppLog(L"🎯 [X校准] 已恢复内置默认点位；点击保存后才会写入 config.ini。", RGB(0, 255, 255));
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
        const bool wasManualAuthCheck = m_bIsManualAuthCheck;
        m_bIsAuthValid = false;
        m_cloudExpireTime = 0;

        if (wasManualAuthCheck) {
            // 手动换卡失败时，才回滚到提交前的旧卡密。
            DnfWriteLocalLicenseKey(s_backupAuthCode);
            s_pendingAuthCode.Empty();
        }
        else {
            WriteMatchLog(L"[授权备份] 云端校验失败（非手动授权），保留本地卡密，不覆盖授权存储。");
        }

        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        OutputDebugAuthInfo();
        if (m_bIsRunning) OnBnClickedStart();

        if (wasManualAuthCheck) { // 🚨 只有手动点授权，才弹失败提示！
            json reply; reply["action"] = "auth_result"; reply["success"] = false;
            reply["message"] = std::string(CW2A(L"❌ 验证失败！\r\n卡密无效或已过期，已还原旧卡密。\r\n原因：" + *pCloudResult, CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
        m_bIsManualAuthCheck = false; // 重置标记

        if (wasManualAuthCheck) {
            CheckTrialAndLicense(); // 重新加载旧授权激活状态
            s_backupAuthCode.Empty();
            s_pendingAuthCode.Empty();
        }
        BroadcastStateToWeb();  // 通知网页刷新状态文字
        delete pCloudResult;
    }
    return 0;
}

// 找到 LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime
LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime(WPARAM wParam, LPARAM lParam) {
    long long cloudTime = (long long)lParam;
    const bool wasManualAuthCheck = m_bIsManualAuthCheck;
    m_cloudExpireTime = cloudTime;
    m_bIsAuthValid = (cloudTime > 1 || cloudTime == 0xFFFFFFFF);

    if (m_bIsAuthValid && wasManualAuthCheck) {
        if (!s_pendingAuthCode.IsEmpty()) {
            DnfWriteLocalLicenseKey(s_pendingAuthCode);
        }
        else {
            WriteMatchLog(L"[授权备份] 手动授权云端验证成功，但没有待提交卡密，已跳过写入。");
        }
    }

    if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
    OutputDebugAuthInfo();
    if (m_bIsAuthValid) AppLog(L"✅ [云端验证] 授权已激活，欢迎使用！", RGB(0, 255, 100));

    if (m_bIsAuthValid) {
        if (wasManualAuthCheck) { // 🚨 只有手动点授权，才弹成功提示！
            json reply; reply["action"] = "auth_result"; reply["success"] = true;
            reply["message"] = std::string(CW2A(L"✅ 授权验证成功！\r\n您已激活专业版。", CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
    }
    m_bIsManualAuthCheck = false; // 重置标记
    if (wasManualAuthCheck) {
        s_backupAuthCode.Empty();
        s_pendingAuthCode.Empty();
    }
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
        if (secondDash <= firstDash + 1) return false;
        int thirdDash = inputKey.Find(L'-', secondDash + 1);

        if (thirdDash > secondDash + 1 && thirdDash < inputKey.GetLength() - 1) {
            CString durStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
            CString nonceStr = inputKey.Mid(secondDash + 1, thirdDash - secondDash - 1);
            CString sigStr = inputKey.Mid(thirdDash + 1);

            wchar_t* durEnd = nullptr;
            const wchar_t* durStart = durStr.GetString();
            long long duration = wcstoll(durStart, &durEnd, 16);
            if (durEnd == durStart || *durEnd != L'\0') return false;

            wchar_t* sigEnd = nullptr;
            const wchar_t* sigStart = sigStr.GetString();
            unsigned int sig = (unsigned int)wcstoul(sigStart, &sigEnd, 16);
            if (sigEnd == sigStart || *sigEnd != L'\0') return false;

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
    HINTERNET hConnect = WinHttpConnect(hSession, DNF_CLOUD_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
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

static CString DnfNormalizeLicenseKey(CString key)
{
    if (!key.IsEmpty() && key[0] == 0xFEFF) key = key.Mid(1);
    key.Remove(L'\r');
    key.Remove(L'\n');
    key.Trim();
    return key;
}

static CString DnfGetLicenseFilePath()
{
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    int slash = path.ReverseFind(L'\\');
    if (slash >= 0) path = path.Left(slash + 1);
    path += L"license.txt";
    return path;
}

static CString DnfReadLicenseFromFile()
{
    CString path = DnfGetLicenseFilePath();
    CFile file;
    if (!file.Open(path, CFile::modeRead | CFile::typeBinary)) return L"";

    ULONGLONG rawLen = file.GetLength();
    if (rawLen <= 0 || rawLen > 4096) {
        file.Close();
        return L"";
    }

    int len = (int)rawLen;
    std::vector<char> buf(len + 1, 0);
    file.Read(buf.data(), len);
    file.Close();

    char* start = buf.data();
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        start += 3;
    }

    CString key = CA2W(start, CP_UTF8);
    return DnfNormalizeLicenseKey(key);
}

static bool DnfWriteLicenseToFileAtomic(const CString& key)
{
    CString path = DnfGetLicenseFilePath();
    CString tmpPath = path + L".tmp";
    CString normalized = DnfNormalizeLicenseKey(key);

    {
        CFile file;
        if (!file.Open(tmpPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
            return false;
        }
        std::string utf8 = CW2A(normalized, CP_UTF8);
        if (!utf8.empty()) {
            file.Write(utf8.c_str(), (UINT)utf8.size());
        }
        file.Flush();
        file.Close();
    }

    if (!MoveFileEx(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFile(tmpPath);
        return false;
    }
    return true;
}

static CString DnfReadLicenseFromRegistry()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, DNF_LICENSE_REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LONG rc = RegQueryValueEx(hKey, DNF_LICENSE_REG_VALUE, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t) || bytes > 4096) {
        RegCloseKey(hKey);
        return L"";
    }

    std::vector<wchar_t> buf((bytes / sizeof(wchar_t)) + 1, 0);
    rc = RegQueryValueEx(hKey, DNF_LICENSE_REG_VALUE, nullptr, &type, (LPBYTE)buf.data(), &bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) return L"";

    return DnfNormalizeLicenseKey(CString(buf.data()));
}

static bool DnfWriteLicenseToRegistry(const CString& key)
{
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    LONG rc = RegCreateKeyEx(HKEY_CURRENT_USER, DNF_LICENSE_REG_PATH, 0, nullptr, 0,
        KEY_READ | KEY_WRITE, nullptr, &hKey, &disposition);
    if (rc != ERROR_SUCCESS) return false;

    CString normalized = DnfNormalizeLicenseKey(key);
    if (normalized.IsEmpty()) {
        RegDeleteValue(hKey, DNF_LICENSE_REG_VALUE);
        RegCloseKey(hKey);
        return true;
    }

    rc = RegSetValueEx(hKey, DNF_LICENSE_REG_VALUE, 0, REG_SZ,
        (const BYTE*)normalized.GetString(),
        (DWORD)((normalized.GetLength() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

static bool DnfWriteLocalLicenseKey(const CString& key)
{
    CString normalized = DnfNormalizeLicenseKey(key);
    bool fileOk = DnfWriteLicenseToFileAtomic(normalized);
    bool regOk = DnfWriteLicenseToRegistry(normalized);

    g_lastLicenseSource = normalized.IsEmpty() ? L"无" : L"手动写入";
    g_lastLicenseRepair.Empty();
    if (!fileOk || !regOk) {
        CString msg;
        msg.Format(L"[授权备份] 写入授权存储不完整：license.txt=%s；注册表=%s。",
            fileOk ? L"成功" : L"失败",
            regOk ? L"成功" : L"失败");
        WriteMatchLog(msg);
    }
    return fileOk || regOk;
}

static CString DnfReadLocalLicenseKey()
{
    CString regKey = DnfReadLicenseFromRegistry();
    CString fileKey = DnfReadLicenseFromFile();

    g_lastLicenseRepair.Empty();
    if (!regKey.IsEmpty()) {
        g_lastLicenseSource = L"注册表";
        if (fileKey.IsEmpty()) {
            if (DnfWriteLicenseToFileAtomic(regKey)) {
                g_lastLicenseRepair = L"已用注册表自动补写 license.txt";
                WriteMatchLog(L"[授权备份] license.txt 缺失或为空，已用注册表授权码自动补写。");
            }
            else {
                g_lastLicenseRepair = L"license.txt 缺失，自动补写失败";
                WriteMatchLog(L"[授权备份] license.txt 缺失或为空，但自动补写失败。");
            }
        }
        else if (DnfNormalizeLicenseKey(fileKey) != regKey) {
            if (DnfWriteLicenseToFileAtomic(regKey)) {
                g_lastLicenseRepair = L"license.txt 与注册表不一致，已按注册表覆盖";
                WriteMatchLog(L"[授权备份] license.txt 与注册表授权码不一致，普通启动按注册表优先并已覆盖 license.txt。");
            }
            else {
                g_lastLicenseRepair = L"license.txt 与注册表不一致，覆盖文件失败";
                WriteMatchLog(L"[授权备份] license.txt 与注册表授权码不一致，普通启动按注册表优先，但覆盖 license.txt 失败。");
            }
        }
        return regKey;
    }

    if (!fileKey.IsEmpty()) {
        g_lastLicenseSource = L"license.txt";
        if (DnfWriteLicenseToRegistry(fileKey)) {
            g_lastLicenseRepair = L"已用 license.txt 自动补写注册表";
            WriteMatchLog(L"[授权备份] 注册表授权码缺失，已用 license.txt 自动补写。");
        }
        else {
            g_lastLicenseRepair = L"注册表缺失，自动补写失败";
            WriteMatchLog(L"[授权备份] 注册表授权码缺失，但自动补写失败。");
        }
        return fileKey;
    }

    g_lastLicenseSource = L"无";
    return L"";
}

bool CDNFGameCaptureDlg::BeginLicenseCloudCheck(const CString& inputKey, bool manualCheck)
{
    CString normalized = DnfNormalizeLicenseKey(inputKey);
    CString hwid = GetMachineID();
    if (normalized.IsEmpty() || !VerifyKey(normalized, hwid)) {
        if (manualCheck) {
            m_bIsManualAuthCheck = false;
        }
        return false;
    }

    m_bIsManualAuthCheck = manualCheck;
    m_bIsTrial = false;
    long long duration = m_keyDuration;
    HWND hWnd = GetSafeHwnd();

    std::thread([this, hWnd, normalized, hwid, duration]() {
        long long cloudExpTime = 0;
        CString cloudResult = CheckCloudBinding(normalized, hwid, duration, cloudExpTime);

        if (cloudResult != L"OK" && ::IsWindow(hWnd)) {
            CString* pResult = new CString(cloudResult);
            ::PostMessage(hWnd, WM_CLOUD_AUTH_FAIL, 0, (LPARAM)pResult);
        }
        else if (cloudResult == L"OK" && ::IsWindow(hWnd)) {
            ::PostMessage(hWnd, WM_UPDATE_AUTH_TIME, 0, (LPARAM)cloudExpTime);
        }
        }).detach();

    return true;
}

static bool DnfPostCloudJson(const std::string& jsonUtf8, std::string& responseUtf8, CString& errorMsg, int timeoutMs)
{
    responseUtf8.clear();
    errorMsg.Empty();

    HINTERNET hSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        errorMsg = L"无法创建 HTTP 会话";
        return false;
    }

    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, DNF_CLOUD_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        errorMsg = L"无法连接云函数";
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        errorMsg = L"无法创建云端请求";
        return false;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    BOOL ok = WinHttpSendRequest(
        hRequest,
        NULL,
        0,
        (LPVOID)jsonUtf8.c_str(),
        (DWORD)jsonUtf8.length(),
        (DWORD)jsonUtf8.length(),
        0
    ) && WinHttpReceiveResponse(hRequest, NULL);

    if (ok) {
        DWORD sz = 0;
        DWORD downloaded = 0;
        while (WinHttpQueryDataAvailable(hRequest, &sz) && sz > 0) {
            std::vector<char> buf(sz + 1, 0);
            if (WinHttpReadData(hRequest, buf.data(), sz, &downloaded)) {
                responseUtf8.append(buf.data(), downloaded);
            }
        }
    }
    else {
        errorMsg = L"云端请求失败或超时";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

CString CDNFGameCaptureDlg::SubmitAliasDbForReview(const std::string& aliasDbPayload, int mainCount, int pairCount)
{
    if (aliasDbPayload.empty()) {
        return L"当前没有可上传的小号库。";
    }

    CString key = DnfReadLocalLicenseKey();
    if (key.IsEmpty()) {
        return L"请先输入并验证授权卡密，再上传共享小号库。";
    }

    CString hwid = GetMachineID();
    if (hwid.IsEmpty()) {
        return L"无法获取本机机器码，上传已取消。";
    }

    try {
        int filteredMainCount = mainCount;
        int filteredPairCount = pairCount;
        int containedNakedAliasCount = 0;
        std::string filteredAliasDbPayload = FilterAliasDbPayloadForReview(aliasDbPayload, filteredMainCount, filteredPairCount, containedNakedAliasCount);
        if (filteredMainCount <= 0 || filteredAliasDbPayload.empty()) {
            if (containedNakedAliasCount > 0) {
                CString detail;
                detail.Format(L"☁️ [推送预过滤] 本地预过滤裸ID %d 个，本次无可提交变化。", containedNakedAliasCount);
                AppLog(detail, RGB(255, 200, 90));
                CString msg;
                msg.Format(L"共享库投稿成功：云端已有包含这些裸ID的小号，已本地预过滤 %d 个。", containedNakedAliasCount);
                return msg;
            }
            AppLog(L"☁️ [推送明细] 本次无可提交变化。", RGB(160, 170, 180));
            return L"当前没有可上传的小号库。";
        }

        DnfLogAliasPushDiff(m_aliasCloudBaselinePlayers, filteredAliasDbPayload, containedNakedAliasCount);

        json req;
        req["action"] = "submit_alias_db";
        req["key"] = std::string(CW2A(key, CP_UTF8));
        req["hwid"] = std::string(CW2A(hwid, CP_UTF8));
        req["clientVersion"] = std::string(CW2A(CURRENT_VERSION, CP_UTF8));
        req["snapshotMode"] = "full";
        req["deleteScopeMainNames"] = BuildAliasCloudDeleteScopeJson();
        req["aliasDB"] = json::parse(filteredAliasDbPayload);

        std::string response;
        CString err;
        if (!DnfPostCloudJson(req.dump(), response, err, 8000)) {
            return L"共享库投稿失败：" + err;
        }

        json reply = json::parse(response);
        std::string status = reply.value("status", "error");
        std::string msg = reply.value("msg", status == "ok" ? "上传成功" : "上传失败");
        CString cmsg = CA2W(msg.c_str(), CP_UTF8);

        if (status == "ok" && !reply.value("aliasSubmit", false)) {
            return L"共享库投稿失败：云函数不是最新版，请先部署新版云函数。";
        }

        if (status == "ok") {
            if (containedNakedAliasCount > 0) {
                CString suffix;
                suffix.Format(L"；本地预过滤裸ID %d 个", containedNakedAliasCount);
                return L"共享库投稿成功：" + cmsg + suffix;
            }
            return L"共享库投稿成功：" + cmsg;
        }
        return L"共享库投稿失败：" + cmsg;
    }
    catch (const std::exception& e) {
        CString msg;
        msg.Format(L"共享库投稿失败：数据打包异常 (%S)", e.what());
        return msg;
    }
}

CString CDNFGameCaptureDlg::DirectSyncAliasDbToCloud(const std::string& aliasDbPayload, int mainCount, int pairCount)
{
    if (mainCount <= 0 || aliasDbPayload.empty()) {
        return L"当前没有可直写的本地小号库。";
    }

    CString key = DnfReadLocalLicenseKey();
    if (key.IsEmpty()) {
        return L"请先输入并验证授权卡密，再使用管理员直写模式。";
    }

    CString hwid = GetMachineID();
    if (hwid.IsEmpty()) {
        return L"无法获取本机机器码，管理员直写已取消。";
    }

    try {
        json req;
        req["action"] = "direct_sync_alias_db";
        req["key"] = std::string(CW2A(key, CP_UTF8));
        req["hwid"] = std::string(CW2A(hwid, CP_UTF8));
        req["clientVersion"] = std::string(CW2A(CURRENT_VERSION, CP_UTF8));
        req["aliasDB"] = json::parse(aliasDbPayload);

        std::string response;
        CString err;
        if (!DnfPostCloudJson(req.dump(), response, err, 8000)) {
            return L"管理员直写失败：" + err;
        }

        json reply = json::parse(response);
        std::string status = reply.value("status", "error");
        std::string msg = reply.value("msg", status == "ok" ? "直写完成" : "直写失败");
        CString cmsg = CA2W(msg.c_str(), CP_UTF8);

        if (status == "ok" && !reply.value("directSync", false)) {
            return L"管理员直写失败：云函数不是最新版，请先部署新版云函数。";
        }

        if (status == "ok") {
            m_aliasDbPendingDeleteMains.clear();
            m_aliasCloudDeleteBaselineMains.clear();
            m_aliasCloudBaselinePlayers.clear();
            for (auto const& [mainNameRaw, aliasesRaw] : m_aliasDB) {
                CString mainName = mainNameRaw;
                mainName.Trim();
                if (mainName.IsEmpty()) continue;
                CString normalizedAliases = DnfNormalizeAliasListString(aliasesRaw);
                if (!normalizedAliases.IsEmpty()) {
                    m_aliasCloudDeleteBaselineMains.push_back(mainName);
                    m_aliasCloudBaselinePlayers[mainName] = normalizedAliases;
                }
            }
            SaveAliasCloudDeleteBaseline();
            int cleanMainCount = 0;
            int cleanPairCount = 0;
            m_aliasDbCloudBaselinePayload = BuildAliasDbJsonPayload(cleanMainCount, cleanPairCount);
            return L"管理员直写成功：" + cmsg;
        }
        return L"管理员直写失败：" + cmsg;
    }
    catch (const std::exception& e) {
        CString msg;
        msg.Format(L"管理员直写失败：数据打包异常 (%S)", e.what());
        return msg;
    }
}

CString CDNFGameCaptureDlg::SyncAliasDbFromCloud()
{
    CString key = DnfReadLocalLicenseKey();
    if (key.IsEmpty()) {
        return L"请先输入并验证授权卡密，再同步云端库。";
    }

    CString hwid = GetMachineID();
    if (hwid.IsEmpty()) {
        return L"无法获取本机机器码，云端同步已取消。";
    }

    try {
        json req;
        req["action"] = "get_public_alias_db";
        req["key"] = std::string(CW2A(key, CP_UTF8));
        req["hwid"] = std::string(CW2A(hwid, CP_UTF8));

        std::string response;
        CString err;
        if (!DnfPostCloudJson(req.dump(), response, err, 8000)) {
            return L"云端库同步失败：" + err;
        }

        json reply = json::parse(response);
        std::string status = reply.value("status", "error");
        std::string msg = reply.value("msg", status == "ok" ? "同步完成" : "同步失败");
        if (status != "ok") {
            CString cmsg = CA2W(msg.c_str(), CP_UTF8);
            return L"云端库同步失败：" + cmsg;
        }

        if (!reply.contains("publicAliasDB") || !reply["publicAliasDB"].contains("players") || !reply["publicAliasDB"]["players"].is_object()) {
            return L"云端库同步失败：公共库数据格式异常";
        }

        int addedAliasCount = 0;
        int upgradedAliasCount = 0;
        int touchedMainCount = 0;
        int touchedLiveAliasCount = 0;
        int beforeMainCount = 0;
        int beforePairCount = 0;
        bool wasDirtyBeforeSync = (BuildAliasDbJsonPayload(beforeMainCount, beforePairCount) != m_aliasDbCloudBaselinePayload);

        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            auto& players = reply["publicAliasDB"]["players"];
            SetAliasCloudDeleteBaselineFromPublicPlayers(players);
            for (auto it = players.begin(); it != players.end(); ++it) {
                CString mainName = CA2W(it.key().c_str(), CP_UTF8);
                mainName.Trim();
                if (mainName.IsEmpty()) continue;

                std::vector<CString> mergedAliases = DnfParseAliasListString(m_aliasDB[mainName]);
                std::vector<CString> beforeAliases = mergedAliases;
                CString beforeAliasesText = DnfFormatAliasListString(mergedAliases);

                auto addAlias = [&](CString aliasName) {
                    aliasName.Trim();
                    if (aliasName.IsEmpty() || aliasName == mainName) return;
                    DnfAliasMergeResult mergeResult = DnfMergeAliasIntoList(mergedAliases, aliasName);
                    if (mergeResult == DnfAliasMergeAdded) {
                        addedAliasCount++;
                    }
                    else if (mergeResult == DnfAliasMergeUpgraded) {
                        upgradedAliasCount++;
                    }
                };

                if (it.value().is_array()) {
                    for (const auto& aliasValue : it.value()) {
                        if (aliasValue.is_string()) {
                            CString aliasText = CA2W(aliasValue.get<std::string>().c_str(), CP_UTF8);
                            addAlias(aliasText);
                        }
                    }
                }
                else if (it.value().is_string()) {
                    CString aliasListText = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
                    for (const auto& aliasName : DnfParseAliasListString(aliasListText)) {
                        addAlias(aliasName);
                    }
                }

                CString afterAliasesText = DnfFormatAliasListString(mergedAliases);
                if (afterAliasesText != beforeAliasesText) {
                    m_aliasDB[mainName] = afterAliasesText;
                    touchedMainCount++;

                    std::vector<CString> addedAliases;
                    std::vector<CString> removedAliases;
                    for (const auto& alias : mergedAliases) {
                        if (!DnfAliasListContainsExact(beforeAliases, alias)) addedAliases.push_back(alias);
                    }
                    for (const auto& alias : beforeAliases) {
                        if (!DnfAliasListContainsExact(mergedAliases, alias)) removedAliases.push_back(alias);
                    }
                    if (!addedAliases.empty()) {
                        AppLog(L"☁️ [云端融合新增] [" + mainName + L"] " + DnfJoinAliasNames(addedAliases), RGB(100, 255, 140));
                    }
                    if (!removedAliases.empty()) {
                        AppLog(L"☁️ [云端融合删除旧写法] [" + mainName + L"] " + DnfJoinAliasNames(removedAliases), RGB(255, 190, 90));
                    }
                }
            }

            for (int i = 0; i < 8; i++) {
                CString mainName = m_players[i].name;
                mainName.Trim();
                if (mainName.IsEmpty() || m_aliasDB.find(mainName) == m_aliasDB.end()) continue;

                CString beforeLiveAliasesText = DnfSerializeAliasDataListRaw(m_players[i].aliases);

                std::vector<CString> dbAliases = DnfParseAliasListString(m_aliasDB[mainName]);
                for (const auto& aliasName : dbAliases) {
                    DnfMergeAliasIntoAliasDataList(m_players[i].aliases, aliasName);
                }
                DnfNormalizeAliasDataList(m_players[i].aliases);

                CString afterLiveAliasesText = DnfSerializeAliasDataListRaw(m_players[i].aliases);
                if (afterLiveAliasesText != beforeLiveAliasesText) {
                    touchedLiveAliasCount++;
                }
            }
        }

        if (touchedMainCount > 0 || touchedLiveAliasCount > 0) {
            SaveAliasDB();
            SaveConfigToFile();
            SyncDataToTree();
            RefreshDisplay();
        }

        if (!wasDirtyBeforeSync) {
            ResetAliasDbCloudBaseline();
        }
        BroadcastStateToWeb();

        CString result;
        result.Format(L"云端库同步完成：更新 %d 个主号关联、新增 %d 个小号、补全职业信息 %d 个、回填当前选手 %d 个。", touchedMainCount, addedAliasCount, upgradedAliasCount, touchedLiveAliasCount);
        if (addedAliasCount == 0 && upgradedAliasCount == 0 && touchedLiveAliasCount == 0) result = L"云端库同步完成：本地已经是最新。";
        return result;
    }
    catch (const std::exception& e) {
        CString msg;
        msg.Format(L"云端库同步失败：数据解析异常 (%S)", e.what());
        return msg;
    }
}

void CDNFGameCaptureDlg::AutoSubmitAliasDbIfDirty()
{
    SaveAliasDB();

    int mainCount = 0;
    int pairCount = 0;
    std::string payload = BuildAliasDbJsonPayload(mainCount, pairCount);
    if (mainCount <= 0 || payload == m_aliasDbCloudBaselinePayload) {
        return;
    }

    CString result;
    if (m_bAliasDirectMode) {
        AppLog(L"☁️ [共享库] 管理员直写模式：退出前直接同步公共库...", RGB(80, 220, 180));
        result = DirectSyncAliasDbToCloud(payload, mainCount, pairCount);
    }
    else {
        AppLog(L"☁️ [共享库] 检测到本地小号库有变动，退出前自动提交待审核...", RGB(80, 220, 180));
        result = SubmitAliasDbForReview(payload, mainCount, pairCount);
    }
    COLORREF logColor = result.Find(L"成功") >= 0 ? RGB(0, 255, 120) : RGB(255, 120, 80);
    AppLog(L"☁️ [共享库] " + result, logColor);
    if (result.Find(L"成功") >= 0) {
        m_aliasDbPendingDeleteMains.clear();
        ResetAliasDbCloudBaseline();
    }
}

CString CDNFGameCaptureDlg::SubmitAliasDbSnapshotIfDirty(bool saveBeforeBuild)
{
    if (saveBeforeBuild) {
        SaveAliasDB();
    }
    else {
        SaveAliasDB(false);
    }

    int mainCount = 0;
    int pairCount = 0;
    std::string payload = BuildAliasDbJsonPayload(mainCount, pairCount);
    if (!m_aliasDbLastSubmittedPayload.empty() && payload == m_aliasDbLastSubmittedPayload) {
        return L"本地小号库没有变化，无需推送。";
    }

    AppLog(L"☁️ [共享库] 正在推送本地小号库快照，云端将生成差异等待审核...", RGB(80, 220, 180));
    CString result = SubmitAliasDbForReview(payload, mainCount, pairCount);
    COLORREF logColor = result.Find(L"成功") >= 0 ? RGB(0, 255, 120) : RGB(255, 120, 80);
    AppLog(L"☁️ [共享库] " + result, logColor);
    if (result.Find(L"成功") >= 0) {
        m_aliasDbPendingDeleteMains.clear();
        ResetAliasDbCloudBaseline();
        m_aliasDbLastSubmittedPayload = m_aliasDbCloudBaselinePayload;
    }
    return result;
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

    // --- 第一阶段：尝试读取本地卡密（注册表优先，license.txt 互补备份） ---
    CString inputKey = DnfReadLocalLicenseKey();
    if (!inputKey.IsEmpty() && BeginLicenseCloudCheck(inputKey, false)) {
        // 【关键修复 2】：删掉 m_bIsAuthValid = true;
        // 离线格式对了也没用，必须设为 false，等待云端判决！
        return;
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

    CString inputKey = DnfReadLocalLicenseKey();
    if (!inputKey.IsEmpty()) {
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
        print(L"卡密来源: " + g_lastLicenseSource, RGB(180, 180, 180));
        if (!g_lastLicenseRepair.IsEmpty()) {
            print(L"卡密备份: " + g_lastLicenseRepair, RGB(180, 180, 180));
        }

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
    else {
        print(L"本地卡密记录: 未找到", RGB(150, 150, 150));
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
    m_pKillDisplayDlg = nullptr;
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
    m_lastLaunchOcrTime = 0;
    m_bOcrRecoveryPending = false;
    m_bOcrHealthCheckPending = false;
    m_ocrRecoveryRequestId = 0;
    ApplyDefaultDeathXPoints();

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

    m_configPath = appDir + L"players_config.txt";
    m_iniPath = appDir + L"config.ini";
    m_webFrontDir = appDir + L"web前端";
    m_bKillDisplayHttpReady = DnfStartKillDisplayHttpServer(this, m_webFrontDir, m_killDisplayHttpError);
    if (m_bKillDisplayHttpReady) {
        OpenKillDisplayWindow();
    }
    if (!m_bKillDisplayHttpReady && !m_killDisplayHttpError.IsEmpty()) {
        WriteMatchLog(L"[击杀展示页] " + m_killDisplayHttpError);
    }
    m_bOutputSeatLabelToKillFile = GetPrivateProfileInt(L"Settings", L"OutputSeatLabelToKillFile", 0, m_iniPath) != 0;
    m_bRedPickFirst = GetPrivateProfileInt(L"Settings", L"RedPickFirst", 0, m_iniPath) != 0;
    wchar_t lastTargetBuf[512];
    ::GetPrivateProfileString(L"Settings", L"LastTargetWindowName", L"", lastTargetBuf, 512, m_iniPath);
    m_lastTargetWindowName = lastTargetBuf;
    m_lastTargetWindowName.Trim();
    wchar_t ocrPathBuf[MAX_PATH];
    ::GetPrivateProfileString(L"Settings", L"OcrExePath", L"", ocrPathBuf, MAX_PATH, m_iniPath);
    m_ocrExePath = ocrPathBuf;
    m_ocrExePath.Trim(L" \t\r\n\"");
    if (m_ocrExePath.IsEmpty()) {
        m_ocrExePath = appDir + L"Umi-OCR.exe";
    }

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
    m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(155, row1_Y + 4, 205, row1_Y + 25), this, 1003); m_status.SetFont(&m_font);

    HWND hDeathAlgoLabel = ::CreateWindowW(
        L"STATIC", L"死亡X算法：",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        210, row1_Y + 4, 85, 24,
        this->GetSafeHwnd(), (HMENU)1035, AfxGetInstanceHandle(), NULL
    );
    if (hDeathAlgoLabel) ::SendMessage(hDeathAlgoLabel, WM_SETFONT, (WPARAM)m_font.GetSafeHandle(), TRUE);

    m_cmbDeathAlgorithm.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(300, row1_Y, 455, row1_Y + 200), this, 1034); m_cmbDeathAlgorithm.SetFont(&m_font);
    m_cmbDeathAlgorithm.AddString(L"大X颜色个数判断");
    m_cmbDeathAlgorithm.AddString(L"打补丁红蓝判断");
    m_nDeathAlgorithmChoice = GetPrivateProfileInt(L"Settings", L"DeathXAlgorithm", 0, m_iniPath);
    if (m_nDeathAlgorithmChoice < 0 || m_nDeathAlgorithmChoice > 1) m_nDeathAlgorithmChoice = 0;
    m_cmbDeathAlgorithm.SetCurSel(m_nDeathAlgorithmChoice);

    m_btnDeathXCalibrate.Create(L"X校准", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(460, row1_Y, 530, row1_Y + 25), this, ID_BTN_DEATH_X_CALIBRATE); m_btnDeathXCalibrate.SetFont(&m_font);

    m_cmbCaptureEngine.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(535, row1_Y, 665, row1_Y + 200), this, 1030); m_cmbCaptureEngine.SetFont(&m_font);
    m_cmbCaptureEngine.AddString(L"🔄 自动选择引擎"); m_cmbCaptureEngine.AddString(L"🎮 WGC 硬件捕获"); m_cmbCaptureEngine.AddString(L"🖥️ PrintWindow");
    m_nCaptureEngineChoice = GetPrivateProfileInt(L"Settings", L"CaptureEngine", 0, m_iniPath); if (m_nCaptureEngineChoice < 0 || m_nCaptureEngineChoice > 2) m_nCaptureEngineChoice = 0;
    m_cmbCaptureEngine.SetCurSel(m_nCaptureEngineChoice);
    m_cmbTargetWindow.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(670, row1_Y, r.right - 105, row1_Y + 400), this, 1031); m_cmbTargetWindow.SetFont(&m_font);
    RefreshTargetList();
    m_chkCropTitle.Create(L"去标题栏", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(r.right - 100, row1_Y, r.right - 10, row1_Y + 25), this, 1032); m_chkCropTitle.SetFont(&m_font); m_chkCropTitle.SetCheck(BST_CHECKED);
    m_chkAutoCropBlackBars.Create(L"自动裁黑边", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(r.right - 125, row1_Y + 28, r.right - 10, row1_Y + 53), this, ID_CHK_AUTO_CROP_BLACK_BARS); m_chkAutoCropBlackBars.SetFont(&m_font);
    m_chkAutoCropBlackBars.SetCheck(GetPrivateProfileInt(L"Settings", L"AutoCropBlackBars", 1, m_iniPath) ? BST_CHECKED : BST_UNCHECKED);

    m_btnDeathXSave.Create(L"保存X点位", WS_CHILD | BS_PUSHBUTTON, CRect(10, 10, 100, 38), this, ID_BTN_DEATH_X_SAVE); m_btnDeathXSave.SetFont(&m_font); m_btnDeathXSave.ShowWindow(SW_HIDE);
    m_btnDeathXCancel.Create(L"取消", WS_CHILD | BS_PUSHBUTTON, CRect(106, 10, 176, 38), this, ID_BTN_DEATH_X_CANCEL); m_btnDeathXCancel.SetFont(&m_font); m_btnDeathXCancel.ShowWindow(SW_HIDE);
    m_btnDeathXDefault.Create(L"恢复默认", WS_CHILD | BS_PUSHBUTTON, CRect(182, 10, 272, 38), this, ID_BTN_DEATH_X_DEFAULT); m_btnDeathXDefault.SetFont(&m_font); m_btnDeathXDefault.ShowWindow(SW_HIDE);

    int row2_Y = row1_Y + 60; int halfW = (r.right - 30) / 2;
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
    LoadDeathXCalibrationFromIni();
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
    EnsureBackgroundTimersStarted();

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
    DnfStopKillDisplayHttpServer();

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
    if (m_pKillDisplayDlg) {
        m_pKillDisplayDlg->DestroyWindow();
        delete m_pKillDisplayDlg;
        m_pKillDisplayDlg = nullptr;
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

bool CDNFGameCaptureDlg::ProbeOcrServiceReady()
{
    std::lock_guard<std::mutex> lk(m_launchMutex);

    if (!m_hHttpSession) {
        m_hHttpSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (m_hHttpSession) WinHttpSetTimeouts(m_hHttpSession, 800, 800, 800, 800);
    }
    if (!m_hHttpSession) return false;

    if (!m_hHttpConnect) {
        m_hHttpConnect = WinHttpConnect(m_hHttpSession, L"127.0.0.1", 1224, 0);
    }
    if (!m_hHttpConnect) return false;

    HINTERNET hProbe = WinHttpOpenRequest(
        m_hHttpConnect, L"GET", L"/",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hProbe) return false;

    WinHttpSetTimeouts(hProbe, 800, 800, 800, 800);
    BOOL ok = WinHttpSendRequest(hProbe, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hProbe, NULL);
    WinHttpCloseHandle(hProbe);
    if (ok) {
        RefreshOcrExePathFromRunningProcess(true);
    }
    return ok == TRUE;
}

bool CDNFGameCaptureDlg::RefreshOcrExePathFromRunningProcess(bool persistToIni)
{
    CString runningPath;
    if (!DnfGetProcessImagePathByName(L"Umi-OCR.exe", runningPath)) {
        return false;
    }

    runningPath.Trim(L" \t\r\n\"");
    if (runningPath.IsEmpty()) {
        return false;
    }

    if (runningPath.CompareNoCase(m_ocrExePath) != 0) {
        CString msg;
        msg.Format(L"🔎 [Umi-OCR] 已缓存实际程序路径：%s", (LPCTSTR)runningPath);
        AppLog(msg, RGB(0, 255, 100));
        WriteMatchLog(msg);
    }

    m_ocrExePath = runningPath;
    if (persistToIni) {
        ::WritePrivateProfileString(L"Settings", L"OcrExePath", m_ocrExePath, m_iniPath);
    }
    return true;
}

void CDNFGameCaptureDlg::SetOcrStartupPendingUI(bool pending)
{
    if (pending) {
        if (m_btnStart.m_hWnd) {
            m_btnStart.SetWindowText(L"启动中...");
            m_btnStart.EnableWindow(FALSE);
        }
        if (m_status.m_hWnd) {
            m_status.SetWindowText(L"正在启动OCR...");
        }
    }
    else {
        if (m_btnStart.m_hWnd) {
            m_btnStart.EnableWindow(TRUE);
            if (!m_bIsRunning) {
                m_btnStart.SetWindowText(L"开始监控");
            }
        }
        if (m_status.m_hWnd && !m_bIsRunning) {
            m_status.SetWindowText(L"就绪");
        }
    }
}

void CDNFGameCaptureDlg::StartMonitoringAfterOcrReady()
{
    if (m_bIsRunning) return;

    EnsureBackgroundTimersStarted();

    m_bIsRunning = TRUE;
    m_btnStart.EnableWindow(TRUE);
    m_btnStart.SetWindowText(L"停止监控");
    // 【身份融合补丁】开始监控时清空上一段录像/上一局残留缓存。
    NotifyIdentityRoundReset(L"开始监控，清空上一段身份缓存");

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
    ResetDeathXStableState();

    if (m_bUseWGC) {
        AppLog(L"✅ [监控已启动] 已启用 WGC 硬件加速捕获 (零闪屏)", RGB(0, 255, 100));
    }
    else if (!hGame) {
        AppLog(L"⚠️ [监控已启动] 未检测到游戏窗口，待命中...", RGB(255, 165, 0));
    }
    else {
        SafeDeleteWGC();

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
    BroadcastStateToWeb();
}

void CDNFGameCaptureDlg::EnsureBackgroundTimersStarted()
{
    static std::atomic<bool> s_timer7Started{ false };
    bool expected = false;
    if (!s_timer7Started.compare_exchange_strong(expected, true)) {
        return;
    }

    if (!GetSafeHwnd()) {
        s_timer7Started = false;
        return;
    }

    if (SetTimer(7, 1000, NULL) == 0) {
        s_timer7Started = false;
        WriteMatchLog(L"[后台轮询] 启动 Timer 7 失败。");
    }
    else {
        WriteMatchLog(L"[后台轮询] Timer 7 已启动。");
    }
}

void CDNFGameCaptureDlg::BeginOcrServiceBootstrap()
{
    if (m_bOcrStartPending.exchange(true)) {
        return;
    }

    DWORD requestId = m_ocrStartRequestId.fetch_add(1) + 1;
    SetOcrStartupPendingUI(true);

    AppLog(L"🔄 [Umi-OCR] 正在启动 OCR 服务，请稍候...", RGB(255, 200, 0));
    WriteMatchLog(L"[Umi-OCR] 正在启动 OCR 服务，请稍候...");
    BroadcastStateToWeb();

    HWND hWnd = GetSafeHwnd();
    std::thread([this, hWnd, requestId]() {
        bool ok = EnsureOcrRunning(false);
        if (!::IsWindow(hWnd)) return;
        if (!m_bOcrStartPending.load() || requestId != m_ocrStartRequestId.load()) return;
        ::PostMessage(hWnd, WM_OCR_START_RESULT, (WPARAM)requestId, ok ? 1 : 0);
        }).detach();
}

LRESULT CDNFGameCaptureDlg::OnOcrStartResult(WPARAM wParam, LPARAM lParam)
{
    DWORD requestId = (DWORD)wParam;
    bool success = (lParam != 0);
    if (!m_bOcrStartPending.load() || requestId != m_ocrStartRequestId.load()) {
        return 0;
    }

    m_bOcrStartPending = false;
    SetOcrStartupPendingUI(false);

    if (success) {
        StartMonitoringAfterOcrReady();
        return 0;
    }

    m_bIsRunning = FALSE;
    m_btnStart.EnableWindow(TRUE);
    m_btnStart.SetWindowText(L"开始监控");
    m_status.SetWindowText(L"OCR未运行");

    CString msg = L"❌ 未检测到 Umi-OCR 服务，已尝试自动启动但没有恢复成功。\r\n\r\n请手动打开软件同目录下的 Umi-OCR.exe，等待 OCR 服务启动完成后，再点击【开始监控】。\r\n\r\n为避免大X触发后 OCR 原文为空，本次不会继续监控。";
    AppLog(L"❌ [开始监控拦截] Umi-OCR 未运行或恢复失败，已取消启动监控。", RGB(255, 80, 80));
    if (!IsWindowVisible() && m_pWebDlg) {
        json reply; reply["action"] = "start_guard"; reply["success"] = false;
        reply["message"] = std::string(CW2A(msg, CP_UTF8));
        CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
        m_pWebDlg->SendStateToWeb(jsonStr);
    }
    else {
        ShowCenteredMsgBox(msg, L"Umi-OCR 未运行", MB_ICONWARNING);
    }
    BroadcastStateToWeb();
    return 0;
}

void CDNFGameCaptureDlg::BeginOcrServiceRecovery(bool probeBeforePending)
{
    if (!m_bIsRunning || m_bOcrStartPending.load()) {
        return;
    }
    if (probeBeforePending) {
        if (m_bOcrRecoveryPending.load() || m_bOcrHealthCheckPending.exchange(true)) {
            return;
        }

        HWND hWnd = GetSafeHwnd();
        std::thread([this, hWnd]() {
            bool processRunning = DnfIsProcessRunningByName(L"Umi-OCR.exe");
            bool ready = ProbeOcrServiceReady();
            if (!::IsWindow(hWnd)) return;
            m_bOcrHealthCheckPending = false;
            if (m_bIsRunning && !m_bOcrStartPending.load() && (!ready || !processRunning)) {
                if (!processRunning) {
                    AppLog(L"🔄 [Umi-OCR] 检测到主进程已退出，正在后台重新拉起...", RGB(255, 200, 0));
                    WriteMatchLog(L"[Umi-OCR] 检测到主进程已退出，正在后台重新拉起。");
                }
                BeginOcrServiceRecovery(false);
            }
        }).detach();
        return;
    }
    if (m_bOcrRecoveryPending.exchange(true)) {
        return;
    }

    DWORD requestId = m_ocrRecoveryRequestId.fetch_add(1) + 1;
    HWND hWnd = GetSafeHwnd();

    std::thread([this, hWnd, requestId]() {
        bool ok = false;
        bool processRunning = DnfIsProcessRunningByName(L"Umi-OCR.exe");
        bool alreadyReady = ProbeOcrServiceReady();
        if (alreadyReady && processRunning) {
            ok = true;
        }
        else {
            if (!processRunning) {
                AppLog(L"🔄 [Umi-OCR] 检测到主进程已退出，正在后台尝试恢复...", RGB(255, 200, 0));
                WriteMatchLog(L"[Umi-OCR] 检测到主进程已退出，正在后台尝试恢复。");
            }
            else {
                AppLog(L"🔄 [Umi-OCR] 运行中检测到服务离线，正在后台尝试恢复...", RGB(255, 200, 0));
                WriteMatchLog(L"[Umi-OCR] 运行中检测到服务离线，正在后台尝试恢复。");
            }
            ok = EnsureOcrRunning(!processRunning);
        }

        if (!::IsWindow(hWnd)) return;
        if (!m_bOcrRecoveryPending.load() || requestId != m_ocrRecoveryRequestId.load()) return;
        ::PostMessage(hWnd, WM_OCR_RECOVER_RESULT, (WPARAM)requestId, ok ? 1 : 0);
        }).detach();
}

LRESULT CDNFGameCaptureDlg::OnOcrRecoverResult(WPARAM wParam, LPARAM lParam)
{
    DWORD requestId = (DWORD)wParam;
    bool success = (lParam != 0);
    if (!m_bOcrRecoveryPending.load() || requestId != m_ocrRecoveryRequestId.load()) {
        return 0;
    }

    m_bOcrRecoveryPending = false;

    if (success) {
        if (m_bIsRunning) {
            m_status.SetWindowText(L"监控中...");
        }
        BroadcastStateToWeb();
        return 0;
    }

    if (m_bIsRunning) {
        AppLog(L"⚠️ [Umi-OCR] 后台恢复暂时失败，将继续保持监控并等待下次重试。", RGB(255, 180, 0));
        WriteMatchLog(L"[Umi-OCR] 后台恢复暂时失败，等待下次重试。");
    }
    BroadcastStateToWeb();
    return 0;
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

    if (!m_hHttpConnect) {
        if (m_bIsRunning) {
            BeginOcrServiceRecovery();
        }
        return result;
    }
    if (m_bOcrRecoveryPending.load())
        return result;

    auto requestOcrRecovery = [&]() {
        if (m_bIsRunning) {
            BeginOcrServiceRecovery();
        }
    };

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
            requestOcrRecovery();
        }
        WinHttpCloseHandle(hRequest);
    }
    else {
        requestOcrRecovery();
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
void CDNFGameCaptureDlg::AddReviewEvent(const RecentEvent& ev)
{
    m_recentEvents.push_back(ev);
}

bool CDNFGameCaptureDlg::ToggleReviewEvent(int eventId)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    for (auto& ev : m_recentEvents) {
        if (ev.id != eventId) continue;

        if (!ev.undone && ev.statsApplied) {
            if (ev.killerIdx >= 0 && ev.killerIdx < 8 && m_players[ev.killerIdx].kills > 0) {
                m_players[ev.killerIdx].kills--;
                if (ev.akDelta > 0 && m_players[ev.killerIdx].akCount > 0) {
                    m_players[ev.killerIdx].akCount--;
                }
            }
            if (ev.deadIdx >= 0 && ev.deadIdx < 8 && m_players[ev.deadIdx].deaths > 0) {
                m_players[ev.deadIdx].deaths--;
            }
            if (ev.redScoreDelta > 0 && m_totalScoreRed > 0) m_totalScoreRed--;
            if (ev.blueScoreDelta > 0 && m_totalScoreBlue > 0) m_totalScoreBlue--;

            ev.undone = true;
            ev.statsApplied = false;
            ev.status = L"已撤销";
            AppLog(L"↩️ [复盘撤销] 已撤销事件：" + ev.killer + L" -> " + ev.dead, RGB(255, 210, 80));
            return true;
        }

        if (ev.undone && !ev.statsApplied) {
            if (ev.killerIdx >= 0 && ev.killerIdx < 8) {
                m_players[ev.killerIdx].kills++;
                if (ev.akDelta > 0) {
                    m_players[ev.killerIdx].akCount++;
                }
                m_lastKillerTeam = m_players[ev.killerIdx].team;
            }
            if (ev.deadIdx >= 0 && ev.deadIdx < 8) {
                m_players[ev.deadIdx].deaths++;
            }
            if (ev.redScoreDelta > 0) m_totalScoreRed++;
            if (ev.blueScoreDelta > 0) m_totalScoreBlue++;

            ev.undone = false;
            ev.statsApplied = true;
            ev.status = L"已计入";
            AppLog(L"🔁 [复盘恢复] 已恢复事件：" + ev.killer + L" -> " + ev.dead, RGB(0, 255, 200));
            return true;
        }

        return false;
    }
    return false;
}

void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide)
{
    int killerArea = (triggerSide == 0) ? 1 : 0;
    int deadArea = triggerSide;
    bool killerIsLeft = (killerArea == 0);

    CString triggerDiag;
    triggerDiag.Format(L"[击杀触发诊断] 进入OCR匹配：当前是否翻转红蓝=%s；物理死亡侧=%s；物理杀手侧=%s；说明=翻转红蓝只影响界面/OBS左右显示，不改变OCR区域、X检测位置和物理侧候选队伍。",
        m_bFlipSides ? L"是" : L"否",
        triggerSide == 0 ? L"左边" : L"右边",
        killerArea == 0 ? L"左边" : L"右边");
    WriteMatchLog(triggerDiag);

    bool killerResolved = false, deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";
    int killerBestP = -1, killerBestA = -1;
    int deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    // 首轮全局最优记录：首轮只收集，扫完再统一决定是否锁定。
    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999;
    CString globalKillerName;
    CString globalKillerAlias;
    CString globalKillerFrameText;
    int globalKillerFrameIdx = -1;
    int globalKillerBestRealLen = 0;
    bool globalKillerBestHasMetaContext = false;
    int globalKillerSecondScore = -1;
    CString globalKillerSecondName;
    CString globalKillerSecondAlias;

    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999;
    CString globalDeadName;
    CString globalDeadAlias;
    CString globalDeadFrameText;
    int globalDeadFrameIdx = -1;
    int globalDeadBestRealLen = 0;
    bool globalDeadBestHasMetaContext = false;
    int globalDeadSecondScore = -1;
    CString globalDeadSecondName;
    CString globalDeadSecondAlias;
    static std::atomic<int> s_reviewEventSeq{ 1 };
    int reviewEventId = s_reviewEventSeq.fetch_add(1);
    CString killerFusionSummary;
    CString deadFusionSummary;

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

    auto PushVisualOnlyLog = [&](const CString& msg, COLORREF color) {
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

    auto recordFirstRoundCandidate = [&](bool isKiller, int score, int p, int a, int passLine,
        int realLen, bool hasMetaContext, const CString& aliasName, const CString& ocrText, int frameIdx) {
            int& bestScore = isKiller ? globalKillerBestScore : globalDeadBestScore;
            int& bestP = isKiller ? globalKillerBestP : globalDeadBestP;
            int& bestA = isKiller ? globalKillerBestA : globalDeadBestA;
            int& bestPassLine = isKiller ? globalKillerPassLine : globalDeadPassLine;
            CString& bestName = isKiller ? globalKillerName : globalDeadName;
            CString& bestAlias = isKiller ? globalKillerAlias : globalDeadAlias;
            CString& bestFrameText = isKiller ? globalKillerFrameText : globalDeadFrameText;
            int& bestFrameIdx = isKiller ? globalKillerFrameIdx : globalDeadFrameIdx;
            int& bestRealLen = isKiller ? globalKillerBestRealLen : globalDeadBestRealLen;
            bool& bestMeta = isKiller ? globalKillerBestHasMetaContext : globalDeadBestHasMetaContext;
            int& secondScore = isKiller ? globalKillerSecondScore : globalDeadSecondScore;
            CString& secondName = isKiller ? globalKillerSecondName : globalDeadSecondName;
            CString& secondAlias = isKiller ? globalKillerSecondAlias : globalDeadSecondAlias;

            CString ownerName;
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                if (p >= 0 && p < 8) ownerName = m_players[p].name;
            }

            bool replaceBest = false;
            if (score > bestScore) {
                replaceBest = true;
            }
            else if (score == bestScore && score > 0 && realLen > bestRealLen) {
                replaceBest = true;
            }

            if (replaceBest) {
                if (bestP != -1) {
                    secondScore = bestScore;
                    secondName = bestName;
                    secondAlias = bestAlias;
                }
                bestScore = score;
                bestP = p;
                bestA = a;
                bestPassLine = passLine;
                bestName = ownerName;
                bestAlias = aliasName;
                bestFrameText = ocrText;
                bestFrameIdx = frameIdx;
                bestRealLen = realLen;
                bestMeta = hasMetaContext;
            }
            else if (score > secondScore) {
                secondScore = score;
                secondName = ownerName;
                secondAlias = aliasName;
            }
        };

    auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName,
        bool isKiller, int& outBestP, int& outBestA,
        int& frameScore, bool isAggressive, int frameIdx, bool collectOnly) -> bool
        {
            frameScore = -2;
            if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1)
                return false;

            CString logMsg;
            logMsg.Format(L"▶ [%s] 第%d帧提取: \"%s\"",
                isKiller ? L"找杀手" : L"找死者", frameIdx, (LPCTSTR)ocrResult);
            WriteMatchLog(logMsg);

            TDnfSimpleAliasMeta ocrMeta = DnfParseAliasMeta(ocrResult);

            CString weakOcrId = ocrMeta.realId.IsEmpty() ? ocrResult : ocrMeta.realId;
            CString weakOcrNorm = DnfNormalizeLooseText(weakOcrId);
            bool weakSingleCharOcr = (weakOcrNorm.GetLength() <= 1 && !ocrMeta.hasArea && !ocrMeta.hasJob);
            if (weakSingleCharOcr) {
                CString guardLog;
                guardLog.Format(L"  └ [🛡单字OCR保护] 本帧只读到 [%s]，没有大区/#职业，不参与名称匹配，避免误命中长ID", (LPCTSTR)ocrResult);
                WriteMatchLog(guardLog);
                return false;
            }

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
                        TDnfPreparedIdMatch preparedExact = DnfPrepareIdMatch(aliasMeta, ocrMeta);
                        bool exactFull = (DnfNormalizeLooseText(m_players[p].aliases[a].name) == DnfNormalizeLooseText(ocrResult));
                        bool exactMeta = DnfAliasMetaExactSame(aliasMeta, ocrMeta);
                        if ((exactFull || exactMeta) && !preparedExact.allowStrongIdLock) {
                            CString exactSkip;
                            exactSkip.Format(L"  └ [🛡单字ID保护] 候选[%s] 与OCR[%s] 看似唯一，但%s，等待职业唯一或更多证据。",
                                (LPCTSTR)m_players[p].aliases[a].name, (LPCTSTR)ocrResult, (LPCTSTR)preparedExact.note);
                            WriteMatchLog(exactSkip);
                            exactFull = false;
                            exactMeta = false;
                        }
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
                if (collectOnly) {
                    CString aliasName;
                    {
                        std::lock_guard<std::mutex> lock(m_dataMutex);
                        if (exactMatchP >= 0 && exactMatchP < 8 && exactMatchA >= 0
                            && (size_t)exactMatchA < m_players[exactMatchP].aliases.size()) {
                            aliasName = m_players[exactMatchP].aliases[exactMatchA].name;
                        }
                    }
                    recordFirstRoundCandidate(isKiller, 100, exactMatchP, exactMatchA, 100,
                        99, true, aliasName, ocrResult, frameIdx);
                    CString collectLog;
                    collectLog.Format(L"  └ [首轮收集] 精确别名候选：%s / %s，100分；等待首轮全部帧结束后统一锁定。",
                        isKiller ? L"杀手" : L"死者",
                        aliasName.IsEmpty() ? L"未知小号" : aliasName.GetString());
                    WriteMatchLog(collectLog);
                    return false;
                }
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
                WriteMatchLog(L"  └ [⚠️别名冲突] 多个玩家拥有相同别名，等待ID帧...");
                return false; // 本帧放弃
            }

            // ====================================================
            // 没有唯一别名命中，进入原有匹配逻辑
            // ====================================================
            int maxScore = -2, bestP = -1, bestA = -1, bestRealLen = 0;
            bool bestHasMetaContext = false;
            std::wstring bestName;
            CString bestAliasName;

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
                    TDnfPreparedIdMatch prepared = DnfPrepareIdMatch(aliasMeta, ocrMeta);
                    CString candId = prepared.candidateId.IsEmpty() ? m_players[p].aliases[a].name : prepared.candidateId;
                    CString ocrId = prepared.ocrId.IsEmpty() ? ocrResult : prepared.ocrId;

                    int aliasScore = m_matcher.GetMatchScore(candId.GetString(), ocrId.GetString(), isAggressive);
                    if (aliasScore == -1) {
                        // 如果 OCR 是纯职业帧，保持旧逻辑：本帧职业干扰，跳过。
                        maxScore = -1;
                        break;
                    }
                    aliasScore = max(aliasScore, prepared.score);

                    bool strongMetaContext = false;
                    int metaScore = DnfMetaContextScore(aliasMeta, ocrMeta, candId.GetLength(), strongMetaContext);
                    aliasScore += metaScore;
                    if (!prepared.allowStrongIdLock) {
                        strongMetaContext = false;
                        aliasScore = min(aliasScore, 34);
                    }
                    if (prepared.areaShortIdAssist && prepared.realIdLen == 2) {
                        strongMetaContext = true;
                    }

                    if (aliasScore > 160) aliasScore = 160;
                    aliasScore -= teamPenalty;

                    if (aliasScore > curScore) {
                        curScore = aliasScore;
                        curBestName = m_players[p].aliases[a].name.GetString();
                        curBestAlias = (int)a;
                        curRealLen = prepared.realIdLen > 0 ? prepared.realIdLen : candId.GetLength();
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
                    bestAliasName = curBestName.c_str();
                    bestRealLen = curRealLen;
                    bestHasMetaContext = curBestMetaContext;
                }
            }
            m_dataMutex.unlock();

            frameScore = maxScore;

            if (maxScore == -1) {
                if (isAggressive) {
                    CString keepJob;
                    keepJob.Format(L"  └ [二轮职业帧] 本帧是职业文本 [%s]，不再当成无效噪声；保留给 #职业/同职业ID 二轮兜底。", (LPCTSTR)ocrResult);
                    WriteMatchLog(keepJob);
                }
                else {
                    WriteMatchLog(L"  └ [⚠️职业干扰] 跳过本帧...");
                }
                return true;
            }

            int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);

            // 更新全局最优记录
            if (bestP != -1) {
                recordFirstRoundCandidate(isKiller, maxScore, bestP, bestA, passLine,
                    bestRealLen, bestHasMetaContext, bestAliasName, ocrResult, frameIdx);
            }

            if (collectOnly) {
                CString collectLog;
                if (bestP != -1) {
                    int secondScore = isKiller ? globalKillerSecondScore : globalDeadSecondScore;
                    collectLog.Format(L"  └ [首轮收集] 本帧最高候选[%s] %d分/线%d；当前首轮最高=%d，第二=%d；等待全部帧结束后统一锁定",
                        bestName.c_str(), maxScore, passLine,
                        isKiller ? globalKillerBestScore : globalDeadBestScore,
                        secondScore);
                }
                else {
                    collectLog.Format(L"  └ [首轮收集] 本帧没有可用候选，最高=%d", maxScore);
                }
                WriteMatchLog(collectLog);
                return false;
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
                    WriteMatchLog(guardLog);
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
                    successLog.Format(L"  └ [✔单帧匹配] 成功指向: %s (%d分)", (LPCTSTR)finalName, maxScore);

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
                    WriteMatchLog(failLog);
            }

            return false;
        };

    auto acceptCollectedFirstRound = [&](bool isKiller, bool& resolved, CString& finalName,
        int& outBestP, int& outBestA, int& lockedTeam) -> bool {
            if (resolved) return false;

            int bestScore = isKiller ? globalKillerBestScore : globalDeadBestScore;
            int bestP = isKiller ? globalKillerBestP : globalDeadBestP;
            int bestA = isKiller ? globalKillerBestA : globalDeadBestA;
            int passLine = isKiller ? globalKillerPassLine : globalDeadPassLine;
            int realLen = isKiller ? globalKillerBestRealLen : globalDeadBestRealLen;
            bool hasMeta = isKiller ? globalKillerBestHasMetaContext : globalDeadBestHasMetaContext;
            int secondScore = isKiller ? globalKillerSecondScore : globalDeadSecondScore;
            CString bestName = isKiller ? globalKillerName : globalDeadName;
            CString bestAlias = isKiller ? globalKillerAlias : globalDeadAlias;
            CString secondName = isKiller ? globalKillerSecondName : globalDeadSecondName;
            CString secondAlias = isKiller ? globalKillerSecondAlias : globalDeadSecondAlias;
            CString frameText = isKiller ? globalKillerFrameText : globalDeadFrameText;
            int frameIdx = isKiller ? globalKillerFrameIdx : globalDeadFrameIdx;
            CString roleText = isKiller ? L"杀手" : L"死者";

            if (bestP < 0 || bestScore < 0) {
                CString noBest;
                noBest.Format(L"[首轮统一决策][%s] 未锁定：没有可用候选。", (LPCTSTR)roleText);
                WriteMatchLog(noBest);
                return false;
            }

            int gap = (secondScore >= 0) ? (bestScore - secondScore) : 999;
            bool pass = bestScore >= passLine;
            CString rejectReason;
            if (!pass) {
                rejectReason.Format(L"分数不够：最高%d，及格线%d", bestScore, passLine);
            }
            else if (realLen <= 2 && !hasMeta) {
                pass = false;
                rejectReason.Format(L"短ID保护：真实ID长度=%d，缺少#职业或2字ID大区辅助", realLen);
            }
            else if (gap < 8 && secondScore >= 0 && bestScore < 95) {
                pass = false;
                rejectReason.Format(L"分差不够：最高%d，第二%d，分差%d", bestScore, secondScore, gap);
            }

            if (!pass) {
                CString fail;
                fail.Format(L"[首轮统一决策][%s] 未锁定：最高=%s/%s %d分/线%d；第二=%s/%s %d分；来源=第%d帧\"%s\"；原因=%s。",
                    (LPCTSTR)roleText,
                    bestName.IsEmpty() ? L"无" : bestName.GetString(),
                    bestAlias.IsEmpty() ? L"无" : bestAlias.GetString(),
                    bestScore, passLine,
                    secondName.IsEmpty() ? L"无" : secondName.GetString(),
                    secondAlias.IsEmpty() ? L"无" : secondAlias.GetString(),
                    secondScore,
                    frameIdx,
                    frameText.IsEmpty() ? L"无" : frameText.GetString(),
                    (LPCTSTR)rejectReason);
                WriteMatchLog(fail);
                CString uiFail;
                uiFail.Format(L"[首轮未锁定][%s] 最高=%s/%s %d分，原因=%s",
                    (LPCTSTR)roleText,
                    bestName.IsEmpty() ? L"无" : bestName.GetString(),
                    bestAlias.IsEmpty() ? L"无" : bestAlias.GetString(),
                    bestScore,
                    (LPCTSTR)rejectReason);
                PushVisualOnlyLog(uiFail, RGB(255, 210, 80));
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                if (bestP < 0 || bestP >= 8 || m_players[bestP].name.IsEmpty()) return false;
                finalName = m_players[bestP].name;
                outBestP = bestP;
                outBestA = bestA;
                lockedTeam = m_players[bestP].team;
            }
            resolved = true;

            CString ok;
            ok.Format(L"[首轮统一决策][%s] 采用：主号=%s；命中小号=%s；最高=%d/线%d；第二=%s/%s %d分；分差=%d；来源=第%d帧\"%s\"；说明=首轮全部帧扫完后统一选择。",
                (LPCTSTR)roleText,
                (LPCTSTR)finalName,
                bestAlias.IsEmpty() ? L"无" : bestAlias.GetString(),
                bestScore, passLine,
                secondName.IsEmpty() ? L"无" : secondName.GetString(),
                secondAlias.IsEmpty() ? L"无" : secondAlias.GetString(),
                secondScore, gap, frameIdx,
                frameText.IsEmpty() ? L"无" : frameText.GetString());
            COLORREF teamColor = (lockedTeam == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
            WriteMatchLog(ok);
            CString bestAliasDisplay = DnfAliasDisplayName(bestAlias, false);
            CString uiOk;
            uiOk.Format(L"[首轮采用][%s] %s <= %s (%d分)",
                (LPCTSTR)roleText,
                (LPCTSTR)finalName,
                bestAliasDisplay.IsEmpty() ? L"无" : bestAliasDisplay.GetString(),
                bestScore);
            PushVisualOnlyLog(uiOk, teamColor);
            return true;
        };

    // ============================================================
    // 【简化兜底】两轮规则：
    //   1. 第一轮只用非纯职业 OCR 帧做正常 ID 匹配。
    //   2. 第一轮未锁定时，第二轮才用纯职业帧匹配 #职业。
    //      同职业唯一直接锁定；同职业多人按第一轮 ID 分择优；ID 分相同固定取遍历顺序第一个。
    //   3. 大区不再作为独立条件/加分/放行依据，只保留在 # 前完整 ID 字符串里参与普通匹配。
    // ============================================================
    auto trySimpleMetaFallback = [&](bool isKiller, int areaIndex, const std::vector<FrameData>& frames,
        bool& resolved, CString& finalName, int& outBestP, int& outBestA, int& lockedTeam) -> bool
        {
            if (resolved) return false;

            CString roleText = isKiller ? L"杀手" : L"死者";
            CString sideText = areaIndex == 0 ? L"左框" : L"右框";
            TDnfPanelSide panelSide = (areaIndex == 0) ? TDnfPanelSide::LeftNameArea : TDnfPanelSide::RightAreaName;
            CString candidateScopeText = L"上场8人";
            const int ID_LOCK_SCORE = 35;

            struct Evidence {
                CString topJob;
                int topJobCount = 0;
                std::vector<CString> idTexts;
                CString rawList;
            } ev;

            // 先收集上场 8 人候选声明过的职业，用于判断 OCR 帧是不是职业帧。
            std::vector<CString> declaredJobs;
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                for (int p = 0; p < 8; ++p) {
                    for (const auto& a : m_players[p].aliases) {
                        TDnfSimpleAliasMeta am = DnfParseAliasMeta(a.name);
                        if (am.hasJob) declaredJobs.push_back(am.job);
                    }
                }
            }

            auto isJobFrame = [&](const CString& raw, CString& jobOut) -> bool {
                // 只把纯职业帧当职业证据。含 ID/大区/额外文字的帧必须留给第一轮 ID 匹配。
                TDnfParsedPanelText parsed = m_identityMatcher.ParsePanelText(raw, panelSide);
                if (parsed.kind == TDnfFrameKind::Profession && !parsed.profession.IsEmpty()) {
                    jobOut = parsed.profession;
                    return true;
                }

                // 再用上场小号声明的 #职业 做补充，兼容职业别名/自定义写法。
                CString nr = DnfNormalizeLooseText(raw);
                if (nr.IsEmpty()) return false;
                for (const CString& j : declaredJobs) {
                    CString nj = DnfNormalizeLooseText(j);
                    if (nj.IsEmpty()) continue;
                    if (nr.GetLength() > nj.GetLength() + 2) continue;
                    int js = DnfFuzzyJobScore(j, raw);
                    if (js >= 72) {
                        // 输出声明职业名，后面仍用模糊匹配判断，不再要求完全相等。
                        jobOut = j;
                        return true;
                    }
                }
                return false;
            };

            auto addIdEvidence = [&](CString id, const CString& why) {
                id.Trim();
                CString nid = DnfNormalizeLooseText(id);
                if (nid.GetLength() < 2) return;
                // 纯职业词不作为 ID 文本；但是“柔道夏乘”这种包含职业词的 ID 必须保留。
                CString tmpJob;
                if (isJobFrame(id, tmpJob)) return;
                for (const CString& oldId : ev.idTexts) {
                    if (DnfNormalizeLooseText(oldId) == nid) return;
                }
                ev.idTexts.push_back(id);
                CString msg;
                msg.Format(L"[简化兜底][%s-%s] 收集ID证据：文本=\"%s\"；来源=%s。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText, (LPCTSTR)id, (LPCTSTR)why);
                WriteMatchLog(msg);
            };

            for (const auto& f : frames) {
                CString raw = f.text;
                raw.Trim();
                if (raw.IsEmpty() || raw.Find(L"No text") >= 0) continue;
                if (!ev.rawList.IsEmpty()) ev.rawList += L" | ";
                CString one;
                one.Format(L"第%d帧=\"%s\"", f.frameIdx, (LPCTSTR)raw);
                ev.rawList += one;

                CString job;
                if (isJobFrame(raw, job)) {
                    CString normJob = DnfNormalizeJobAlias(job);
                    if (ev.topJob.IsEmpty()) {
                        ev.topJob = normJob.IsEmpty() ? job : normJob;
                        ev.topJobCount = 1;
                    }
                    else if (DnfFuzzyJobSame(ev.topJob, job)) {
                        CString nj = normJob.IsEmpty() ? job : normJob;
                        if (DnfNormalizeLooseText(nj).GetLength() >= DnfNormalizeLooseText(ev.topJob).GetLength()) {
                            ev.topJob = nj;
                        }
                        ev.topJobCount++;
                    }
                    else {
                        if (ev.topJobCount <= 1) {
                            ev.topJob = normJob.IsEmpty() ? job : normJob;
                            ev.topJobCount = 1;
                        }
                    }
                }
                else {
                    addIdEvidence(raw, L"非职业OCR原文整体");
                }
            }

            CString head;
            head.Format(L"[简化兜底][%s-%s] 开始：候选范围=%s；翻转红蓝=%s；OCR原文={%s}；职业证据=%s(%d帧)；ID文本数量=%d；规则=第一轮ID匹配，第二轮#职业匹配，大区仅作为ID普通字符。",
                (LPCTSTR)sideText, (LPCTSTR)roleText,
                (LPCTSTR)candidateScopeText, m_bFlipSides ? L"是" : L"否",
                ev.rawList.IsEmpty() ? L"空" : ev.rawList.GetString(),
                ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString(), ev.topJobCount,
                (int)ev.idTexts.size());
            WriteMatchLog(head);

            struct SimpleCandidate {
                int p = -1;
                int a = -1;
                int team = -1;
                int realIdLen = 0;
                int passLine = 999;
                CString owner;
                CString alias;
                CString matchId;
                CString job;
                CString bestIdText;
                CString scoreCandId;
                CString scoreOcrId;
                CString matchMode;
                CString idNote;
                bool hasJob = false;
                bool jobMatch = false;
                bool allowStrongIdLock = false;
                bool areaShortIdAssist = false;
                int idScore = 0;
            };
            std::vector<SimpleCandidate> list;

            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                for (int p = 0; p < 8; ++p) {
                    if (m_players[p].name.IsEmpty()) continue;
                    for (size_t a = 0; a < m_players[p].aliases.size(); ++a) {
                        TDnfSimpleAliasMeta am = DnfParseAliasMeta(m_players[p].aliases[a].name);
                        CString matchId = am.matchId.IsEmpty() ? m_players[p].aliases[a].name : am.matchId;
                        if (matchId.IsEmpty()) continue;

                        SimpleCandidate c;
                        c.p = p;
                        c.a = (int)a;
                        c.team = m_players[p].team;
                        c.owner = m_players[p].name;
                        c.alias = m_players[p].aliases[a].name;
                        c.matchId = matchId;
                        c.job = am.job;
                        c.hasJob = am.hasJob;
                        c.realIdLen = DnfNormalizeLooseText(am.realId.IsEmpty() ? matchId : am.realId).GetLength();
                        c.passLine = CNameMatcher::GetDynamicThreshold(c.realIdLen);
                        int jobFuzzyScore = 0;
                        c.jobMatch = am.hasJob && !ev.topJob.IsEmpty() && DnfFuzzyJobSame(am.job, ev.topJob, &jobFuzzyScore);

                        for (const CString& idText : ev.idTexts) {
                            TDnfPreparedIdMatch prepared = DnfPrepareIdMatch(matchId, idText);
                            if (prepared.score > c.idScore) {
                                c.idScore = prepared.score;
                                c.bestIdText = idText;
                                c.scoreCandId = prepared.candidateId;
                                c.scoreOcrId = prepared.ocrId;
                                c.matchMode = prepared.mode;
                                c.idNote = prepared.note;
                                c.allowStrongIdLock = prepared.allowStrongIdLock;
                                c.areaShortIdAssist = prepared.areaShortIdAssist;
                            }
                        }

                        list.push_back(c);
                    }
                }
            }

            for (int i = 0; i < (int)list.size(); ++i) {
                const auto& c = list[i];
                CString line;
                line.Format(L"[简化兜底][%s-%s] 候选：主号=%s；小号=%s；匹配ID=%s；职业=%s；真实ID长度=%d；动态线=%d；ID分=%d；最佳ID证据=%s；候选打分ID=%s；OCR打分ID=%s；匹配方式=%s；2字大区辅助=%s；职业一致=%s；阶段=候选收集；是否采用=否；原因=%s。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    (LPCTSTR)c.owner, (LPCTSTR)c.alias,
                    c.matchId.IsEmpty() ? L"无" : c.matchId.GetString(),
                    c.job.IsEmpty() ? L"无" : c.job.GetString(),
                    c.realIdLen, c.passLine, c.idScore,
                    c.bestIdText.IsEmpty() ? L"无" : c.bestIdText.GetString(),
                    c.scoreCandId.IsEmpty() ? L"无" : c.scoreCandId.GetString(),
                    c.scoreOcrId.IsEmpty() ? L"无" : c.scoreOcrId.GetString(),
                    c.matchMode.IsEmpty() ? L"无" : c.matchMode.GetString(),
                    (LPCTSTR)DnfBoolCN(c.areaShortIdAssist),
                    (LPCTSTR)DnfBoolCN(c.jobMatch),
                    c.idNote.IsEmpty() ? L"等待两轮规则选择" : c.idNote.GetString());
                WriteMatchLog(line);
            }

            int blockedTeam = -1;
            if (isKiller && lockedDeadTeam != -1) blockedTeam = lockedDeadTeam;
            if (!isKiller && lockedKillerTeam != -1) blockedTeam = lockedKillerTeam;

            auto chooseByIdScore = [&](const std::vector<int>& indices, int minScore, bool requirePassLine,
                bool allowOneCharStrongId, CString& tieList, int& tieCount, int& bestScore) -> int {
                bestScore = -1;
                tieCount = 0;
                tieList.Empty();
                int selectedIdx = -1;
                for (int idx : indices) {
                    if (idx < 0 || idx >= (int)list.size()) continue;
                    const auto& c = list[idx];
                    if (blockedTeam != -1 && c.team == blockedTeam) {
                        CString skip;
                        skip.Format(L"[简化兜底][%s-%s] 跳过候选：主号=%s；小号=%s；ID分=%d；队伍=%d；原因=与另一方已锁定队伍%d冲突。",
                            (LPCTSTR)sideText, (LPCTSTR)roleText,
                            (LPCTSTR)c.owner, (LPCTSTR)c.alias,
                            c.idScore, c.team, blockedTeam);
                        WriteMatchLog(skip);
                        continue;
                    }
                    if (c.idScore < minScore) continue;
                    if (requirePassLine && c.idScore < c.passLine) continue;
                    if (!allowOneCharStrongId && c.realIdLen <= 1 && !c.allowStrongIdLock) continue;
                    if (!c.allowStrongIdLock) {
                        CString skip;
                        skip.Format(L"[简化兜底][%s-%s] 跳过候选：主号=%s；小号=%s；ID分=%d；候选打分ID=%s；OCR打分ID=%s；匹配方式=%s；原因=%s。",
                            (LPCTSTR)sideText, (LPCTSTR)roleText,
                            (LPCTSTR)c.owner, (LPCTSTR)c.alias,
                            c.idScore,
                            c.scoreCandId.IsEmpty() ? L"无" : c.scoreCandId.GetString(),
                            c.scoreOcrId.IsEmpty() ? L"无" : c.scoreOcrId.GetString(),
                            c.matchMode.IsEmpty() ? L"无" : c.matchMode.GetString(),
                            c.idNote.IsEmpty() ? L"真实ID仅1字，不作为强ID锁定依据" : c.idNote.GetString());
                        WriteMatchLog(skip);
                        continue;
                    }
                    if (c.idScore > bestScore) {
                        bestScore = c.idScore;
                        selectedIdx = idx;
                        tieCount = 1;
                        tieList.Empty();
                        CString one;
                        one.Format(L"%s/%s/槽位%d/队伍%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.p + 1, c.team);
                        tieList = one;
                    }
                    else if (c.idScore == bestScore) {
                        tieCount++;
                        CString one;
                        one.Format(L"%s/%s/槽位%d/队伍%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.p + 1, c.team);
                        if (!tieList.IsEmpty()) tieList += L"；";
                        tieList += one;
                    }
                }
                return selectedIdx;
            };

            auto chooseJobCandidateByIdScore = [&](const std::vector<int>& indices,
                CString& tieList, int& tieCount, int& bestScore, CString& bestDetail, CString& allScores) -> int {
                    bestScore = 0;
                    tieCount = 0;
                    tieList.Empty();
                    bestDetail.Empty();
                    allScores.Empty();
                    int selectedIdx = -1;
                    for (int idx : indices) {
                        if (idx < 0 || idx >= (int)list.size()) continue;
                        const auto& c = list[idx];
                        CString oneScore;
                        oneScore.Format(L"%s/%s ID分=%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.idScore);
                        if (!allScores.IsEmpty()) allScores += L"；";
                        allScores += oneScore;

                        if (c.idScore < 1) continue;
                        if (c.idScore > bestScore) {
                            bestScore = c.idScore;
                            selectedIdx = idx;
                            tieCount = 1;
                            CString one;
                            one.Format(L"%s/%s/槽位%d/队伍%d/ID分%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.p + 1, c.team, c.idScore);
                            tieList = one;
                            bestDetail.Format(L"%s/%s ID分=%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.idScore);
                        }
                        else if (c.idScore == bestScore) {
                            tieCount++;
                            CString one;
                            one.Format(L"%s/%s/槽位%d/队伍%d/ID分%d", (LPCTSTR)c.owner, (LPCTSTR)c.alias, c.p + 1, c.team, c.idScore);
                            if (!tieList.IsEmpty()) tieList += L"；";
                            tieList += one;
                        }
                    }
                    return selectedIdx;
                };

            auto acceptCandidate = [&](int selectedIdx, const CString& mode, const CString& reason) -> bool {
                if (selectedIdx < 0 || selectedIdx >= (int)list.size()) return false;
                const auto& c = list[selectedIdx];
                resolved = true;
                finalName = c.owner;
                outBestP = c.p;
                outBestA = c.a;
                lockedTeam = c.team;
                CString ok;
                ok.Format(L"[最终采用结果][%s-%s] 通过简化兜底：主号=%s；命中小号=%s；模式=%s；ID分=%d；队伍=%d；采用原因=%s；说明=两轮兜底；大区未作为独立条件、加分项或放行条件。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    (LPCTSTR)c.owner, (LPCTSTR)c.alias,
                    (LPCTSTR)mode, c.idScore, c.team,
                    (LPCTSTR)reason);
                WriteMatchLog(ok);
                COLORREF teamColor = (lockedTeam == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
                CString aliasDisplay = DnfAliasDisplayName(c.alias, mode.Find(L"第二轮职业") == 0);
                CString uiHit;
                uiHit.Format(L"[简化兜底命中][%s] %s <= %s (%s, ID分=%d)",
                    (LPCTSTR)roleText,
                    (LPCTSTR)finalName,
                    aliasDisplay.IsEmpty() ? L"无" : aliasDisplay.GetString(),
                    (LPCTSTR)mode,
                    c.idScore);
                PushVisualOnlyLog(uiHit, teamColor);
                return true;
            };

            std::vector<int> allIndices;
            allIndices.reserve(list.size());
            for (int i = 0; i < (int)list.size(); ++i) allIndices.push_back(i);

            CString tieList;
            int tieCount = 0;
            int bestIdScore = -1;
            int selectedIdx = chooseByIdScore(allIndices, 1, true, true, tieList, tieCount, bestIdScore);
            if (selectedIdx >= 0) {
                const auto& picked = list[selectedIdx];
                CString summary;
                summary.Format(L"[简化兜底][%s-%s] 第一轮正常ID匹配：候选范围=上场8人；采用动态及格线；选中动态线=%d；最高ID分=%d；最高分同分候选数=%d；同分处理=固定选择遍历顺序第一个；并列=%s。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    picked.passLine, bestIdScore, tieCount,
                    tieList.IsEmpty() ? L"无" : tieList.GetString());
                WriteMatchLog(summary);
                return acceptCandidate(selectedIdx, L"第一轮正常ID匹配", L"非职业ID帧达到动态及格线，按最高ID分选择；ID分相同固定取遍历顺序第一个");
            }

            CString firstFail;
            firstFail.Format(L"[简化兜底][%s-%s] 第一轮正常ID匹配未锁定：候选范围=上场8人；规则=动态及格线+短ID保护；当前最高可用ID分=%d；处理=进入第二轮职业匹配。",
                (LPCTSTR)sideText, (LPCTSTR)roleText, bestIdScore);
            WriteMatchLog(firstFail);

            std::vector<int> jobIndices;
            for (int i = 0; i < (int)list.size(); ++i) {
                if (list[i].jobMatch) jobIndices.push_back(i);
            }

            if (ev.topJob.IsEmpty() || jobIndices.empty()) {
                CString fail;
                fail.Format(L"[简化兜底][%s-%s] 第二轮职业匹配失败：职业证据=%s；同职业候选数=%d。处理结果=两轮均失败，彻底失败。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString(),
                    (int)jobIndices.size());
                WriteMatchLog(fail);
                CString uiFail;
                uiFail.Format(L"[二轮职业失败][%s] 职业=%s，同职业候选=%d",
                    (LPCTSTR)roleText,
                    ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString(),
                    (int)jobIndices.size());
                PushVisualOnlyLog(uiFail, RGB(255, 210, 80));
                return false;
            }

            std::vector<int> usableJobIndices;
            for (int idx : jobIndices) {
                if (idx < 0 || idx >= (int)list.size()) continue;
                const auto& c = list[idx];
                if (blockedTeam != -1 && c.team == blockedTeam) {
                    CString skip;
                    skip.Format(L"[简化兜底][%s-%s] 跳过候选：主号=%s；小号=%s；ID分=%d；队伍=%d；阶段=第二轮职业匹配；原因=与另一方已锁定队伍%d冲突。",
                        (LPCTSTR)sideText, (LPCTSTR)roleText,
                        (LPCTSTR)c.owner, (LPCTSTR)c.alias,
                        c.idScore, c.team, blockedTeam);
                    WriteMatchLog(skip);
                    continue;
                }
                usableJobIndices.push_back(idx);
            }

            if (usableJobIndices.empty()) {
                CString fail;
                fail.Format(L"[简化兜底][%s-%s] 第二轮职业匹配失败：同职业候选数=%d，但全部被队伍约束跳过。处理结果=两轮均失败，彻底失败。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText, (int)jobIndices.size());
                WriteMatchLog(fail);
                CString uiFail;
                uiFail.Format(L"[二轮职业失败][%s] 同职业候选全部被队伍约束跳过", (LPCTSTR)roleText);
                PushVisualOnlyLog(uiFail, RGB(255, 210, 80));
                return false;
            }

            if (usableJobIndices.size() == 1) {
                int onlyIdx = usableJobIndices[0];
                CString summary;
                summary.Format(L"[简化兜底][%s-%s] 第二轮职业匹配：职业=%s；同职业可用候选数=1；处理=职业唯一直接锁定。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString());
                WriteMatchLog(summary);
                return acceptCandidate(onlyIdx, L"第二轮职业唯一匹配", L"#职业在上场8人可用候选中唯一，直接锁定");
            }

            CString jobTieList;
            int jobTieCount = 0;
            int jobBestIdScore = -1;
            CString jobBestDetail;
            CString jobAllScores;
            int jobSelectedIdx = chooseJobCandidateByIdScore(usableJobIndices, jobTieList, jobTieCount, jobBestIdScore, jobBestDetail, jobAllScores);
            if (jobSelectedIdx >= 0) {
                CString summary;
                summary.Format(L"[简化兜底][%s-%s] 第二轮职业匹配：职业=%s；同职业可用候选数=%d；最高ID分=%d；最高分同分候选数=%d；同分处理=固定选择遍历顺序第一个；并列=%s；同职业ID分=%s；选择说明=同职业多人按ID分选择：%s。",
                    (LPCTSTR)sideText, (LPCTSTR)roleText,
                    ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString(),
                    (int)usableJobIndices.size(), jobBestIdScore, jobTieCount,
                    jobTieList.IsEmpty() ? L"无" : jobTieList.GetString(),
                    jobAllScores.IsEmpty() ? L"无" : jobAllScores.GetString(),
                    jobBestDetail.IsEmpty() ? L"无" : jobBestDetail.GetString());
                WriteMatchLog(summary);
                return acceptCandidate(jobSelectedIdx, L"第二轮职业多人按ID分匹配", L"同职业多人，按第一轮ID分最高选择；ID分相同固定取遍历顺序第一个");
            }

            CString fail;
            fail.Format(L"[简化兜底][%s-%s] 第二轮职业匹配失败：职业=%s；同职业可用候选数=%d；最高ID分=%d；同职业ID分=%s；原因=同职业多人但ID分全为0，拒绝猜测。处理结果=两轮均失败，彻底失败。",
                (LPCTSTR)sideText, (LPCTSTR)roleText,
                ev.topJob.IsEmpty() ? L"无" : ev.topJob.GetString(),
                (int)usableJobIndices.size(), jobBestIdScore,
                jobAllScores.IsEmpty() ? L"无" : jobAllScores.GetString());
            WriteMatchLog(fail);
            CString uiFail;
            uiFail.Format(L"[二轮职业失败][%s] 同职业多人但ID分全为0", (LPCTSTR)roleText);
            PushVisualOnlyLog(uiFail, RGB(255, 210, 80));
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
            CString fusionBrief;
            fusionBrief.Format(L"%s/%s best=%s final=%d gap=%d cacheInsufficient=%s",
                (LPCTSTR)sideText, (LPCTSTR)tag,
                fusion.best.candidate.name.IsEmpty() ? L"无" : fusion.best.candidate.name.GetString(),
                fusion.best.finalScore, fusion.best.gapToSecond,
                fusion.cacheInsufficient ? L"是" : L"否");
            int fusionTopN = (int)(fusion.topScores.size() < 3 ? fusion.topScores.size() : 3);
            for (int ti = 0; ti < fusionTopN; ++ti) {
                const auto& ts = fusion.topScores[ti];
                CString one;
                one.Format(L" | Top%d:%s final=%d id=%d area=%+d job=%+d",
                    ti + 1,
                    ts.candidate.name.IsEmpty() ? L"无" : ts.candidate.name.GetString(),
                    ts.finalScore, ts.idScore, ts.areaCtxScore, ts.jobCtxScore);
                fusionBrief += one;
            }
            if (isKiller) killerFusionSummary = fusionBrief;
            else deadFusionSummary = fusionBrief;

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
        if (!m_bIsRunning)
            break;

        // 按需克隆单帧
        HBITMAP hSnapshot = CloneHistoryFrame(validSlots[i].ringIdx);
        if (!hSnapshot)
            continue;

        // 并行 OCR
        std::future<OcrResultData> futKiller, futDead;
        futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);

        OcrResultData resK = { L"", NULL };
        OcrResultData resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get();
        if (futDead.valid())   resD = futDead.get();

        // ★ 用完立即释放克隆帧，不累积内存
        ::DeleteObject(hSnapshot);

        // 记录有效文本用于二轮匹配
        if (!resK.text.IsEmpty() && resK.text.Find(L"No text") == -1)
            historyKTexts.push_back({ resK.text, (int)(i + 1) });
        if (!resD.text.IsEmpty() && resD.text.Find(L"No text") == -1)
            historyDTexts.push_back({ resD.text, (int)(i + 1) });

        // 首轮只收集候选，不在单帧内直接锁定。
        int kScore = -2, dScore = -2;
        processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, false, (int)(i + 1), true);
        processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, false, (int)(i + 1), true);

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
                globalKillerName.IsEmpty() ? L"未定" : globalKillerName.GetString(),
                globalDeadName.IsEmpty() ? L"未定" : globalDeadName.GetString());
        }
        ::InvalidateRect(m_hWnd, &m_previewRect, FALSE);
    }

    // ============================================================
    // 首轮 OCR 扫描结束后，统一采用首轮最高候选；未锁定再进入两轮兜底。
    // ============================================================
    WriteMatchLog(L"[首轮统一决策] 首轮OCR扫描结束：普通模糊匹配不再单帧锁定，开始按最高分/分差/短ID保护统一决策。");
    acceptCollectedFirstRound(true, killerResolved, finalKillerName, killerBestP, killerBestA, lockedKillerTeam);
    acceptCollectedFirstRound(false, deadResolved, finalDeadName, deadBestP, deadBestA, lockedDeadTeam);

    // ============================================================
    // 【简化兜底】首轮统一决策未锁定后，再按两轮规则补充判断。
    // ============================================================
    WriteMatchLog(L"[简化兜底] 首轮OCR结束：按两轮规则检查上场8人：先用非职业ID帧匹配，未锁定再用纯职业帧匹配 #职业。" );
    trySimpleMetaFallback(true, killerArea, historyKTexts, killerResolved, finalKillerName, killerBestP, killerBestA, lockedKillerTeam);
    trySimpleMetaFallback(false, deadArea, historyDTexts, deadResolved, finalDeadName, deadBestP, deadBestA, lockedDeadTeam);

    // ---- 二轮降级匹配（杀手） ----
    if (!killerResolved && !historyKTexts.empty()) {
        PushVisualLog(L"▶ [找杀手] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyKTexts) {
            int kScore = -2;
            processMatch(frame.text, killerResolved, finalKillerName, true,
                killerBestP, killerBestA, kScore, true, frame.frameIdx, false);
            if (killerResolved) break;
        }
    }

    // ---- 二轮降级匹配（死者） ----
    if (!deadResolved && !historyDTexts.empty()) {
        PushVisualLog(L"▶ [找死者] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyDTexts) {
            int dScore = -2;
            processMatch(frame.text, deadResolved, finalDeadName, false,
                deadBestP, deadBestA, dScore, true, frame.frameIdx, false);
            if (deadResolved) break;
        }
    }

    // 二轮后再尝试一次简化兜底。
    if (!killerResolved || !deadResolved) {
        WriteMatchLog(L"[简化兜底] 二轮OCR结束：再次按两轮规则检查上场8人：ID帧优先，职业帧只做第二轮兜底。" );
        trySimpleMetaFallback(true, killerArea, historyKTexts, killerResolved, finalKillerName, killerBestP, killerBestA, lockedKillerTeam);
        trySimpleMetaFallback(false, deadArea, historyDTexts, deadResolved, finalDeadName, deadBestP, deadBestA, lockedDeadTeam);
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

    if (killerResolved && deadResolved && lockedKillerTeam != -1 && lockedDeadTeam != -1
        && lockedKillerTeam == lockedDeadTeam) {
        CString sameTeamLog;
        sameTeamLog.Format(L"[队伍约束] 拒绝落账：杀手=%s(队伍%d)，死者=%s(队伍%d)，两者同队；请检查 OCR/小号绑定或手动加分。",
            (LPCTSTR)finalKillerName, lockedKillerTeam,
            (LPCTSTR)finalDeadName, lockedDeadTeam);
        WriteMatchLog(sameTeamLog);
        PushVisualLog(L"❌ [队伍约束] 杀手和死者识别到同一队，本次不自动加分", RGB(255, 120, 80));

        RecentEvent review;
        review.id = reviewEventId;
        review.killer = finalKillerName;
        review.dead = finalDeadName;
        review.time = GetTickCount();
        review.timeText = DnfFormatClockNow();
        review.triggerSide = triggerSide;
        review.killerIdx = killerBestP;
        review.deadIdx = deadBestP;
        review.killerTeam = lockedKillerTeam;
        review.deadTeam = lockedDeadTeam;
        review.statsApplied = false;
        review.status = L"队伍冲突";
        review.algorithmName = (m_nDeathAlgorithmChoice == DEATH_X_ALGO_PATCH) ? L"打补丁红蓝判断" : L"大X颜色个数判断";
        review.ocrSummary = L"杀手OCR: ";
        for (const auto& f : historyKTexts) {
            CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
            review.ocrSummary += one;
        }
        review.ocrSummary += L" 死者OCR: ";
        for (const auto& f : historyDTexts) {
            CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
            review.ocrSummary += one;
        }
        review.candidateSummary = sameTeamLog;
        AddReviewEvent(review);
        PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        return;
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
            if (!ev.statsApplied || ev.undone) continue;
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
            RecentEvent review;
            review.id = reviewEventId;
            review.killer = finalKillerName;
            review.dead = finalDeadName;
            review.time = now;
            review.timeText = DnfFormatClockNow();
            review.triggerSide = triggerSide;
            review.killerIdx = killerBestP;
            review.deadIdx = deadBestP;
            review.killerTeam = lockedKillerTeam;
            review.deadTeam = lockedDeadTeam;
            review.statsApplied = true;
            review.status = L"已计入";
            review.algorithmName = (m_nDeathAlgorithmChoice == DEATH_X_ALGO_PATCH) ? L"打补丁红蓝判断" : L"大X颜色个数判断";
            review.ocrSummary = L"杀手OCR: ";
            for (const auto& f : historyKTexts) {
                CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
                review.ocrSummary += one;
            }
            review.ocrSummary += L" 死者OCR: ";
            for (const auto& f : historyDTexts) {
                CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
                review.ocrSummary += one;
            }
            review.candidateSummary.Format(L"旧算法最优：杀手=%s %d分/线%d；死者=%s %d分/线%d。融合：%s || %s",
                globalKillerName.IsEmpty() ? L"无" : globalKillerName.GetString(), globalKillerBestScore, globalKillerPassLine,
                globalDeadName.IsEmpty() ? L"无" : globalDeadName.GetString(), globalDeadBestScore, globalDeadPassLine,
                killerFusionSummary.IsEmpty() ? L"无" : killerFusionSummary.GetString(),
                deadFusionSummary.IsEmpty() ? L"无" : deadFusionSummary.GetString());

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++)
                    if (p != killerBestP)
                        m_players[p].currentStreak = 0;

                m_players[killerBestP].kills++;
                m_players[killerBestP].currentStreak++;

                CString displayName = m_players[killerBestP].name;
                if (killerBestA != -1 && (size_t)killerBestA < m_players[killerBestP].aliases.size())
                    displayName = m_players[killerBestP].aliases[killerBestA].name;
                CString visualDisplayName = DnfAliasDisplayName(displayName, false);

                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                CString actionLog;
                actionLog.Format(L"⚔ [击杀成功] 玩家 [%s] 拿下一击！连杀: %d",
                    (LPCTSTR)visualDisplayName, m_players[killerBestP].currentStreak);
                PushVisualLog(actionLog, teamColor);

                if (m_players[killerBestP].currentStreak == 4) {
                    m_players[killerBestP].akCount++;
                    m_players[killerBestP].currentStreak = 0;
                    review.akDelta = 1;
                    PushVisualLog(L"🌟 [AK宣告] 恐怖如斯！玩家 [" + visualDisplayName + L"] 完成一次 AK！",
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
                if (m_lastKillerTeam == 0) review.redScoreDelta = 1;
                else if (m_lastKillerTeam == 1) review.blueScoreDelta = 1;
                for (int p = 0; p < 8; p++)
                    m_players[p].currentStreak = 0;

                // 大比分真正落账时也强制启动 ROUND_END 冷却。
                // 原来只在“全队 X 视觉检测”阶段启动；如果比分是在击杀结算线程里落账，
                // 或视觉全队 X 未稳定触发，就可能只走普通击杀冷却，导致 COOLDOWN_ROUND_END 没生效。
                KillTimer(2);
                m_bCanTrigger = FALSE;
                g_triggerCooldownKind = 2;
                SetTimer(2, COOLDOWN_ROUND_END, NULL);
                m_bCanTriggerTeamScore = FALSE;
                KillTimer(4);
                SetTimer(4, COOLDOWN_TEAM_SCORE, NULL);

                PushVisualLog(L"🏆 [结算] 局间大比分变动！所有人连击清零！ROUND_END 冷却已启动。", RGB(0, 255, 100));
                // 【身份融合补丁】新一局开始，两边固定框都可能换人，清空身份段和运行时学习。
                NotifyIdentityRoundReset(L"局间大比分变动/新一局开始");
            }

            AddReviewEvent(review);
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
        else {
            CString logMsg;
            logMsg.Format(L"⏳ [冷却拦截] 玩家 [%s] 在 %d 秒内已产生过战绩，本次忽略！", (LPCTSTR)conflictName, DUP_KILL_LIMIT_TIME / 1000);
            PushVisualLog(logMsg, RGB(255, 165, 0));

            RecentEvent review;
            review.id = reviewEventId;
            review.killer = finalKillerName;
            review.dead = finalDeadName;
            review.time = now;
            review.timeText = DnfFormatClockNow();
            review.triggerSide = triggerSide;
            review.killerIdx = killerBestP;
            review.deadIdx = deadBestP;
            review.killerTeam = lockedKillerTeam;
            review.deadTeam = lockedDeadTeam;
            review.statsApplied = false;
            review.status = L"冷却拦截";
            review.algorithmName = (m_nDeathAlgorithmChoice == DEATH_X_ALGO_PATCH) ? L"打补丁红蓝判断" : L"大X颜色个数判断";
            review.ocrSummary = L"杀手OCR: ";
            for (const auto& f : historyKTexts) {
                CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
                review.ocrSummary += one;
            }
            review.ocrSummary += L" 死者OCR: ";
            for (const auto& f : historyDTexts) {
                CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
                review.ocrSummary += one;
            }
            review.candidateSummary = conflictReason;
            AddReviewEvent(review);
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
    }
    else {
        RecentEvent review;
        review.id = reviewEventId;
        review.killer = L"待定";
        review.dead = L"待定";
        review.time = GetTickCount();
        review.timeText = DnfFormatClockNow();
        review.triggerSide = triggerSide;
        review.statsApplied = false;
        review.status = L"未判定";
        review.algorithmName = (m_nDeathAlgorithmChoice == DEATH_X_ALGO_PATCH) ? L"打补丁红蓝判断" : L"大X颜色个数判断";
        review.ocrSummary = L"杀手OCR: ";
        for (const auto& f : historyKTexts) {
            CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
            review.ocrSummary += one;
        }
        review.ocrSummary += L" 死者OCR: ";
        for (const auto& f : historyDTexts) {
            CString one; one.Format(L"第%d帧=%s; ", f.frameIdx, (LPCTSTR)f.text);
            review.ocrSummary += one;
        }
        review.candidateSummary.Format(L"旧算法最优：杀手=%s %d分/线%d；死者=%s %d分/线%d。融合：%s || %s",
            globalKillerName.IsEmpty() ? L"无" : globalKillerName.GetString(), globalKillerBestScore, globalKillerPassLine,
            globalDeadName.IsEmpty() ? L"无" : globalDeadName.GetString(), globalDeadBestScore, globalDeadPassLine,
            killerFusionSummary.IsEmpty() ? L"无" : killerFusionSummary.GetString(),
            deadFusionSummary.IsEmpty() ? L"无" : deadFusionSummary.GetString());
        AddReviewEvent(review);
        PostMessage(WM_UPDATE_ALL_UI, 0, 0);
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
    bool rawDeadArr[DEATH_POINT_COUNT] = { false };
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
    COLORREF debugHitColor[DEATH_POINT_COUNT][16] = { 0 };
    int debugColorSampleCount[DEATH_POINT_COUNT] = { 0 };
    float debugColorSampleX[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT] = { 0 };
    float debugColorSampleY[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT] = { 0 };
    bool debugColorSampleHit[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT] = { false };
    COLORREF debugColorSampleColor[DEATH_POINT_COUNT][DEATH_X_COLOR_SAMPLE_COUNT] = { 0 };
    int debugPatchPointCount[DEATH_POINT_COUNT] = { 0 };
    float debugPatchX[DEATH_POINT_COUNT][4] = { 0 };
    float debugPatchY[DEATH_POINT_COUNT][4] = { 0 };
    COLORREF debugPatchColor[DEATH_POINT_COUNT][4] = { 0 };
    int debugPatchClass[DEATH_POINT_COUNT][4] = { 0 };
    bool debugPatchPass[DEATH_POINT_COUNT] = { false };
    int currentDeathAlgorithm = m_nDeathAlgorithmChoice;

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
            (void)isActive;

            // 当前算法只认死亡 X 基准色：#D53000 = RGB(213, 48, 0)，三通道上下 40 容差。
            const int baseR = 0xD5;
            const int baseG = 0x30;
            const int baseB = 0x00;
            return abs(r - baseR) <= DEATH_X_COLOR_TOL &&
                   abs(g - baseG) <= DEATH_X_COLOR_TOL &&
                   abs(b - baseB) <= DEATH_X_COLOR_TOL;
        };

        // X 高光/前景色检测：真实 X 中心经常是白色/浅黄高光，而不是纯红色。
        // 中心门槛允许红/橙或高光；方向采样允许前景色，但最终至少要有 1 个红/橙方向证据。
        auto isXHighlightLike = [](COLORREF c, bool isActive) -> bool {
            (void)c;
            (void)isActive;
            return false; // 当前算法不再把白色/黄色高光当成死亡 X 命中。
        };

        // 距离动态容差：只给“斜边射线远端”使用，中心点仍然用严格 isXRedLike。
        // 目的：真实大 X 下半边越往外越暗，固定 #D53000 容差会漏；但不能全局放宽导致红底误判。
        auto isXRedLikeAtDistance = [&](COLORREF c, bool isActive, int stepIndex) -> bool {
            (void)stepIndex;
            return isXRedLike(c, isActive); // 不再按距离动态放宽，统一使用 #D53000 ±40。
        };

        auto isXForegroundLike = [&](COLORREF c, bool isActive) -> bool {
            return isXRedLike(c, isActive) || isXHighlightLike(c, isActive);
        };

        auto countLocalXRedColor = [&](int x, int y, int radius, bool isActive) -> int {
            int hits = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    if (isXRedLike(::GetPixel(hMemDC, sx, sy), isActive)) {
                        hits++;
                    }
                }
            }
            return hits;
        };

        auto hasLocalXRedColor = [&](int x, int y, int radius, bool isActive) -> bool {
            return countLocalXRedColor(x, y, radius, isActive) > 0;
        };

        struct XRayColorStat {
            int strongHits;
            int weakHits;
            COLORREF firstColor;
            bool hasColor;
        };

        auto countLocalXRedColorDynamic = [&](int x, int y, int radius, bool isActive, int stepIndex) -> XRayColorStat {
            XRayColorStat stat = { 0, 0, RGB(0, 0, 0), false };
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    COLORREF c = ::GetPixel(hMemDC, sx, sy);
                    if (isXRedLike(c, isActive)) {
                        stat.strongHits++;
                        if (!stat.hasColor) { stat.firstColor = c; stat.hasColor = true; }
                    }
                    else if (isXRedLikeAtDistance(c, isActive, stepIndex)) {
                        stat.weakHits++;
                        if (!stat.hasColor) { stat.firstColor = c; stat.hasColor = true; }
                    }
                }
            }
            return stat;
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

        struct PatchColorSample {
            int cls; // 0=无效，1=红，2=蓝
            COLORREF color;
            int x;
            int y;
        };

        auto patchColorDist = [](COLORREF c, int tr, int tg, int tb) -> int {
            int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
            return abs(r - tr) + abs(g - tg) + abs(b - tb);
        };

        auto classifyPatchColor = [&](COLORREF c) -> int {
            int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
            bool isRed = abs(r - 255) <= DEATH_X_PATCH_COLOR_TOL && g <= DEATH_X_PATCH_COLOR_TOL && b <= DEATH_X_PATCH_COLOR_TOL;
            bool isBlue = r <= DEATH_X_PATCH_COLOR_TOL && g <= DEATH_X_PATCH_COLOR_TOL && abs(b - 255) <= DEATH_X_PATCH_COLOR_TOL;
            if (isRed && isBlue) return patchColorDist(c, 255, 0, 0) <= patchColorDist(c, 0, 0, 255) ? 1 : 2;
            if (isRed) return 1;
            if (isBlue) return 2;
            return 0;
        };

        auto findPatchColorNear = [&](int x, int y, int radius) -> PatchColorSample {
            PatchColorSample best = { 0, RGB(0, 0, 0), x, y };
            if (x >= 0 && x < m_w && y >= 0 && y < m_h) best.color = ::GetPixel(hMemDC, x, y);
            int redHits = 0;
            int blueHits = 0;
            int redBestDist = 999999;
            int blueBestDist = 999999;
            COLORREF redBestColor = best.color;
            COLORREF blueBestColor = best.color;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (sx < 0 || sx >= m_w || sy < 0 || sy >= m_h) continue;
                    COLORREF c = ::GetPixel(hMemDC, sx, sy);
                    int cls = classifyPatchColor(c);
                    if (cls == 0) continue;
                    if (cls == 1) {
                        redHits++;
                        int dist = patchColorDist(c, 255, 0, 0);
                        if (dist < redBestDist) {
                            redBestDist = dist;
                            redBestColor = c;
                        }
                    }
                    else if (cls == 2) {
                        blueHits++;
                        int dist = patchColorDist(c, 0, 0, 255);
                        if (dist < blueBestDist) {
                            blueBestDist = dist;
                            blueBestColor = c;
                        }
                    }
                }
            }
            if (redHits > 0 || blueHits > 0) {
                if (redHits > blueHits || (redHits == blueHits && redBestDist <= blueBestDist)) {
                    best.cls = 1;
                    best.color = redBestColor;
                }
                else {
                    best.cls = 2;
                    best.color = blueBestColor;
                }
            }
            return best;
        };

        // 打补丁红蓝判断：恢复上下辅助点。
        // 点位顺序：0=上，1=下，2=左，3=右。
        // 判定条件：左右必须都接近纯红或纯蓝，并且一红一蓝；上/下至少一个点接近纯红或纯蓝。
        // 每个点只允许接近纯红或纯蓝，RGB 单通道容差 ±30。
        auto checkDeadPatchRaw = [&](int logicalIdx) -> bool {
            ScorePointF logicPt = GetDeathXPoint(logicalIdx);
            float cx = logicPt.x;
            float cy = logicPt.y;
            if (cx <= 0 || cy <= 0) return false;

            int centerX = (int)(cx * m_w);
            int centerY = (int)(cy * m_h);
            if (centerX < 0 || centerX >= m_w || centerY < 0 || centerY >= m_h) return false;

            float pxNorm[DEATH_X_PATCH_SAMPLE_COUNT] = {};
            float pyNorm[DEATH_X_PATCH_SAMPLE_COUNT] = {};
            BuildDeathXPatchSamples(logicalIdx, logicPt, pxNorm, pyNorm);

            int okCount = 0;
            for (int pidx = 0; pidx < DEATH_X_PATCH_SAMPLE_COUNT; ++pidx) {
                int sx = (int)(pxNorm[pidx] * m_w);
                int sy = (int)(pyNorm[pidx] * m_h);
                PatchColorSample sample = findPatchColorNear(sx, sy, DEATH_X_PATCH_SEARCH_RADIUS);
                debugPatchX[logicalIdx][pidx] = sx / (float)max(1, m_w);
                debugPatchY[logicalIdx][pidx] = sy / (float)max(1, m_h);
                debugPatchColor[logicalIdx][pidx] = sample.color;
                debugPatchClass[logicalIdx][pidx] = sample.cls;
                if (sample.cls != 0) okCount++;
            }
            debugPatchPointCount[logicalIdx] = 4;
            debugDrawX[logicalIdx] = cx;
            debugDrawY[logicalIdx] = cy;
            colorDeath[logicalIdx] = ::GetPixel(hMemDC, centerX, centerY);
            debugRoiHits[logicalIdx] = okCount;

            bool leftRightOpposite = debugPatchClass[logicalIdx][2] != 0 &&
                                     debugPatchClass[logicalIdx][3] != 0 &&
                                     debugPatchClass[logicalIdx][2] != debugPatchClass[logicalIdx][3];

            bool upDownHasColor = debugPatchClass[logicalIdx][0] != 0 ||
                                  debugPatchClass[logicalIdx][1] != 0;

            bool pass = leftRightOpposite && upDownHasColor;
            debugCenterGate[logicalIdx] = pass;
            debugPatchPass[logicalIdx] = pass;
            debugMatchCount[logicalIdx] = (leftRightOpposite ? 20 : 0) + (upDownHasColor ? 10 : 0) + okCount;
            return pass;
        };

        // 红色中心 + X 四方向结构探测器：
        // 仍然只使用每组第 0 个有效点作为中心；中心必须有红色系证据，四方向至少 3 个方向命中红色系。
        auto checkDeadRaw = [&](int logicalIdx) -> bool {
            if (currentDeathAlgorithm == DEATH_X_ALGO_PATCH) {
                return checkDeadPatchRaw(logicalIdx);
            }

            ScorePointF logicPt = GetDeathXPoint(logicalIdx);
            float cx = logicPt.x;
            float cy = logicPt.y;
            if (cx <= 0 || cy <= 0) return false;

            int centerX = (int)(cx * m_w);
            int centerY = (int)(cy * m_h);
            if (centerX < 0 || centerX >= m_w || centerY < 0 || centerY >= m_h)
                return false;

            const bool isActive = IsActiveDeathPoint(logicalIdx);
            const int localRadius = isActive ? 3 : 3;
            
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
            int dirRedCount[4] = { 0, 0, 0, 0 };
            bool dirFarHit[4] = { false, false, false, false };
            COLORREF centerColor = ::GetPixel(hMemDC, centerX, centerY);
            auto appendDebugHit = [&](float hx, float hy, int dirTag, COLORREF actualColor) {
                int idx = debugHitCount[logicalIdx];
                if (idx < 16) {
                    debugHitX[logicalIdx][idx] = hx;
                    debugHitY[logicalIdx][idx] = hy;
                    debugHitDir[logicalIdx][idx] = dirTag;
                    debugHitColor[logicalIdx][idx] = actualColor;
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
                debugColorSampleCount[logicalIdx] = BuildDeathXColorSamples(logicalIdx, logicPt, debugColorSampleX[logicalIdx], debugColorSampleY[logicalIdx]);
                return false;
            }
            matchCount++;

            // 动态步长：主将大 X 使用原步长，替补小 X 步长减半。
            float stepX = 0.0f;
            float stepY = 0.0f;
            GetDeathXColorStep(logicalIdx, stepX, stepY);

            // 沿 X 的两条斜边四个方向采样。
            // 最终判定不再看单方向/远端，只统计上边两条边和下边两条边的橙红命中采样点数量。
            int dirScore[4] = { 0, 0, 0, 0 };
            int dirStrongSamples[4] = { 0, 0, 0, 0 };
            int dirWeakSamples[4] = { 0, 0, 0, 0 };

            auto sampleDirectionPoint = [&](int sampleIdx, int dirIdx, int stepIndex, float nx, float ny) {
                int px = (int)(nx * m_w);
                int py = (int)(ny * m_h);
                if (px < 0 || px >= m_w || py < 0 || py >= m_h) return;

                XRayColorStat stat = countLocalXRedColorDynamic(px, py, localRadius, isActive, stepIndex);
                if (sampleIdx >= 0 && sampleIdx < DEATH_X_COLOR_SAMPLE_COUNT) {
                    debugColorSampleColor[logicalIdx][sampleIdx] = stat.hasColor ? stat.firstColor : ::GetPixel(hMemDC, px, py);
                    debugColorSampleHit[logicalIdx][sampleIdx] = (stat.strongHits > 0 || stat.weakHits > 0);
                }
                if (stat.strongHits <= 0 && stat.weakHits <= 0) return;

                // 一个采样点最多给本方向一次分：严格红 +2，动态弱红 +1。
                if (stat.strongHits > 0) {
                    dirScore[dirIdx] += 2;
                    dirStrongSamples[dirIdx]++;
                }
                else {
                    dirScore[dirIdx] += 1;
                    dirWeakSamples[dirIdx]++;
                }

                dirRedCount[dirIdx] += stat.strongHits + stat.weakHits;
                if (stepIndex >= 3) dirFarHit[dirIdx] = true;
                if (dirIdx == 0 || dirIdx == 1) diagDownHits++;
                else diagUpHits++;
                matchCount++;
                appendDebugHit(nx, ny, dirIdx, stat.hasColor ? stat.firstColor : ::GetPixel(hMemDC, px, py));
            };

            debugColorSampleCount[logicalIdx] = BuildDeathXColorSamples(logicalIdx, logicPt, debugColorSampleX[logicalIdx], debugColorSampleY[logicalIdx]);
            for (int i = 1; i <= 4; i++) {
                int baseSample = (i - 1) * 4;
                // \ 方向：左上、右下
                sampleDirectionPoint(baseSample + 0, 0, i, cx - i * stepX, cy - i * stepY);
                sampleDirectionPoint(baseSample + 1, 1, i, cx + i * stepX, cy + i * stepY);

                // / 方向：右上、左下
                sampleDirectionPoint(baseSample + 2, 2, i, cx + i * stepX, cy - i * stepY);
                sampleDirectionPoint(baseSample + 3, 3, i, cx - i * stepX, cy + i * stepY);
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

            // 新判定：不再按“单方向通过/远端证据”判断。
            // 用户实测大 X 只有黑色和橙红色，重点是橙红采样点数量：
            //   1) 中心点必须通过严格红色检测；
            //   2) 上边两条边（左上 + 右上）命中采样点总数 >= 5；
            //   3) 下边两条边（右下 + 左下）命中采样点总数 >= 2。
            // 这里的“命中采样点”按射线上的采样点计数，不再要求远端命中。
            int upperHitSamples = dirStrongSamples[0] + dirWeakSamples[0]
                                + dirStrongSamples[2] + dirWeakSamples[2];
            int lowerHitSamples = dirStrongSamples[1] + dirWeakSamples[1]
                                + dirStrongSamples[3] + dirWeakSamples[3];

            // 下面这些布尔值只用于调试绘制/状态观察，不再作为最终通过条件。
            hitLeftUp = redLeftUp = (dirStrongSamples[0] + dirWeakSamples[0]) > 0;
            hitRightDown = redRightDown = (dirStrongSamples[1] + dirWeakSamples[1]) > 0;
            hitRightUp = redRightUp = (dirStrongSamples[2] + dirWeakSamples[2]) > 0;
            hitLeftDown = redLeftDown = (dirStrongSamples[3] + dirWeakSamples[3]) > 0;

            bool upperPass = upperHitSamples >= 5;
            bool lowerPass = lowerHitSamples >= 2;

            // 调试编码：十位/百位为上边命中数，个位为下边命中数，方便看日志/断点。
            debugMatchCount[logicalIdx] = upperHitSamples * 10 + lowerHitSamples;
            return upperPass && lowerPass;
        };

        // 8 个逻辑状态，使用旧 40 点中每组第 0 个有效点：
        // 0-3 左侧，4-7 右侧。
        for (int logicalIdx = 0; logicalIdx < DEATH_POINT_COUNT; logicalIdx++) {
            rawDeadArr[logicalIdx] = checkDeadRaw(logicalIdx);
        }

        for (int logicalIdx = 0; logicalIdx < DEATH_POINT_COUNT; logicalIdx++) {
            if (rawDeadArr[logicalIdx]) {
                if (m_deathXStableOn[logicalIdx] < DEATH_X_STABLE_ON_FRAMES) {
                    m_deathXStableOn[logicalIdx]++;
                }
                m_deathXStableOff[logicalIdx] = 0;
            }
            else {
                if (m_deathXStableOff[logicalIdx] < DEATH_X_STABLE_OFF_FRAMES) {
                    m_deathXStableOff[logicalIdx]++;
                }
                m_deathXStableOn[logicalIdx] = 0;
            }

            if (!m_deathXStableState[logicalIdx] && m_deathXStableOn[logicalIdx] >= DEATH_X_STABLE_ON_FRAMES) {
                m_deathXStableState[logicalIdx] = true;
            }
            else if (m_deathXStableState[logicalIdx] && m_deathXStableOff[logicalIdx] >= DEATH_X_STABLE_OFF_FRAMES) {
                m_deathXStableState[logicalIdx] = false;
            }

            isDeadArr[logicalIdx] = m_deathXStableState[logicalIdx];
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
                    g_deathXDebug.hitColor[i][k] = debugHitColor[i][k];
                }
                g_deathXDebug.colorSampleCount[i] = debugColorSampleCount[i];
                for (int k = 0; k < DEATH_X_COLOR_SAMPLE_COUNT; k++) {
                    g_deathXDebug.colorSampleX[i][k] = debugColorSampleX[i][k];
                    g_deathXDebug.colorSampleY[i][k] = debugColorSampleY[i][k];
                    g_deathXDebug.colorSampleHit[i][k] = debugColorSampleHit[i][k];
                    g_deathXDebug.colorSampleColor[i][k] = debugColorSampleColor[i][k];
                }
                g_deathXDebug.patchPointCount[i] = debugPatchPointCount[i];
                g_deathXDebug.patchPass[i] = debugPatchPass[i];
                for (int k = 0; k < 4; k++) {
                    g_deathXDebug.patchX[i][k] = debugPatchX[i][k];
                    g_deathXDebug.patchY[i][k] = debugPatchY[i][k];
                    g_deathXDebug.patchColor[i][k] = debugPatchColor[i][k];
                    g_deathXDebug.patchClass[i][k] = debugPatchClass[i][k];
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
    DWORD nowTick = GetTickCount();

    // 普通击杀冷却期间出现的新 X，不能直接吃掉边沿；
    // 记录为“待触发 X”，等普通击杀冷却结束后，如果 X 仍存在，再补触发一次。
    // 注意：局间 ROUND_END 35 秒冷却不补触发，避免本局结束残留 X 被重复计算。
    const DWORD PENDING_ACTIVE_X_KEEP_MS = COOLDOWN_KILL_TRIGGER + 5000;

    auto clearPendingActiveX = [&](const CString& reason) {
        if (g_pendingActiveDeathSide >= 0) {
            CString line;
            line.Format(L"[X待触发] 清除待触发X：原物理侧=%s；原因=%s。",
                g_pendingActiveDeathSide == 0 ? L"左边" : L"右边", reason.GetString());
            WriteMatchLog(line);
        }
        g_pendingActiveDeathSide = -1;
        g_pendingActiveDeathTick = 0;
        g_pendingActiveDeathReason.Empty();
    };

    auto rememberPendingActiveX = [&](int side, const CString& reason) {
        // 只有普通击杀冷却允许 pending；局间/大比分冷却不补触发。
        if (g_triggerCooldownKind != 1) {
            CString line;
            line.Format(L"[X待触发] 不记录待触发X：物理侧=%s；当前冷却类型=%s；原因=不是普通击杀冷却，不补触发，避免局间残留X重复结算。",
                side == 0 ? L"左边" : L"右边",
                g_triggerCooldownKind == 2 ? L"局间/大比分冷却" : L"未知/未设置冷却");
            WriteMatchLog(line);
            return;
        }
        if (g_pendingActiveDeathSide == side) return;
        g_pendingActiveDeathSide = side;
        g_pendingActiveDeathTick = nowTick;
        g_pendingActiveDeathReason = reason;
        CString line;
        line.Format(L"[X待触发] 已记录普通冷却期间出现的新X：物理死亡侧=%s；记录原因=%s；保留时间=%lu毫秒；说明=冷却结束后若X仍存在，将补进OCR匹配。",
            side == 0 ? L"左边" : L"右边", reason.GetString(), PENDING_ACTIVE_X_KEEP_MS);
        WriteMatchLog(line);
    };

    auto fireActiveXTrigger = [&](int deadSide, const CString& sourceReason) {
        CString line;
        line.Format(L"[X触发诊断] %s侧大X进入OCR匹配：来源=%s；当前是否翻转红蓝=%s；物理死亡侧=%s；物理杀手侧=%s；说明=翻转红蓝只影响界面/OBS左右显示，不改变OCR区域、X检测位置和物理侧候选队伍。",
            deadSide == 0 ? L"左" : L"右", sourceReason.GetString(), m_bFlipSides ? L"是" : L"否",
            deadSide == 0 ? L"左边" : L"右边", deadSide == 0 ? L"右边" : L"左边");
        WriteMatchLog(line);
        m_bCanTrigger = FALSE;
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, deadSide).detach();
        g_triggerCooldownKind = 1;
        SetTimer(2, COOLDOWN_KILL_TRIGGER, NULL);
    };

    // 如果普通冷却期间吞过一次 X 边沿，冷却结束后在这里补触发。
    if (g_pendingActiveDeathSide >= 0) {
        bool pendingStillDead = (g_pendingActiveDeathSide == 0) ? leftActiveDead : rightActiveDead;
        DWORD pendingAge = nowTick - g_pendingActiveDeathTick;
        if (!pendingStillDead) {
            clearPendingActiveX(L"对应大X已经消失，说明不需要补触发");
        }
        else if (pendingAge > PENDING_ACTIVE_X_KEEP_MS) {
            clearPendingActiveX(L"待触发X超过保留时间，避免录像暂停/残留X重复结算");
        }
        else if (m_bCanTrigger) {
            int side = g_pendingActiveDeathSide;
            CString oldReason = g_pendingActiveDeathReason;
            clearPendingActiveX(L"普通冷却结束且X仍存在，准备补触发");
            CString fireReason = L"普通冷却期间待触发X补触发；原记录原因=";
            fireReason += oldReason;
            fireActiveXTrigger(side, fireReason);
        }
    }

    // ========================================================
    // 日志输出：只提示主将 X 已识别但未触发，避免刷屏
    // ========================================================
    if (nowTick - g_lastDeathXBlockedLogTick > 1000)
    {
        if (leftActiveDead && (!m_bCanTrigger || g_leftActiveWasDead))
        {
            CString reason = !m_bCanTrigger ? L"防抖冷却中" : L"状态已记录(未重置)";
            AppLog(L"🟡 左侧大X已识别但未触发匹配 (原因:" + reason + L")", RGB(255, 180, 0));
            CString diag;
            diag.Format(L"[X触发诊断] 大X已经识别但没有进入OCR匹配：当前是否翻转红蓝=%s；物理侧=左边；是否允许触发击杀=%s；未触发原因=%s；左侧X上一状态=%s；右侧X上一状态=%s；待触发X=%s。",
                m_bFlipSides ? L"是" : L"否", m_bCanTrigger ? L"是" : L"否", (LPCTSTR)reason,
                g_leftActiveWasDead ? L"已记录为死亡" : L"未记录为死亡",
                g_rightActiveWasDead ? L"已记录为死亡" : L"未记录为死亡",
                g_pendingActiveDeathSide == 0 ? L"左侧已记录" : (g_pendingActiveDeathSide == 1 ? L"右侧已记录" : L"无"));
            WriteMatchLog(diag);
            g_lastDeathXBlockedLogTick = nowTick;
        }

        if (rightActiveDead && (!m_bCanTrigger || g_rightActiveWasDead))
        {
            CString reason = !m_bCanTrigger ? L"防抖冷却中" : L"状态已记录(未重置)";
            AppLog(L"🟡 右侧大X已识别但未触发匹配 (原因:" + reason + L")", RGB(255, 180, 0));
            CString diag;
            diag.Format(L"[X触发诊断] 大X已经识别但没有进入OCR匹配：当前是否翻转红蓝=%s；物理侧=右边；是否允许触发击杀=%s；未触发原因=%s；左侧X上一状态=%s；右侧X上一状态=%s；待触发X=%s。",
                m_bFlipSides ? L"是" : L"否", m_bCanTrigger ? L"是" : L"否", (LPCTSTR)reason,
                g_leftActiveWasDead ? L"已记录为死亡" : L"未记录为死亡",
                g_rightActiveWasDead ? L"已记录为死亡" : L"未记录为死亡",
                g_pendingActiveDeathSide == 0 ? L"左侧已记录" : (g_pendingActiveDeathSide == 1 ? L"右侧已记录" : L"无"));
            WriteMatchLog(diag);
            g_lastDeathXBlockedLogTick = nowTick;
        }
    }

    // 🎯 左边正在打的死了 -> 右边赢了这一小局！(传入 0 代表左边被击杀)
    if (leftActiveDead && !g_leftActiveWasDead) {
        g_leftActiveWasDead = true;
        if (m_bCanTrigger) {
            fireActiveXTrigger(0, L"左侧大X产生新的死亡边沿，允许触发");
        }
        else {
            rememberPendingActiveX(0, L"左侧大X产生新的死亡边沿，但当时正在防抖冷却");
        }
    }
    else if (!leftActiveDead && g_leftActiveWasDead) {
        g_leftActiveWasDead = false;
        if (g_pendingActiveDeathSide == 0) clearPendingActiveX(L"左侧大X消失，边沿状态复位");
    }

    // 🎯 右边正在打的死了 -> 左边赢了这一小局！(传入 1 代表右边被击杀)
    if (rightActiveDead && !g_rightActiveWasDead) {
        g_rightActiveWasDead = true;
        if (m_bCanTrigger) {
            fireActiveXTrigger(1, L"右侧大X产生新的死亡边沿，允许触发");
        }
        else {
            rememberPendingActiveX(1, L"右侧大X产生新的死亡边沿，但当时正在防抖冷却");
        }
    }
    else if (!rightActiveDead && g_rightActiveWasDead) {
        g_rightActiveWasDead = false;
        if (g_pendingActiveDeathSide == 1) clearPendingActiveX(L"右侧大X消失，边沿状态复位");
    }

    // ========================================================
    // 5. 大比分检测
    // ========================================================
    if ((leftTeamDead || rightTeamDead) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;

        KillTimer(2);
        clearPendingActiveX(L"进入局间大比分35秒冷却，残留X不补触发");
        m_bCanTrigger = FALSE;
        g_triggerCooldownKind = 2;
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
    m_bOcrStartPending = false;
    m_ocrStartRequestId.fetch_add(1);
    m_bOcrHealthCheckPending = false;
    m_bOcrRecoveryPending = false;
    m_ocrRecoveryRequestId.fetch_add(1);
    KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4);
    ResetDeathXStableState();

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

    if (!m_bIsRunning) {
        if (m_bOcrStartPending.load()) {
            BroadcastStateToWeb();
            return;
        }

        m_bOcrHealthCheckPending = false;
        m_bOcrRecoveryPending = false;
        m_ocrRecoveryRequestId.fetch_add(1);

        CString missingAliasPlayers;
        CString shortAliasPlayers;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex);
            for (int i = 0; i < 8; ++i) {
                if (m_players[i].name.IsEmpty()) continue;
                if (m_players[i].aliases.empty()) {
                    if (!missingAliasPlayers.IsEmpty()) missingAliasPlayers += L"、";
                    missingAliasPlayers += m_players[i].name;
                }
                for (const auto& a : m_players[i].aliases) {
                    if (DnfIsLegacyShortAliasWithoutMeta(a.name)) {
                        if (!shortAliasPlayers.IsEmpty()) shortAliasPlayers += L"；";
                        shortAliasPlayers += L"【" + m_players[i].name + L"】" + a.name;
                    }
                }
            }
        }
        if (!missingAliasPlayers.IsEmpty() || !shortAliasPlayers.IsEmpty()) {
            CString msg = L"检测到上场选手信息不完整，暂时不能开始监控：";
            if (!missingAliasPlayers.IsEmpty()) {
                msg += L"\r\n\r\n没有小号：" + missingAliasPlayers + L"。主号不参与OCR名称匹配，请至少添加一个小号。";
            }
            if (!shortAliasPlayers.IsEmpty()) {
                msg += L"\r\n\r\n未加大区/#职业的2字短ID：" + shortAliasPlayers + L"。允许保留在列表中，但开始监控前请补充大区或 #职业。";
            }
            CString blockLog = L"[开始监控拦截] ";
            blockLog += msg;
            WriteMatchLog(blockLog);
            if (!IsWindowVisible() && m_pWebDlg) {
                json reply; reply["action"] = "start_guard"; reply["success"] = false;
                reply["message"] = std::string(CW2A(msg, CP_UTF8));
                CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
                m_pWebDlg->SendStateToWeb(jsonStr);
            }
            else {
                ShowCenteredMsgBox(msg, L"无法开始监控", MB_ICONWARNING);
            }
            BroadcastStateToWeb();
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

        if (m_nDeathAlgorithmChoice == DEATH_X_ALGO_PATCH && !EnsureDeathPatchInstalled()) {
            BroadcastStateToWeb();
            return;
        }

        if (ProbeOcrServiceReady()) {
            StartMonitoringAfterOcrReady();
        }
        else {
            BeginOcrServiceBootstrap();
        }
        return;
    }

    else {
        m_bIsRunning = FALSE;
        KillTimer(1);
        KillTimer(3);
        ResetDeathXStableState();
        m_bOcrRecoveryPending = false;
        m_ocrRecoveryRequestId.fetch_add(1);

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

LRESULT CDNFGameCaptureDlg::OnOcrServiceFail(WPARAM wParam, LPARAM lParam) {
    static DWORD s_lastOcrFailPrompt = 0;
    DWORD now = GetTickCount();
    if (now - s_lastOcrFailPrompt < 15000) return 0;
    s_lastOcrFailPrompt = now;

    if (m_bIsRunning) {
        m_bIsRunning = FALSE;
        KillTimer(1);
        KillTimer(3);
        m_btnStart.SetWindowText(L"开始监控");
        m_status.SetWindowText(L"OCR已停止");
    }
    m_bOcrHealthCheckPending = false;
    m_bOcrRecoveryPending = false;

    CString msg = L"❌ Umi-OCR 已关闭或服务无响应。\r\n\r\n软件已经尝试自动恢复，但 OCR 服务仍不可用，所以已自动停止监控。\r\n\r\n请手动打开软件同目录下的 Umi-OCR.exe，确认 OCR 服务启动后，再重新开始监控。";
    AppLog(L"❌ [Umi-OCR] 服务离线且自动恢复失败，已停止监控，避免继续产生 OCR 空结果。", RGB(255, 80, 80));
    WriteMatchLog(L"[Umi-OCR] 服务离线且自动恢复失败，已停止监控。" );

    if (!IsWindowVisible() && m_pWebDlg) {
        json reply; reply["action"] = "start_guard"; reply["success"] = false;
        reply["message"] = std::string(CW2A(msg, CP_UTF8));
        CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
        m_pWebDlg->SendStateToWeb(jsonStr);
    }
    else {
        ShowCenteredMsgBox(msg, L"Umi-OCR 服务异常", MB_ICONWARNING);
    }

    BroadcastStateToWeb();
    return 0;
}

void CDNFGameCaptureDlg::OnBnClickedInputKey() {
    if (m_bIsManualAuthCheck && m_cloudExpireTime == -1) {
        MessageBox(L"上一条授权卡密正在云端验证中，请稍后再试。", L"授权验证中", MB_ICONINFORMATION);
        return;
    }

    CString currentText;
    m_btnInputKey.GetWindowText(currentText);

    // ==========================================
    // 阶段一：点击“输入授权码”，打开记事本，并将按钮变身
    // ==========================================
    if (currentText == L"输入授权码") {
        s_backupAuthCode = DnfReadLocalLicenseKey();
        CString path = DnfGetLicenseFilePath();

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
        // 1. 读取用户刚保存的 license.txt，但先恢复旧授权，避免无效输入污染正式存储。
        CString fileKey = DnfReadLicenseFromFile();
        DnfWriteLocalLicenseKey(s_backupAuthCode);
        s_pendingAuthCode = DnfNormalizeLicenseKey(fileKey);

        if (s_pendingAuthCode.IsEmpty() || !BeginLicenseCloudCheck(s_pendingAuthCode, true)) {
            s_pendingAuthCode.Empty();
            s_backupAuthCode.Empty();

            if (m_editVisualLogs.m_hWnd) {
                m_editVisualLogs.SetWindowText(L"");
            }
            OutputDebugAuthInfo();

            m_status.SetWindowText(L"授权码格式无效，已保留旧授权");
            m_btnInputKey.SetWindowText(L"输入授权码");
            MessageBox(L"授权卡密格式无效，已丢弃本次输入并保留旧授权。", L"授权失败", MB_ICONWARNING);
            BroadcastStateToWeb();
            return;
        }

        // 2. 重新读取卡密并执行静默检查
        // 新卡密已作为候选值提交云端，验证成功后才会写入 license.txt 和注册表。

        // 3. 清空旧面板，强制打印最新的状态
        if (m_editVisualLogs.m_hWnd) {
            m_editVisualLogs.SetWindowText(L"");
        }
        OutputDebugAuthInfo();

        m_status.SetWindowText(L"授权码校验已触发");

        // 4. 【关键】：将按钮文字还原，完成闭环
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
    CString flipLog;
    flipLog.Format(L"[红蓝翻转] 用户点击翻转红蓝：翻转后状态=%s；说明=只影响软件界面、网页和OBS输出左右显示；不改变游戏物理左/右框、简化兜底候选队伍、身份缓存、OCR区域和X检测位置。",
        m_bFlipSides ? L"开启" : L"关闭");
    WriteMatchLog(flipLog);
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
        m_recentEvents.clear();
        ResetMatchCooldownState(L"手动战绩归零");
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

void CDNFGameCaptureDlg::ResetFrameHistory()
{
    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) {
        if (m_historyBmps[i]) {
            ::DeleteObject(m_historyBmps[i]);
            m_historyBmps[i] = nullptr;
        }
    }
    m_historyIdx = 0;
}

bool CDNFGameCaptureDlg::TryAutoCropBlackBars(HBITMAP& hBmp, int& w, int& h, const wchar_t* sourceTag)
{
    if (!hBmp || w <= 0 || h <= 0) return false;
    if (!m_chkAutoCropBlackBars.m_hWnd || m_chkAutoCropBlackBars.GetCheck() != BST_CHECKED) return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels((size_t)w * h * 4);
    HDC hdc = ::GetDC(NULL);
    if (!hdc) return false;
    int got = ::GetDIBits(hdc, hBmp, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
    if (got != h) {
        ::ReleaseDC(NULL, hdc);
        return false;
    }

    int top = 0;
    while (top < h && DnfIsBlackBarRow(pixels, w, h, top)) ++top;

    int bottom = h - 1;
    while (bottom >= top && DnfIsBlackBarRow(pixels, w, h, bottom)) --bottom;

    int left = 0;
    while (left < w && DnfIsBlackBarColumn(pixels, w, h, left)) ++left;

    int right = w - 1;
    while (right >= left && DnfIsBlackBarColumn(pixels, w, h, right)) --right;

    int cropTop = max(0, top);
    int cropBottom = max(0, h - 1 - bottom);
    int cropLeft = max(0, left);
    int cropRight = max(0, w - 1 - right);

    if (cropTop < DNF_BLACK_BAR_MIN_EDGE) cropTop = 0;
    if (cropBottom < DNF_BLACK_BAR_MIN_EDGE) cropBottom = 0;
    if (cropLeft < DNF_BLACK_BAR_MIN_EDGE) cropLeft = 0;
    if (cropRight < DNF_BLACK_BAR_MIN_EDGE) cropRight = 0;

    if (cropTop == 0 && cropBottom == 0 && cropLeft == 0 && cropRight == 0) {
        ::ReleaseDC(NULL, hdc);
        return false;
    }

    int newW = w - cropLeft - cropRight;
    int newH = h - cropTop - cropBottom;
    if (newW < 320 || newH < 180 || newW < (int)(w * 0.50) || newH < (int)(h * 0.50)) {
        ::ReleaseDC(NULL, hdc);
        return false;
    }

    HBITMAP hCropped = ::CreateCompatibleBitmap(hdc, newW, newH);
    if (!hCropped) {
        ::ReleaseDC(NULL, hdc);
        return false;
    }

    HDC hSrcDC = ::CreateCompatibleDC(hdc);
    HDC hDstDC = ::CreateCompatibleDC(hdc);
    if (!hSrcDC || !hDstDC) {
        if (hSrcDC) ::DeleteDC(hSrcDC);
        if (hDstDC) ::DeleteDC(hDstDC);
        ::DeleteObject(hCropped);
        ::ReleaseDC(NULL, hdc);
        return false;
    }

    HGDIOBJ oldSrc = ::SelectObject(hSrcDC, hBmp);
    HGDIOBJ oldDst = ::SelectObject(hDstDC, hCropped);
    BOOL ok = ::BitBlt(hDstDC, 0, 0, newW, newH, hSrcDC, cropLeft, cropTop, SRCCOPY);
    ::SelectObject(hSrcDC, oldSrc);
    ::SelectObject(hDstDC, oldDst);
    ::DeleteDC(hSrcDC);
    ::DeleteDC(hDstDC);
    ::ReleaseDC(NULL, hdc);

    if (!ok) {
        ::DeleteObject(hCropped);
        return false;
    }

    CString logKey;
    logKey.Format(L"%s|%dx%d|%d,%d,%d,%d|%dx%d",
        sourceTag ? sourceTag : L"未知", w, h, cropLeft, cropTop, cropRight, cropBottom, newW, newH);
    static CString s_lastAutoCropLogKey;
    static DWORD s_lastAutoCropLogTick = 0;
    DWORD nowTick = ::GetTickCount();
    if (logKey != s_lastAutoCropLogKey || nowTick - s_lastAutoCropLogTick >= 10000) {
        CString logLine;
        logLine.Format(L"[自动裁黑边] 来源=%s；原始=%dx%d；裁剪=左%d 上%d 右%d 下%d；结果=%dx%d。",
            sourceTag ? sourceTag : L"未知", w, h, cropLeft, cropTop, cropRight, cropBottom, newW, newH);
        WriteMatchLog(logLine);
        s_lastAutoCropLogKey = logKey;
        s_lastAutoCropLogTick = nowTick;
    }

    ::DeleteObject(hBmp);
    hBmp = hCropped;
    w = newW;
    h = newH;
    return true;
}

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
    g_triggerCooldownKind = 1;
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
            if (!hGame && m_cachedGameHwnd && ::IsWindow(m_cachedGameHwnd)) {
                hGame = m_cachedGameHwnd;
            }
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

        m_cachedGameHwnd = hGame;

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

                TryAutoCropBlackBars(hFrame, w, h, L"WGC");

                std::lock_guard<std::mutex> lock(g_bmpMutex);
                if (w != m_w || h != m_h) {
                    ResetFrameHistory();
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
                    HDC hGameDC = ::GetDC(hGame);
                    HBITMAP hFrame = ::CreateCompatibleBitmap(hGameDC, newW, newH);
                    if (!hFrame) {
                        ::ReleaseDC(hGame, hGameDC);
                        return;
                    }

                    HDC hMemDC = ::CreateCompatibleDC(hGameDC);
                    HGDIOBJ oldBmp = ::SelectObject(hMemDC, hFrame);

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

                    int frameW = newW;
                    int frameH = newH;
                    TryAutoCropBlackBars(hFrame, frameW, frameH, L"PrintWindow");

                    if (frameW != m_w || frameH != m_h) {
                        ResetFrameHistory();
                    }
                    if (m_bmp) { ::DeleteObject(m_bmp); m_bmp = nullptr; }
                    m_bmp = hFrame;
                    m_w = frameW;
                    m_h = frameH;
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
    UpdateDeathXCalibrationButtons();
    InvalidateRect(&topHalf, FALSE);
}



bool CDNFGameCaptureDlg::EnsureOcrRunning(bool forceRestart) {
    std::lock_guard<std::mutex> lk(m_launchMutex);

    auto ensureHttpHandles = [&]() -> bool {
        if (!m_hHttpSession) {
            m_hHttpSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (m_hHttpSession) WinHttpSetTimeouts(m_hHttpSession, 800, 800, 800, 800);
        }
        if (!m_hHttpSession) return false;
        if (!m_hHttpConnect) {
            m_hHttpConnect = WinHttpConnect(m_hHttpSession, L"127.0.0.1", 1224, 0);
        }
        return m_hHttpConnect != NULL;
    };

    auto probeOcr = [&]() -> bool {
        if (!ensureHttpHandles()) return false;
        HINTERNET hProbe = WinHttpOpenRequest(
            m_hHttpConnect, L"GET", L"/",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hProbe) return false;
        WinHttpSetTimeouts(hProbe, 800, 800, 800, 800);
        BOOL ok = WinHttpSendRequest(hProbe, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hProbe, NULL);
        WinHttpCloseHandle(hProbe);
        return ok == TRUE;
    };

    // 先探测端口：Umi-OCR 已经在运行时，不重复启动。
    bool processRunning = DnfIsProcessRunningByName(L"Umi-OCR.exe");
    if (!forceRestart && processRunning && probeOcr()) {
        RefreshOcrExePathFromRunningProcess(true);
        return true;
    }

    if (GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) {
        RefreshOcrExePathFromRunningProcess(true);
    }

    if (GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) {
        CString msg;
        msg.Format(L"❌ [Umi-OCR] 未找到 OCR 程序：%s", (LPCTSTR)m_ocrExePath);
        AppLog(msg, RGB(255, 80, 80));
        WriteMatchLog(msg);
        return false;
    }

    DWORD now = GetTickCount();
    if (forceRestart || !processRunning || now - m_lastLaunchOcrTime >= 10000) {
        m_lastLaunchOcrTime = now;
        SHELLEXECUTEINFO sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = m_ocrExePath;
        sei.nShow = SW_SHOWMINNOACTIVE;
        if (!ShellExecuteEx(&sei)) {
            CString msg;
            msg.Format(L"❌ [Umi-OCR] 自动启动失败：%s", (LPCTSTR)m_ocrExePath);
            AppLog(msg, RGB(255, 80, 80));
            WriteMatchLog(msg);
            return false;
        }
        AppLog(L"🔄 [Umi-OCR] 未检测到 OCR 服务，已尝试自动启动 Umi-OCR...", RGB(255, 200, 0));
    }

    // 等待 Umi-OCR 拉起端口。这里最多等 6 秒，避免继续监控时 OCR 永远为空。
    for (int i = 0; i < 12; ++i) {
        Sleep(500);
        if (probeOcr()) {
            AppLog(L"✅ [Umi-OCR] OCR 服务已恢复，可以继续监控。", RGB(0, 255, 100));
            return true;
        }
    }

    AppLog(L"❌ [Umi-OCR] 自动恢复失败，OCR 服务未响应。", RGB(255, 80, 80));
    WriteMatchLog(L"[Umi-OCR] 自动恢复失败：127.0.0.1:1224 未响应，等待上层处理或下次后台重试。");
    return false;
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

static bool DnfIsLegacySeatToken(CString token)
{
    token.Trim();
    return token.Left(5).CompareNoCase(L"SEAT=") == 0;
}

CString CDNFGameCaptureDlg::GetPickSeatLabelForIndex(int index) const
{
    static const wchar_t* redFirstRed[] = { L"x选", L"1选", L"4选", L"6选" };
    static const wchar_t* redFirstBlue[] = { L"h选", L"2选", L"3选", L"5选" };
    static const wchar_t* redSecondRed[] = { L"h选", L"2选", L"4选", L"5选" };
    static const wchar_t* redSecondBlue[] = { L"x选", L"1选", L"3选", L"6选" };
    if (index < 0 || index >= 8) return L"";
    const int row = index % 4;
    if (index < 4) return m_bRedPickFirst ? redFirstRed[row] : redSecondRed[row];
    return m_bRedPickFirst ? redFirstBlue[row] : redSecondBlue[row];
}

void CDNFGameCaptureDlg::WriteScoreToFile() {
    struct ScoreFileRow {
        PlayerData player;
        CString pickLabel;
    };

    std::vector<ScoreFileRow> r, b;
    r.reserve(4);
    b.reserve(4);
    for (int i = 0; i < 4; i++) {
        r.push_back({ m_players[i], GetPickSeatLabelForIndex(i) });
    }
    for (int i = 4; i < 8; i++) {
        b.push_back({ m_players[i], GetPickSeatLabelForIndex(i) });
    }
    std::vector<ScoreFileRow>& lT = m_bFlipSides ? b : r; std::vector<ScoreFileRow>& rT = m_bFlipSides ? r : b;

    CString pathScore = m_outputDir + L"\\比分.txt"; CString pathLeft = m_outputDir + L"\\左侧人头.txt"; CString pathRight = m_outputDir + L"\\右侧人头.txt"; CString pathKill = m_outputDir + L"\\击杀.txt";
    FILE* fS = NULL;
    if (_wfopen_s(&fS, pathScore, L"wt, ccs=UTF-8") == 0 && fS) { fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue); fclose(fS); }

    auto gs_full = [](ScoreFileRow& row) {
        PlayerData& p = row.player;
        if (p.name.IsEmpty()) return CString(L""); CString s; s.Format(L"%s%02d/%02d", p.name.GetString(), p.kills, p.deaths);
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L" A%d", p.akCount);else  s += L" -"; return s;
        };

    FILE* fKL = NULL; if (_wfopen_s(&fKL, pathLeft, L"wt, ccs=UTF-8") == 0 && fKL) { for (int i = 0; i < 4; i++) { CString ls = gs_full(lT[i]); if (!ls.IsEmpty()) fwprintf(fKL, L"%s\n", ls.GetString()); } fclose(fKL); }
    FILE* fKR = NULL; if (_wfopen_s(&fKR, pathRight, L"wt, ccs=UTF-8") == 0 && fKR) { for (int i = 0; i < 4; i++) { CString rs = gs_full(rT[i]); if (!rs.IsEmpty()) fwprintf(fKR, L"%s\n", rs.GetString()); } fclose(fKR); }

    auto gs_kill_only = [this](ScoreFileRow& row) {
        PlayerData& p = row.player;
        if (p.name.IsEmpty()) return CString(L""); CString s;
        if (m_bOutputSeatLabelToKillFile && !row.pickLabel.IsEmpty()) {
            s.Format(L"%s %s %02d", row.pickLabel.GetString(), p.name.GetString(), p.kills);
        }
        else {
            s.Format(L"%s %02d", p.name.GetString(), p.kills);
        }
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L"A%d", p.akCount);else  s += L" -"; return s;
        };
    FILE* fKill = NULL;
    if (_wfopen_s(&fKill, pathKill, L"wt, ccs=UTF-8") == 0 && fKill) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_kill_only(lT[i]); CString rs = gs_kill_only(rT[i]);
            if (ls.IsEmpty() && rs.IsEmpty()) continue;
            if (ls.IsEmpty()) fwprintf(fKill, L"%s\n", rs.GetString());
            else if (rs.IsEmpty()) fwprintf(fKill, L"%s\n", ls.GetString());
            else fwprintf(fKill, L"%s %s\n", ls.GetString(), rs.GetString());
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

            for (const CString& token : DnfParseAliasListString(it->second)) {
                r.aliases.push_back(token);
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
    const bool showCalFooter = m_bDeathXCalibrationMode && m_previewRect.Width() > 0 && m_previewRect.Height() > 0;
    const int calFooterGap = 8;
    const int calFooterH = 54;
    const int calFooterTop = showCalFooter ? max(m_previewRect.top + 10, m_previewRect.bottom - calFooterGap - calFooterH) : 0;

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
        int debugBottom = showCalFooter ? (calFooterTop - 6) : (m_previewRect.bottom - 25);
        CRect cr(m_previewRect.left + 15,
            debugBottom - 8 - tR.Height(),
            m_previewRect.left + 15 + tR.Width(),
            debugBottom);
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
    int cY = showCalFooter ? (calFooterTop - 12) : (m_previewRect.bottom - 20);
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

        int currentDrawAlgorithm = m_nDeathAlgorithmChoice;
        dc.SelectStockObject(NULL_BRUSH);

        auto drawPreviewDot = [&](int x, int y, int radius, COLORREF outline, COLORREF fill, bool filled) {
            CPen dotPen(PS_SOLID, 1, outline);
            CBrush dotBrush(fill);
            CPen* oldPen = dc.SelectObject(&dotPen);
            CBrush* oldBrush = filled ? dc.SelectObject(&dotBrush) : (CBrush*)dc.SelectStockObject(NULL_BRUSH);
            dc.Ellipse(x - radius, y - radius, x + radius, y + radius);
            dc.SelectObject(oldBrush);
            dc.SelectObject(oldPen);
        };

        // 遍历 8 个关键死亡 X 中心点
        for (int i = 0; i < DEATH_POINT_COUNT; i++) {
            ScorePointF logicPt = GetDeathXPoint(i);
            float cx = logicPt.x;
            float cy = logicPt.y;

            // 基础射线的伸展步长，与底层检测代码保持同步
            float stepX = 0.0f;
            float stepY = 0.0f;
            GetDeathXColorStep(i, stepX, stepY);

            COLORREF xColor = snap.dead[i] ? RGB(255, 0, 0) : RGB(0, 255, 255); // 上方 8 个检测位：触发画红色，未触发画蓝色

            if (currentDrawAlgorithm == DEATH_X_ALGO_COLOR) {
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
                dc.SelectObject(pOldPen);

                float sampleX[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                float sampleY[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                bool sampleHit[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                COLORREF sampleColor[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                int sampleCount = BuildDeathXColorSamples(i, logicPt, sampleX, sampleY);
                int liveSampleCount = min(snap.colorSampleCount[i], DEATH_X_COLOR_SAMPLE_COUNT);
                for (int k = 0; k < sampleCount && k < liveSampleCount; ++k) {
                    sampleHit[k] = snap.colorSampleHit[i][k];
                    sampleColor[k] = snap.colorSampleColor[i][k];
                }

                for (int k = 0; k < sampleCount; ++k) {
                    if (sampleX[k] <= 0.0f || sampleX[k] >= 1.0f || sampleY[k] <= 0.0f || sampleY[k] >= 1.0f) continue;
                    int hx = m_previewRect.left + (int)(sampleX[k] * m_previewRect.Width());
                    int hy = m_previewRect.top + (int)(sampleY[k] * m_previewRect.Height());
                    if (sampleHit[k]) {
                        COLORREF hitColor = sampleColor[k] == 0 ? RGB(255, 230, 0) : sampleColor[k];
                        drawPreviewDot(hx, hy, 4, RGB(0, 0, 0), hitColor, true);
                    }
                    else {
                        drawPreviewDot(hx, hy, 3, RGB(180, 180, 180), RGB(25, 25, 25), false);
                    }
                }
            }
            else {
                float patchX[DEATH_X_PATCH_SAMPLE_COUNT] = {};
                float patchY[DEATH_X_PATCH_SAMPLE_COUNT] = {};
                int patchCount = BuildDeathXPatchSamples(i, logicPt, patchX, patchY);

                for (int k = 0; k < patchCount; ++k) {
                    if (patchX[k] <= 0.0f || patchX[k] >= 1.0f || patchY[k] <= 0.0f || patchY[k] >= 1.0f) continue;
                    int px = m_previewRect.left + (int)(patchX[k] * m_previewRect.Width());
                    int py = m_previewRect.top + (int)(patchY[k] * m_previewRect.Height());
                    bool hasLivePatch = snap.patchPointCount[i] > k;
                    COLORREF pc = hasLivePatch ? snap.patchColor[i][k] : RGB(25, 25, 25);
                    COLORREF outline = !hasLivePatch || snap.patchClass[i][k] == 0 ? RGB(255, 255, 255) : RGB(0, 0, 0);
                    drawPreviewDot(px, py, 5, outline, pc, hasLivePatch && snap.patchClass[i][k] != 0);
                }
            }

            if (currentDrawAlgorithm == DEATH_X_ALGO_COLOR && snap.centerGate[i]) {
                int ccx = m_previewRect.left + (int)(cx * m_previewRect.Width());
                int ccy = m_previewRect.top + (int)(cy * m_previewRect.Height());
                CPen centerGatePen(PS_SOLID, 1, RGB(255, 255, 255));
                CPen* pOldGatePen = dc.SelectObject(&centerGatePen);
                dc.MoveTo(ccx - 4, ccy - 4); dc.LineTo(ccx + 4, ccy - 4);
                dc.LineTo(ccx + 4, ccy + 4); dc.LineTo(ccx - 4, ccy + 4);
                dc.LineTo(ccx - 4, ccy - 4);
                dc.SelectObject(pOldGatePen);
            }
        }
    }

    if (m_bDeathXCalibrationMode && m_previewRect.Width() > 0 && m_previewRect.Height() > 0) {
        dc.SetBkMode(TRANSPARENT);
        CFont labelFont;
        labelFont.CreatePointFont(95, L"微软雅黑");
        CFont* oldFont = dc.SelectObject(&labelFont);

        for (int i = 0; i < DEATH_POINT_COUNT; ++i) {
            CPoint p = DeathXPointToClient(m_deathXPoints[i]);
            bool dragging = (i == m_dragDeathXPoint);
            bool selected = (i == m_selectedDeathXPoint);
            COLORREF markerColor = (i < 4) ? RGB(255, 70, 70) : RGB(80, 170, 255);
            COLORREF borderColor = selected ? RGB(255, 255, 0) : RGB(255, 255, 255);
            CPen markerPen(PS_SOLID, selected || dragging ? 3 : 2, borderColor);
            CBrush markerBrush(markerColor);
            CPen* oldPen = dc.SelectObject(&markerPen);
            CBrush* oldBrush = dc.SelectObject(&markerBrush);
            int radius = selected || dragging ? 10 : 7;
            dc.Ellipse(p.x - radius, p.y - radius, p.x + radius, p.y + radius);
            dc.SelectObject(oldBrush);
            dc.SelectObject(oldPen);

            CPen centerPen(PS_SOLID, 2, RGB(255, 255, 255));
            CPen* oldCenterPen = dc.SelectObject(&centerPen);
            dc.MoveTo(p.x - 5, p.y);
            dc.LineTo(p.x + 6, p.y);
            dc.MoveTo(p.x, p.y - 5);
            dc.LineTo(p.x, p.y + 6);
            dc.SelectObject(oldCenterPen);

            if (selected) {
                CPen selectPen(PS_SOLID, 1, RGB(0, 0, 0));
                CPen* oldSelectPen = dc.SelectObject(&selectPen);
                dc.Ellipse(p.x - radius - 3, p.y - radius - 3, p.x + radius + 3, p.y + radius + 3);
                dc.SelectObject(oldSelectPen);
            }

            CString label;
            label.Format(L"%d %s", i + 1, GetDeathPointName(i));
            CRect labelRect(p.x + 12, p.y - 12, p.x + 82, p.y + 14);
            dc.FillSolidRect(&labelRect, selected ? RGB(60, 55, 0) : RGB(10, 10, 10));
            dc.SetTextColor(selected ? RGB(255, 255, 0) : markerColor);
            dc.DrawText(label, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        dc.SelectObject(oldFont);
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

                    ScorePointF logicPt = GetDeathXPoint(idx);
                    int srcCX = (int)(logicPt.x * m_w);
                    int srcCY = (int)(logicPt.y * m_h);
                    bool isActive = IsActiveDeathPoint(idx);

                    // 这里只放大“蓝色 X 实际检查范围”，不再带大量周边无效画面。
                    float stepX = 0.0f;
                    float stepY = 0.0f;
                    GetDeathXColorStep(idx, stepX, stepY);

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

                    int currentDrawAlgorithm = m_nDeathAlgorithmChoice;
                    auto drawCellDot = [&](int x, int y, int radius, COLORREF outline, COLORREF fill, bool filled) {
                        CPen dotPen(PS_SOLID, 1, outline);
                        CBrush dotBrush(fill);
                        CPen* oldPen = dc.SelectObject(&dotPen);
                        CBrush* oldBrush = filled ? dc.SelectObject(&dotBrush) : (CBrush*)dc.SelectStockObject(NULL_BRUSH);
                        dc.Ellipse(x - radius, y - radius, x + radius, y + radius);
                        dc.SelectObject(oldBrush);
                        dc.SelectObject(oldPen);
                    };

                    if (currentDrawAlgorithm == DEATH_X_ALGO_COLOR && snap.centerGate[idx]) {
                        int ccx = mapToCellX(logicPt.x);
                        int ccy = mapToCellY(logicPt.y);
                        CPen gatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        CPen* oldGatePen = dc.SelectObject(&gatePen);
                        dc.MoveTo(ccx - 4, ccy - 4); dc.LineTo(ccx + 4, ccy - 4);
                        dc.LineTo(ccx + 4, ccy + 4); dc.LineTo(ccx - 4, ccy + 4);
                        dc.LineTo(ccx - 4, ccy - 4);
                        dc.SelectObject(oldGatePen);
                    }

                    if (currentDrawAlgorithm == DEATH_X_ALGO_COLOR) {
                        float sampleX[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                        float sampleY[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                        bool sampleHit[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                        COLORREF sampleColor[DEATH_X_COLOR_SAMPLE_COUNT] = {};
                        int sampleCount = BuildDeathXColorSamples(idx, logicPt, sampleX, sampleY);
                        int liveSampleCount = min(snap.colorSampleCount[idx], DEATH_X_COLOR_SAMPLE_COUNT);
                        for (int k = 0; k < sampleCount && k < liveSampleCount; ++k) {
                            sampleHit[k] = snap.colorSampleHit[idx][k];
                            sampleColor[k] = snap.colorSampleColor[idx][k];
                        }

                        for (int k = 0; k < sampleCount; ++k) {
                            if (sampleX[k] <= 0.0f || sampleX[k] >= 1.0f || sampleY[k] <= 0.0f || sampleY[k] >= 1.0f) continue;
                            int hx = mapToCellX(sampleX[k]);
                            int hy = mapToCellY(sampleY[k]);
                            if (sampleHit[k]) {
                                COLORREF hitColor = sampleColor[k] == 0 ? RGB(255, 230, 0) : sampleColor[k];
                                drawCellDot(hx, hy, 3, RGB(0, 0, 0), hitColor, true);
                            }
                            else {
                                drawCellDot(hx, hy, 2, RGB(190, 190, 190), RGB(20, 20, 20), false);
                            }
                        }
                    }
                    else {
                        float patchX[DEATH_X_PATCH_SAMPLE_COUNT] = {};
                        float patchY[DEATH_X_PATCH_SAMPLE_COUNT] = {};
                        int patchCount = BuildDeathXPatchSamples(idx, logicPt, patchX, patchY);

                        for (int k = 0; k < patchCount; ++k) {
                            if (patchX[k] <= 0.0f || patchX[k] >= 1.0f || patchY[k] <= 0.0f || patchY[k] >= 1.0f) continue;
                            int px = mapToCellX(patchX[k]);
                            int py = mapToCellY(patchY[k]);
                            bool hasLivePatch = snap.patchPointCount[idx] > k;
                            COLORREF pc = hasLivePatch ? snap.patchColor[idx][k] : RGB(20, 20, 20);
                            COLORREF outline = !hasLivePatch || snap.patchClass[idx][k] == 0 ? RGB(255, 255, 255) : RGB(0, 0, 0);
                            drawCellDot(px, py, 4, outline, pc, hasLivePatch && snap.patchClass[idx][k] != 0);

                            CString label;
                            label.Format(L"%d", k + 1);
                            dc.SetBkMode(TRANSPARENT);
                            dc.SetTextColor(outline == RGB(0, 0, 0) ? RGB(255, 255, 255) : RGB(255, 220, 0));
                            dc.TextOut(px + 5, py - 7, label);
                        }
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
    if (!m_bDeathXCalibrationMode && m_previewRect.PtInRect(pt)) {
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

        // 坐标提示
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 255, 0));
        CString tip;
        tip.Format(L"坐标: %.4f, %.4f",
            (float)(pt.x - m_previewRect.left) / (float)max(1, m_previewRect.Width()),
            (float)(pt.y - m_previewRect.top) / (float)max(1, m_previewRect.Height()));
        dc.TextOut(drawX + 5, drawY + magH - 25, tip);
    }

    if (showCalFooter) {
        dc.SetBkMode(TRANSPARENT);
        CRect footerRect(m_previewRect.left + 8, calFooterTop, m_previewRect.right - 8, calFooterTop + calFooterH);
        dc.FillSolidRect(&footerRect, RGB(20, 20, 20));
        CFont tipFont;
        tipFont.CreatePointFont(88, L"微软雅黑");
        CFont* oldFont = dc.SelectObject(&tipFont);
        dc.SetTextColor(RGB(0, 255, 255));
        CString tip = L"X校准：拖动或方向键1像素微调；1-8选点，Shift+方向键10像素";
        CRect tipRect(footerRect.left + 10, footerRect.top + 2, footerRect.right - 10, footerRect.top + 20);
        dc.DrawText(tip, &tipRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        dc.SelectObject(oldFont);
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
        g_triggerCooldownKind = 0;
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
        std::deque<VisualLogMsg> pendingLogs;
        {
            std::lock_guard<std::mutex> lkLog(g_visualLogMutex);
            pendingLogs = g_visualLogs;
            g_visualLogs.clear();
        }

        if (!pendingLogs.empty() && m_editVisualLogs.m_hWnd) {
            // ★ 超过 300 行时砍掉前半，防止再次撞上限
            int lineCount = m_editVisualLogs.GetLineCount();
            if (lineCount > 300) {
                int charIdx = m_editVisualLogs.LineIndex(lineCount - 150);
                m_editVisualLogs.SetSel(0, charIdx);
                m_editVisualLogs.ReplaceSel(L"");
            }

            for (const auto& log : pendingLogs) {
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
        }

        if (!pendingLogs.empty() && m_pWebDlg) {
            try {
                json msg;
                msg["action"] = "console_logs";
                msg["logs"] = json::array();
                for (const auto& log : pendingLogs) {
                    CString colorText;
                    colorText.Format(L"#%02X%02X%02X", GetRValue(log.color), GetGValue(log.color), GetBValue(log.color));
                    json item;
                    item["text"] = std::string(CW2A(log.text, CP_UTF8));
                    item["color"] = std::string(CW2A(colorText, CP_UTF8));
                    msg["logs"].push_back(item);
                }
                CString jsonStr = CA2W(msg.dump().c_str(), CP_UTF8);
                m_pWebDlg->SendStateToWeb(jsonStr);
            }
            catch (...) {}
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
        static DWORD s_lastOcrHealthKick = 0;  // OCR 健康检查节流

        // 1. 问系统：现在屏幕最前面的是不是咱们的软件？
        HWND hForeground = ::GetForegroundWindow();
        bool bIsOurAppFocused = (hForeground == m_hWnd || ::IsChild(m_hWnd, hForeground));

        if (m_bIsRunning && !m_bOcrStartPending.load() && !m_bOcrRecoveryPending.load()) {
            DWORD now = ::GetTickCount();
            if (now - s_lastOcrHealthKick >= 8000) {
                s_lastOcrHealthKick = now;
                BeginOcrServiceRecovery(true);
            }
        }

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

    // --- 读取并转换更新配置内容 ---
    // 服务器文件可能是 UTF-8，也可能被编辑器保存成 ANSI/GBK。
    // 先严格按 UTF-8 解；失败再按 GBK/系统代码页解，避免中文更新日志乱码。
    ULONGLONG dwLength = file.GetLength();
    char* pBuf = new char[(size_t)dwLength + 1];
    memset(pBuf, 0, (size_t)dwLength + 1);
    file.Read(pBuf, (UINT)dwLength);
    file.Close();
    ::DeleteFile(tempFile);

    CString content;
    const char* pRaw = pBuf;
    int byteLen = (dwLength > 0x7fffffffULL) ? 0x7fffffff : (int)dwLength;

    // UTF-16 LE BOM 兼容。
    if (byteLen >= 2 &&
        (unsigned char)pRaw[0] == 0xFF &&
        (unsigned char)pRaw[1] == 0xFE) {
        int wcharCount = (byteLen - 2) / 2;
        content = CString((LPCWSTR)(pRaw + 2), wcharCount);
    }
    else {
        // UTF-8 BOM 兼容。
        if (byteLen >= 3 &&
            (unsigned char)pRaw[0] == 0xEF &&
            (unsigned char)pRaw[1] == 0xBB &&
            (unsigned char)pRaw[2] == 0xBF) {
            pRaw += 3;
            byteLen -= 3;
        }

        auto DecodeBytes = [&](UINT codePage, DWORD flags) -> bool {
            int nLen = MultiByteToWideChar(codePage, flags, pRaw, byteLen, NULL, 0);
            if (nLen <= 0) return false;
            std::vector<wchar_t> wbuf(nLen + 1, 0);
            if (MultiByteToWideChar(codePage, flags, pRaw, byteLen, wbuf.data(), nLen) <= 0) return false;
            content = CString(wbuf.data(), nLen);
            return true;
        };

        if (!DecodeBytes(CP_UTF8, MB_ERR_INVALID_CHARS)) {
            if (!DecodeBytes(936, 0)) {
                DecodeBytes(CP_ACP, 0);
            }
        }
    }

    delete[] pBuf;

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
            CString visibleUpdateLog = FilterUpdateLogGreaterThanCurrent(updateLog, currentVersion);
            if (visibleUpdateLog.IsEmpty()) {
                visibleUpdateLog = L"没有找到高于当前版本的更新说明。";
            }

            // 用专用更新弹窗显示，保留 update_v2.txt 的换行、缩进和空行；
            // 不再用 MessageBox 自动换行，避免更新说明格式被打乱。
            if (ShowUpdateConfirmDialog(serverVersion, currentVersion, visibleUpdateLog) == IDYES) {
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
                        CString normalizedAliases = DnfNormalizeAliasListString(aliases);
                        if (!normalizedAliases.IsEmpty()) {
                            m_aliasDB[mainName] = normalizedAliases;
                        }
                    }
                }
            }
        }
        file.Close();
    }

    // 加载完后刷新左下角列表
    UpdateAndRefreshRecentList();
    LoadAliasCloudDeleteBaseline();
    ResetAliasDbCloudBaseline();
}

void CDNFGameCaptureDlg::SaveAliasDB()
{
    SaveAliasDB(true);
}

void CDNFGameCaptureDlg::SaveAliasDB(bool mergeActivePlayers) {
    if (mergeActivePlayers) {
        for (int i = 0; i < 8; i++) {
            CString mName = m_players[i].name;
            mName.Trim();

            if (!mName.IsEmpty() && !m_players[i].aliases.empty()) {
                std::vector<CString> mergedAliases = DnfParseAliasListString(m_aliasDB[mName]);

                for (const auto& a : m_players[i].aliases) {
                    CString aName = a.name;
                    aName.Trim();
                    DnfMergeAliasIntoList(mergedAliases, aName);
                }

                CString normalizedAliases = DnfFormatAliasListString(mergedAliases);
                if (!normalizedAliases.IsEmpty()) {
                    m_aliasDB[mName] = normalizedAliases;
                }
            }
        }
    }

    for (auto it = m_aliasDB.begin(); it != m_aliasDB.end(); ) {
        CString normalizedAliases = DnfNormalizeAliasListString(it->second);
        if (it->first.IsEmpty() || normalizedAliases.IsEmpty()) {
            auto eraseIt = it++;
            m_aliasDB.erase(eraseIt);
        }
        else {
            it->second = normalizedAliases;
            ++it;
        }
    }

    // 写入 ini 文件
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_db.ini";

    CString content;
    for (const auto& pair : m_aliasDB) {
        content += pair.first + L"=" + pair.second + L"\r\n";
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

void CDNFGameCaptureDlg::LoadAliasCloudDeleteBaseline()
{
    m_aliasCloudDeleteBaselineMains.clear();
    m_aliasCloudBaselinePlayers.clear();

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_cloud_baseline.json";

    CFile file;
    if (!file.Open(path, CFile::modeRead)) return;

    int len = (int)file.GetLength();
    if (len <= 0) {
        file.Close();
        return;
    }

    std::string content;
    content.resize(len);
    file.Read(content.data(), len);
    file.Close();

    try {
        json root = json::parse(content);
        json mains = root.is_array() ? root : root.value("mainNames", json::array());
        if (mains.is_array()) {
            for (const auto& value : mains) {
                if (!value.is_string()) continue;
                CString mainName = CA2W(value.get<std::string>().c_str(), CP_UTF8);
                mainName.Trim();
                if (mainName.IsEmpty()) continue;
                if (std::find(m_aliasCloudDeleteBaselineMains.begin(), m_aliasCloudDeleteBaselineMains.end(), mainName) == m_aliasCloudDeleteBaselineMains.end()) {
                    m_aliasCloudDeleteBaselineMains.push_back(mainName);
                }
            }
        }

        json players = root.is_object() ? root.value("players", json::object()) : json::object();
        if (players.is_object()) {
            for (auto it = players.begin(); it != players.end(); ++it) {
                CString mainName = CA2W(it.key().c_str(), CP_UTF8);
                mainName.Trim();
                if (mainName.IsEmpty()) continue;

                std::vector<CString> aliases;
                if (it.value().is_array()) {
                    for (const auto& item : it.value()) {
                        if (!item.is_string()) continue;
                        CString aliasText = CA2W(item.get<std::string>().c_str(), CP_UTF8);
                        DnfMergeAliasIntoList(aliases, aliasText);
                    }
                }
                else if (it.value().is_string()) {
                    CString aliasList = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
                    for (const auto& alias : DnfParseAliasListString(aliasList)) {
                        DnfMergeAliasIntoList(aliases, alias);
                    }
                }

                CString normalizedAliases = DnfFormatAliasListString(aliases);
                if (!normalizedAliases.IsEmpty()) {
                    m_aliasCloudBaselinePlayers[mainName] = normalizedAliases;
                }
            }
        }
    }
    catch (...) {
        m_aliasCloudDeleteBaselineMains.clear();
        m_aliasCloudBaselinePlayers.clear();
    }
}

void CDNFGameCaptureDlg::SaveAliasCloudDeleteBaseline() const
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_cloud_baseline.json";

    json root;
    root["version"] = 1;
    root["mainNames"] = json::array();
    for (auto mainName : m_aliasCloudDeleteBaselineMains) {
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        root["mainNames"].push_back(std::string(CW2A(mainName, CP_UTF8)));
    }
    root["players"] = json::object();
    for (auto const& [mainNameRaw, aliasesRaw] : m_aliasCloudBaselinePlayers) {
        CString mainName = mainNameRaw;
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        std::vector<CString> aliases = DnfParseAliasListString(aliasesRaw);
        if (aliases.empty()) continue;

        json aliasArray = json::array();
        for (auto alias : aliases) {
            alias.Trim();
            if (!alias.IsEmpty()) aliasArray.push_back(std::string(CW2A(alias, CP_UTF8)));
        }
        if (!aliasArray.empty()) {
            root["players"][std::string(CW2A(mainName, CP_UTF8))] = aliasArray;
        }
    }

    std::string content = root.dump(2);
    CFile file;
    if (file.Open(path, CFile::modeCreate | CFile::modeWrite)) {
        file.Write(content.c_str(), (UINT)content.length());
        file.Close();
    }
}

void CDNFGameCaptureDlg::SetAliasCloudDeleteBaselineFromPublicPlayers(const nlohmann::json& players)
{
    m_aliasCloudDeleteBaselineMains.clear();
    m_aliasCloudBaselinePlayers.clear();
    if (!players.is_object()) {
        SaveAliasCloudDeleteBaseline();
        return;
    }

    for (auto it = players.begin(); it != players.end(); ++it) {
        CString mainName = CA2W(it.key().c_str(), CP_UTF8);
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        if (std::find(m_aliasCloudDeleteBaselineMains.begin(), m_aliasCloudDeleteBaselineMains.end(), mainName) == m_aliasCloudDeleteBaselineMains.end()) {
            m_aliasCloudDeleteBaselineMains.push_back(mainName);
        }

        std::vector<CString> aliases;
        if (it.value().is_array()) {
            for (const auto& item : it.value()) {
                if (!item.is_string()) continue;
                CString aliasText = CA2W(item.get<std::string>().c_str(), CP_UTF8);
                DnfMergeAliasIntoList(aliases, aliasText);
            }
        }
        else if (it.value().is_string()) {
            CString aliasList = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
            for (const auto& alias : DnfParseAliasListString(aliasList)) {
                DnfMergeAliasIntoList(aliases, alias);
            }
        }
        CString normalizedAliases = DnfFormatAliasListString(aliases);
        if (!normalizedAliases.IsEmpty()) {
            m_aliasCloudBaselinePlayers[mainName] = normalizedAliases;
        }
    }
    SaveAliasCloudDeleteBaseline();
}

nlohmann::json CDNFGameCaptureDlg::BuildAliasCloudDeleteScopeJson() const
{
    json scope = json::array();
    for (auto mainName : m_aliasCloudDeleteBaselineMains) {
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        scope.push_back(std::string(CW2A(mainName, CP_UTF8)));
    }
    return scope;
}

std::string CDNFGameCaptureDlg::FilterAliasDbPayloadForReview(const std::string& aliasDbPayload, int& mainCount, int& pairCount, int& containedNakedAliasCount) const
{
    mainCount = 0;
    pairCount = 0;
    containedNakedAliasCount = 0;
    if (aliasDbPayload.empty()) return "";

    json input = json::parse(aliasDbPayload);
    if (!input.is_object()) return "";

    json output = json::object();
    for (auto it = input.begin(); it != input.end(); ++it) {
        CString mainName = CA2W(it.key().c_str(), CP_UTF8);
        mainName.Trim();
        if (mainName.IsEmpty()) continue;

        std::vector<CString> localAliases;
        if (it.value().is_string()) {
            CString aliasList = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
            localAliases = DnfParseAliasListString(aliasList);
        }
        else if (it.value().is_array()) {
            for (const auto& item : it.value()) {
                if (!item.is_string()) continue;
                CString aliasText = CA2W(item.get<std::string>().c_str(), CP_UTF8);
                DnfMergeAliasIntoList(localAliases, aliasText);
            }
        }

        auto cloudIt = m_aliasCloudBaselinePlayers.find(mainName);
        std::vector<CString> cloudAliases = cloudIt == m_aliasCloudBaselinePlayers.end()
            ? std::vector<CString>()
            : DnfParseAliasListString(cloudIt->second);

        std::vector<CString> keptAliases;
        for (auto aliasName : localAliases) {
            aliasName.Trim();
            if (aliasName.IsEmpty() || aliasName == mainName) continue;

            bool shouldFilter = false;
            if (!DnfAliasHasDeclaredJob(aliasName)) {
                for (const auto& cloudAlias : cloudAliases) {
                    if (DnfAliasCloudContainsNakedAlias(cloudAlias, aliasName)) {
                        shouldFilter = true;
                        DnfMergeAliasIntoList(keptAliases, cloudAlias);
                        break;
                    }
                }
            }

            if (shouldFilter) {
                containedNakedAliasCount++;
                continue;
            }
            DnfMergeAliasIntoList(keptAliases, aliasName);
        }

        if (!keptAliases.empty()) {
            CString normalizedAliases = DnfFormatAliasListString(keptAliases);
            output[std::string(CW2A(mainName, CP_UTF8))] = std::string(CW2A(normalizedAliases, CP_UTF8));
            mainCount++;
            pairCount += (int)keptAliases.size();
        }
        else if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) != m_aliasDbPendingDeleteMains.end()) {
            output[std::string(CW2A(mainName, CP_UTF8))] = "";
            mainCount++;
        }
    }

    for (auto mainName : m_aliasDbPendingDeleteMains) {
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        std::string utf8Main = std::string(CW2A(mainName, CP_UTF8));
        if (output.contains(utf8Main)) continue;
        output[utf8Main] = "";
        mainCount++;
    }

    return output.dump();
}

std::string CDNFGameCaptureDlg::BuildAliasDbJsonPayload(int& mainCount, int& pairCount) const
{
    json aliasDb = json::object();
    mainCount = 0;
    pairCount = 0;

    for (auto const& [name, aliases] : m_aliasDB) {
        CString mainName = name;
        mainName.Trim();
        CString normalizedAliases = DnfNormalizeAliasListString(aliases);
        std::vector<CString> aliasList = DnfParseAliasListString(normalizedAliases);
        if (mainName.IsEmpty()) continue;
        if (aliasList.empty()) {
            if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) == m_aliasDbPendingDeleteMains.end()) continue;
            aliasDb[std::string(CW2A(mainName, CP_UTF8))] = "";
            mainCount++;
            continue;
        }

        aliasDb[std::string(CW2A(mainName, CP_UTF8))] = std::string(CW2A(normalizedAliases, CP_UTF8));
        mainCount++;
        pairCount += (int)aliasList.size();
    }

    for (auto mainName : m_aliasDbPendingDeleteMains) {
        mainName.Trim();
        if (mainName.IsEmpty()) continue;
        if (m_aliasDB.find(mainName) != m_aliasDB.end()) continue;
        aliasDb[std::string(CW2A(mainName, CP_UTF8))] = "";
        mainCount++;
    }

    return aliasDb.dump();
}

void CDNFGameCaptureDlg::ResetAliasDbCloudBaseline()
{
    int mainCount = 0;
    int pairCount = 0;
    m_aliasDbCloudBaselinePayload = BuildAliasDbJsonPayload(mainCount, pairCount);
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
    for (auto aN : DnfParseAliasListString(dbAliases)) {
        aN.Trim();
        bool exists = false;
        for (const auto& ea : existAliases) {
            if (DnfAliasSameStorageEntry(ea, aN)) { exists = true; break; }
        }

        if (!exists && !aN.IsEmpty()) aliasesToInsert += L"(" + aN + L")";
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
                    DnfAliasMergeResult mergeResult = DnfMergeAliasIntoAliasDataList(m_players[targetIdx].aliases, aN);
                    if (mergeResult != DnfAliasMergeNone) {
                        addAliasCount++;
                        CString logPrefix = mergeResult == DnfAliasMergeUpgraded ? L" ├ 🧩补全小号: [" : L" ├ ➕追加小号: [";
                        AppLog(logPrefix + aN + L"]", RGB(100, 255, 100));
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
            if (m_pWebDlg) m_pWebDlg->WriteWebHostDiagnostics(L"前端page_ready");
        }
        else if (action == "cmd_set_appearance_panel_open") {
            if (m_pWebDlg) {
                m_pWebDlg->SetAppearancePanelExpanded(j.value("open", false));
            }
        }
        else if (action == "web_layout_diagnostics") {
            auto& data = j["data"];
            CString layoutVersion = data.contains("layoutVersion") ? CA2W(data["layoutVersion"].get<std::string>().c_str(), CP_UTF8) : L"";
            CString metaLayoutVersion = data.contains("metaLayoutVersion") ? CA2W(data["metaLayoutVersion"].get<std::string>().c_str(), CP_UTF8) : L"";
            CString href = data.contains("href") ? CA2W(data["href"].get<std::string>().c_str(), CP_UTF8) : L"";
            double fitScale = data.value("fitScale", 1.0);
            double dpr = data.value("devicePixelRatio", 1.0);
            int innerWidth = data.value("innerWidth", 0);
            int innerHeight = data.value("innerHeight", 0);
            int bodyScrollWidth = data.value("bodyScrollWidth", 0);
            int bodyScrollHeight = data.value("bodyScrollHeight", 0);
            int appShellNaturalWidth = data.value("appShellNaturalWidth", 0);
            int appShellNaturalHeight = data.value("appShellNaturalHeight", 0);
            int usableWidth = data.value("usableWidth", 0);
            CString reason = j.contains("reason") ? CA2W(j["reason"].get<std::string>().c_str(), CP_UTF8) : L"";

            CString diag;
            diag.Format(L"[Web布局诊断][JS] 原因=%s；layoutVersion=%s；meta=%s；href=%s；inner=%dx%d；DPR=%.3f；bodyScroll=%dx%d；appShell自然=%dx%d；usableWidth=%d；fitScale=%.3f。",
                reason.GetString(),
                layoutVersion.GetString(),
                metaLayoutVersion.GetString(),
                href.GetString(),
                innerWidth, innerHeight,
                dpr,
                bodyScrollWidth, bodyScrollHeight,
                appShellNaturalWidth, appShellNaturalHeight,
                usableWidth,
                fitScale);
            WriteMatchLog(diag);

            if (m_pWebDlg && reason != L"zoom-calibrated") {
                bool calibrated = m_pWebDlg->CalibrateZoomFromWebMetrics(innerWidth, innerHeight, reason);
                if (calibrated) {
                    DnfSendWebToast(m_pWebDlg, L"web_zoom_calibrated", L"");
                }
            }

            const bool versionMismatch = layoutVersion != metaLayoutVersion;
            const bool severeAutoFit = fitScale > 0.0 && fitScale < 0.90;
            if (versionMismatch) {
                CString warn;
                warn.Format(L"⚠️ [Web布局] 前端文件可能未同步：JS=%s，HTML=%s，fit=%.3f，scrollW=%d，innerW=%d",
                    layoutVersion.GetString(), metaLayoutVersion.GetString(), fitScale, bodyScrollWidth, innerWidth);
                AppLog(warn, RGB(255, 180, 0));
            }
            else if (severeAutoFit) {
                CString warn;
                warn.Format(L"⚠️ [Web布局] 窗口空间不足，前端已自动缩放适配：fit=%.3f，scrollW=%d，innerW=%d",
                    fitScale, bodyScrollWidth, innerWidth);
                AppLog(warn, RGB(255, 180, 0));
            }
        }
        else if (action == "update_state") {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            auto& data = j["data"];

            m_totalScoreBlue = data["blueScore"].get<int>();
            m_totalScoreRed = data["redScore"].get<int>();
            if (data.contains("redPickMode")) {
                std::string mode = data["redPickMode"].get<std::string>();
                m_bRedPickFirst = (mode != "second");
                ::WritePrivateProfileString(L"Settings", L"RedPickFirst", m_bRedPickFirst ? L"1" : L"0", m_iniPath);
            }

            // Web 端会把永久小号库 fullAliasDB 一起传回来。
            // 这里必须同步到 C++ 的 m_aliasDB，否则 Web 里改完小号名后，下一次 C++ 广播会用旧库把它刷回去。
            if (data.contains("fullAliasDB") && data["fullAliasDB"].is_object()) {
                std::vector<CString> oldMainNames;
                for (const auto& pair : m_aliasDB) oldMainNames.push_back(pair.first);
                m_aliasDB.clear();
                for (auto it = data["fullAliasDB"].begin(); it != data["fullAliasDB"].end(); ++it) {
                    CString mainName = CA2W(it.key().c_str(), CP_UTF8);
                    CString aliases = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
                    mainName.Trim();
                    aliases.Trim();
                    CString normalizedAliases = DnfNormalizeAliasListString(aliases);
                    if (!mainName.IsEmpty()) {
                        if (!normalizedAliases.IsEmpty()) {
                            m_aliasDB[mainName] = normalizedAliases;
                            auto delIt = std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName);
                            if (delIt != m_aliasDbPendingDeleteMains.end()) m_aliasDbPendingDeleteMains.erase(delIt);
                        }
                        else if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) == m_aliasDbPendingDeleteMains.end()) {
                            m_aliasDbPendingDeleteMains.push_back(mainName);
                        }
                    }
                }

                for (auto oldMainName : oldMainNames) {
                    oldMainName.Trim();
                    if (oldMainName.IsEmpty() || m_aliasDB.find(oldMainName) != m_aliasDB.end()) continue;
                    if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), oldMainName) == m_aliasDbPendingDeleteMains.end()) {
                        m_aliasDbPendingDeleteMains.push_back(oldMainName);
                    }
                }
            }

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
                    // Web 同步阶段只接收并保存小号，不因为 2 字短 ID 清空选手。
                    // 裸短 ID / 无小号 的拦截统一放到“开始监控”阶段处理。
                    for (auto& a : p["aliases"]) {
                        AliasData ad;
                        ad.name = CA2W(a.get<std::string>().c_str(), CP_UTF8);
                        ad.name.Trim();
                        if (!ad.name.IsEmpty()) {
                            DnfMergeAliasIntoAliasDataList(m_players[mfcIdx].aliases, ad.name);
                        }
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && m_players[mfcIdx].aliases.empty()) {
                        AppLog(L"⚠️ [Web同步提示] [" + m_players[mfcIdx].name + L"] 只有主号、没有小号：保留在选手列表中，但开始监控会被拦截。", RGB(255, 180, 0));
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
                    // Web 同步阶段只接收并保存小号，不因为 2 字短 ID 清空选手。
                    // 裸短 ID / 无小号 的拦截统一放到“开始监控”阶段处理。
                    for (auto& a : p["aliases"]) {
                        AliasData ad;
                        ad.name = CA2W(a.get<std::string>().c_str(), CP_UTF8);
                        ad.name.Trim();
                        if (!ad.name.IsEmpty()) {
                            DnfMergeAliasIntoAliasDataList(m_players[mfcIdx].aliases, ad.name);
                        }
                    }
                    if (!m_players[mfcIdx].name.IsEmpty() && m_players[mfcIdx].aliases.empty()) {
                        AppLog(L"⚠️ [Web同步提示] [" + m_players[mfcIdx].name + L"] 只有主号、没有小号：保留在选手列表中，但开始监控会被拦截。", RGB(255, 180, 0));
                    }
                }
            }
            else {
                MessageBox(L"Web端发来的数据长度不对！", L"同步异常", MB_ICONWARNING);
            }

            SaveAliasDB(false);
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
        else if (action == "cmd_set_death_algorithm") {
            int algo = j["value"].get<int>();
            if (algo < 0 || algo > 1) algo = 0;
            m_nDeathAlgorithmChoice = algo;
            if (m_cmbDeathAlgorithm.m_hWnd) m_cmbDeathAlgorithm.SetCurSel(m_nDeathAlgorithmChoice);

            CString val;
            val.Format(L"%d", m_nDeathAlgorithmChoice);
            WritePrivateProfileString(L"Settings", L"DeathXAlgorithm", val, m_iniPath);

            CString names[] = { L"大X颜色个数判断", L"打补丁红蓝判断" };
            AppLog(L"⚙️ [设置] 死亡X算法已切换为: " + names[m_nDeathAlgorithmChoice], RGB(0, 255, 255));
            BroadcastStateToWeb();
        }
        else if (action == "cmd_set_output_seat_label") {
            m_bOutputSeatLabelToKillFile = j.value("enabled", false);
            ::WritePrivateProfileString(L"Settings", L"OutputSeatLabelToKillFile", m_bOutputSeatLabelToKillFile ? L"1" : L"0", m_iniPath);
            WriteScoreToFile();
            AppLog(m_bOutputSeatLabelToKillFile ? L"📝 [TXT输出] 击杀.txt 已开启编号前缀。" : L"📝 [TXT输出] 击杀.txt 已关闭编号前缀。", RGB(255, 210, 106));
            BroadcastStateToWeb();
        }
        else if (action == "cmd_set_red_pick_mode") {
            std::string mode = j.value("mode", "first");
            m_bRedPickFirst = (mode != "second");
            ::WritePrivateProfileString(L"Settings", L"RedPickFirst", m_bRedPickFirst ? L"1" : L"0", m_iniPath);
            WriteScoreToFile();
            AppLog(m_bRedPickFirst ? L"🎯 [选人顺序] 红队先选，编号已切换为 x/1/4/6。" : L"🎯 [选人顺序] 红队后选，编号已切换为 h/2/4/5。", RGB(255, 210, 106));
            BroadcastStateToWeb();
        }
        else if (action == "cmd_set_scoreboard_text_styles") {
            if (j.contains("styles") && j["styles"].is_object()) {
                DnfSaveScoreboardTextStylesJson(m_iniPath, j["styles"]);
                AppLog(L"🎨 [外观] 记分板文字样式已保存。", RGB(255, 210, 106));
                BroadcastStateToWeb();
            }
        }
        else if (action == "cmd_set_kill_display_settings") {
            if (j.contains("settings") && j["settings"].is_object()) {
                DnfSaveKillDisplaySettingsJson(m_iniPath, j["settings"]);
                AppLog(L"🎨 [击杀展示页] 外观样式已保存。", RGB(255, 210, 106));
                BroadcastStateToWeb();
            }
        }
        else if (action == "cmd_open_kill_display") {
            OpenKillDisplayWindow();
            BroadcastStateToWeb();
        }
        else if (action == "cmd_toggle_kill_display") {
            ToggleKillDisplayWindow();
            BroadcastStateToWeb();
        }
        else if (action == "cmd_copy_kill_obs_url") {
            CString obsUrl = GetKillDisplayObsUrl();
            bool ok = DnfCopyUnicodeTextToClipboard(GetSafeHwnd(), obsUrl);
            CString msg;
            if (ok) msg = L"OBS网址已复制：" + obsUrl;
            else msg = L"OBS网址复制失败，请手动填写：http://127.0.0.1:18777/kill.html";
            CString logMsg = ok ? L"🔗 [击杀展示页] " : L"⚠️ [击杀展示页] ";
            logMsg += msg;
            AppLog(logMsg, ok ? RGB(0, 255, 100) : RGB(255, 180, 0));
            if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"kill_obs_url_result", msg);
        }
        else if (action == "cmd_resize_web") {
            // 旧前端可能还会上报内容高度；当前版本固定 Web 窗口尺寸，忽略动态 resize。
        }
        else if (action == "cmd_copy_web_window_to_clipboard") {
            CString errorMsg;
            bool ok = (m_pWebDlg != nullptr) && m_pWebDlg->CopyWindowImageToClipboard(errorMsg);
            if (ok) {
                AppLog(L"📋 [随机工具] Web计分窗口截图已复制到剪贴板。", RGB(0, 255, 100));
                WriteMatchLog(L"[随机工具] Web计分窗口截图已复制到剪贴板。");
            }
            else {
                if (errorMsg.IsEmpty()) errorMsg = L"截图复制失败，请确认 Web 计分窗口未最小化。";
                AppLog(L"⚠️ [随机工具] " + errorMsg, RGB(255, 180, 0));
                WriteMatchLog(L"[随机工具] " + errorMsg);
                if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"copy_window_clipboard_result", errorMsg);
            }
        }
        else if (action == "cmd_auth") {
            std::string codeStr = j["code"].get<std::string>();
            CString newAuthCode = CA2W(codeStr.c_str(), CP_UTF8);

            if (m_bIsManualAuthCheck && m_cloudExpireTime == -1) {
                json reply; reply["action"] = "auth_result"; reply["success"] = false;
                reply["message"] = std::string(CW2A(L"上一条授权卡密正在云端验证中，请稍后再试。", CP_UTF8));
                CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
                if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
                return 0;
            }

            s_backupAuthCode = DnfReadLocalLicenseKey();
            s_pendingAuthCode = DnfNormalizeLicenseKey(newAuthCode);

            if (s_pendingAuthCode.IsEmpty() || !BeginLicenseCloudCheck(s_pendingAuthCode, true)) {
                DnfWriteLocalLicenseKey(s_backupAuthCode);
                s_pendingAuthCode.Empty();
                s_backupAuthCode.Empty();

                json reply; reply["action"] = "auth_result"; reply["success"] = false;
                reply["message"] = std::string(CW2A(L"授权卡密格式无效，已丢弃本次输入并保留旧授权。", CP_UTF8));
                CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
                if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
                BroadcastStateToWeb();
                return 0;
            }

            json reply; reply["action"] = "auth_result"; reply["success"] = true;
            // 🚨【关键防崩溃修复】：必须强制转为 UTF-8！
            reply["message"] = std::string(CW2A(L"🔄 已提交卡密，正在云端验证中，请稍候...", CP_UTF8));
            CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
            if (m_pWebDlg) m_pWebDlg->SendStateToWeb(jsonStr);
        }
        else if (action == "cmd_direct_sync_alias_db") {
            m_bAliasDirectMode = true;
            if (j.contains("data") && j["data"].contains("fullAliasDB") && j["data"]["fullAliasDB"].is_object()) {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                m_aliasDB.clear();
                m_aliasDbPendingDeleteMains.clear();
                for (auto it = j["data"]["fullAliasDB"].begin(); it != j["data"]["fullAliasDB"].end(); ++it) {
                    CString mainName = CA2W(it.key().c_str(), CP_UTF8);
                    CString aliases = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
                    mainName.Trim();
                    aliases.Trim();
                    CString normalizedAliases = DnfNormalizeAliasListString(aliases);
                    if (!mainName.IsEmpty()) {
                        if (!normalizedAliases.IsEmpty()) m_aliasDB[mainName] = normalizedAliases;
                        else m_aliasDbPendingDeleteMains.push_back(mainName);
                    }
                }
            }

            SaveAliasDB();
            int mainCount = 0;
            int pairCount = 0;
            std::string payload = BuildAliasDbJsonPayload(mainCount, pairCount);
            AppLog(L"☁️ [共享库] 管理员直写模式：正在同步本地小号库到公共库...", RGB(80, 220, 180));
            CString result = DirectSyncAliasDbToCloud(payload, mainCount, pairCount);
            COLORREF logColor = result.Find(L"成功") >= 0 ? RGB(0, 255, 120) : RGB(255, 120, 80);
            AppLog(L"☁️ [共享库] " + result, logColor);
            if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"alias_direct_sync_result", result);
        }
        else if (action == "cmd_set_alias_direct_mode") {
            m_bAliasDirectMode = j.value("enabled", false);
            AppLog(m_bAliasDirectMode ? L"☁️ [共享库] 管理员直写模式已开启" : L"☁️ [共享库] 管理员直写模式已关闭", RGB(80, 220, 180));
        }
        else if (action == "cmd_sync_alias_db") {
            AppLog(L"☁️ [共享库] 正在从云端公共库同步小号数据...", RGB(80, 220, 180));
            CString result = SyncAliasDbFromCloud();
            COLORREF logColor = result.Find(L"失败") >= 0 ? RGB(255, 120, 80) : RGB(0, 255, 120);
            AppLog(L"☁️ [共享库] " + result, logColor);
            if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"alias_sync_result", result);
        }
        else if (action == "cmd_push_alias_db") {
            if (j.contains("data") && j["data"].contains("fullAliasDB") && j["data"]["fullAliasDB"].is_object()) {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                m_aliasDB.clear();
                m_aliasDbPendingDeleteMains.clear();
                for (auto it = j["data"]["fullAliasDB"].begin(); it != j["data"]["fullAliasDB"].end(); ++it) {
                    CString mainName = CA2W(it.key().c_str(), CP_UTF8);
                    CString aliases = CA2W(it.value().get<std::string>().c_str(), CP_UTF8);
                    mainName.Trim();
                    aliases.Trim();
                    CString normalizedAliases = DnfNormalizeAliasListString(aliases);
                    if (!mainName.IsEmpty()) {
                        if (!normalizedAliases.IsEmpty()) {
                            m_aliasDB[mainName] = normalizedAliases;
                        }
                        else if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) == m_aliasDbPendingDeleteMains.end()) {
                            m_aliasDbPendingDeleteMains.push_back(mainName);
                        }
                    }
                }
            }
            CString result = SubmitAliasDbSnapshotIfDirty(false);
            if (m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"alias_submit_result", result);
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

            bool legacyShortAlias = DnfIsLegacyShortAliasWithoutMeta(aliasName);
            if (legacyShortAlias) {
                AppLog(L"⚠️ [旧库短ID清理] [" + mainName + L"] " + DnfLegacyShortAliasDeleteReason(aliasName), RGB(255, 180, 0));
            }

            // 1. 从场上活跃选手 (m_players) 中剥离
            for (int i = 0; i < 8; i++) {
                if (m_players[i].name == mainName) {
                    for (auto it = m_players[i].aliases.begin(); it != m_players[i].aliases.end(); ) {
                        if (DnfAliasSameStorageEntry(it->name, aliasName)) {
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
                dbAliases = DnfRemoveAliasFromAliasListString(dbAliases, aliasName);
                if (dbAliases.IsEmpty()) {
                    m_aliasDB.erase(mainName);
                    if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) == m_aliasDbPendingDeleteMains.end()) {
                        m_aliasDbPendingDeleteMains.push_back(mainName);
                    }
                }
            }

            // 3. 落地保存并刷新所有界面（这会触发 BroadcastStateToWeb 告诉网页更新成功）
            SaveAliasDB();
            SaveConfigToFile();
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
            if (m_bAliasDirectMode) {
                int mainCount = 0;
                int pairCount = 0;
                std::string payload = BuildAliasDbJsonPayload(mainCount, pairCount);
                DirectSyncAliasDbToCloud(payload, mainCount, pairCount);
            }
        }
        else if (action == "cmd_undo_event") {
            int eventId = j["id"].get<int>();
            bool ok = ToggleReviewEvent(eventId);
            if (!ok) {
                AppLog(L"⚠️ [复盘操作] 事件不存在或当前状态不可操作。", RGB(255, 180, 0));
            }
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
        else if (action == "cmd_reset_stats") {
            const bool clearPlayers = j.value("clearPlayers", false);
            {
                std::lock_guard<std::mutex> dataLock(m_dataMutex);
                m_totalScoreRed = 0;
                m_totalScoreBlue = 0;
                m_recentEvents.clear();
                ResetMatchCooldownState(L"Web重置");
                m_bRedPickFirst = false;
                ::WritePrivateProfileString(L"Settings", L"RedPickFirst", L"0", m_iniPath);
                for (int i = 0; i < 8; i++) {
                    if (clearPlayers) {
                        m_players[i].name.Empty();
                        m_players[i].aliases.clear();
                        m_players[i].team = (i < 4) ? 0 : 1;
                    }
                    m_players[i].kills = 0;
                    m_players[i].deaths = 0;
                    m_players[i].currentStreak = 0;
                    m_players[i].akCount = 0;
                }
            }
            if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
            NotifyIdentityRoundReset(clearPlayers ? L"Web端清空场上数据/重置战绩/清空复盘事件" : L"Web端重置战绩/清空复盘事件");
            m_status.SetWindowText(clearPlayers ? L"场上数据已清空！" : L"战绩已归零！");
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
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

json CDNFGameCaptureDlg::DnfBuildSharedWebStateJson()
{
    json data;
    data["blueScore"] = m_totalScoreBlue;
    data["redScore"] = m_totalScoreRed;

    data["isMonitoring"] = (m_bIsRunning == TRUE);
    data["isStartPending"] = (m_bOcrStartPending.load() == true);
    data["isFlipped"] = (m_bFlipSides == true);
    data["isMfcVisible"] = (IsWindowVisible() == TRUE);
    data["deathXAlgorithm"] = m_nDeathAlgorithmChoice;
    data["outputSeatLabelToKillFile"] = m_bOutputSeatLabelToKillFile;
    data["redPickMode"] = m_bRedPickFirst ? "first" : "second";
    data["redPickFirst"] = m_bRedPickFirst;
    data["scoreboardTextStyles"] = DnfBuildScoreboardTextStylesJson(m_iniPath);
    data["killDisplaySettings"] = DnfBuildKillDisplaySettingsJson(m_iniPath);
    data["killDisplayObsUrl"] = KILL_DISPLAY_OBS_URL_UTF8;
    data["killDisplayHttpReady"] = m_bKillDisplayHttpReady;
    data["killDisplayHttpError"] = DnfJsonUtf8(m_killDisplayHttpError);
    data["killDisplayWindowVisible"] = IsKillDisplayWindowVisible();
    data["systemFonts"] = DnfBuildInstalledFontListJson();

    bool deathPatchInstalled = false;
    wchar_t cachedImagePacks2[MAX_PATH] = { 0 };
    ::GetPrivateProfileString(L"Settings", L"ImagePacks2Path", L"", cachedImagePacks2, MAX_PATH, m_iniPath);
    CString cachedPatchDir = cachedImagePacks2;
    cachedPatchDir.Trim();
    if (!cachedPatchDir.IsEmpty()) {
        deathPatchInstalled = DnfFileExists(DnfJoinPath(cachedPatchDir, DEATH_X_PATCH_FILE_NAME));
    }
    data["deathPatchInstalled"] = deathPatchInstalled;

    data["isAuthValid"] = (m_bIsAuthValid == true);
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
    data["authText"] = std::string(CW2A(expStr, CP_UTF8));
    data["outputDir"] = std::string(CW2A(m_outputDir, CP_UTF8));

    json playersArray = json::array();
    for (int i = 0; i < 4; i++) {
        json p;
        p["team"] = 0;
        p["name"] = std::string(CW2A(m_players[i].name, CP_UTF8));
        p["kills"] = m_players[i].kills;
        p["deaths"] = m_players[i].deaths;
        p["akCount"] = m_players[i].akCount;
        p["seatLabel"] = std::string(CW2A(GetPickSeatLabelForIndex(i), CP_UTF8));
        json aliases = json::array();
        for (auto& a : m_players[i].aliases) {
            aliases.push_back(std::string(CW2A(a.name, CP_UTF8)));
        }
        p["aliases"] = aliases;
        playersArray.push_back(p);
    }

    for (int i = 4; i < 8; i++) {
        json p;
        p["team"] = 1;
        p["name"] = std::string(CW2A(m_players[i].name, CP_UTF8));
        p["kills"] = m_players[i].kills;
        p["deaths"] = m_players[i].deaths;
        p["akCount"] = m_players[i].akCount;
        p["seatLabel"] = std::string(CW2A(GetPickSeatLabelForIndex(i), CP_UTF8));
        json aliases = json::array();
        for (auto& a : m_players[i].aliases) {
            aliases.push_back(std::string(CW2A(a.name, CP_UTF8)));
        }
        p["aliases"] = aliases;
        playersArray.push_back(p);
    }
    data["players"] = playersArray;

    json recentJson = json::array();
    for (auto it = m_recentEvents.rbegin(); it != m_recentEvents.rend(); ++it) {
        const RecentEvent& ev = *it;
        json e;
        e["id"] = ev.id;
        e["time"] = std::string(CW2A(ev.timeText, CP_UTF8));
        e["triggerSide"] = std::string(CW2A(ev.triggerSide == 0 ? L"左侧死亡" : (ev.triggerSide == 1 ? L"右侧死亡" : L"未知"), CP_UTF8));
        e["killer"] = std::string(CW2A(ev.killer, CP_UTF8));
        e["dead"] = std::string(CW2A(ev.dead, CP_UTF8));
        e["killerTeam"] = ev.killerTeam;
        e["deadTeam"] = ev.deadTeam;
        e["status"] = std::string(CW2A(ev.status, CP_UTF8));
        e["statsApplied"] = ev.statsApplied;
        e["undone"] = ev.undone;
        e["algorithm"] = std::string(CW2A(ev.algorithmName, CP_UTF8));
        e["ocrSummary"] = std::string(CW2A(ev.ocrSummary, CP_UTF8));
        e["candidateSummary"] = std::string(CW2A(ev.candidateSummary, CP_UTF8));
        e["snapshotPath"] = std::string(CW2A(ev.snapshotPath, CP_UTF8));
        recentJson.push_back(e);
    }
    data["recentEvents"] = recentJson;

    json dbJson = json::object();
    for (auto const& [name, aliases] : m_aliasDB) {
        std::string utf8Name = std::string(CW2A(name, CP_UTF8));
        CString normalizedAliases = DnfNormalizeAliasListString(aliases);
        std::string utf8Aliases = std::string(CW2A(normalizedAliases, CP_UTF8));
        dbJson[utf8Name] = utf8Aliases;
    }
    data["fullAliasDB"] = dbJson;

    return data;
}

std::string CDNFGameCaptureDlg::BuildKillDisplayStatePayload()
{
    try {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        json j;
        j["action"] = "sync_state";
        j["data"] = DnfBuildSharedWebStateJson();
        return j.dump();
    }
    catch (const std::exception& e) {
        json err;
        err["action"] = "sync_state";
        err["error"] = e.what();
        err["data"] = json::object();
        return err.dump();
    }
    catch (...) {
        json err;
        err["action"] = "sync_state";
        err["error"] = "unknown";
        err["data"] = json::object();
        return err.dump();
    }
}

bool CDNFGameCaptureDlg::SaveKillDisplaySettingsPayload(const std::string& requestBody, std::string& responseBody)
{
    try {
        json incoming = json::parse(requestBody.empty() ? "{}" : requestBody);
        if (!incoming.is_object() || !incoming.contains("settings") || !incoming["settings"].is_object()) {
            responseBody = "{\"ok\":false,\"error\":\"missing settings\"}";
            return false;
        }

        DnfSaveKillDisplaySettingsJson(m_iniPath, incoming["settings"]);
        PostMessage(WM_UPDATE_ALL_UI, 0, 0);

        json response;
        response["ok"] = true;
        response["settings"] = DnfBuildKillDisplaySettingsJson(m_iniPath);
        responseBody = response.dump();
        return true;
    }
    catch (const std::exception& e) {
        json response;
        response["ok"] = false;
        response["error"] = e.what();
        responseBody = response.dump();
        return false;
    }
    catch (...) {
        responseBody = "{\"ok\":false,\"error\":\"unknown\"}";
        return false;
    }
}

void CDNFGameCaptureDlg::BroadcastStateToWeb()
{
    if (m_pWebDlg == nullptr) return;

    try {
        json j;
        j["action"] = "sync_state";
        j["data"] = DnfBuildSharedWebStateJson();

        CString jsonStr = CA2W(j.dump().c_str(), CP_UTF8);
        m_pWebDlg->SendStateToWeb(jsonStr);
        m_pWebDlg->ApplyFixedWindowHeight();
    }
    catch (...) {}
}

CString CDNFGameCaptureDlg::GetKillDisplayObsUrl() const
{
    return CString(KILL_DISPLAY_OBS_URL_W);
}

void CDNFGameCaptureDlg::OpenKillDisplayWindow()
{
    if (!m_bKillDisplayHttpReady) {
        CString msg = m_killDisplayHttpError;
        if (msg.IsEmpty()) msg = L"击杀展示页本地服务未启动。";
        AppLog(L"⚠️ [击杀展示页] " + msg, RGB(255, 180, 0));
        ShowCenteredMsgBox(msg, L"击杀展示页", MB_ICONWARNING | MB_OK);
        return;
    }

    if (m_pKillDisplayDlg == nullptr) {
        m_pKillDisplayDlg = new CKillDisplayDlg(nullptr);
        m_pKillDisplayDlg->Create(IDD_WEB_SCORE_DIALOG, nullptr);
    }

    if (m_pKillDisplayDlg) {
        m_pKillDisplayDlg->ShowWindow(SW_SHOW);
        m_pKillDisplayDlg->ShowWindow(SW_RESTORE);
        m_pKillDisplayDlg->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
        m_pKillDisplayDlg->SetForegroundWindow();
    }
}

void CDNFGameCaptureDlg::HideKillDisplayWindow()
{
    if (m_pKillDisplayDlg && ::IsWindow(m_pKillDisplayDlg->GetSafeHwnd())) {
        m_pKillDisplayDlg->ShowWindow(SW_HIDE);
    }
}

void CDNFGameCaptureDlg::ToggleKillDisplayWindow()
{
    if (IsKillDisplayWindowVisible()) {
        HideKillDisplayWindow();
    }
    else {
        OpenKillDisplayWindow();
    }
}

bool CDNFGameCaptureDlg::IsKillDisplayWindowVisible() const
{
    return m_pKillDisplayDlg &&
        ::IsWindow(m_pKillDisplayDlg->GetSafeHwnd()) &&
        m_pKillDisplayDlg->IsWindowVisible();
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
                if (legacyShortAlias) {
                    AppLog(L"⚠️ [旧库短ID清理] [" + mainName + L"] " + DnfLegacyShortAliasDeleteReason(subName), RGB(255, 180, 0));
                }

                if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
                    CString& dbAliases = m_aliasDB[mainName];
                    dbAliases = DnfRemoveAliasFromAliasListString(dbAliases, subName);
                    if (dbAliases.IsEmpty()) {
                        m_aliasDB.erase(mainName);
                        if (std::find(m_aliasDbPendingDeleteMains.begin(), m_aliasDbPendingDeleteMains.end(), mainName) == m_aliasDbPendingDeleteMains.end()) {
                            m_aliasDbPendingDeleteMains.push_back(mainName);
                        }
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
        for (const auto& token : DnfParseAliasListString(it->second)) {
            bool exists = false;
            for (const auto& alias : allAliases) {
                if (DnfAliasSameDuplicateId(alias, token)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) allAliases.push_back(token);
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
                if (DnfAliasSameDuplicateId(oa.name, na)) {
                    return otherMain + L" (小号ID互斥: " + DnfAliasDuplicateKey(na) + L")";
                }
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
                if (DnfIsLegacySeatToken(aName)) {
                    continue;
                }
                bool aDup = false;
                for (int k = 0; k < 8; k++) {
                    if (m_players[k].name == aName && k != targetIdx) { aDup = true; break; }
                    for (auto& ea : m_players[k].aliases) {
                        bool aliasClash = k == targetIdx
                            ? DnfAliasSameStorageEntry(ea.name, aName)
                            : DnfAliasSameDuplicateId(ea.name, aName);
                        if (aliasClash) { aDup = true; break; }
                    }
                }
                if (!aDup && !aName.IsEmpty()) DnfMergeAliasIntoAliasDataList(m_players[targetIdx].aliases, aName);
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
                        bool aliasClash = k == targetIdx
                            ? DnfAliasSameStorageEntry(ea.name, aN)
                            : DnfAliasSameDuplicateId(ea.name, aN);
                        if (aliasClash) { aDup = true; break; }
                    }
                }
                if (!aDup && !aN.IsEmpty()) DnfMergeAliasIntoAliasDataList(m_players[targetIdx].aliases, aN);
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
                bool aliasClash = curAIdx != -1
                    ? DnfAliasSameDuplicateId(m_players[i].aliases[j].name, newNameOnly)
                    : (m_players[i].aliases[j].name == newNameOnly);
                if (aliasClash) { isDup = true; break; }
            }
        }
        else {
            // 同一名选手内部允许：主号名称 == 自己的小号名称。
            // 但仍然禁止同一名选手的小号之间出现同 ID 同职业的重复项。
            if (curAIdx != -1) {
                for (int j = 0; j < (int)m_players[i].aliases.size(); j++) {
                    if (j != curAIdx && DnfAliasBlocksSamePlayerAlias(m_players[i].aliases[j].name, newNameOnly)) { isDup = true; break; }
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
            dbAliases = DnfRenameAliasInAliasListString(dbAliases, oldAliasName, line);
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
                m_aliasDB[newMainName] = DnfNormalizeAliasListString(m_aliasDB[oldMainName]);
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

LRESULT CDNFGameCaptureDlg::OnKillDisplayVisibilityChanged(WPARAM wParam, LPARAM lParam)
{
    BroadcastStateToWeb();
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


static bool DnfFileExists(const CString& path)
{
    DWORD attr = ::GetFileAttributes(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DnfDirExists(const CString& path)
{
    DWORD attr = ::GetFileAttributes(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static CString DnfJoinPath(const CString& a, const CString& b)
{
    CString out = a;
    out.TrimRight(L"\\/");
    return out + L"\\" + b;
}

static CString DnfGetExeDir()
{
    wchar_t exePath[MAX_PATH] = { 0 };
    ::GetModuleFileName(NULL, exePath, MAX_PATH);
    CString dir = exePath;
    int pos = dir.ReverseFind(L'\\');
    if (pos >= 0) dir = dir.Left(pos);
    return dir;
}

static bool DnfSearchImagePacks2Recursive(const CString& root, CString& outDir, int depth)
{
    if (depth < 0) return false;
    if (!DnfDirExists(root)) return false;

    CString direct = DnfJoinPath(root, L"ImagePacks2");
    if (DnfDirExists(direct)) {
        CString normalizedRoot = root;
        normalizedRoot.MakeLower();
        if (normalizedRoot.Find(L"地下城与勇士") >= 0 || normalizedRoot.Find(L"dnf") >= 0 || normalizedRoot.Find(L"neople") >= 0) {
            outDir = direct;
            return true;
        }
    }

    if (depth == 0) return false;

    CString pattern = DnfJoinPath(root, L"*");
    WIN32_FIND_DATA fd = {};
    HANDLE hFind = ::FindFirstFile(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        CString name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        CString lower = name;
        lower.MakeLower();
        if (lower == L"windows" || lower == L"program files" || lower == L"program files (x86)" ||
            lower == L"system volume information" || lower == L"$recycle.bin") {
            continue;
        }

        CString child = DnfJoinPath(root, name);
        if (DnfSearchImagePacks2Recursive(child, outDir, depth - 1)) {
            ::FindClose(hFind);
            return true;
        }
    } while (::FindNextFile(hFind, &fd));

    ::FindClose(hFind);
    return false;
}

bool CDNFGameCaptureDlg::FindDnfImagePacks2Folder(CString& outDir)
{
    outDir.Empty();

    wchar_t cached[MAX_PATH] = { 0 };
    ::GetPrivateProfileString(L"Settings", L"ImagePacks2Path", L"", cached, MAX_PATH, m_iniPath);
    CString cachedDir = cached;
    cachedDir.Trim();
    if (!cachedDir.IsEmpty() && DnfDirExists(cachedDir)) {
        outDir = cachedDir;
        return true;
    }

    wchar_t drives[512] = { 0 };
    DWORD n = ::GetLogicalDriveStrings(511, drives);
    for (wchar_t* d = drives; d && *d; d += wcslen(d) + 1) {
        CString drive = d;
        drive.TrimRight(L"\\/");

        CString candidates[] = {
            DnfJoinPath(drive, L"地下城与勇士\\ImagePacks2"),
            DnfJoinPath(drive, L"DNF\\ImagePacks2"),
            DnfJoinPath(drive, L"Neople\\DNF\\ImagePacks2"),
            DnfJoinPath(drive, L"WeGameApps\\rail_apps\\地下城与勇士\\ImagePacks2"),
            DnfJoinPath(drive, L"Program Files\\腾讯游戏\\地下城与勇士\\ImagePacks2"),
            DnfJoinPath(drive, L"Program Files (x86)\\腾讯游戏\\地下城与勇士\\ImagePacks2")
        };
        for (auto& c : candidates) {
            if (DnfDirExists(c)) {
                outDir = c;
                ::WritePrivateProfileString(L"Settings", L"ImagePacks2Path", outDir, m_iniPath);
                return true;
            }
        }
    }

    for (wchar_t* d = drives; d && *d; d += wcslen(d) + 1) {
        CString drive = d;
        drive.TrimRight(L"\\/");
        if (DnfSearchImagePacks2Recursive(drive + L"\\", outDir, 5)) {
            ::WritePrivateProfileString(L"Settings", L"ImagePacks2Path", outDir, m_iniPath);
            return true;
        }
    }

    return false;
}

static void DnfSendWebToast(CWebScoreDlg* webDlg, const CString& action, const CString& message)
{
    if (!webDlg) return;
    json reply;
    reply["action"] = std::string(CW2A(action, CP_UTF8));
    reply["success"] = false;
    reply["message"] = std::string(CW2A(message, CP_UTF8));
    CString jsonStr = CA2W(reply.dump().c_str(), CP_UTF8);
    webDlg->SendStateToWeb(jsonStr);
}

bool CDNFGameCaptureDlg::EnsureDeathPatchInstalled()
{
    CString exeDir = DnfGetExeDir();
    CString srcPatch = DnfJoinPath(exeDir, DEATH_X_PATCH_FILE_NAME);
    if (!DnfFileExists(srcPatch)) {
        CString msg;
        msg.Format(L"选择了【打补丁红蓝判断】，但 EXE 同目录没有找到：\r\n%s\r\n\r\n请先把这个 NPK 放到软件 EXE 同目录，再开始监控。", DEATH_X_PATCH_FILE_NAME);
        if (!IsWindowVisible() && m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"patch_result", msg);
        else ShowCenteredMsgBox(msg, L"缺少补丁文件", MB_ICONWARNING);
        return false;
    }

    CString imagePacks2;
    bool found = FindDnfImagePacks2Folder(imagePacks2);
    if (!found) {
        CString msg = L"选择了【打补丁红蓝判断】，但没有找到地下城与勇士\\ImagePacks2 文件夹。\r\n\r\n软件会自动搜索各磁盘并缓存路径；本次搜索失败。请确认 DNF 已安装，或把 ImagePacks2Path 写入 config.ini 的 [Settings]。";
        if (!IsWindowVisible() && m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"patch_result", msg);
        else ShowCenteredMsgBox(msg, L"未找到DNF目录", MB_ICONWARNING);
        return false;
    }

    CString dstPatch = DnfJoinPath(imagePacks2, DEATH_X_PATCH_FILE_NAME);
    if (DnfFileExists(dstPatch)) {
        AppLog(L"🧩 [补丁算法] 已检测到补丁：" + dstPatch, RGB(0, 255, 100));
        return true;
    }

    CString notice;
    notice.Format(L"选择了【打补丁红蓝判断】，当前还没有检测到补丁。\r\n\r\n第一次打补丁请先关闭游戏客户端；如果游戏正在运行，补丁复制后也需要重新上游戏才会生效。\r\n\r\n软件已自动找到游戏补丁目录：\r\n%s\r\n\r\n点击【确定】会把 EXE 同目录下的\r\n%s\r\n复制进去并继续开始监控；点击【取消】则不打补丁，也不会开始监控。", (LPCTSTR)imagePacks2, DEATH_X_PATCH_FILE_NAME);

    // C++ 端点击开始监控：使用原生“确定/取消”对话框。
    // Web 端点击开始监控：前端已经弹过确认框，这里直接执行复制，避免二次确认。
    if (!IsWindowVisible() && m_pWebDlg) {
        AppLog(L"🧩 [补丁算法] Web端已确认安装补丁，开始复制 NPK。", RGB(0, 255, 255));
    }
    else {
        int ret = ShowCenteredMsgBox(notice, L"确认安装死亡X补丁", MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_SYSTEMMODAL);
        if (ret != IDOK) {
            AppLog(L"🧩 [补丁算法] 用户取消安装补丁，本次不会开始监控。", RGB(255, 165, 0));
            return false;
        }
    }

    if (!::CopyFile(srcPatch, dstPatch, FALSE)) {
        DWORD err = ::GetLastError();
        CString msg;
        msg.Format(L"补丁复制失败。\r\n\r\n源文件：%s\r\n目标：%s\r\n错误码：%lu\r\n\r\n请尝试用管理员身份运行，或手动复制。", (LPCTSTR)srcPatch, (LPCTSTR)dstPatch, err);
        if (!IsWindowVisible() && m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"patch_result", msg);
        else ShowCenteredMsgBox(msg, L"补丁安装失败", MB_ICONWARNING);
        return false;
    }

    CString okMsg;
    okMsg.Format(L"补丁安装成功！\r\n\r\n已复制到：\r\n%s\r\n\r\n如果游戏客户端正在运行，请重新上游戏后再使用【打补丁红蓝判断】。", (LPCTSTR)dstPatch);
    AppLog(L"✅ [补丁算法] 已自动安装补丁：" + dstPatch, RGB(0, 255, 100));
    if (!IsWindowVisible() && m_pWebDlg) DnfSendWebToast(m_pWebDlg, L"patch_result", okMsg);
    else ShowCenteredMsgBox(okMsg, L"补丁安装成功", MB_ICONINFORMATION);
    return true;
}

void CDNFGameCaptureDlg::OnCbnSelchangeDeathAlgorithm()
{
    m_nDeathAlgorithmChoice = m_cmbDeathAlgorithm.GetCurSel();
    if (m_nDeathAlgorithmChoice < 0 || m_nDeathAlgorithmChoice > 1) m_nDeathAlgorithmChoice = 0;

    CString val;
    val.Format(L"%d", m_nDeathAlgorithmChoice);
    ::WritePrivateProfileString(L"Settings", L"DeathXAlgorithm", val, m_iniPath);

    CString algoNames[] = { L"大X颜色个数判断", L"打补丁红蓝判断" };
    AppLog(L"⚙️ [设置] 死亡X算法已切换为: " + algoNames[m_nDeathAlgorithmChoice], RGB(0, 255, 255));
    BroadcastStateToWeb();
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

void CDNFGameCaptureDlg::OnBnClickedAutoCropBlackBars()
{
    bool enabled = (m_chkAutoCropBlackBars.GetCheck() == BST_CHECKED);
    ::WritePrivateProfileString(L"Settings", L"AutoCropBlackBars", enabled ? L"1" : L"0", m_iniPath);
    AppLog(enabled ? L"⚙️ [设置] 已开启自动裁黑边" : L"⚙️ [设置] 已关闭自动裁黑边", RGB(0, 255, 255));

    ClearPreview();
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
    DWORD_PTR curSelData = (DWORD_PTR)-1;
    CString curSelLabel;
    if (m_cmbTargetWindow.GetCurSel() != -1) {
        int curSel = m_cmbTargetWindow.GetCurSel();
        curSelData = m_cmbTargetWindow.GetItemData(curSel);
        m_cmbTargetWindow.GetLBText(curSel, curSelLabel);
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
        if (m_cmbTargetWindow.GetItemData(i) == curSelData) {
            m_cmbTargetWindow.SetCurSel(i);
            restored = true; break;
        }
    }
    if (!restored && !curSelLabel.IsEmpty()) {
        for (int i = 0; i < m_cmbTargetWindow.GetCount(); i++) {
            CString itemText;
            m_cmbTargetWindow.GetLBText(i, itemText);
            if (itemText == curSelLabel) {
                m_cmbTargetWindow.SetCurSel(i);
                restored = true; break;
            }
        }
    }
    if (!restored && !m_lastTargetWindowName.IsEmpty()) {
        for (int i = 0; i < m_cmbTargetWindow.GetCount(); i++) {
            CString itemText;
            m_cmbTargetWindow.GetLBText(i, itemText);
            if (itemText == m_lastTargetWindowName) {
                m_cmbTargetWindow.SetCurSel(i);
                restored = true; break;
            }
        }
    }
    if (!restored) m_cmbTargetWindow.SetCurSel(0);
}

CString CDNFGameCaptureDlg::GetSelectedTargetWindowLabel()
{
    CString label;
    if (m_cmbTargetWindow.m_hWnd && m_cmbTargetWindow.GetCurSel() != -1) {
        m_cmbTargetWindow.GetLBText(m_cmbTargetWindow.GetCurSel(), label);
    }
    label.Trim();
    return label;
}

void CDNFGameCaptureDlg::SaveSelectedTargetWindowName()
{
    CString label = GetSelectedTargetWindowLabel();
    if (label.IsEmpty()) return;

    m_lastTargetWindowName = label;
    ::WritePrivateProfileString(L"Settings", L"LastTargetWindowName", m_lastTargetWindowName, m_iniPath);
}

void CDNFGameCaptureDlg::OnCbnDropdownTargetWindow() {
    RefreshTargetList(); // 每次点开下拉框，实时刷新最新的窗口列表
}

// 只在用户"确认选择并关闭下拉框"时触发，滚动期间不触发
// =============================================================
void CDNFGameCaptureDlg::OnCbnCloseupTargetWindow() {
    SaveSelectedTargetWindowName();

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
