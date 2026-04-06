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

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Gdiplus.lib")

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
ScorePointF g_scorePts[16] = {
    { 0.1594f, 0.0348f }, { 0.1922f, 0.0377f }, { 0.1761f, 0.1116f }, { 0.1957f, 0.1138f },
    { 0.2738f, 0.1127f }, { 0.2925f, 0.1127f }, { 0.3714f, 0.1104f }, { 0.3902f, 0.1138f },
    { 0.8105f, 0.0338f }, { 0.8457f, 0.0372f }, { 0.6085f, 0.1104f }, { 0.6281f, 0.1116f },
    { 0.7050f, 0.1127f }, { 0.7242f, 0.1127f }, { 0.8019f, 0.1127f }, { 0.8214f, 0.1116f },
};

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


// 【新增】：全局通用的 UI 日志输出助手，随处可用
void AppLog(const CString& msg, COLORREF color) {
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
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip)
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
    ON_MESSAGE(WM_UPDATE_OCR_DROPDOWNS, &CDNFGameCaptureDlg::OnUpdateOcrDropdowns)
    ON_MESSAGE(WM_UPDATE_ALL_UI, &CDNFGameCaptureDlg::OnUpdateAllUI)// 【新增】：绑定自定义 UI 刷新消息
    ON_MESSAGE(WM_CLOUD_AUTH_FAIL, &CDNFGameCaptureDlg::OnCloudAuthFail) // 【新增】
    ON_CBN_SELCHANGE(1010, &CDNFGameCaptureDlg::OnCbnSelchangeLeft)
    ON_CBN_SELCHANGE(1030, &CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine)


END_MESSAGE_MAP()


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


// 本地卡密格式校验
bool CDNFGameCaptureDlg::VerifyKey(CString inputKey, CString) {
    if (inputKey.Left(4) != L"DNF-") return false;

    int firstDash = 3;
    int secondDash = inputKey.Find(L'-', firstDash + 1);
    int thirdDash = inputKey.Find(L'-', secondDash + 1);

    // 情况 A：新版三段式卡密
    if (thirdDash != -1) {
        CString expStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
        CString nonceStr = inputKey.Mid(secondDash + 1, thirdDash - secondDash - 1);
        CString sigStr = inputKey.Mid(thirdDash + 1);

        long long expTime = wcstoll(expStr, NULL, 16);
        unsigned int sig = wcstoul(sigStr, NULL, 16);

        CString signData;
        signData.Format(L"%llX-%s-MySuperSecretKey2026", expTime, (LPCTSTR)nonceStr);

        std::string ansiSignData = CW2A(signData, CP_UTF8);
        if (sig != CustomSimpleHash(ansiSignData)) return false;

        return (expTime >= 0xFFFFFFF0) || ((long long)time(nullptr) <= expTime);
    }

    // 情况 B：旧版两段式卡密
    if (secondDash != -1 && thirdDash == -1) {
        CString expStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
        CString sigStr = inputKey.Mid(secondDash + 1);

        long long expTime = wcstoll(expStr, NULL, 16);
        unsigned int sig = wcstoul(sigStr, NULL, 16);

        CString signData;
        signData.Format(L"%llX-MySuperSecretKey2026", expTime);

        std::string ansiSignData = CW2A(signData, CP_UTF8);
        if (sig != CustomSimpleHash(ansiSignData)) return false;

        return (expTime >= 0xFFFFFFF0) || ((long long)time(nullptr) <= expTime);
    }
    return false;
}

CString CDNFGameCaptureDlg::CheckCloudBinding(CString key, CString hwid) {
    CString jsonStr;
    jsonStr.Format(L"{\"key\": \"%s\", \"hwid\": \"%s\"}", key, hwid);
    std::string jsonUtf8 = CW2A(jsonStr, CP_UTF8);

    HINTERNET hSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = WinHttpConnect(hSession, L"verifykey-thaovfpoib.cn-hangzhou.fcapp.run", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    CString resultMsg = L"未知请求异常";
    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        if (WinHttpSendRequest(hRequest, NULL, 0, (LPVOID)jsonUtf8.c_str(), (DWORD)jsonUtf8.length(), (DWORD)jsonUtf8.length(), 0) && WinHttpReceiveResponse(hRequest, NULL)) {
            std::string resp;
            DWORD sz = 0, dl = 0;
            while (WinHttpQueryDataAvailable(hRequest, &sz) && sz > 0) {
                std::vector<char> buf(sz + 1, 0);
                if (WinHttpReadData(hRequest, buf.data(), sz, &dl)) resp.append(buf.data(), dl);
            }

            if (resp.find("\"status\":\"ok\"") != std::string::npos) {
                resultMsg = L"OK";
            }
            else {
                size_t p1 = resp.find("\"msg\":\"");
                if (p1 != std::string::npos) {
                    p1 += 7;
                    size_t p2 = resp.find("\"", p1);
                    if (p2 != std::string::npos) {
                        // 【修复核心】：提取 UTF-8 字符串
                        std::string utf8Msg = resp.substr(p1, p2 - p1);
                        // 使用 CA2W 并指定 CP_UTF8，将其完美转换为 CString
                        resultMsg = CA2W(utf8Msg.c_str(), CP_UTF8);
                    }
                    else {
                        resultMsg = L"解析异常失败";
                    }
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

        // 基础算法校验（离线验证）
        if (!inputKey.IsEmpty() && VerifyKey(inputKey, hwid)) {
            // 离线校验通过，先允许进入软件
            m_bIsAuthValid = true;
            m_bIsTrial = false;

            // --- 第二阶段：异步云端二次校验（防多开、防封卡） ---
            // 核心：捕获当前窗口句柄，用于跨线程安全通信
            HWND hWnd = GetSafeHwnd();

            std::thread([this, hWnd, inputKey, hwid]() {
                // 在后台线程请求云端 API
                CString cloudResult = CheckCloudBinding(inputKey, hwid);

                // 如果云端返回不是 "OK"，说明卡密已被封或机器码不匹配
                if (cloudResult != L"OK" && ::IsWindow(hWnd)) {
                    // 【关键】：绝不在子线程弹窗！
                    // 创建一个堆内存字符串，把错误信息“邮寄”给主线程
                    CString* pResult = new CString(cloudResult);
                    if (!::PostMessage(hWnd, WM_CLOUD_AUTH_FAIL, 0, (LPARAM)pResult)) {
                        delete pResult; // 如果发送失败（窗口已关闭），手动回收内存
                    }
                }
                }).detach();

            return; // 离线校验成功，直接返回
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
        print(L"本地卡密记录: " + inputKey, RGB(180, 180, 180));

        int firstDash = 3;
        int secondDash = inputKey.Find(L'-', firstDash + 1);
        if (secondDash != -1) {
            CString expStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
            long long keyExpTime = wcstoll(expStr, NULL, 16);
            print(L"该卡密到期时间: " + FormatTimeStamp(keyExpTime), RGB(200, 200, 200));
        }
    }
    else {
        print(L"本地卡密记录: 未找到 (请点击下方[输入授权码]绑定)", RGB(150, 150, 150));
    }
    print(L"==================================", RGB(255, 215, 0));
}

// ============================================================================
// 初始化与窗口过程
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    m_bIsAuthValid = false;
    CheckTrialAndLicense();

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

    // 然后再执行 CreateEx ...
    CreateEx(0, cls, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100, (int)(750 * WINDOW_SCALE), (int)(760 * WINDOW_SCALE), NULL, NULL);

    InitTrayIcon();

    // 注册全局测试快捷键
    ::RegisterHotKey(m_hWnd, 8008, MOD_CONTROL, VK_F8);
    ::RegisterHotKey(m_hWnd, 8009, MOD_CONTROL, VK_F9);
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
    if ((nID & 0xFFF0) == SC_CLOSE) { if (m_bIsRunning) ShowWindow(SW_HIDE); else DoRealExit(); return; }
    if ((nID & 0xFFF0) == SC_MINIMIZE) { ShowWindow(SW_HIDE); return; }
    CWnd::OnSysCommand(nID, lParam);
}

LRESULT CDNFGameCaptureDlg::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    // 左键单击：显示主界面
    if (lParam == WM_LBUTTONUP) {
        ShowWindow(SW_SHOW);
        ShowWindow(SW_RESTORE);
        SetForegroundWindow();
    }
    // 右键单击：弹出菜单
    else if (lParam == WM_RBUTTONUP) {
        CPoint pt;
        GetCursorPos(&pt);

        CMenu m;
        m.CreatePopupMenu();
        m.AppendMenu(MF_STRING, 101, L"显示面板");

        // 【新增】：在右键菜单里加上“检查更新”选项
        m.AppendMenu(MF_STRING, 103, L"检查更新");

        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING, 102, L"完全退出");

        SetForegroundWindow();
        int cmd = m.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);

        // 处理用户的点击
        if (cmd == 101) {
            ShowWindow(SW_SHOW);
            ShowWindow(SW_RESTORE);
            SetForegroundWindow();
        }
        else if (cmd == 103) {
            // 【新增】：手动点击检查更新，开启后台线程。
            // 注意这里传入的是 false，代表“非静默模式”。
            // 这样即使用户已经是最新版，系统也会弹个窗告诉他“当前已是最新版本”，体验更好。
            std::thread([this]() {
                CheckForUpdates(false);
                }).detach();
        }
        else if (cmd == 102) {
            DoRealExit();
        }
    }
    return 0;
}

void CDNFGameCaptureDlg::DoRealExit() { m_bIsRunning = FALSE; KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4); DestroyWindow(); PostQuitMessage(0); }
void CDNFGameCaptureDlg::OnClose() { ShowWindow(SW_HIDE); }

// ============================================================================
// UI 事件响应与授权软拦截
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedStart() {
    static bool once;
    if (!once) {
        if (!m_bIsAuthValid) {
            CString msg = L"❌ 您的授权已到期或未激活!\r\n\r\n监控功能已锁定,请联系作者获取正式卡密:\r\nQQ:974294684\r\n微信:aym724794\r\n\r\n获取卡密后点击下方【输入授权码】即可激活。";
            MessageBox(msg, L"需要授权", MB_ICONWARNING);
            return;
        }

        if (m_bIsTrial && !m_bIsRunning) {
            once = true;
            CString trialMsg;
            trialMsg.Format(L"【欢迎试用 DNF 击杀统计工具】\r\n\r\n您当前处于免费试用阶段,试用结束时间:\r\n%s\r\n\r\n点击确定后将开启监控功能。", (LPCTSTR)FormatTimeStamp(m_trialEnd));
            MessageBox(trialMsg, L"试用阶段", MB_ICONINFORMATION);
        }
    }

    if (!m_bIsRunning) {
        m_bIsRunning = TRUE;
        m_btnStart.SetWindowText(L"停止监控");

        HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);

        // 如果引擎还没就绪，主动尝试激活一次
        bool shouldTryWGC = (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1);
        if (hGame && shouldTryWGC && !m_bUseWGC) {
            try {
                if (WGCCapture::IsSupported()) {
                    if (!m_pWGC) m_pWGC = new WGCCapture();
                    if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) {
                        m_bUseWGC = true;
                    }
                }
            }
            catch (...) {
                if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
            }
        }

        m_nBlankFrameCount = 0;
        m_bAlreadyPrompted = false;

        // 打印相应的状态日志
        if (m_bUseWGC) {
            AppLog(L"✅ [监控已启动] 已启用 WGC 硬件加速捕获 (零闪屏)", RGB(0, 255, 100));
        }
        else {
            if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
            if (m_nCaptureEngineChoice == 1) {
                AppLog(L"❌ [监控已启动] WGC 初始化失败,自动降级为 PrintWindow", RGB(255, 80, 80));
            }
            else if (m_nCaptureEngineChoice == 2) {
                AppLog(L"✅ [监控已启动] 用户选择 PrintWindow 兼容模式", RGB(0, 255, 100));
            }
            else if (!hGame) {
                AppLog(L"⚠️ [监控已启动] 未检测到游戏窗口,待命中...", RGB(255, 165, 0));
            }
            else {
                AppLog(L"⚠️ [监控已启动] WGC 不可用,已降级为 PrintWindow", RGB(255, 165, 0));
            }
        }

        SetTimer(1, 50, NULL);
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
}

void CDNFGameCaptureDlg::OnBnClickedInputKey() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    // 【完美修复】：静默处理文件丢失的情况
    // 如果文件被删除，这里会自动创建一个空的 txt 文件，如果还在就不做任何破坏
    CFile file;
    if (file.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite)) {
        file.Close();
    }

    // 调起记事本，此时无论如何文件都是存在的，直接打开
    ShellExecute(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);

    MessageBox(L"请在打开的 license.txt 中粘贴新卡密并保存。\r\n\r\n保存后关闭记事本，点击软件上的【应用修改】即可重新验证授权状态。", L"输入授权码", MB_ICONINFORMATION);
}

void CDNFGameCaptureDlg::OnBnClickedApply() {
    SaveConfigToFile();
    SyncDataToTree();
    // 重新执行静默检查，但不输出长串授权信息
    CheckTrialAndLicense();
    m_status.SetWindowText(L"应用修改成功");
}

void CDNFGameCaptureDlg::OnBnClickedFlip() { m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED); WriteScoreToFile(); RefreshDisplay(); }

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
        SaveConfigToFile();
        RefreshDisplay();
        WriteScoreToFile();
        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        OutputDebugAuthInfo();
        m_status.SetWindowText(L"战绩已归零！");
    }
}

void CDNFGameCaptureDlg::OnBnClickedBrowseDir() {
    // Win7 不支持 CFolderPickerDialog，用传统的 SHBrowseForFolder
    BROWSEINFO bi = { 0 };
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = L"选择输出目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

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
    }
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

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= 16) m_selectPts.clear();
        m_selectPts.push_back(CPoint(
            (int)(((float)(point.x - m_previewRect.left) / m_previewRect.Width()) * 10000.0f),
            (int)(((float)(point.y - m_previewRect.top) / m_previewRect.Height()) * 10000.0f)
        ));
        InvalidateRect(&m_previewRect, FALSE);
        if (m_selectPts.size() == 16) {
            CString res = L"ScorePointF g_scorePts[16] = {\r\n";
            for (int i = 0; i < 16; i++) {
                CString t; t.Format(L"    { %.4ff, %.4ff },\r\n", m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f); res += t;
            }
            m_editOcrResult.SetWindowText(res + L"};\r\n"); MessageBox(L"坐标已采集，见右侧框。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
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
            HDC hDC = ::GetDC(NULL); HDC hSrc = CreateCompatibleDC(hDC); HDC hDst = CreateCompatibleDC(hDC);
            if (!m_historyBmps[m_historyIdx]) m_historyBmps[m_historyIdx] = CreateCompatibleBitmap(hDC, m_w, m_h);
            HGDIOBJ os = SelectObject(hSrc, m_bmp); HGDIOBJ od = SelectObject(hDst, m_historyBmps[m_historyIdx]);
            BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
            SelectObject(hSrc, os); SelectObject(hDst, od); DeleteDC(hSrc); DeleteDC(hDst); ::ReleaseDC(NULL, hDC);
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
    SetTimer(2, 10000, NULL);
}

void CDNFGameCaptureDlg::Capture() {
    // 1. 智能判定目标句柄：测试环境拿全屏，正式环境找游戏
#if ENABLE_CLOUD_TEST_MODE
    HWND hGame = ::GetDesktopWindow();
#else
    HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
#endif

    if (!hGame) {
        if (m_pWGC && !m_bIsRunning) {
            m_pWGC->StopCapture();
            delete m_pWGC;
            m_pWGC = nullptr;
            m_bUseWGC = false;
        }
        return;
    }

    // 2. 智能激活引擎：测试环境跳过WGC(无显卡必败)，正式环境按需激活
#if !ENABLE_CLOUD_TEST_MODE
    if (!m_bUseWGC && (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1)) {
        static DWORD lastTryTime = 0;
        DWORD now = GetTickCount();
        if (now - lastTryTime > 2000) {
            lastTryTime = now;
            try {
                if (WGCCapture::IsSupported()) {
                    if (!m_pWGC) m_pWGC = new WGCCapture();
                    if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) m_bUseWGC = true;
                    else { delete m_pWGC; m_pWGC = nullptr; }
                }
            }
            catch (...) {
                if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
            }
        }
    }
#endif

    bool bNeedBlankCheck = false;
    int  capturedW = 0, capturedH = 0;
    HBITMAP hCapturedBmp = nullptr;

    // 3. WGC 捕获 (正式版专用)
    if (m_bUseWGC && m_pWGC) {
        int w = 0, h = 0;
        HBITMAP hFrame = m_pWGC->GetLatestFrame(w, h);
        if (hFrame && w > 0 && h > 0) {
            if (!m_bAlreadyPrompted && m_nCaptureEngineChoice == 0 && IsBitmapBlank(hFrame, w, h)) {
                m_nBlankFrameCount++;
                if (m_nBlankFrameCount >= 5) {
                    AppLog(L"⚠️ [捕获引擎] WGC 持续黑屏,自动降级为 PrintWindow", RGB(255, 165, 0));
                    m_bUseWGC = false;
                    m_pWGC->StopCapture();
                    m_nBlankFrameCount = 0;
                    DeleteObject(hFrame);
                    goto fallback_printwindow;
                }
            }
            else {
                m_nBlankFrameCount = 0;
            }

            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (m_bmp) DeleteObject(m_bmp);
            m_bmp = hFrame;
            m_w = w;
            m_h = h;
        }
    }
    // 4. 备用捕获引擎
    else {
    fallback_printwindow:
        {
            std::lock_guard<std::mutex> lock(g_bmpMutex);

#if ENABLE_CLOUD_TEST_MODE
            // 【云端测试环境】：强行 BitBlt 截取全屏桌面，无视播放器黑屏
            m_w = GetSystemMetrics(SM_CXSCREEN);
            m_h = GetSystemMetrics(SM_CYSCREEN);
            if (m_w > 0 && m_h > 0) {
                HDC hdcScreen = ::GetDC(NULL);
                if (!m_bmp) m_bmp = ::CreateCompatibleBitmap(hdcScreen, m_w, m_h);
                HDC hMem = ::CreateCompatibleDC(hdcScreen);
                HGDIOBJ old = ::SelectObject(hMem, m_bmp);
                ::BitBlt(hMem, 0, 0, m_w, m_h, hdcScreen, 0, 0, SRCCOPY);
                ::SelectObject(hMem, old);
                ::DeleteDC(hMem);
                ::ReleaseDC(NULL, hdcScreen);

                capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp;
                bNeedBlankCheck = false; // 测试模式永不报黑屏
            }
#else
            // 【正式环境】：正常 PrintWindow 捕获游戏窗口
            RECT rc;
            ::GetClientRect(hGame, &rc);
            m_w = rc.right - rc.left;
            m_h = rc.bottom - rc.top;
            if (m_w > 0 && m_h > 0) {
                if (!m_bmp) {
                    HDC hdc = ::GetDC(hGame);
                    m_bmp = ::CreateCompatibleBitmap(hdc, m_w, m_h);
                    ::ReleaseDC(hGame, hdc);
                }
                HDC hGameDC = ::GetDC(hGame);
                HDC hMem = ::CreateCompatibleDC(hGameDC);
                HGDIOBJ old = ::SelectObject(hMem, m_bmp);
                ::PrintWindow(hGame, hMem, 2);
                ::SelectObject(hMem, old);
                ::DeleteDC(hMem);
                ::ReleaseDC(hGame, hGameDC);

                capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp;
                bNeedBlankCheck = !m_bAlreadyPrompted;
            }
#endif
        }

        // 黑屏检测与权限弹窗 (仅正式环境生效)
        if (m_bIsRunning && bNeedBlankCheck && IsBitmapBlank(hCapturedBmp, capturedW, capturedH)) {
            m_nBlankFrameCount++;
            if (m_nBlankFrameCount >= 5) {
                m_bAlreadyPrompted = true;
                KillTimer(1);
                if (!IsRunningAsAdmin()) {
                    int ret = MessageBox(
                        L"⚠️ 检测到画面连续黑屏\r\n请尝试以管理员身份运行软件。",
                        L"权限不足", MB_ICONWARNING | MB_YESNO | MB_SYSTEMMODAL);
                    if (ret == IDYES) {
                        m_bIsRunning = FALSE; KillTimer(3);
                        if (!RelaunchAsAdmin()) MessageBox(L"自动提权失败，请手动管理员运行", L"错误", MB_ICONERROR);
                        return;
                    }
                }
                if (m_bIsRunning) SetTimer(1, 50, NULL);
            }
        }
        else if (!m_bIsRunning || bNeedBlankCheck) {
            m_nBlankFrameCount = 0;
        }
    }

    // 5. 渲染预览图逻辑
    CRect client; GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, client.right, splitY);
    float aspect = (float)m_w / (float)m_h;
    int drawW = topHalf.Width();
    int drawH = (int)(drawW / aspect);
    if (drawH > topHalf.Height()) {
        drawH = topHalf.Height();
        drawW = (int)(drawH * aspect);
    }
    int dX = topHalf.left + (topHalf.Width() - drawW) / 2;
    int dY = topHalf.top + (topHalf.Height() - drawH) / 2;
    m_previewRect = CRect(dX, dY, dX + drawW, dY + drawH);
    InvalidateRect(&topHalf, FALSE);
}

void CDNFGameCaptureDlg::CheckColorTrigger() {
    if (!m_bmp || !m_bIsRunning) return;
    COLORREF c_k[4], c_t[16];
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        HDC hMem = ::CreateCompatibleDC(NULL); HGDIOBJ old = ::SelectObject(hMem, m_bmp);
        c_k[0] = ::GetPixel(hMem, (int)(m_w * 0.187f), (int)(m_h * 0.036f)); c_k[1] = ::GetPixel(hMem, (int)(m_w * 0.157f), (int)(m_h * 0.034f));
        c_k[2] = ::GetPixel(hMem, (int)(m_w * 0.840f), (int)(m_h * 0.039f)); c_k[3] = ::GetPixel(hMem, (int)(m_w * 0.810f), (int)(m_h * 0.039f));
        for (int i = 0; i < 16; i++) c_t[i] = ::GetPixel(hMem, (int)(m_w * g_scorePts[i].x), (int)(m_h * g_scorePts[i].y));
        ::SelectObject(hMem, old); ::DeleteDC(hMem);
    }

    auto eq = [](COLORREF a, COLORREF b) { return abs(GetRValue(a) - GetRValue(b)) < 25 && abs(GetGValue(a) - GetGValue(b)) < 25 && abs(GetBValue(a) - GetBValue(b)) < 25; };
    auto mk = [&](int p1, int p2) { return (eq(c_k[p1], COLOR_BLUE) && eq(c_k[p2], COLOR_RED)) || (eq(c_k[p1], COLOR_RED) && eq(c_k[p2], COLOR_BLUE)); };
    auto mt = [&](int p1, int p2) { return (eq(c_t[p1], COLOR_BLUE) && eq(c_t[p2], COLOR_RED)) || (eq(c_t[p1], COLOR_RED) && eq(c_t[p2], COLOR_BLUE)); };

    if ((mt(0, 1) && mt(2, 3) && mt(4, 5) && mt(6, 7) || mt(8, 9) && mt(10, 11) && mt(12, 13) && mt(14, 15)) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        { std::lock_guard<std::mutex> dataLock(m_dataMutex); m_bPendingTeamScoreWin = true; }
        SetTimer(4, 120000, NULL);
    }
    if ((mk(0, 1) || mk(2, 3)) && m_bCanTrigger) {
        m_bCanTrigger = FALSE;
        int killSide = mk(0, 1) ? 0 : 1;
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
        SetTimer(2, 10000, NULL);
    }
}

// ============================================================================
// 时光回溯匹配逻辑 (略，保持原逻辑，无需修改直接保留)
// ============================================================================
void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide) {
    int killerArea = (triggerSide == 0) ? 1 : 0; int deadArea = triggerSide;
    bool killerIsLeft = (killerArea == 0);
    bool killerResolved = false, deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";
    int killerBestP = -1, killerBestA = -1, deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999; CString globalKillerName = L"";
    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999; CString globalDeadName = L"";

    struct FrameData { CString text; int frameIdx; };
    std::vector<FrameData> historyKTexts; std::vector<FrameData> historyDTexts;

    auto PushVisualLog = [&](const CString& msg, COLORREF color) {
        time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
        CString tStr; tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
        std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ tStr, color }); WriteMatchLog(msg);
        };

    std::vector<HBITMAP> historyClones;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        for (int i = 1; i <= MAX_HISTORY_FRAMES; i++) {
            int idx = (m_historyIdx - i + MAX_HISTORY_FRAMES) % MAX_HISTORY_FRAMES;
            if (m_historyBmps[idx]) {
                HDC hDC = ::GetDC(NULL); HDC hSrc = CreateCompatibleDC(hDC); HDC hDst = CreateCompatibleDC(hDC);
                HBITMAP clone = CreateCompatibleBitmap(hDC, m_w, m_h);
                HGDIOBJ os = SelectObject(hSrc, m_historyBmps[idx]); HGDIOBJ od = SelectObject(hDst, clone);
                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
                SelectObject(hSrc, os); SelectObject(hDst, od); DeleteDC(hSrc); DeleteDC(hDst); ::ReleaseDC(NULL, hDC);
                historyClones.push_back(clone);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
        for (auto& r : m_ocrRecordsLeft) if (r.hBmp) DeleteObject(r.hBmp);
        for (auto& r : m_ocrRecordsRight) if (r.hBmp) DeleteObject(r.hBmp);
        m_ocrRecordsLeft.clear(); m_ocrRecordsRight.clear(); m_viewIndexLeft = -1; m_viewIndexRight = -1;
    }
    PostMessage(WM_UPDATE_OCR_DROPDOWNS, 1, 0);

    auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller, int& outBestP, int& outBestA, int& frameScore, bool isAggressive, int frameIdx) -> bool {
        frameScore = -2; if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1) return false;
        CString logMsg; logMsg.Format(L"▶ [%s] 第%d帧提取: \"%s\"", isKiller ? L"找杀手" : L"找死者", frameIdx, (LPCTSTR)ocrResult);
        PushVisualLog(logMsg, RGB(180, 180, 180));

        int maxS = -2, bestP = -1, bestA = -1, bestRealLen = 0; std::wstring bestN = L"";
        m_dataMutex.lock();
        for (int p = 0; p < 8; p++) {
            if (m_players[p].name.IsEmpty()) continue;
            int teamPenalty = 0;
            if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) teamPenalty = 20;
            if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) teamPenalty = 20;

            int curScore = m_matcher.GetMatchScore(m_players[p].name.GetString(), ocrResult.GetString(), isAggressive);
            if (curScore == -1) { maxS = -1; break; }

            curScore -= teamPenalty;
            std::wstring curBestN = m_players[p].name.GetString(); int curBestA = -1, curRealLen = m_players[p].name.GetLength();
            for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                int as = m_matcher.GetMatchScore(m_players[p].aliases[a].name.GetString(), ocrResult.GetString(), isAggressive);
                if (as == -1) { maxS = -1; break; }
                as -= teamPenalty;
                if (as > curScore) { curScore = as; curBestN = m_players[p].aliases[a].name.GetString(); curBestA = (int)a; curRealLen = m_players[p].aliases[a].name.GetLength(); }
            }
            if (maxS == -1) break;
            if (curScore > maxS || (curScore == maxS && maxS > 0 && curRealLen > bestRealLen)) {
                maxS = curScore; bestP = p; bestA = curBestA; bestN = curBestN; bestRealLen = curRealLen;
            }
        }
        m_dataMutex.unlock();

        frameScore = maxS;
        if (maxS == -1) { PushVisualLog(L"  └ [⚠️职业干扰] 跳过本帧...", RGB(120, 120, 120)); return true; }

        int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);
        if (bestP != -1) {
            if (isKiller && maxS > globalKillerBestScore) { globalKillerBestScore = maxS; globalKillerBestP = bestP; globalKillerBestA = bestA; globalKillerPassLine = passLine; globalKillerName = bestN.c_str(); }
            else if (!isKiller && maxS > globalDeadBestScore) { globalDeadBestScore = maxS; globalDeadBestP = bestP; globalDeadBestA = bestA; globalDeadPassLine = passLine; globalDeadName = bestN.c_str(); }
        }

        if (bestP != -1 && maxS >= passLine) {
            resolved = true; finalName = bestN.c_str(); outBestP = bestP; outBestA = bestA;
            CString successLog;
            if (isAggressive) successLog.Format(L"  └ [✨二轮匹配] 强行锁定: %s (%d分)", (LPCTSTR)finalName, maxS);
            else successLog.Format(L"  └ [✔首轮匹配] 成功指向: %s (%d分)", (LPCTSTR)finalName, maxS);
            PushVisualLog(successLog, (m_players[bestP].team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255));

            m_dataMutex.lock();
            if (isKiller) lockedKillerTeam = m_players[bestP].team; else lockedDeadTeam = m_players[bestP].team;
            m_dataMutex.unlock();
        }
        else {
            CString failLog; failLog.Format(L"  └ [✖未达标] 最高 %d 分 (及格线: %d)", maxS, passLine);
            if (!isAggressive) PushVisualLog(failLog, RGB(120, 120, 120));
        }
        return false;
        };

    for (size_t i = 0; i < historyClones.size(); i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved)) break;
        HBITMAP hSnapshot = historyClones[i];
        std::future<OcrResultData> futKiller, futDead;
        if (!killerResolved) futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        if (!deadResolved) futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);

        OcrResultData resK = { L"", NULL }, resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get(); if (futDead.valid()) resD = futDead.get();
        if (!killerResolved && !resK.text.IsEmpty() && resK.text.Find(L"No text") == -1) historyKTexts.push_back({ resK.text, (int)(i + 1) });
        if (!deadResolved && !resD.text.IsEmpty() && resD.text.Find(L"No text") == -1) historyDTexts.push_back({ resD.text, (int)(i + 1) });

        int kScore = -2, dScore = -2;
        processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, false, (int)(i + 1));
        processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, false, (int)(i + 1));

        OcrResultData& resL = killerIsLeft ? resK : resD; OcrResultData& resR = killerIsLeft ? resD : resK;
        {
            std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
            if (resL.hBmp) { CString lbl; lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resL.text); m_ocrRecordsLeft.push_back({ resL.hBmp, lbl }); }
            if (resR.hBmp) { CString lbl; lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resR.text); m_ocrRecordsRight.push_back({ resR.hBmp, lbl }); }
        }
        PostMessage(WM_UPDATE_OCR_DROPDOWNS, 0, 0);
        { std::lock_guard<std::mutex> lk(m_debugMutex); m_debugOcrResult.Format(L"时光倒流 %d/%d | 杀:%s 亡:%s", (int)(i + 1), MAX_HISTORY_FRAMES, killerResolved ? finalKillerName : L"未定", deadResolved ? finalDeadName : L"未定"); }
        // 去掉 UpdateWindow，子线程强行要求同步重绘极易引发死锁闪退
        ::InvalidateRect(m_hWnd, &m_previewRect, FALSE);
    }

    if (!killerResolved && !historyKTexts.empty()) {
        PushVisualLog(L"▶ [找杀手] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyKTexts) {
            int kScore = -2; processMatch(frame.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, true, frame.frameIdx);
            if (killerResolved) break;
        }
    }

    if (!deadResolved && !historyDTexts.empty()) {
        PushVisualLog(L"▶ [找死者] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyDTexts) {
            int dScore = -2; processMatch(frame.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, true, frame.frameIdx);
            if (deadResolved) break;
        }
    }

    if (!killerResolved && globalKillerBestP != -1 && globalKillerBestScore >= (globalKillerPassLine - 20) && globalKillerBestScore >= 40) {
        killerResolved = true; killerBestP = globalKillerBestP; killerBestA = globalKillerBestA; finalKillerName = globalKillerName;
    }
    if (!deadResolved && globalDeadBestP != -1 && globalDeadBestScore >= (globalDeadPassLine - 20) && globalDeadBestScore >= 40) {
        deadResolved = true; deadBestP = globalDeadBestP; deadBestA = globalDeadBestA; finalDeadName = globalDeadName;
    }

    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex); DWORD now = GetTickCount(); bool isDup = false;
        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 20000)) { isDup = true; break; }
        }
        if (!isDup) {
            m_recentEvents.push_back({ finalKillerName, finalDeadName, now });
            m_recentEvents.erase(std::remove_if(m_recentEvents.begin(), m_recentEvents.end(), [&](const RecentEvent& ev) { return now - ev.time > 25000; }), m_recentEvents.end());

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++) if (p != killerBestP) m_players[p].currentStreak = 0;
                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                m_players[killerBestP].kills++; m_players[killerBestP].currentStreak++;
                CString displayName = m_players[killerBestP].name;
                if (killerBestA != -1 && (size_t)killerBestA < m_players[killerBestP].aliases.size()) displayName = m_players[killerBestP].aliases[killerBestA].name;

                CString actionLog; actionLog.Format(L"⚔ [击杀成功] 玩家 [%s] 拿下一击！连杀: %d", (LPCTSTR)displayName, m_players[killerBestP].currentStreak);
                PushVisualLog(actionLog, teamColor);
                if (m_players[killerBestP].currentStreak == 4) {
                    m_players[killerBestP].akCount++; m_players[killerBestP].currentStreak = 0;
                    PushVisualLog(L"🌟 [AK宣告] 恐怖如斯！玩家 [" + displayName + L"] 完成一次 AK！", RGB(255, 215, 0));
                }
                m_lastKillerTeam = m_players[killerBestP].team;
            }
            if (deadResolved && deadBestP != -1) m_players[deadBestP].deaths++;
            if (m_bPendingTeamScoreWin) {
                m_bPendingTeamScoreWin = false;
                if (m_lastKillerTeam == 0) m_totalScoreRed++; else if (m_lastKillerTeam == 1) m_totalScoreBlue++;
                for (int p = 0; p < 8; p++) m_players[p].currentStreak = 0;
                PushVisualLog(L"🏆 [结算] 局间大比分变动！所有人连击清零！", RGB(0, 255, 100));
            }
            // 【核心修复】：绝对不能在子线程调 SyncDataToTree！发消息让主线程去干！
            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
    }
    for (HBITMAP hb : historyClones) DeleteObject(hb);
}

// HTTP OCR 请求逻辑 (略，保持原逻辑)
OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hT, int nA) {
    OcrResultData ret = { L"", NULL }; if (!m_hHttpConnect) return ret;
    RECT r = (nA == 0) ? RECT{ (long)(m_w * 0.190f), (long)(m_h * 0.004f), (long)(m_w * 0.360f), (long)(m_h * 0.040f) } : RECT{ (long)(m_w * 0.655f), (long)(m_h * 0.004f), (long)(m_w * 0.815f), (long)(m_h * 0.040f) };
    int sw = r.right - r.left, sh = r.bottom - r.top, sc = 2, pa = 30, dW = sw * sc + pa * 2, dH = sh * sc + pa * 2;
    HDC hS = CreateCompatibleDC(NULL), hD = CreateCompatibleDC(NULL);
    HBITMAP hB = CreateCompatibleBitmap(GetDC()->GetSafeHdc(), dW, dH);
    SelectObject(hS, hT); SelectObject(hD, hB);
    RECT bg = { 0, 0, dW, dH }; HBRUSH wB = CreateSolidBrush(RGB(255, 255, 255)); FillRect(hD, &bg, wB); DeleteObject(wB);
    SetStretchBltMode(hD, HALFTONE); StretchBlt(hD, pa, pa, sw * sc, sh * sc, hS, r.left, r.top, sw, sh, SRCCOPY);
    BITMAP bm; GetObject(hB, sizeof(BITMAP), &bm);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB } };
    std::vector<BYTE> px(bm.bmWidth * bm.bmHeight * 4); GetDIBits(hD, hB, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);
    for (size_t i = 0; i < px.size(); i += 4) { int g = (px[i + 2] * 299 + px[i + 1] * 587 + px[i] * 114) / 1000; px[i] = px[i + 1] = px[i + 2] = (g > 90) ? 0 : 255; }
    SetDIBits(hD, hB, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);
    ret.hBmp = (HBITMAP)CopyImage(hB, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    IStream* pS = NULL; CreateStreamOnHGlobal(NULL, TRUE, &pS);
    { Bitmap b(hB, NULL); CLSID c; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &c); b.Save(pS, &c, NULL); }
    HGLOBAL hM = NULL; GetHGlobalFromStream(pS, &hM); LPVOID pDa = GlobalLock(hM); SIZE_T nS = GlobalSize(hM); DWORD b6L = 0;
    CryptBinaryToStringA((const BYTE*)pDa, (DWORD)nS, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b6L);
    std::string b6S(b6L, '\0'); CryptBinaryToStringA((const BYTE*)pDa, (DWORD)nS, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b6S[0], &b6L);
    GlobalUnlock(hM); pS->Release(); DeleteObject(hB); DeleteDC(hS); DeleteDC(hD);
    if (!b6S.empty() && b6S.back() == '\0') b6S.pop_back();
    std::string json = "{\"base64\": \"" + b6S + "\"}"; CString res = L"";
    HINTERNET hReq = WinHttpOpenRequest(m_hHttpConnect, L"POST", L"/api/ocr", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (hReq) {
        std::wstring hd = L"Content-Type: application/json\r\n"; WinHttpAddRequestHeaders(hReq, hd.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (WinHttpSendRequest(hReq, NULL, 0, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0) && WinHttpReceiveResponse(hReq, NULL)) {
            std::string rS; DWORD sz = 0, dL = 0;
            while (WinHttpQueryDataAvailable(hReq, &sz) && sz > 0) { std::vector<char> b(sz + 1, 0); if (WinHttpReadData(hReq, (LPVOID)b.data(), sz, &dL)) rS.append(b.data(), dL); }
            size_t sP = 0;
            while ((sP = rS.find("\"text\"", sP)) != std::string::npos) {
                size_t cP = rS.find(":", sP); size_t q1 = rS.find("\"", cP); size_t q2 = rS.find("\"", q1 + 1);
                if (q2 > q1) {
                    std::string t = rS.substr(q1 + 1, q2 - q1 - 1); int wL = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, NULL, 0);
                    if (wL > 0) { std::vector<wchar_t> wB(wL); MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, wB.data(), wL); res += wB.data(); }
                } sP = q2 + 1;
            }
        }
        else EnsureOcrRunning();
        WinHttpCloseHandle(hReq);
    }
    res.Replace(L"\\n", L""); res.Replace(L"\\r", L"");
    int uPos = 0;
    while ((uPos = res.Find(L"\\u", uPos)) != -1) {
        if (uPos + 5 < res.GetLength()) { CString hexStr = res.Mid(uPos + 2, 4); wchar_t wc = (wchar_t)wcstol(hexStr.GetString(), NULL, 16); res.Delete(uPos, 6); res.Insert(uPos, CString(wc)); uPos += 1; }
        else uPos += 2;
    }
    res.Replace(L"\\\"", L""); res.Trim(); ret.text = res; return ret;
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
    CString sS; sS.Format(L"============= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
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
    // 【新增】：确保 Timer 7 绝对能启动
    static bool s_bTimer7Started = false;
    if (!s_bTimer7Started) {
        SetTimer(7, 1000, NULL); // 启动 1 秒钟的心跳
        s_bTimer7Started = true;
    }

    // 【新增】：界面渲染后，延迟 2 秒偷偷检测是否有终极 ZIP 包
    static bool s_bUpdateTimerStarted = false;
    if (!s_bUpdateTimerStarted) {
        SetTimer(8, 2000, NULL);
        s_bUpdateTimerStarted = true;
    }

    // 启动后强制把自己拉到前台（解决更新后窗口不弹出的问题）
    static bool s_bBringToFrontOnce = false;
    if (!s_bBringToFrontOnce) {
        s_bBringToFrontOnce = true;

        // 绕过 Windows 前台锁：先 attach 到前台线程，再 SetForegroundWindow
        HWND hFore = ::GetForegroundWindow();
        DWORD dwFore = ::GetWindowThreadProcessId(hFore, NULL);
        DWORD dwSelf = ::GetCurrentThreadId();

        ::AttachThreadInput(dwSelf, dwFore, TRUE);
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::ShowWindow(m_hWnd, SW_RESTORE);       // 如果被最小化
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetForegroundWindow(m_hWnd);
        ::SetActiveWindow(m_hWnd);
        ::SetFocus(m_hWnd);
        ::AttachThreadInput(dwSelf, dwFore, FALSE);
    }

    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

    int splitY = max(100, r.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, r.right, splitY);
    CRect uiRect(0, splitY, r.right, r.bottom);

    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(95, L"微软雅黑");
        int row1_Y = splitY + 5;

        m_chkFlip.Create(L"翻转红蓝(蓝左红右)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            CRect(10, row1_Y, 185, row1_Y + 25), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font);

        m_btnHelp.Create(L"❓说明", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(190, row1_Y, 270, row1_Y + 25), this, 1021);
        m_btnHelp.SetFont(&m_font);

        m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER,
            CRect(280, row1_Y + 4, 420, row1_Y + 25), this, 1003);
        m_status.SetFont(&m_font);

        // 【新增】:捕获引擎选择下拉框
        m_cmbCaptureEngine.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            CRect(425, row1_Y, 560, row1_Y + 200), this, 1030);
        m_cmbCaptureEngine.SetFont(&m_font);
        m_cmbCaptureEngine.AddString(L"🔄 自动选择");
        m_cmbCaptureEngine.AddString(L"🎮 WGC捕获");
        m_cmbCaptureEngine.AddString(L"🖥️ PrintWindow");

        // 从配置文件读取上次的选择
        m_nCaptureEngineChoice = GetPrivateProfileInt(
            L"Settings", L"CaptureEngine", 0, m_iniPath);
        if (m_nCaptureEngineChoice < 0 || m_nCaptureEngineChoice > 2)
            m_nCaptureEngineChoice = 0;
        m_cmbCaptureEngine.SetCurSel(m_nCaptureEngineChoice);


        int cmbW = 150;
        m_cmbRight.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            CRect(r.right - 10 - cmbW, row1_Y, r.right - 10, row1_Y + 300), this, 1009);
        m_cmbRight.SetFont(&m_font);
        m_cmbRight.AddString(L"[蓝]自动");
        m_cmbRight.SetCurSel(0);

        m_cmbLeft.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            CRect(r.right - 10 - cmbW * 2 - 10, row1_Y, r.right - 10 - cmbW - 10, row1_Y + 300), this, 1010);
        m_cmbLeft.SetFont(&m_font);
        m_cmbLeft.AddString(L"[红]自动");
        m_cmbLeft.SetCurSel(0);

        int halfW = (r.right - 30) / 2;
        int row2_Y = row1_Y + 30;
        int row2_Bottom = r.bottom - (int)(75 * WINDOW_SCALE);
        int scoreH = (int)(150 * WINDOW_SCALE);

        m_cmbTeamSelect.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            CRect(10, row2_Y + 10, 80, row2_Y + 150), this, 1024);
        m_cmbTeamSelect.SetFont(&m_font);
        if (m_cmbTeamSelect.GetCount() == 0) {
            m_cmbTeamSelect.AddString(L"[红队]");
            m_cmbTeamSelect.AddString(L"[蓝队]");
            m_cmbTeamSelect.SetCurSel(0);
        }

        m_editQuickAdd.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
            CRect(90, row2_Y, halfW - 60, row2_Y + 45), this, 1025);
        m_editQuickAdd.SetFont(&m_font);
        m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT);

        m_btnQuickAdd.Create(L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(halfW - 55, row2_Y, 10 + halfW, row2_Y + 45), this, 1022);
        m_btnQuickAdd.SetFont(&m_font);

        m_treePlayers.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
            CRect(10, row2_Y + 55, 10 + halfW, row2_Bottom), this, 1023);
        m_treePlayers.SetFont(&m_font);

        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            CRect(20 + halfW, row2_Y, r.right - 10, row2_Y + scoreH), this, 1002);
        m_editOcrResult.SetFont(&m_font);

        m_editVisualLogs.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            CRect(20 + halfW, row2_Y + scoreH + 5, r.right - 10, row2_Bottom), this, 1011);
        m_editVisualLogs.SetFont(&m_font);
        m_editVisualLogs.SetBackgroundColor(FALSE, RGB(30, 30, 30));

        int btnY = row2_Bottom + 8;
        int btnH = (int)(28 * WINDOW_SCALE);
        int bW = (r.right - 40) / 3;

        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(10, btnY, 10 + bW, btnY + btnH), this, ID_BTN_START);
        m_btnStart.SetFont(&m_font);

#ifdef _DEBUG
        CString strApplyBtn = L"应用修改";
        CString strResetBtn = L"战绩归零(Ctrl断试用)";
#else
        CString strApplyBtn = L"应用修改";
        CString strResetBtn = L"战绩归零";
#endif
        m_btnApply.Create(strApplyBtn, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(20 + bW, btnY, 20 + bW * 2, btnY + btnH), this, ID_BTN_APPLY);
        m_btnApply.SetFont(&m_font);

        m_btnReset.Create(strResetBtn, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(30 + bW * 2, btnY, r.right - 10, btnY + btnH), this, ID_BTN_RESET);
        m_btnReset.SetFont(&m_font);

        int dirY = btnY + btnH + 5;
        int rightBtnW = 110;

        m_editOutDir.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
            CRect(10, dirY, r.right - (rightBtnW * 2) - 30, dirY + btnH), this, ID_EDIT_DIR);
        m_editOutDir.SetFont(&m_font);
        m_editOutDir.SetWindowText(m_outputDir);

        m_btnBrowseDir.Create(L"更改目录", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(r.right - (rightBtnW * 2) - 20, dirY, r.right - rightBtnW - 20, dirY + btnH), this, ID_BTN_BROWSE);
        m_btnBrowseDir.SetFont(&m_font);

        m_btnInputKey.Create(L"输入授权码", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(r.right - rightBtnW - 10, dirY, r.right - 10, dirY + btnH), this, ID_BTN_INPUT_KEY);
        m_btnInputKey.SetFont(&m_font);

        static bool configLoadedonce = false;
        if (!configLoadedonce) {
            LoadConfigFromFile();
            LoadAliasDB();
            configLoadedonce = true;
        }

        SyncDataToTree();
        RefreshDisplay();
        WriteScoreToFile();
        OutputDebugAuthInfo();

        std::thread([this]() {
            CheckForUpdates(true);
            }).detach();

        // 【新增】：开启全局日志刷新定时器（每 100 毫秒刷新一次日志面板）
        SetTimer(5, 100, NULL);

        // 【新增】:开启游戏画面预览定时器(窗口创建即生效,不依赖"开始监控")
        SetTimer(6, 50, NULL);
    }

    dc.FillSolidRect(&uiRect, GetSysColor(COLOR_BTNFACE));
    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&dc, topHalf.Width(), topHalf.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    memDC.FillSolidRect(0, 0, topHalf.Width(), topHalf.Height(), RGB(15, 15, 15));

    if (m_w > 0 && m_h > 0) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);

            memDC.SetStretchBltMode(HALFTONE);
            memDC.StretchBlt(m_previewRect.left, m_previewRect.top,
                m_previewRect.Width(), m_previewRect.Height(),
                CDC::FromHandle(hBmpDC), 0, 0, m_w, m_h, SRCCOPY);

            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }
    }

    Draw(memDC);
    dc.BitBlt(0, 0, topHalf.Width(), topHalf.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
}

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;
    CPen p1(PS_SOLID, 2, RGB(255, 0, 0)), p3(PS_SOLID, 2, RGB(255, 165, 0)); dc.SelectStockObject(NULL_BRUSH); dc.SelectObject(&p1);
    float pX[4] = { 0.187f, 0.157f, 0.840f, 0.810f }; float pY[4] = { 0.036f, 0.034f, 0.039f, 0.039f };
    for (int i = 0; i < 4; i++) dc.Ellipse(m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) - 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) - 5, m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) + 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) + 5);
    dc.SelectObject(&p3);
    for (int i = 0; i < 16; i++) dc.Ellipse(m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) - 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) - 5, m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) + 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) + 5);
    CString h; { std::lock_guard<std::mutex> lk(m_debugMutex); h = m_debugOcrResult; }
    if (!h.IsEmpty()) {
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(0, 255, 0)); CFont f; f.CreatePointFont(105, L"黑体"); CFont* of = dc.SelectObject(&f);
        CRect tR(0, 0, 0, 0); dc.DrawText(h, &tR, DT_LEFT | DT_TOP | DT_CALCRECT);
        CRect cr(m_previewRect.left + 15, m_previewRect.bottom - 25 - tR.Height(), m_previewRect.left + 15 + tR.Width(), m_previewRect.bottom - 25); cr.InflateRect(8, 8);
        dc.FillSolidRect(&cr, RGB(25, 25, 25)); dc.DrawText(h, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); dc.SelectObject(of);
    }
    HBITMAP hL = NULL, hR = NULL;
    {
        std::lock_guard<std::mutex> lkBmp(m_ocrRecordMutex);
        if (m_viewIndexLeft >= 0 && m_viewIndexLeft < (int)m_ocrRecordsLeft.size()) hL = m_ocrRecordsLeft[m_viewIndexLeft].hBmp; else if (!m_ocrRecordsLeft.empty()) hL = m_ocrRecordsLeft.back().hBmp;
        if (m_viewIndexRight >= 0 && m_viewIndexRight < (int)m_ocrRecordsRight.size()) hR = m_ocrRecordsRight[m_viewIndexRight].hBmp; else if (!m_ocrRecordsRight.empty()) hR = m_ocrRecordsRight.back().hBmp;
    }
    HBITMAP arr[2] = { hL, hR }; int cY = m_previewRect.bottom - 20; int tW = max(180, m_previewRect.Width() / 4);
    for (int i = 1; i >= 0; i--) {
        if (arr[i]) {
            BITMAP bm; GetObject(arr[i], sizeof(BITMAP), &bm);
            int sW = (int)(bm.bmWidth * 0.70); int sH = bm.bmHeight; int sX = (i == 0) ? 0 : (bm.bmWidth - sW); int dW = tW; int dH = (int)((float)sH / sW * dW); cY -= dH;
            int iX = m_previewRect.right - 15 - dW; HDC hM = CreateCompatibleDC(dc.GetSafeHdc()); HGDIOBJ oB = SelectObject(hM, arr[i]); COLORREF bC = (i == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
            dc.FillSolidRect(iX - 2, cY - 2, dW + 4, dH + 4, bC); dc.SetStretchBltMode(HALFTONE); dc.StretchBlt(iX, cY, dW, dH, CDC::FromHandle(hM), sX, 0, sW, sH, SRCCOPY);
            dc.SetBkMode(TRANSPARENT); dc.SetTextColor(bC); CFont fM; fM.CreatePointFont(90, L"微软雅黑"); CFont* oM = dc.SelectObject(&fM); dc.TextOut(iX, cY - 18, i == 0 ? L"左侧提取区" : L"右侧提取区"); dc.SelectObject(oM);
            cY -= 25; SelectObject(hM, oB); DeleteDC(hM);
        }
    }
}

// 【修改】：点击说明按钮弹出的消息框，详细更新功能手册
void CDNFGameCaptureDlg::OnBnClickedHelp() {
    CString msg = L"💡 树状战绩管理核心操作说明\r\n\r\n"
        L"【一、 录入与补全】\r\n"
        L"1. 批量添加：输入框支持“主号(小号1)(小号2)”格式，按回车或点击[添加]解析入库。\r\n"
        L"2. 智能补全：输入主号后打出左括号“(”，系统会自动去“数据库”里检索并补齐小号。\r\n\r\n"
        L"【二、 右键菜单功能 (极其强大)】\r\n"
        L"1. 队伍操作：在【红/蓝队】根节点右键，可修改队伍大比分，或一键清空该队全员。\r\n"
        L"2. 主号操作：在[主号]节点右键，可加减战绩，或将玩家连带小号【一键移动】到对面阵营（如果对面满员，会自动弹出替换互换菜单）。\r\n"
        L"3. 小号操作：在[小号]节点右键，可以仅从本局移除，或者选择【彻底删除】（连同自动补全数据库里的记忆一并抹除）。\r\n\r\n"
        L"【三、 快捷修改与交互】\r\n"
        L"1. 直接改名：左键单击选中某个主号或小号，再点一下名字，即可像重命名文件一样修改名字或战绩数值。\r\n"
        L"2. 展开折叠：节点前有 [+] / [-] 可以自由隐藏或显示小号，让界面更清爽。\r\n"
        L"------------------------------------\r\n"
        L"🛠️ 调试模式快捷键：\r\n"
        L"Ctrl+F8 : 强制触发【红队】击杀识图\r\n"
        L"Ctrl+F9 : 强制触发【蓝队】击杀识图";

    MessageBox(msg, L"最新操作逻辑与指南", MB_ICONINFORMATION);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nID) {
    if (nID == 1 && m_bIsRunning) {
        Capture();
        CheckColorTrigger();
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
        if (m_bmp) {
            HDC hDC = ::GetDC(NULL);
            HDC hSrc = CreateCompatibleDC(hDC);
            HDC hDst = CreateCompatibleDC(hDC);
            if (!m_historyBmps[m_historyIdx])
                m_historyBmps[m_historyIdx] = CreateCompatibleBitmap(hDC, m_w, m_h);
            HGDIOBJ os = SelectObject(hSrc, m_bmp);
            HGDIOBJ od = SelectObject(hDst, m_historyBmps[m_historyIdx]);
            BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
            SelectObject(hSrc, os);
            SelectObject(hDst, od);
            DeleteDC(hSrc);
            DeleteDC(hDst);
            ::ReleaseDC(NULL, hDC);
            m_historyIdx = (m_historyIdx + 1) % MAX_HISTORY_FRAMES;
        }
    }
    else if (nID == 5) {
        std::lock_guard<std::mutex> lkLog(g_visualLogMutex);
        if (!g_visualLogs.empty() && m_editVisualLogs.m_hWnd) {
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
    // 【Timer 6】:独立预览定时器
    // 软件打开就自动显示画面，不依赖"开始监控"
    // ==========================================
    else if (nID == 6) {
        if (!m_bIsRunning) {
            // 只在非监控状态下执行预览截图
            // 监控状态下由 Timer 1 接管，避免重复截图
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
    const CString BRIDGE_VERSION = L"2.2.0";  // 桥接版本号

    if (currentVersion == BRIDGE_VERSION) {
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        AppLog(L"🔄 [桥接升级] 检测到这是过渡版本", RGB(255, 215, 0));
        AppLog(L"   正在自动升级到最新正式版,请稍候...", RGB(255, 215, 0));
        AppLog(L"   升级完成后软件会自动重启", RGB(255, 215, 0));
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        Sleep(800);  // 让用户有时间看到提示
        DownloadAndApplyUpdate(downloadUrl);
    }
    else if (bSilent) {
        // 【普通版 - 后台静默检测】：不打扰用户,只在日志里提示有新版本
        CString logMsg;
        logMsg.Format(L"💡 [发现新版本] 服务器版本 %s,点击菜单可手动更新", serverVersion.GetString());
        AppLog(logMsg, RGB(100, 200, 255));
        // 注意:这里不调用 DownloadAndApplyUpdate,等用户主动点"检查更新"
    }
    else {
        // 【普通版 - 用户手动点"检查更新"】：弹窗让用户自己选
        CString msg;
        msg.Format(L"发现新版本:%s\n\n更新内容:\n%s\n\n是否立即更新?", serverVersion, updateLog);
        if (MessageBox(msg, L"发现新版本", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            DownloadAndApplyUpdate(downloadUrl);
        }
    }
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

            m_aliasDB.clear(); // 【关键】：先清空内存，防止重复加载

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

                    mainName.Trim(); // 【关键清洗】：洗掉隐形空格！
                    aliases.Trim();

                    if (!mainName.IsEmpty()) {
                        // std::map 自带去重属性，同名会自动覆盖，留下最新的
                        m_aliasDB[mainName] = aliases;
                    }
                }
            }
        }
        file.Close();
    }
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
}

void CDNFGameCaptureDlg::OnChangeEditNamesInput() {
    static int s_prevLen = 0;
    static CString s_lastAutoExpandedName = L""; // 记忆：上次因为打字而自动展开的主号名

    int curLen = m_editQuickAdd.GetWindowTextLength();
    bool isBackspace = (curLen < s_prevLen);
    s_prevLen = curLen;

    int nStart, nEnd;
    m_editQuickAdd.GetSel(nStart, nEnd);

    CString fullText;
    m_editQuickAdd.GetWindowText(fullText);

    // ==========================================
    // 1. 安全提取：当前光标所在行的文本 (修复了旧版换行符引发的错位BUG)
    // ==========================================
    int lineStart = 0;
    for (int i = nStart - 1; i >= 0; i--) {
        if (fullText[i] == L'\n') { lineStart = i + 1; break; }
    }
    int lineEnd = fullText.GetLength();
    for (int i = nStart; i < fullText.GetLength(); i++) {
        if (fullText[i] == L'\r' || fullText[i] == L'\n') { lineEnd = i; break; }
    }
    CString currentLine = fullText.Mid(lineStart, lineEnd - lineStart);

    // 解析出正在输入的主名 (去掉括号和后面的内容)
    int fP = currentLine.Find(L'(');
    if (fP == -1) fP = currentLine.Find(L'（');
    CString typingMainName = (fP != -1) ? currentLine.Left(fP) : currentLine;
    typingMainName.Trim();

    // ==========================================
    // 2. 满员智能跳转：自动将下拉框切到未满员的队伍
    // ==========================================
    int redCount = 0, blueCount = 0;
    m_dataMutex.lock();
    for (int i = 0; i < 4; i++) if (!m_players[i].name.IsEmpty()) redCount++;
    for (int i = 4; i < 8; i++) if (!m_players[i].name.IsEmpty()) blueCount++;
    m_dataMutex.unlock();

    if (redCount >= 4 && blueCount < 4) {
        m_cmbTeamSelect.SetCurSel(1); // 红队满，切蓝队
    }
    else if (blueCount >= 4 && redCount < 4) {
        m_cmbTeamSelect.SetCurSel(0); // 蓝队满，切红队
    }

    // ==========================================
    // 3. 树状图状态跟踪与下拉框强覆盖 (核心视觉反馈)
    // ==========================================
    // 定义一个极简的辅助函数：专门用来展开或收缩指定的节点
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

                if (nodeName == targetName) {
                    m_treePlayers.Expand(hChild, action);
                    return;
                }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
        };

    // 去战局里找找，当前打字的名字是不是已经在场上了
    int foundTeam = -1;
    m_dataMutex.lock();
    for (int i = 0; i < 8; i++) {
        if (!m_players[i].name.IsEmpty() && m_players[i].name == typingMainName) {
            foundTeam = m_players[i].team;
            break;
        }
    }
    m_dataMutex.unlock();

    // 状态分发
    if (foundTeam != -1) {
        // 【命中老玩家】：无视队伍满不满，强行把下拉框切到他所在的队
        m_cmbTeamSelect.SetCurSel(foundTeam);

        // 发现新目标：展开节点，并记在脑子里
        if (s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(typingMainName, TVE_EXPAND);
            s_lastAutoExpandedName = typingMainName;
        }
    }
    else {
        // 【未命中或被删除了】：把刚才自动展开的节点收起来
        if (!s_lastAutoExpandedName.IsEmpty() && s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(s_lastAutoExpandedName, TVE_COLLAPSE);
            s_lastAutoExpandedName = L""; // 清除记忆
        }
    }

    // ==========================================
    // 4. 括号探测：小号智能补全拦截区
    // ==========================================
    // 如果是按退格键、或者光标异常，立即拦截，不再执行补全
    if (isBackspace || nStart == 0 || nStart > fullText.GetLength()) return;

    // 没打括号，立即拦截
    wchar_t lastChar = fullText.GetAt(nStart - 1);
    if (lastChar != L'(' && lastChar != L'（') return;

    // 名字为空或数据库里没有记录，立即拦截
    if (typingMainName.IsEmpty() || m_aliasDB.find(typingMainName) == m_aliasDB.end()) return;

    CString dbAliases = m_aliasDB[typingMainName];
    std::vector<CString> existAliases;

    // 查一下树状图里已经有这个人的哪些小号了
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

    // 补齐小号并移动光标
    if (!aliasesToInsert.IsEmpty()) {
        m_editQuickAdd.SetSel(nStart - 1, nStart);
        m_editQuickAdd.ReplaceSel(aliasesToInsert);
        s_prevLen = m_editQuickAdd.GetWindowTextLength();
    }
}

// ============================================================================
// 新版 GUI 核心逻辑：添加、树状渲染、右键菜单、存取配置
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedQuickAdd() {
    CString input;
    m_editQuickAdd.GetWindowText(input);
    input.Trim();
    if (input.IsEmpty() || input == PLACEHOLDER_TEXT) return;

    input.Replace(L"\r\n", L"\n");
    int start = 0;
    int currentTeam = m_cmbTeamSelect.GetCurSel();
    int lastIdx = -1;

    int addMainCount = 0, addAliasCount = 0, dupFilteredCount = 0;
    CString strTeamFullAlert = L"", strDupAliasAlert = L"";

    std::vector<CString> currentAdded; // 【新增追踪器】：精准记录本次操作被修改的主号

    std::lock_guard<std::mutex> lk(m_dataMutex);
    AppLog(L"================================", RGB(150, 150, 150));
    AppLog(L"📥 [系统] 开始解析导入名单数据...", RGB(255, 215, 0));

    while (start < input.GetLength()) {
        int nl = input.Find(L'\n', start);
        CString line = (nl != -1) ? input.Mid(start, nl - start) : input.Mid(start);
        start = (nl != -1) ? nl + 1 : input.GetLength();
        line.Trim(); if (line.IsEmpty()) continue;

        if (line.Find(L"操作说明") != -1 || line.Find(L"分队：") != -1 || line.Find(L"绑定小号：") != -1 ||
            line.Find(L"手动改分") != -1 || line.Find(L"手动改AK") != -1 || line.Find(L"💡") != -1) continue;

        if (line.Find(L"红") != -1 && line.Find(L"蓝") != -1 && (line.Find(L":") != -1 || line.Find(L"：") != -1)) {
            int rPos = line.Find(L"红"), bPos = line.Find(L"蓝"), cPos = line.Find(L":"); if (cPos == -1) cPos = line.Find(L"：");
            if (cPos != -1 && rPos < cPos && cPos < bPos) {
                m_totalScoreRed = _wtoi(line.Mid(rPos + 1, cPos - rPos - 1));
                m_totalScoreBlue = _wtoi(line.Mid(cPos + 1, bPos - cPos - 1));
                AppLog(L"📌 [比分修改] 识别并修改全局比分", RGB(200, 200, 200));
            }
            continue;
        }

        if (line.Find(L"【红队】") != -1) { currentTeam = 0; continue; }
        if (line.Find(L"【蓝队】") != -1) { currentTeam = 1; continue; }

        if (line.Left(1) == L"└" || line.Left(1) == L"├" || line.Left(1) == L"+") {
            if (lastIdx != -1) {
                CString aN = line.Mid(1); aN.Trim(); bool isDup = false;
                for (int i = 0; i < 8 && !isDup; i++) {
                    if (m_players[i].name == aN) { isDup = true; break; }
                    for (const auto& ea : m_players[i].aliases) { if (ea.name == aN) { isDup = true; break; } }
                }
                if (!isDup && !aN.IsEmpty()) {
                    m_players[lastIdx].aliases.push_back({ aN }); addAliasCount++;
                    currentAdded.push_back(m_players[lastIdx].name); // 🎯标记
                    AppLog(L" ├ ➕追加小号: [" + aN + L"]", RGB(100, 255, 100));
                }
                else if (isDup) { dupFilteredCount++; strDupAliasAlert += L"[" + aN + L"] "; }
            }
            continue;
        }

        int eP = line.Find(L'='); if (eP == -1) eP = line.Find(L'＝'); CString namePart = (eP != -1) ? line.Left(eP) : line; namePart.Trim();
        int fP = namePart.Find(L'('); if (fP == -1) fP = namePart.Find(L'（'); CString mainName = (fP != -1) ? namePart.Left(fP) : namePart; mainName.Trim();
        int targetIdx = -1;

        for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { targetIdx = i; break; } }

        // =========================================================
        // 【核心修改】：智能识别主号队伍。不再强制物理搬家！
        // 如果主号已存在，无论当前选择了什么队伍，都自动追加到他原本的队伍中
        // =========================================================
        if (targetIdx != -1) {
            if (m_players[targetIdx].team != currentTeam) {
                CString teamNameStr = (m_players[targetIdx].team == 0) ? L"红队" : L"蓝队";
                AppLog(L"💡 [智能归属] 主号 [" + mainName + L"] 已在" + teamNameStr + L"，自动追加至该队", RGB(0, 255, 255));
            }
            // 标记为已修改，为了稍后的自动展开功能
            currentAdded.push_back(mainName);
        }

        if (targetIdx == -1) {
            bool isAliasElsewhere = false;
            for (int i = 0; i < 8 && !isAliasElsewhere; i++) { for (const auto& a : m_players[i].aliases) { if (a.name == mainName) { isAliasElsewhere = true; break; } } }
            if (isAliasElsewhere) { strDupAliasAlert += L"[" + mainName + L"](被占) "; continue; }
            int sI = (currentTeam == 0) ? 0 : 4, eI = (currentTeam == 0) ? 4 : 8;
            for (int i = sI; i < eI; i++) {
                if (m_players[i].name.IsEmpty()) {
                    targetIdx = i; m_players[i].name = mainName; m_players[i].team = currentTeam; addMainCount++;
                    currentAdded.push_back(mainName); // 🎯标记
                    AppLog(L"👤 [新增主号] [" + mainName + L"]", RGB(80, 180, 255)); break;
                }
            }
            if (targetIdx == -1) { strTeamFullAlert += L"[" + mainName + L"](满) "; }
        }

        if (targetIdx != -1) {
            lastIdx = targetIdx;
            if (fP != -1) {
                CString aR = namePart.Mid(fP); int c = 0;
                while (true) {
                    CString tS = aR.Mid(c); int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（'); int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
                    if (Lr == -1 || Rr == -1 || Rr <= Lr) break;
                    CString aN = tS.Mid(Lr + 1, Rr - Lr - 1); aN.Trim(); bool isDup = false;
                    for (int i = 0; i < 8 && !isDup; i++) { if (m_players[i].name == aN) { isDup = true; break; } for (const auto& ea : m_players[i].aliases) { if (ea.name == aN) { isDup = true; break; } } }
                    if (!isDup && !aN.IsEmpty()) {
                        m_players[targetIdx].aliases.push_back({ aN }); addAliasCount++;
                        currentAdded.push_back(m_players[targetIdx].name); // 🎯标记
                        AppLog(L" ├ ➕追加小号: [" + aN + L"]", RGB(100, 255, 100));
                    }
                    else if (isDup) { dupFilteredCount++; strDupAliasAlert += L"[" + aN + L"] "; }
                    c += Rr + 1;
                }
            }
            if (eP != -1) {
                CString scorePart = line.Mid(eP + 1); scorePart.Trim(); int aP = scorePart.Find(L'A');
                if (aP != -1) { m_players[targetIdx].akCount = _wtoi(scorePart.Mid(aP + 1)); if (m_players[targetIdx].akCount == 0) m_players[targetIdx].akCount = 1; scorePart = scorePart.Left(aP); }
                int sl = scorePart.Find(L'/'); if (sl == -1) sl = scorePart.Find(L'-');
                if (sl != -1) { m_players[targetIdx].kills = _wtoi(scorePart.Left(sl)); m_players[targetIdx].deaths = _wtoi(scorePart.Mid(sl + 1)); }
            }
        }
    }

    CString summaryLog; summaryLog.Format(L"✅ [解析完毕] 新增:%d | 追加:%d | 过滤:%d", addMainCount, addAliasCount, dupFilteredCount); AppLog(summaryLog, RGB(0, 255, 255));

    if (!strTeamFullAlert.IsEmpty() || !strDupAliasAlert.IsEmpty()) {
        CString alertMsg = L"遇到以下拦截：\n\n";
        if (!strTeamFullAlert.IsEmpty()) alertMsg += L"🛑 队伍已满：" + strTeamFullAlert + L"\n";
        if (!strDupAliasAlert.IsEmpty()) alertMsg += L"⚠️ 名字冲突：" + strDupAliasAlert + L"\n";
        MessageBox(alertMsg, L"添加结果提示", MB_ICONWARNING | MB_OK);
    }
    else { m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT); }

    SaveAliasDB();
    SaveConfigToFile();
    WriteScoreToFile();

    SyncDataToTree(); // 同步到树状图
    RefreshDisplay();

    // ==========================================
    // 【新增核心】：遍历树状图，只展开刚刚修改过的主号
    // ==========================================
    if (currentAdded.size() > 0) {
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                CString text = m_treePlayers.GetItemText(hChild);
                int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                CString name = (eqPos != -1) ? text.Left(eqPos) : text;
                name.Trim();

                for (const auto& addedName : currentAdded) {
                    if (name == addedName) {
                        m_treePlayers.Expand(hChild, TVE_EXPAND);
                        break;
                    }
                }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
    }
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

    CString redTitle; redTitle.Format(L"【红队】  -  %d 分", m_totalScoreRed);
    CString blueTitle; blueTitle.Format(L"【蓝队】  -  %d 分", m_totalScoreBlue);

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
            menu.AppendMenu(MF_STRING, 3, L"➕ 战绩：击杀 +1");
            menu.AppendMenu(MF_STRING, 4, L"➖ 战绩：死亡 +1");
            menu.AppendMenu(MF_STRING, 5, L"🌟 战绩：AK +1");
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
            else if (cmd == 4) { m_players[data].deaths++; }
            else if (cmd == 5) { m_players[data].akCount++; }
            else if (cmd == 6) {
                m_players[data].kills = 0;
                m_players[data].deaths = 0;
                m_players[data].akCount = 0;
            }

            SaveAliasDB();
            SyncDataToTree();
            WriteScoreToFile();
            RefreshDisplay();
            SaveConfigToFile();
        }
    }
    *pResult = 0;
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

    // 处理 UTF-8 BOM 头
    char* start = buf;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        start += 3;
    }

    CString content = CA2W(start, CP_UTF8);
    delete[] buf;

    int pos = 0;
    int currentTeamContext = 0; // 记忆纯文本格式下的当前队伍，默认红队(0)

    while (pos < content.GetLength()) {
        int nl = content.Find(L'\n', pos);
        CString line = (nl != -1) ? content.Mid(pos, nl - pos) : content.Mid(pos);
        pos = (nl != -1) ? nl + 1 : content.GetLength();

        line.Remove(L'\r');
        line.Trim();

        if (line.IsEmpty()) continue;

        // ==========================================
        // 【新增】：垃圾文案过滤器，拦截说明文本
        // ==========================================
        if (line.Find(L"操作说明") != -1 ||
            line.Find(L"分队：") != -1 ||
            line.Find(L"绑定小号：") != -1 ||
            line.Find(L"手动改分") != -1 ||
            line.Find(L"手动改AK") != -1 ||
            line.Find(L"💡") != -1) {
            continue; // 遇到说明书文本直接跳过
        }

        // ==========================================
        // 兼容层 1：识别纯文本格式的队伍 Header 标签
        // ==========================================
        if (line.Find(L"【红队】") != -1) {
            currentTeamContext = 0;
            continue;
        }
        if (line.Find(L"【蓝队】") != -1) {
            currentTeamContext = 1;
            continue;
        }

        // ==========================================
        // 兼容层 2：处理结构化 `|` 分隔格式（含新老两版）
        // ==========================================
        if (line.Find(L"|") != -1) {
            std::vector<CString> tokens;
            int splitPos = 0; CString token;
            while (AfxExtractSubString(token, line, splitPos, L'|')) {
                tokens.push_back(token); splitPos++;
            }
            if (tokens.size() < 4) continue;

            int team = _wtoi(tokens[0]);
            CString mainName = tokens[1]; mainName.Trim();

            // 查重与空位寻找
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

            // 探测是否包含 AK 次数
            if (tokens.size() >= 5) {
                CString t4 = tokens[4]; t4.Trim();
                bool isNumeric = !t4.IsEmpty();
                for (int i = 0; i < t4.GetLength(); i++) { if (t4[i] < L'0' || t4[i] > L'9') { isNumeric = false; break; } }
                if (isNumeric && t4.GetLength() <= 3) {
                    m_players[targetIdx].akCount = _wtoi(t4);
                    aliasStartIndex = 5;
                }
            }

            // 载入小号
            for (size_t i = aliasStartIndex; i < tokens.size(); i++) {
                CString aName = tokens[i]; aName.Trim();
                bool aDup = false;
                for (int k = 0; k < 8; k++) {
                    if (m_players[k].name == aName) { aDup = true; break; }
                    for (auto& ea : m_players[k].aliases) { if (ea.name == aName) { aDup = true; break; } }
                }
                if (!aDup && !aName.IsEmpty()) m_players[targetIdx].aliases.push_back({ aName });
            }
        }
        // ==========================================
        // 兼容层 3：处理纯文本 `=` 格式（如：五九(...) = 15/6 A2）
        // ==========================================
        else if (line.Find(L"=") != -1 || line.Find(L"＝") != -1) {
            int eqPos = line.Find(L'=');
            if (eqPos == -1) eqPos = line.Find(L'＝');

            CString leftPart = line.Left(eqPos);
            CString rightPart = line.Mid(eqPos + 1);
            leftPart.Trim(); rightPart.Trim();

            // 解析主号名
            int firstParen = leftPart.Find(L'(');
            if (firstParen == -1) firstParen = leftPart.Find(L'（');

            CString mainName = (firstParen != -1) ? leftPart.Left(firstParen) : leftPart;
            mainName.Trim();
            if (mainName.IsEmpty()) continue;

            // 查重拦截
            bool isDup = false;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { isDup = true; break; } }
            if (isDup) continue;

            // 在当前队伍找空位（依赖前面的【红队】/【蓝队】标签设定的 currentTeamContext）
            int targetIdx = -1;
            int sIdx = (currentTeamContext == 0) ? 0 : 4;
            int eIdx = (currentTeamContext == 0) ? 4 : 8;
            for (int i = sIdx; i < eIdx; i++) {
                if (m_players[i].name.IsEmpty()) { targetIdx = i; break; }
            }
            if (targetIdx == -1) continue; // 队伍满了就跳过

            m_players[targetIdx].name = mainName;
            m_players[targetIdx].team = currentTeamContext;

            // 解析右侧战绩：支持 "15/6 A2", "15/6", "15-6"
            int aP = rightPart.Find(L'A');
            if (aP != -1) {
                m_players[targetIdx].akCount = _wtoi(rightPart.Mid(aP + 1));
                if (m_players[targetIdx].akCount == 0) m_players[targetIdx].akCount = 1; // 如果写了A但没数字，保底算1次
                rightPart = rightPart.Left(aP); // 剥离出K/D部分
            }
            else {
                m_players[targetIdx].akCount = 0;
            }

            int slash = rightPart.Find(L'/');
            if (slash == -1) slash = rightPart.Find(L'-'); // 兼容使用减号的情况

            if (slash != -1) {
                m_players[targetIdx].kills = _wtoi(rightPart.Left(slash));
                m_players[targetIdx].deaths = _wtoi(rightPart.Mid(slash + 1));
            }
            else {
                m_players[targetIdx].kills = 0; m_players[targetIdx].deaths = 0;
            }

            // 解析左侧的小号群： (小号1)(小号2)
            if (firstParen != -1) {
                CString aliasesPart = leftPart.Mid(firstParen);
                int c = 0;
                while (true) {
                    CString tS = aliasesPart.Mid(c);
                    int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（');
                    int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
                    if (Lr == -1 || Rr == -1 || Rr <= Lr) break;

                    CString aN = tS.Mid(Lr + 1, Rr - Lr - 1);
                    aN.Trim();

                    // 小号查重
                    bool aDup = false;
                    for (int k = 0; k < 8; k++) {
                        if (m_players[k].name == aN) { aDup = true; break; }
                        for (auto& ea : m_players[k].aliases) {
                            if (ea.name == aN) { aDup = true; break; }
                        }
                    }
                    if (!aDup && !aN.IsEmpty()) m_players[targetIdx].aliases.push_back({ aN });

                    c += Rr + 1; // 游标移到下一个括号后
                }
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

    if (pTVDispInfo->item.pszText != NULL) {
        CString line = pTVDispInfo->item.pszText; line.Trim();
        if (line.IsEmpty()) return;

        DWORD_PTR data = m_treePlayers.GetItemData(pTVDispInfo->item.hItem);
        std::lock_guard<std::mutex> lk(m_dataMutex);

        CString newNameOnly = line;
        if (!(data & 0x80000000)) {
            int eP = line.Find(L'='); if (eP == -1) eP = line.Find(L'＝');
            if (eP != -1) { newNameOnly = line.Left(eP); newNameOnly.Trim(); }
        }

        int curPIdx = (data & 0x80000000) ? ((data & 0x7FFFFFFF) >> 16) : (int)data;
        int curAIdx = (data & 0x80000000) ? (data & 0xFFFF) : -1;

        // 查重拦截逻辑
        bool isDup = false;
        for (int i = 0; i < 8 && !isDup; i++) {
            if (m_players[i].name.IsEmpty()) continue;

            if (m_players[i].name == newNameOnly && !(i == curPIdx && curAIdx == -1)) {
                isDup = true; break;
            }
            for (int j = 0; j < (int)m_players[i].aliases.size(); j++) {
                if (m_players[i].aliases[j].name == newNameOnly && !(i == curPIdx && j == curAIdx)) {
                    isDup = true; break;
                }
            }
        }

        if (isDup) {
            AppLog(L"❌ [重命名失败] 名称 [" + newNameOnly + L"] 已经被其他玩家占用，修改被拦截！", RGB(255, 100, 100));
            MessageBox(L"修改失败！该名称已经被其他主号或小号占用，请使用唯一名称。", L"命名冲突", MB_ICONWARNING);
            return;
        }

        // 修改保存逻辑
        if (data & 0x80000000) {
            // =========================================================
            // 【新增】：小号更名，精准同步替换数据库里的旧名字
            // =========================================================
            CString oldAliasName = m_players[curPIdx].aliases[curAIdx].name;
            CString mainName = m_players[curPIdx].name;

            if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
                CString& dbAliases = m_aliasDB[mainName];
                // 兼容中英文括号替换，避免旧名字变幽灵数据
                dbAliases.Replace(L"(" + oldAliasName + L")", L"(" + line + L")");
                dbAliases.Replace(L"（" + oldAliasName + L"）", L"（" + line + L"）");
            }

            m_players[curPIdx].aliases[curAIdx].name = line;
        }
        else {
            int eP = line.Find(L'='); if (eP == -1) eP = line.Find(L'＝');
            CString oldMainName = m_players[data].name;
            CString newMainName = line;

            if (eP != -1) {
                newMainName = line.Left(eP); newMainName.Trim();
                // 战绩解析
                CString score = line.Mid(eP + 1); score.Trim();
                int aP = score.Find(L'A');
                if (aP != -1) { m_players[data].akCount = _wtoi(score.Mid(aP + 1)); score = score.Left(aP); }
                int sl = score.Find(L'/'); if (sl == -1) sl = score.Find(L'-');
                if (sl != -1) { m_players[data].kills = _wtoi(score.Left(sl)); m_players[data].deaths = _wtoi(score.Mid(sl + 1)); }
            }

            // =========================================================
            // 【新增】：主号更名，同步转移数据库的 Key，防止小号库丢失
            // =========================================================
            if (oldMainName != newMainName && !oldMainName.IsEmpty()) {
                if (m_aliasDB.find(oldMainName) != m_aliasDB.end()) {
                    m_aliasDB[newMainName] = m_aliasDB[oldMainName]; // 继承该主号下的所有小号数据
                    m_aliasDB.erase(oldMainName); // 删除旧的主号 Key
                }
            }

            m_players[data].name = newMainName;
        }

        AppLog(L"✏️ [信息修改] 成功保存更新: " + line, RGB(0, 255, 100));

        SaveAliasDB();      // 先把内存里的字典写入文件
        SaveConfigToFile(); // 再保存对局文件
        WriteScoreToFile(); // 刷新输出的TXT
        SyncDataToTree();   // 重绘树状图
        RefreshDisplay();   // 刷新右侧看板
    }
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
    // 先调用父类的默认处理，拿到默认的画刷
    HBRUSH hbr = CWnd::OnCtlColor(pDC, pWnd, nCtlColor);

    // 1. 判断当前发消息的控件是不是我们的“快速添加框”（ID为1025）
    if (pWnd->GetDlgCtrlID() == 1025) {
        CString txt;
        pWnd->GetWindowText(txt);


        // 2. 判断内容是否为提示水印
        if (txt == PLACEHOLDER_TEXT) {
            // 如果是水印，把画笔颜色设为灰色
            pDC->SetTextColor(RGB(160, 160, 160));
        }
        else {
            // 如果用户开始打字了，恢复成正常的黑色
            pDC->SetTextColor(RGB(0, 0, 0));
        }
    }

    return hbr;
}

// 【核心修复】：专门接收子线程消息，在安全的主线程中刷新树状图和看板
LRESULT CDNFGameCaptureDlg::OnUpdateAllUI(WPARAM wParam, LPARAM lParam) {
    SyncDataToTree();
    RefreshDisplay();
    return 0;
}

// 接收来自后台线程的“坏消息”，由主线程安全弹窗
LRESULT CDNFGameCaptureDlg::OnCloudAuthFail(WPARAM wParam, LPARAM lParam) {
    CString* pCloudResult = (CString*)lParam;
    if (pCloudResult) {
        // 1. 立即标记授权失效，停止所有监控任务
        m_bIsAuthValid = false;
        if (m_bIsRunning) {
            OnBnClickedStart(); // 模拟点击停止按钮，清理定时器和引擎
        }

        // 2. 格式化错误提示
        CString errMsg;
        errMsg.Format(L"授权校验失败：\r\n%s\r\n\r\n软件将限制使用部分功能。", (LPCTSTR)*pCloudResult);

        // 3. 安全弹窗（主线程弹窗绝对不会闪退）
        MessageBox(errMsg, L"安全拦截", MB_ICONERROR | MB_SYSTEMMODAL);

        // 4. 释放子线程 new 出来的内存，防止内存泄漏
        delete pCloudResult;
    }
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

// 检测位图是否全黑（PrintWindow 权限不足时的典型表现）
bool CDNFGameCaptureDlg::IsBitmapBlank(HBITMAP hBmp, int w, int h) {
    if (!hBmp || w <= 0 || h <= 0) return true;

    HDC hDC = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(hDC, hBmp);

    // 采样检测：在画面中心区域取 9 个点
    // 避免检测边缘（可能本来就是黑的）
    int checkPoints[][2] = {
        { w / 4, h / 4 },     { w / 2, h / 4 },     { w * 3 / 4, h / 4 },
        { w / 4, h / 2 },     { w / 2, h / 2 },     { w * 3 / 4, h / 2 },
        { w / 4, h * 3 / 4 }, { w / 2, h * 3 / 4 }, { w * 3 / 4, h * 3 / 4 },
    };

    int blackCount = 0;
    for (auto& pt : checkPoints) {
        COLORREF c = GetPixel(hDC, pt[0], pt[1]);
        // RGB 三通道加起来 < 30 视为黑色
        if (GetRValue(c) + GetGValue(c) + GetBValue(c) < 30) {
            blackCount++;
        }
    }

    SelectObject(hDC, old);
    DeleteDC(hDC);

    // 9 个采样点中有 8 个以上是黑的，判定为全黑
    return (blackCount >= 8);
}

void CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine() {
    m_nCaptureEngineChoice = m_cmbCaptureEngine.GetCurSel();

    // 保存到配置文件
    CString val;
    val.Format(L"%d", m_nCaptureEngineChoice);
    WritePrivateProfileString(L"Settings", L"CaptureEngine", val, m_iniPath);

    CString engineNames[] = { L"自动选择", L"WGC 硬件加速", L"PrintWindow 兼容模式" };
    AppLog(L"⚙️ [设置] 捕获引擎已切换为: " + engineNames[m_nCaptureEngineChoice], RGB(0, 255, 255));

    ClearPreview();

    // ==========================================
    // 【核心优化】：无论是否在监控，直接干掉旧引擎
    // 接下来的 50 毫秒内，Timer 1 或 Timer 6 会调用 Capture() 自动重建新引擎！
    // ==========================================
    if (m_pWGC) {
        m_pWGC->StopCapture();
        delete m_pWGC;
        m_pWGC = nullptr;
    }
    m_bUseWGC = false;
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

