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

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

// ============================================================================
// 全局变量与辅助函数
// ============================================================================
std::mutex g_bmpMutex;

struct VisualLogMsg {
    CString text;
    COLORREF color;
};
std::deque<VisualLogMsg> g_visualLogs;
std::mutex g_visualLogMutex;

const float WINDOW_SCALE = 1.6f;
const int ID_BTN_START = 1005;
const int ID_BTN_APPLY = 1006;
const int ID_CHK_FLIP = 1007;
const int ID_BTN_RESET = 1008;
const int ID_BTN_BROWSE = 1013;
const int ID_EDIT_DIR = 1014;

struct ScorePointF { float x; float y; };
ScorePointF g_scorePts[16] = {
    { 0.1594f, 0.0348f }, { 0.1922f, 0.0377f }, { 0.1761f, 0.1116f }, { 0.1957f, 0.1138f },
    { 0.2738f, 0.1127f }, { 0.2925f, 0.1127f }, { 0.3714f, 0.1104f }, { 0.3902f, 0.1138f },
    { 0.8105f, 0.0338f }, { 0.8457f, 0.0372f }, { 0.6085f, 0.1104f }, { 0.6281f, 0.1116f },
    { 0.7050f, 0.1127f }, { 0.7242f, 0.1127f }, { 0.8019f, 0.1127f }, { 0.8214f, 0.1116f },
};

int GetVisualWidth(const CString& s) {
    int w = 0;
    for (int i = 0; i < s.GetLength(); i++) {
        w += (s[i] >= 0x4E00 && s[i] <= 0x9FFF) ? 2 : 1;
    }
    return w;
}

void WriteMatchLog(const CString& logLine) {
    CFile file;
    if (file.Open(L"match_debug.log", CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite)) {
        if (file.GetLength() == 0) {
            unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            file.Write(bom, 3);
        }
        file.SeekToEnd();
        time_t now = time(0);
        tm t; localtime_s(&t, &now);
        CString fullLine;
        fullLine.Format(L"[%02d:%02d:%02d] %s\r\n", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)logLine);
        std::string utf8Line = CW2A(fullLine, CP_UTF8);
        file.Write(utf8Line.c_str(), (UINT)utf8Line.length());
        file.Close();
    }
}


// ============================================================================
// 消息映射
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
    ON_WM_SYSCOMMAND()
    ON_MESSAGE(WM_TRAY_MESSAGE, &CDNFGameCaptureDlg::OnTrayMessage)
    ON_MESSAGE(WM_UPDATE_OCR_DROPDOWNS, &CDNFGameCaptureDlg::OnUpdateOcrDropdowns)
    ON_CBN_SELCHANGE(1010, &CDNFGameCaptureDlg::OnCbnSelchangeLeft)
    ON_CBN_SELCHANGE(1009, &CDNFGameCaptureDlg::OnCbnSelchangeRight)
END_MESSAGE_MAP()


// ============================================================================
// 授权验证逻辑 (防篡改与试用期)
// ============================================================================
CString CDNFGameCaptureDlg::GetMachineID() {
    DWORD volSerial = 0;
    GetVolumeInformation(L"C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0);
    CString hwid;
    hwid.Format(L"%08X", volSerial);
    return hwid;
}

bool CDNFGameCaptureDlg::VerifyKey(CString inputKey, CString machineID) {
    if (inputKey.Left(4) != L"DNF-") return false;

    int firstDash = 3;
    int secondDash = inputKey.Find(L'-', firstDash + 1);
    if (secondDash == -1) return false;

    long long expTime = wcstoll(inputKey.Mid(firstDash + 1, secondDash - firstDash - 1), NULL, 16);
    long long sig = wcstoll(inputKey.Mid(secondDash + 1), NULL, 16);
    long long expectedSig = wcstoll(machineID, NULL, 16) ^ expTime ^ 0x5AA55AA5;

    if (sig != expectedSig) return false;
    if (expTime == 0xFFFFFFFF) return true;

    return (long long)time(nullptr) <= expTime;
}

void CDNFGameCaptureDlg::CheckTrialAndLicense() {
    CString hwid = GetMachineID();
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
        if (VerifyKey(inputKey, hwid)) return;
    }

    HKEY hKey;
    DWORD disp;
    time_t now = time(nullptr);
    bool bTrialValid = false;
    long long left = 0;

    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, &disp) == ERROR_SUCCESS) {
        DWORD iT = 0, lR = 0, sz = 4;
        if (RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz) != ERROR_SUCCESS) {
            iT = (DWORD)now;
            RegSetValueEx(hKey, L"InstallTime", 0, REG_DWORD, (const BYTE*)&iT, 4);
        }

        sz = 4;
        if (RegQueryValueEx(hKey, L"LastRun", NULL, NULL, (LPBYTE)&lR, &sz) == ERROR_SUCCESS && (DWORD)now < lR - 3600) {
            iT = 0;
        }

        DWORD cR = (DWORD)now;
        RegSetValueEx(hKey, L"LastRun", 0, REG_DWORD, (const BYTE*)&cR, 4);
        RegCloseKey(hKey);

        if (iT > 0 && (DWORD)now <= iT + 604800) {
            bTrialValid = true;
            left = (iT + 604800) - (DWORD)now;
        }
    }

    if (bTrialValid) {
        CString m;
        m.Format(L"【欢迎试用】\r\n剩余试用时间: %lld 小时。", left / 3600);
        MessageBox(m, L"试用提示", MB_ICONINFORMATION);
        return;
    }

    if (::OpenClipboard(NULL)) {
        ::EmptyClipboard();
        size_t s = (hwid.GetLength() + 1) * 2;
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s);
        memcpy(GlobalLock(h), hwid.GetBuffer(), s);
        GlobalUnlock(h);
        ::SetClipboardData(CF_UNICODETEXT, h);
        ::CloseClipboard();
    }

    MessageBox(L"授权已过期！机器码 " + hwid + L" 已复制。", L"拦截", MB_ICONERROR);
    exit(0);
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
        };

    print(L"====== [授权诊断报告] ======", RGB(255, 100, 255));
    print(L"机器码: " + GetMachineID(), RGB(255, 255, 255));
    print(L"软件版本: v" CURRENT_VERSION, RGB(255, 255, 255));
    print(L"============================", RGB(255, 100, 255));
}


// ============================================================================
// 初始化与窗口过程
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
#ifndef _DEBUG
    CheckTrialAndLicense();
#endif

    m_hSingleInstanceMutex = CreateMutex(NULL, TRUE, L"Global\\DNFGameCapture_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(L"程序已经在运行中！\r\n\r\n请在右下角任务栏（系统托盘）中查找。", L"提示", MB_ICONINFORMATION | MB_OK);
        exit(0);
    }

    m_bmp = NULL;
    m_w = 0;
    m_h = 0;
    m_bIsRunning = FALSE;
    m_bCanTrigger = TRUE;
    m_bCanTriggerTeamScore = TRUE;
    m_historyIdx = 0;
    m_bPendingTeamScoreWin = false;
    m_totalScoreRed = 0;
    m_totalScoreBlue = 0;
    m_lastKillerTeam = -1;
    m_bFlipSides = false;
    m_hDebugOcrBmp[0] = NULL;
    m_hDebugOcrBmp[1] = NULL;
    m_viewIndexLeft = -1;
    m_viewIndexRight = -1;

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
    if (m_outputDir.Right(1) == L"\\") m_outputDir.TrimRight(L"\\");

    AfxInitRichEdit2();
    for (int i = 0; i < 25; i++) {
        m_historyBmps[i] = NULL;
    }

    for (int i = 0; i < 8; i++) {
        m_players[i].kills = 0;
        m_players[i].deaths = 0;
        m_players[i].currentStreak = 0;
        m_players[i].akCount = 0;
        m_players[i].team = (i < 4 ? 0 : 1);
    }

    m_players[0].name = L"白羽"; m_players[0].team = 0; m_players[0].aliases.push_back({ L"抖音FSN白羽" });
    m_players[1].name = L"大崩"; m_players[1].team = 0; m_players[1].aliases.push_back({ L"流年兮" });
    m_players[2].name = L"夏法"; m_players[2].team = 0;
    m_players[3].name = L"逍遥"; m_players[3].team = 0;
    m_players[4].name = L"老王"; m_players[4].team = 1; m_players[4].aliases.push_back({ L"旋律" });
    m_players[5].name = L"夜风"; m_players[5].team = 1;
    m_players[6].name = L"二海"; m_players[6].team = 1; m_players[6].aliases.push_back({ L"疯疯熊冲鸭" });
    m_players[7].name = L"九哥"; m_players[7].team = 1; m_players[7].aliases.push_back({ L"米叹米叹" });

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CString title;
    title.Format(L"DNF击杀统计 - 终极完备版 (v%s)", CURRENT_VERSION);

    CreateEx(0, cls, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100, (int)(750 * WINDOW_SCALE), (int)(730 * WINDOW_SCALE), NULL, NULL);

    InitTrayIcon();
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    if (m_hSingleInstanceMutex) {
        CloseHandle(m_hSingleInstanceMutex);
    }
    RemoveTrayIcon();

    if (m_bmp) ::DeleteObject(m_bmp);
    for (int i = 0; i < 25; i++) {
        if (m_historyBmps[i]) ::DeleteObject(m_historyBmps[i]);
    }
    if (m_hHttpConnect) WinHttpCloseHandle(m_hHttpConnect);
    if (m_hHttpSession) WinHttpCloseHandle(m_hHttpSession);

    GdiplusShutdown(m_gdiplusToken);
}


// ============================================================================
// 托盘与更新系统
// ============================================================================
void CDNFGameCaptureDlg::InitTrayIcon() {
    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = GetSafeHwnd();
    m_nid.uID = 10001;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_MESSAGE;

    wchar_t p[MAX_PATH];
    GetModuleFileName(NULL, p, MAX_PATH);
    m_nid.hIcon = ExtractIcon(AfxGetInstanceHandle(), p, 0);
    if (!m_nid.hIcon) m_nid.hIcon = AfxGetApp()->LoadStandardIcon(IDI_APPLICATION);

    wcscpy_s(m_nid.szTip, L"DNF击杀统计 - 运行中");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void CDNFGameCaptureDlg::RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
}

void CDNFGameCaptureDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if ((nID & 0xFFF0) == SC_CLOSE) {
        if (m_bIsRunning) {
            ShowWindow(SW_HIDE);
        }
        else {
            DoRealExit();
        }
        return;
    }
    if ((nID & 0xFFF0) == SC_MINIMIZE) {
        ShowWindow(SW_HIDE);
        return;
    }
    CWnd::OnSysCommand(nID, lParam);
}

LRESULT CDNFGameCaptureDlg::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    if (lParam == WM_LBUTTONUP) {
        ShowWindow(SW_SHOW);
        ShowWindow(SW_RESTORE);
        SetForegroundWindow();
    }
    else if (lParam == WM_RBUTTONUP) {
        CPoint pt;
        GetCursorPos(&pt);
        CMenu m;
        m.CreatePopupMenu();
        m.AppendMenu(MF_STRING, 101, L"显示面板");
        m.AppendMenu(MF_STRING, 103, L"检查更新 (当前 v" CURRENT_VERSION L")");
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING, 102, L"完全退出");
        SetForegroundWindow();

        int cmd = m.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);
        if (cmd == 101) {
            ShowWindow(SW_SHOW);
            ShowWindow(SW_RESTORE);
            SetForegroundWindow();
        }
        else if (cmd == 103) {
            CheckForUpdates(false);
        }
        else if (cmd == 102) {
            DoRealExit();
        }
    }
    return 0;
}

void CDNFGameCaptureDlg::DoRealExit() {
    m_bIsRunning = FALSE;
    KillTimer(1);
    KillTimer(2);
    KillTimer(3);
    KillTimer(4);
    DestroyWindow();
    PostQuitMessage(0);
}

void CDNFGameCaptureDlg::OnClose() {
    ShowWindow(SW_HIDE);
}

void CDNFGameCaptureDlg::CheckForUpdates(bool bSilent) {
    if (!m_hHttpSession) return;

    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[1024];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;

    if (!WinHttpCrackUrl(UPDATE_CHECK_URL, 0, 0, &uc)) return;

    HINTERNET hC = WinHttpConnect(m_hHttpSession, host, uc.nPort, 0);
    if (!hC) return;

    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    std::string s = "";

    if (WinHttpSendRequest(hR, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hR, NULL)) {
        DWORD sz = 0, dwD = 0;
        while (WinHttpQueryDataAvailable(hR, &sz) && sz > 0) {
            std::vector<char> b(sz + 1, 0);
            if (WinHttpReadData(hR, (LPVOID)b.data(), sz, &dwD)) {
                s.append(b.data(), dwD);
            }
        }
    }

    WinHttpCloseHandle(hR);
    WinHttpCloseHandle(hC);

    if (s.empty()) {
        if (!bSilent) MessageBox(L"获取更新失败");
        return;
    }

    int wL = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> wB(wL);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wB.data(), wL);

    CString full(wB.data());
    full.Replace(L"\r\n", L"\n");
    int p1 = full.Find(L'\n');
    if (p1 == -1) return;

    CString ver = full.Left(p1).Trim();
    int p2 = full.Find(L'\n', p1 + 1);
    if (p2 == -1) return;

    CString url = full.Mid(p1 + 1, p2 - p1 - 1).Trim();

    if (ver == CURRENT_VERSION) {
        if (!bSilent) MessageBox(L"已是最新版");
        return;
    }

    if (MessageBox(L"发现新版 " + ver + L"，是否更新？", L"更新", MB_YESNO) == IDYES) {
        std::thread([this, url]() { DownloadAndApplyUpdate(url); }).detach();
    }
}

void CDNFGameCaptureDlg::DownloadAndApplyUpdate(CString url) {
    wchar_t p[MAX_PATH];
    GetModuleFileName(NULL, p, MAX_PATH);
    CString cp(p);
    CString d = cp.Left(cp.ReverseFind(L'\\') + 1);
    CString t = d + L"update_temp.exe";
    CString b = d + L"update.bat";

    if (URLDownloadToFile(NULL, url, t, 0, NULL) != S_OK) {
        MessageBox(L"下载更新文件失败，请检查网络！", L"更新失败", MB_ICONERROR);
        return;
    }

    CFile f;
    if (f.Open(b, CFile::modeCreate | CFile::modeWrite)) {
        CString s;
        s.Format(
            L"@echo off\r\n"
            L":Retry\r\n"
            L"ping 127.0.0.1 -n 2 > nul\r\n"
            L"del \"%s\"\r\n"
            L"if exist \"%s\" goto Retry\r\n"
            L"rename \"%s\" \"%s\"\r\n"
            L"start \"\" \"%s\"\r\n"
            L"del \"%%~f0\"\r\n",
            cp.GetString(), cp.GetString(), t.GetString(),
            cp.Mid(cp.ReverseFind(L'\\') + 1).GetString(), cp.GetString()
        );
        std::string a = CW2A(s, CP_OEMCP);
        f.Write(a.c_str(), (UINT)a.length());
        f.Close();
    }

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = b;
    sei.nShow = SW_HIDE;

    if (ShellExecuteEx(&sei)) {
        exit(0);
    }
    else {
        MessageBox(L"更新脚本执行失败，请尝试以管理员身份运行软件。", L"错误", MB_ICONERROR);
    }
}


// ============================================================================
// UI 事件响应
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedStart() {
    if (!m_bIsRunning) {
        UpdatePlayersFromUI();
        m_bIsRunning = TRUE;
        m_btnStart.SetWindowText(L"停止监控");
        SetTimer(1, 50, NULL);
        SetTimer(3, 300, NULL);
        m_status.SetWindowText(L"监控中...");
    }
    else {
        m_bIsRunning = FALSE;
        KillTimer(1);
        KillTimer(3);
        m_btnStart.SetWindowText(L"开始监控");
        m_status.SetWindowText(L"已停止");
    }
}

void CDNFGameCaptureDlg::OnBnClickedApply() {
    if (GetKeyState(VK_CONTROL) < 0) {
        OutputDebugAuthInfo();
        m_status.SetWindowText(L"诊断已输出");
        return;
    }
    UpdatePlayersFromUI();
    m_status.SetWindowText(L"修改生效");
}

void CDNFGameCaptureDlg::OnBnClickedFlip() {
    m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED);
    WriteScoreToFile();
    RefreshDisplay();
}

void CDNFGameCaptureDlg::OnBnClickedReset() {
    if (MessageBox(L"确定要将战绩全部归零吗？", L"确认", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        m_totalScoreRed = 0;
        m_totalScoreBlue = 0;
        for (int i = 0; i < 8; i++) {
            m_players[i].kills = 0;
            m_players[i].deaths = 0;
            m_players[i].currentStreak = 0;
            m_players[i].akCount = 0;
        }
        SyncDataToInputBox();
        RefreshDisplay();
        WriteScoreToFile();
        if (m_editVisualLogs.m_hWnd) {
            m_editVisualLogs.SetWindowText(L"");
        }
        m_status.SetWindowText(L"战绩已归零！");
    }
}

void CDNFGameCaptureDlg::OnBnClickedBrowseDir() {
    CFolderPickerDialog dlg(m_outputDir, 0, this, 0);
    if (dlg.DoModal() == IDOK) {
        m_outputDir = dlg.GetPathName();
        if (m_outputDir.Right(1) == L"\\") {
            m_outputDir.TrimRight(L"\\");
        }
        m_editOutDir.SetWindowText(m_outputDir);
        WritePrivateProfileString(L"Settings", L"OutputDir", m_outputDir, m_iniPath);
        WriteScoreToFile();
        m_status.SetWindowText(L"输出目录已更新");
    }
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) {
    return TRUE;
}

LRESULT CDNFGameCaptureDlg::OnUpdateOcrDropdowns(WPARAM wParam, LPARAM lParam) {
    std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
    if (wParam == 1) {
        m_cmbLeft.ResetContent();
        m_cmbLeft.AddString(L"[红] 左侧自动追踪");
        m_cmbLeft.SetCurSel(0);
        m_cmbRight.ResetContent();
        m_cmbRight.AddString(L"[蓝] 右侧自动追踪");
        m_cmbRight.SetCurSel(0);
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

void CDNFGameCaptureDlg::OnCbnSelchangeLeft() {
    m_viewIndexLeft = (m_cmbLeft.GetCurSel() == 0) ? -1 : (m_cmbLeft.GetCurSel() - 1);
    InvalidateRect(&m_previewRect, FALSE);
}

void CDNFGameCaptureDlg::OnCbnSelchangeRight() {
    m_viewIndexRight = (m_cmbRight.GetCurSel() == 0) ? -1 : (m_cmbRight.GetCurSel() - 1);
    InvalidateRect(&m_previewRect, FALSE);
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= 16) m_selectPts.clear();
        m_selectPts.push_back(CPoint((int)(((float)(point.x - m_previewRect.left) / m_previewRect.Width()) * 10000.0f),
            (int)(((float)(point.y - m_previewRect.top) / m_previewRect.Height()) * 10000.0f)));
        InvalidateRect(&m_previewRect, FALSE);
        if (m_selectPts.size() == 16) {
            CString res = L"ScorePointF g_scorePts[16] = {\r\n";
            for (int i = 0; i < 16; i++) {
                CString t;
                t.Format(L"    { %.4ff, %.4ff },\r\n", m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f);
                res += t;
            }
            m_editOcrResult.SetWindowText(res + L"};\r\n");
            MessageBox(L"坐标已采集，见右侧框。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}


// ============================================================================
// 画面捕获与色彩触发
// ============================================================================
void CDNFGameCaptureDlg::Capture() {
    HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
    if (!hGame) return;

    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        RECT rc;
        ::GetClientRect(hGame, &rc);
        m_w = rc.right - rc.left;
        m_h = rc.bottom - rc.top;
        if (m_w <= 0 || m_h <= 0) return;

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
    }

    CRect client;
    GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(420 * WINDOW_SCALE));
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
        HDC hMem = ::CreateCompatibleDC(NULL);
        HGDIOBJ old = ::SelectObject(hMem, m_bmp);

        c_k[0] = ::GetPixel(hMem, (int)(m_w * 0.187f), (int)(m_h * 0.036f));
        c_k[1] = ::GetPixel(hMem, (int)(m_w * 0.157f), (int)(m_h * 0.034f));
        c_k[2] = ::GetPixel(hMem, (int)(m_w * 0.840f), (int)(m_h * 0.039f));
        c_k[3] = ::GetPixel(hMem, (int)(m_w * 0.810f), (int)(m_h * 0.039f));

        for (int i = 0; i < 16; i++) {
            c_t[i] = ::GetPixel(hMem, (int)(m_w * g_scorePts[i].x), (int)(m_h * g_scorePts[i].y));
        }
        ::SelectObject(hMem, old);
        ::DeleteDC(hMem);
    }

    auto eq = [](COLORREF a, COLORREF b) {
        return abs(GetRValue(a) - GetRValue(b)) < 25 &&
            abs(GetGValue(a) - GetGValue(b)) < 25 &&
            abs(GetBValue(a) - GetBValue(b)) < 25;
        };

    auto mk = [&](int p1, int p2) {
        return (eq(c_k[p1], COLOR_BLUE) && eq(c_k[p2], COLOR_RED)) ||
            (eq(c_k[p1], COLOR_RED) && eq(c_k[p2], COLOR_BLUE));
        };

    auto mt = [&](int p1, int p2) {
        return (eq(c_t[p1], COLOR_BLUE) && eq(c_t[p2], COLOR_RED)) ||
            (eq(c_t[p1], COLOR_RED) && eq(c_t[p2], COLOR_BLUE));
        };

    if ((mt(0, 1) && mt(2, 3) && mt(4, 5) && mt(6, 7) || mt(8, 9) && mt(10, 11) && mt(12, 13) && mt(14, 15)) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex);
            m_bPendingTeamScoreWin = true;
        }
        SetTimer(4, 120000, NULL);
    }

    if ((mk(0, 1) || mk(2, 3)) && m_bCanTrigger) {
        m_bCanTrigger = FALSE;
        int killSide = mk(0, 1) ? 0 : 1;
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
        // 【核心修复】：将冷却时间改回 10 秒，防止短暂时间内不断启动新的识别线程导致卡顿和霸屏
        SetTimer(2, 10000, NULL);
    }
}


// ============================================================================
// 时光回溯匹配与数据结算
// ============================================================================
void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide) {
    int killerArea = (triggerSide == 0) ? 1 : 0;
    int deadArea = triggerSide;
    bool killerIsLeft = (killerArea == 0);

    bool killerResolved = false, deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";
    int killerBestP = -1, killerBestA = -1, deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999;
    CString globalKillerName = L"";
    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999;
    CString globalDeadName = L"";

    // 存储历史帧文字，供二轮降级匹配使用
    struct FrameData { CString text; int frameIdx; };
    std::vector<FrameData> historyKTexts;
    std::vector<FrameData> historyDTexts;

    // 【新增】：统一带时间戳的日志输出函数
    auto PushVisualLog = [&](const CString& msg, COLORREF color) {
        time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
        CString tStr; tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.push_back({ tStr, color });
        WriteMatchLog(msg);
        };

    std::vector<HBITMAP> historyClones;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        for (int i = 1; i <= 25; i++) {
            int idx = (m_historyIdx - i + 25) % 25;
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

    // ==========================================================
    // 【核心修复】：将 processMatch 函数提升到 for 循环外部！
    // ==========================================================
    auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller, int& outBestP, int& outBestA, int& frameScore, bool isAggressive, int frameIdx) -> bool {
        frameScore = -2;
        if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1) return false;

        CString logMsg;
        logMsg.Format(L"▶ [%s] 第%d帧提取: \"%s\"", isKiller ? L"找杀手" : L"找死者", frameIdx, (LPCTSTR)ocrResult);
        PushVisualLog(logMsg, RGB(180, 180, 180)); // 提取过程设为淡灰色，不抢眼

        int maxS = -2, bestP = -1, bestA = -1, bestRealLen = 0;
        std::wstring bestN = L"";

        m_dataMutex.lock();
        for (int p = 0; p < 8; p++) {
            if (m_players[p].name.IsEmpty()) continue;

            int teamPenalty = 0;
            if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) teamPenalty = 20;
            if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) teamPenalty = 20;

            int curScore = m_matcher.GetMatchScore(m_players[p].name.GetString(), ocrResult.GetString(), isAggressive);
            if (curScore == -1) { maxS = -1; break; }
            curScore -= teamPenalty;

            std::wstring curBestN = m_players[p].name.GetString();
            int curBestA = -1, curRealLen = m_players[p].name.GetLength();

            for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                int as = m_matcher.GetMatchScore(m_players[p].aliases[a].name.GetString(), ocrResult.GetString(), isAggressive);
                if (as == -1) { maxS = -1; break; }
                as -= teamPenalty;
                if (as > curScore) {
                    curScore = as;
                    curBestN = m_players[p].aliases[a].name.GetString();
                    curBestA = (int)a;
                    curRealLen = m_players[p].aliases[a].name.GetLength();
                }
            }

            if (maxS == -1) break;
            if (curScore > maxS || (curScore == maxS && maxS > 0 && curRealLen > bestRealLen)) {
                maxS = curScore; bestP = p; bestA = curBestA; bestN = curBestN; bestRealLen = curRealLen;
            }
        }
        m_dataMutex.unlock();

        frameScore = maxS;
        if (maxS == -1) {
            PushVisualLog(L"  └ [⚠️职业干扰] 触发职业拦截，跳过本帧...", RGB(120, 120, 120));
            return true;
        }

        int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);
        if (bestP != -1) {
            if (isKiller && maxS > globalKillerBestScore) {
                globalKillerBestScore = maxS; globalKillerBestP = bestP; globalKillerBestA = bestA; globalKillerPassLine = passLine; globalKillerName = bestN.c_str();
            }
            else if (!isKiller && maxS > globalDeadBestScore) {
                globalDeadBestScore = maxS; globalDeadBestP = bestP; globalDeadBestA = bestA; globalDeadPassLine = passLine; globalDeadName = bestN.c_str();
            }
        }

        if (bestP != -1 && maxS >= passLine) {
            resolved = true;
            finalName = bestN.c_str();
            outBestP = bestP;
            outBestA = bestA;

            CString successLog;
            if (isAggressive) {
                successLog.Format(L"  └ [✨二轮净化匹配] 强行剥离锁定: %s (得分:%d, 及格:%d)", (LPCTSTR)finalName, maxS, passLine);
                PushVisualLog(successLog, RGB(255, 100, 255)); // 品红色，极为醒目
            }
            else {
                successLog.Format(L"  └ [✔首轮匹配] 成功指向: %s (得分:%d, 及格:%d)", (LPCTSTR)finalName, maxS, passLine);
                PushVisualLog(successLog, (m_players[bestP].team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255)); // 阵营色
            }

            m_dataMutex.lock();
            if (isKiller) lockedKillerTeam = m_players[bestP].team;
            else lockedDeadTeam = m_players[bestP].team;
            m_dataMutex.unlock();
        }
        else {
            CString failLog;
            failLog.Format(L"  └ [✖未达标] 最高仅 %d 分 (及格线: %d)", maxS, passLine);
            if (!isAggressive) PushVisualLog(failLog, RGB(120, 120, 120));
        }
        return false;
        };

    // 开始遍历 25 帧历史画面
    for (size_t i = 0; i < historyClones.size(); i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved)) break;

        HBITMAP hSnapshot = historyClones[i];
        std::future<OcrResultData> futKiller, futDead;

        if (!killerResolved) futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        if (!deadResolved) futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);

        OcrResultData resK = { L"", NULL }, resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get();
        if (futDead.valid()) resD = futDead.get();

        // 收集文本供二轮使用
        if (!killerResolved && !resK.text.IsEmpty() && resK.text.Find(L"No text") == -1) historyKTexts.push_back({ resK.text, (int)(i + 1) });
        if (!deadResolved && !resD.text.IsEmpty() && resD.text.Find(L"No text") == -1) historyDTexts.push_back({ resD.text, (int)(i + 1) });

        int kScore = -2, dScore = -2;
        bool kIsJob = processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, false, (int)(i + 1));
        bool dIsJob = processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, false, (int)(i + 1));

        OcrResultData& resL = killerIsLeft ? resK : resD;
        OcrResultData& resR = killerIsLeft ? resD : resK;

        {
            std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
            if (resL.hBmp) {
                CString lbl; lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resL.text);
                m_ocrRecordsLeft.push_back({ resL.hBmp, lbl });
            }
            if (resR.hBmp) {
                CString lbl; lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resR.text);
                m_ocrRecordsRight.push_back({ resR.hBmp, lbl });
            }
        }

        PostMessage(WM_UPDATE_OCR_DROPDOWNS, 0, 0);
        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult.Format(L"时光倒流帧 %d/25 | 杀:%s 亡:%s", (int)(i + 1), killerResolved ? finalKillerName : L"未定", deadResolved ? finalDeadName : L"未定");
        }
        InvalidateRect(&m_previewRect, FALSE);
        UpdateWindow();
    }

    // ==========================================================
    // 二轮降级匹配（免除职业拦截，降低长度惩罚）
    // ==========================================================
    if (!killerResolved && !historyKTexts.empty()) {
        PushVisualLog(L"▶ [找杀手] 25帧首轮均未命中，启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyKTexts) {
            int kScore = -2;
            processMatch(frame.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, true, frame.frameIdx);
            if (killerResolved) break;
        }
    }
    if (!deadResolved && !historyDTexts.empty()) {
        PushVisualLog(L"▶ [找死者] 25帧首轮均未命中，启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyDTexts) {
            int dScore = -2;
            processMatch(frame.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, true, frame.frameIdx);
            if (deadResolved) break;
        }
    }

    // 三轮：最后兜底（如果连二轮都没过，但首轮有勉强接近及格线的）
    if (!killerResolved && globalKillerBestP != -1 && globalKillerBestScore >= (globalKillerPassLine - 20) && globalKillerBestScore >= 35) {
        killerResolved = true; killerBestP = globalKillerBestP; killerBestA = globalKillerBestA; finalKillerName = globalKillerName;
    }
    if (!deadResolved && globalDeadBestP != -1 && globalDeadBestScore >= (globalDeadPassLine - 20) && globalDeadBestScore >= 35) {
        deadResolved = true; deadBestP = globalDeadBestP; deadBestA = globalDeadBestA; finalDeadName = globalDeadName;
    }

    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();
        bool isDup = false;

        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 20000)) {
                isDup = true; break;
            }
        }

        if (!isDup) {
            m_recentEvents.push_back({ finalKillerName, finalDeadName, now });
            m_recentEvents.erase(std::remove_if(m_recentEvents.begin(), m_recentEvents.end(), [&](const RecentEvent& ev) {
                return now - ev.time > 25000;
                }), m_recentEvents.end());

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++) {
                    if (p != killerBestP) { m_players[p].currentStreak = 0; }
                }

                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                CString actionLog;

                m_players[killerBestP].kills++;
                m_players[killerBestP].currentStreak++;

                CString displayName = m_players[killerBestP].name;
                if (killerBestA != -1 && (size_t)killerBestA < m_players[killerBestP].aliases.size()) {
                    displayName = m_players[killerBestP].aliases[killerBestA].name;
                }

                actionLog.Format(L"⚔ [击杀成功] 玩家 [%s] 拿下一击！连杀: %d", (LPCTSTR)displayName, m_players[killerBestP].currentStreak);
                PushVisualLog(actionLog, teamColor);

                if (m_players[killerBestP].currentStreak == 4) {
                    m_players[killerBestP].akCount++;
                    m_players[killerBestP].currentStreak = 0;
                    PushVisualLog(L"🌟 [AK宣告] 恐怖如斯！玩家 [" + displayName + L"] 完成一次 AK！", RGB(255, 215, 0));
                }
                m_lastKillerTeam = m_players[killerBestP].team;
            }

            if (deadResolved && deadBestP != -1) {
                m_players[deadBestP].deaths++;
            }

            if (m_bPendingTeamScoreWin) {
                m_bPendingTeamScoreWin = false;
                if (m_lastKillerTeam == 0) m_totalScoreRed++;
                else if (m_lastKillerTeam == 1) m_totalScoreBlue++;

                for (int p = 0; p < 8; p++) {
                    m_players[p].currentStreak = 0;
                }

                PushVisualLog(L"🏆 [结算] 局间大比分变动！所有人连击次数已清零！", RGB(0, 255, 100));
            }

            SyncDataToInputBox();
            RefreshDisplay();
            WriteScoreToFile();
        }
    }

    for (HBITMAP hb : historyClones) {
        DeleteObject(hb);
    }
}

// ============================================================================
// OCR HTTP 请求解析
// ============================================================================
OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hT, int nA) {
    OcrResultData ret = { L"", NULL };
    if (!m_hHttpConnect) return ret;

    RECT r = (nA == 0) ? RECT{ (long)(m_w * 0.190f), (long)(m_h * 0.004f), (long)(m_w * 0.360f), (long)(m_h * 0.040f) } : RECT{ (long)(m_w * 0.655f), (long)(m_h * 0.004f), (long)(m_w * 0.815f), (long)(m_h * 0.040f) };
    int sw = r.right - r.left, sh = r.bottom - r.top, sc = 2, pa = 30, dW = sw * sc + pa * 2, dH = sh * sc + pa * 2;

    HDC hS = CreateCompatibleDC(NULL), hD = CreateCompatibleDC(NULL);
    HBITMAP hB = CreateCompatibleBitmap(GetDC()->GetSafeHdc(), dW, dH);
    SelectObject(hS, hT);
    SelectObject(hD, hB);

    RECT bg = { 0, 0, dW, dH };
    HBRUSH wB = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hD, &bg, wB);
    DeleteObject(wB);

    SetStretchBltMode(hD, HALFTONE);
    StretchBlt(hD, pa, pa, sw * sc, sh * sc, hS, r.left, r.top, sw, sh, SRCCOPY);

    BITMAP bm;
    GetObject(hB, sizeof(BITMAP), &bm);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB } };
    std::vector<BYTE> px(bm.bmWidth * bm.bmHeight * 4);
    GetDIBits(hD, hB, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);

    for (size_t i = 0; i < px.size(); i += 4) {
        int g = (px[i + 2] * 299 + px[i + 1] * 587 + px[i] * 114) / 1000;
        px[i] = px[i + 1] = px[i + 2] = (g > 90) ? 0 : 255;
    }
    SetDIBits(hD, hB, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS);

    ret.hBmp = (HBITMAP)CopyImage(hB, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    IStream* pS = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pS);
    {
        Bitmap b(hB, NULL);
        CLSID c;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &c);
        b.Save(pS, &c, NULL);
    }

    HGLOBAL hM = NULL;
    GetHGlobalFromStream(pS, &hM);
    LPVOID pDa = GlobalLock(hM);
    SIZE_T nS = GlobalSize(hM);
    DWORD b6L = 0;

    CryptBinaryToStringA((const BYTE*)pDa, (DWORD)nS, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b6L);
    std::string b6S(b6L, '\0');
    CryptBinaryToStringA((const BYTE*)pDa, (DWORD)nS, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b6S[0], &b6L);

    GlobalUnlock(hM);
    pS->Release();
    DeleteObject(hB);
    DeleteDC(hS);
    DeleteDC(hD);

    if (!b6S.empty() && b6S.back() == '\0') b6S.pop_back();

    std::string json = "{\"base64\": \"" + b6S + "\"}";
    CString res = L"";
    HINTERNET hReq = WinHttpOpenRequest(m_hHttpConnect, L"POST", L"/api/ocr", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (hReq) {
        std::wstring hd = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hReq, hd.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (WinHttpSendRequest(hReq, NULL, 0, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0) && WinHttpReceiveResponse(hReq, NULL)) {
            std::string rS;
            DWORD sz = 0, dL = 0;
            while (WinHttpQueryDataAvailable(hReq, &sz) && sz > 0) {
                std::vector<char> b(sz + 1, 0);
                if (WinHttpReadData(hReq, (LPVOID)b.data(), sz, &dL)) {
                    rS.append(b.data(), dL);
                }
            }
            size_t sP = 0;
            while ((sP = rS.find("\"text\"", sP)) != std::string::npos) {
                size_t cP = rS.find(":", sP);
                size_t q1 = rS.find("\"", cP);
                size_t q2 = rS.find("\"", q1 + 1);
                if (q2 > q1) {
                    std::string t = rS.substr(q1 + 1, q2 - q1 - 1);
                    int wL = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, NULL, 0);
                    if (wL > 0) {
                        std::vector<wchar_t> wB(wL);
                        MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, wB.data(), wL);
                        res += wB.data();
                    }
                }
                sP = q2 + 1;
            }
        }
        else {
            EnsureOcrRunning();
        }
        WinHttpCloseHandle(hReq);
    }

    res.Replace(L"\\n", L"");
    res.Replace(L"\\r", L"");

    // ==========================================================
    // 【核心解码恢复】：把 \uXXXX 重新转回中文字符！
    // ==========================================================
    int uPos = 0;
    while ((uPos = res.Find(L"\\u", uPos)) != -1) {
        if (uPos + 5 < res.GetLength()) {
            CString hexStr = res.Mid(uPos + 2, 4);
            wchar_t wc = (wchar_t)wcstol(hexStr.GetString(), NULL, 16);
            res.Delete(uPos, 6);
            res.Insert(uPos, CString(wc));
            uPos += 1;
        }
        else {
            uPos += 2;
        }
    }
    // 过滤掉 UmiOCR 返回的多余斜杠引号
    res.Replace(L"\\\"", L"");
    res.Trim();

    ret.text = res;
    return ret;
}

void CDNFGameCaptureDlg::EnsureOcrRunning() {
    std::lock_guard<std::mutex> lk(m_launchMutex);
    DWORD now = GetTickCount();
    if (now - m_lastLaunchOcrTime < 10000 || GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) return;

    m_lastLaunchOcrTime = now;
    SHELLEXECUTEINFO s = { sizeof(s) };
    s.fMask = SEE_MASK_FLAG_NO_UI;
    s.lpVerb = L"open";
    s.lpFile = m_ocrExePath;
    s.nShow = SW_SHOWMINNOACTIVE;
    ShellExecuteEx(&s);
}

// ============================================================================
// UI 与数据读写
// ============================================================================
void CDNFGameCaptureDlg::UpdatePlayersFromUI() {
    std::lock_guard<std::mutex> lk(m_dataMutex);
    CString txt;
    m_editNamesInput.GetWindowText(txt);
    int st = 0, cT = -1, rI = 0, bI = 4;
    PlayerData old[8];

    for (int i = 0; i < 8; i++) {
        old[i] = m_players[i];
        m_players[i].name = L"";
        m_players[i].aliases.clear();
        m_players[i].kills = 0;
        m_players[i].deaths = 0;
        m_players[i].akCount = 0;
        m_players[i].team = (i < 4 ? 0 : 1);
    }

    while (st < txt.GetLength()) {
        int nl = txt.Find(L'\n', st);
        CString l = (nl != -1) ? txt.Mid(st, nl - st) : txt.Mid(st);
        st = (nl != -1) ? nl + 1 : (int)txt.GetLength();
        l.Remove(L'\r');
        l.Trim();

        if (l.IsEmpty() || l.Find(L"💡") != -1 || l.Find(L"1. 分队") != -1 ||
            l.Find(L"2. 绑定") != -1 || l.Find(L"3. 手动") != -1 || l.Find(L"4. 手动") != -1) {
            continue;
        }

        if (l.Find(L"红") != -1 && l.Find(L"蓝") != -1 && l.Find(L":") != -1 && l.Find(L"【") == -1) {
            CString m = l.Mid(l.Find(L"红") + 1, l.Find(L"蓝") - l.Find(L"红") - 1);
            int c = m.Find(L":");
            if (c != -1) {
                m_totalScoreRed = _wtoi(m.Left(c));
                m_totalScoreBlue = _wtoi(m.Mid(c + 1));
            }
            continue;
        }

        if (l.Find(L"【红队】") != -1 || l == L"红队") { cT = 0; continue; }
        if (l.Find(L"【蓝队】") != -1 || l == L"蓝队") { cT = 1; continue; }
        if (cT == -1) continue;

        int pI = (cT == 0 ? rI : bI);
        if ((cT == 0 && rI >= 4) || (cT == 1 && bI >= 8)) continue;

        int eP = l.FindOneOf(L"=＝");
        CString nP = (eP != -1 ? l.Left(eP) : l);
        nP.Trim();
        int fP = nP.FindOneOf(L"(（");

        if (fP != -1) {
            m_players[pI].name = nP.Left(fP);
            m_players[pI].name.Trim();
            CString aR = nP.Mid(fP);
            int c = 0;
            while (true) {
                CString tS = aR.Mid(c);
                int Lr = tS.FindOneOf(L"(（"), Rr = tS.FindOneOf(L")）");
                if (Lr == -1 || Rr == -1) break;
                CString aN = aR.Mid(c + Lr + 1, c + Rr - (c + Lr) - 1);
                aN.Trim();
                if (!aN.IsEmpty()) m_players[pI].aliases.push_back({ aN });
                c += Rr + 1;
            }
        }
        else {
            m_players[pI].name = nP;
        }

        if (eP != -1) {
            CString sP = l.Mid(eP + 1);
            sP.Trim();
            int sB = sP.Find(L'[');
            CString mS = (sB != -1 ? sP.Left(sB) : sP);
            mS.Trim();

            int aP = mS.Find(L'A');
            if (aP != -1) {
                CString ak = mS.Mid(aP + 1);
                m_players[pI].akCount = ak.IsEmpty() ? 1 : _wtoi(ak);
                mS = mS.Left(aP);
            }

            int sl = mS.FindOneOf(L"/-");
            if (sl != -1) {
                m_players[pI].kills = _wtoi(mS.Left(sl));
                m_players[pI].deaths = _wtoi(mS.Mid(sl + 1));
            }
            else {
                m_players[pI].kills = _wtoi(mS);
            }
        }
        else {
            m_players[pI].kills = old[pI].kills;
            m_players[pI].deaths = old[pI].deaths;
            m_players[pI].akCount = old[pI].akCount;
        }

        if (cT == 0) rI++; else bI++;
    }

    FilterLivePlatformPrefixes();
    SyncDataToInputBox();
    WriteScoreToFile();
    RefreshDisplay();
}

void CDNFGameCaptureDlg::FilterLivePlatformPrefixes() {
    std::vector<CString> keywords = { L"FSN", L"TV", L"直播", L"抖音", L"快手", L"斗鱼", L"虎牙", L"B站", L"BILIBILI", L"企冲", L"熊猫", L"战旗" };
    for (const CString& kw : keywords) {
        int count = 0;
        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            bool foundInPlayer = false;
            CString upperName = m_players[i].name; upperName.MakeUpper();
            CString upperKw = kw; upperKw.MakeUpper();

            if (upperName.Find(upperKw) != -1) {
                foundInPlayer = true;
            }
            else {
                for (const auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name; upperAlias.MakeUpper();
                    if (upperAlias.Find(upperKw) != -1) { foundInPlayer = true; break; }
                }
            }
            if (foundInPlayer) count++;
        }

        if (count >= 2) {
            CString upperKw = kw; upperKw.MakeUpper();
            for (int i = 0; i < 8; i++) {
                if (m_players[i].name.IsEmpty()) continue;
                CString upperName = m_players[i].name; upperName.MakeUpper();
                int pos = upperName.Find(upperKw);
                while (pos != -1) {
                    m_players[i].name.Delete(pos, kw.GetLength());
                    m_players[i].name.Trim(L"-_. ");
                    upperName = m_players[i].name; upperName.MakeUpper();
                    pos = upperName.Find(upperKw);
                }
                for (auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name; upperAlias.MakeUpper();
                    int apos = upperAlias.Find(upperKw);
                    while (apos != -1) {
                        a.name.Delete(apos, kw.GetLength());
                        a.name.Trim(L"-_. ");
                        upperAlias = a.name; upperAlias.MakeUpper();
                        apos = upperAlias.Find(upperKw);
                    }
                }
            }
        }
    }
}

void CDNFGameCaptureDlg::SyncDataToInputBox() {
    CHARRANGE cr;
    m_editNamesInput.GetSel(cr);
    m_editNamesInput.SetWindowText(L"");

    auto ap = [&](const CString& t, COLORREF c, bool b = false) {
        int l = m_editNamesInput.GetWindowTextLength();
        m_editNamesInput.SetSel(l, l);
        CHARFORMAT cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.crTextColor = c;
        cf.dwEffects = b ? CFE_BOLD : 0;
        m_editNamesInput.SetSelectionCharFormat(cf);
        m_editNamesInput.ReplaceSel(t);
        };

    ap(L"💡 操作说明\r\n1. 分队：输入“红队”或“蓝队”进行换行分组。\r\n2. 绑定小号：主号(小号1)..。小号击杀算给主号。\r\n3. 手动改分：[角色名]=杀/死\r\n4. 手动改AK：后加 A次数\r\n\r\n", RGB(150, 150, 150));
    CString sL;
    sL.Format(L"               红 %d  :  %d 蓝\r\n", m_totalScoreRed, m_totalScoreBlue);
    ap(sL, RGB(0, 150, 0), true);

    auto renderTeam = [&](int startIdx, int endIdx, COLORREF color, const CString& title) {
        ap(title, color, true);
        for (int i = startIdx; i < endIdx; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            CString l = L"  " + m_players[i].name;
            for (auto& a : m_players[i].aliases) {
                l += L"(" + a.name + L")";
            }
            l.AppendFormat(L" = %d/%d", m_players[i].kills, m_players[i].deaths);
            if (m_players[i].akCount == 1) l += L" A";
            else if (m_players[i].akCount > 1) l.AppendFormat(L" A%d", m_players[i].akCount);
            ap(l + L"\r\n", color);
        }
        };

    renderTeam(0, 4, RGB(220, 0, 0), L"【红队】\r\n");
    renderTeam(4, 8, RGB(0, 0, 220), L"【蓝队】\r\n");

    m_editNamesInput.SetSel(cr);
    SaveConfigToFile();
}

void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) r.push_back(m_players[i]);
        else b.push_back(m_players[i]);
    }
    while (r.size() < 4) r.push_back({ L"",0,{},0,0,0,0 });
    while (b.size() < 4) b.push_back({ L"",1,{},0,0,0,0 });

    std::vector<PlayerData>& lT = m_bFlipSides ? b : r;
    std::vector<PlayerData>& rT = m_bFlipSides ? r : b;

    CString pathScore = m_outputDir + L"\\比分.txt";
    CString pathLeft = m_outputDir + L"\\左侧人头.txt";
    CString pathRight = m_outputDir + L"\\右侧人头.txt";
    CString pathKill = m_outputDir + L"\\击杀.txt";

    FILE* fS = NULL;
    if (_wfopen_s(&fS, pathScore, L"wt, ccs=UTF-8") == 0 && fS) {
        fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
        fclose(fS);
    }

    auto gs_full = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L"");
        CString s;
        s.Format(L"%s%02d/%02d", p.name.GetString(), p.kills, p.deaths);
        if (p.akCount == 1) s += L" A";
        else if (p.akCount > 1) s.AppendFormat(L" A%d", p.akCount);
        return s;
        };

    FILE* fKL = NULL;
    if (_wfopen_s(&fKL, pathLeft, L"wt, ccs=UTF-8") == 0 && fKL) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_full(lT[i]);
            if (!ls.IsEmpty()) fwprintf(fKL, L"%s\n", ls.GetString());
        }
        fclose(fKL);
    }

    FILE* fKR = NULL;
    if (_wfopen_s(&fKR, pathRight, L"wt, ccs=UTF-8") == 0 && fKR) {
        for (int i = 0; i < 4; i++) {
            CString rs = gs_full(rT[i]);
            if (!rs.IsEmpty()) fwprintf(fKR, L"%s\n", rs.GetString());
        }
        fclose(fKR);
    }

    auto gs_kill_only = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L"");
        CString s;
        s.Format(L"%s%02d", p.name.GetString(), p.kills);
        if (p.akCount == 1) s += L" A";
        else if (p.akCount > 1) s.AppendFormat(L"A%d", p.akCount);
        return s;
        };

    FILE* fKill = NULL;
    if (_wfopen_s(&fKill, pathKill, L"wt, ccs=UTF-8") == 0 && fKill) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_kill_only(lT[i]);
            CString rs = gs_kill_only(rT[i]);
            if (ls.IsEmpty() && rs.IsEmpty()) continue;
            int pad = max(1, 9 - GetVisualWidth(ls));
            CString spaces(L' ', pad);
            fwprintf(fKill, L"%s%s%s\n", ls.GetString(), spaces.GetString(), rs.GetString());
        }
        fclose(fKill);
    }
}

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");
    CString sS;
    sS.Format(L"============= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);

    auto ap = [&](const CString& t, COLORREF c) {
        int l = m_editOcrResult.GetWindowTextLength();
        m_editOcrResult.SetSel(l, l);
        CHARFORMAT cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = c;
        m_editOcrResult.SetSelectionCharFormat(cf);
        m_editOcrResult.ReplaceSel(t);
        };

    ap(sS, RGB(0, 100, 0));
    ap(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0));
    ap(L"------------------------------------------\r\n", RGB(150, 150, 150));

    std::vector<int> rI, bI;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) rI.push_back(i); else bI.push_back(i);
    }

    std::vector<int>& lIdx = m_bFlipSides ? bI : rI;
    std::vector<int>& rIdx = m_bFlipSides ? rI : bI;
    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0);
    COLORREF rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    for (size_t i = 0; i < (std::max)(lIdx.size(), rIdx.size()); i++) {
        CString lT = L"";
        if (i < lIdx.size()) {
            int p = lIdx[i];
            lT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) lT += L" A";
            else if (m_players[p].akCount > 1) lT.AppendFormat(L" A%d", m_players[p].akCount);
        }
        ap(lT, lC);

        int curW = GetVisualWidth(lT);
        for (int s = 0; s < (32 - curW); s++) ap(L" ", 0);

        CString rT = L"";
        if (i < rIdx.size()) {
            int p = rIdx[i];
            rT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) rT += L" A";
            else if (m_players[p].akCount > 1) rT.AppendFormat(L" A%d", m_players[p].akCount);
            rT += L"\r\n";
        }
        else {
            rT = L"\r\n";
        }
        ap(rT, rC);
    }
}

void CDNFGameCaptureDlg::SaveConfigToFile() {
    if (!m_editNamesInput.m_hWnd) return;
    CString text;
    m_editNamesInput.GetWindowText(text);
    if (text.IsEmpty()) return;

    CFile file;
    if (file.Open(m_configPath, CFile::modeCreate | CFile::modeWrite)) {
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        file.Write(bom, 3);
        std::string utf8 = CW2A(text, CP_UTF8);
        file.Write(utf8.c_str(), (UINT)utf8.length());
        file.Close();
    }
}


// ============================================================================
// 绘制模块
// ============================================================================
void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;

    CPen p1(PS_SOLID, 2, RGB(255, 0, 0)), p3(PS_SOLID, 2, RGB(255, 165, 0));
    dc.SelectStockObject(NULL_BRUSH);
    dc.SelectObject(&p1);

    float pX[4] = { 0.187f, 0.157f, 0.840f, 0.810f };
    float pY[4] = { 0.036f, 0.034f, 0.039f, 0.039f };

    for (int i = 0; i < 4; i++) {
        dc.Ellipse(m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) - 5,
            m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) - 5,
            m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) + 5,
            m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) + 5);
    }

    dc.SelectObject(&p3);
    for (int i = 0; i < 16; i++) {
        dc.Ellipse(m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) - 5,
            m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) - 5,
            m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) + 5,
            m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) + 5);
    }

    CString h;
    {
        std::lock_guard<std::mutex> lk(m_debugMutex);
        h = m_debugOcrResult;
    }

    if (!h.IsEmpty()) {
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 255, 0));
        CFont f; f.CreatePointFont(105, L"黑体");
        CFont* of = dc.SelectObject(&f);
        CRect tR(0, 0, 0, 0);
        dc.DrawText(h, &tR, DT_LEFT | DT_TOP | DT_CALCRECT);
        CRect cr(m_previewRect.left + 15, m_previewRect.bottom - 25 - tR.Height(), m_previewRect.left + 15 + tR.Width(), m_previewRect.bottom - 25);
        cr.InflateRect(8, 8);
        dc.FillSolidRect(&cr, RGB(25, 25, 25));
        dc.DrawText(h, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(of);
    }

    HBITMAP hL = NULL, hR = NULL;
    {
        std::lock_guard<std::mutex> lkBmp(m_ocrRecordMutex);
        if (m_viewIndexLeft >= 0 && m_viewIndexLeft < (int)m_ocrRecordsLeft.size()) {
            hL = m_ocrRecordsLeft[m_viewIndexLeft].hBmp;
        }
        else if (!m_ocrRecordsLeft.empty()) {
            hL = m_ocrRecordsLeft.back().hBmp;
        }
        if (m_viewIndexRight >= 0 && m_viewIndexRight < (int)m_ocrRecordsRight.size()) {
            hR = m_ocrRecordsRight[m_viewIndexRight].hBmp;
        }
        else if (!m_ocrRecordsRight.empty()) {
            hR = m_ocrRecordsRight.back().hBmp;
        }
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
}

void CDNFGameCaptureDlg::OnPaint() {
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

    int splitY = max(100, r.bottom - (int)(420 * WINDOW_SCALE));
    CRect topHalf(0, 0, r.right, splitY);
    CRect uiRect(0, splitY, r.right, r.bottom);

    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(95, L"微软雅黑");
        int row1_Y = splitY + 5;

        m_chkFlip.Create(L"翻转红蓝(蓝左红右)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(10, row1_Y, 200, row1_Y + 25), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font);

        m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(210, row1_Y + 4, 380, row1_Y + 25), this, 1003);
        m_status.SetFont(&m_font);

        int cmbW = 150;
        m_cmbRight.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(r.right - 10 - cmbW, row1_Y, r.right - 10, row1_Y + 300), this, 1009);
        m_cmbRight.SetFont(&m_font);
        m_cmbRight.AddString(L"[蓝]自动");
        m_cmbRight.SetCurSel(0);

        m_cmbLeft.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(r.right - 10 - cmbW * 2 - 10, row1_Y, r.right - 10 - cmbW - 10, row1_Y + 300), this, 1010);
        m_cmbLeft.SetFont(&m_font);
        m_cmbLeft.AddString(L"[红]自动");
        m_cmbLeft.SetCurSel(0);

        int halfW = (r.right - 30) / 2;
        int row2_Y = row1_Y + 30;
        int row2_Bottom = r.bottom - (int)(75 * WINDOW_SCALE);

        m_editNamesInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL, CRect(10, row2_Y, 10 + halfW, row2_Bottom), this, 1001);
        m_editNamesInput.SetFont(&m_font);

        int scoreH = (int)(150 * WINDOW_SCALE);
        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(20 + halfW, row2_Y, r.right - 10, row2_Y + scoreH), this, 1002);
        m_editOcrResult.SetFont(&m_font);

        m_editVisualLogs.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(20 + halfW, row2_Y + scoreH + 5, r.right - 10, row2_Bottom), this, 1011);
        m_editVisualLogs.SetFont(&m_font);
        m_editVisualLogs.SetBackgroundColor(FALSE, RGB(30, 30, 30));

        int btnY = row2_Bottom + 8;
        int btnH = (int)(28 * WINDOW_SCALE);
        int bW = (r.right - 40) / 3;

        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, btnY, 10 + bW, btnY + btnH), this, ID_BTN_START);
        m_btnStart.SetFont(&m_font);
        m_btnApply.Create(L"应用修改", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(20 + bW, btnY, 20 + bW * 2, btnY + btnH), this, ID_BTN_APPLY);
        m_btnApply.SetFont(&m_font);
        m_btnReset.Create(L"战绩归零", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(30 + bW * 2, btnY, r.right - 10, btnY + btnH), this, ID_BTN_RESET);
        m_btnReset.SetFont(&m_font);

        int dirY = btnY + btnH + 5;
        m_editOutDir.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, CRect(10, dirY, r.right - 100, dirY + btnH), this, ID_EDIT_DIR);
        m_editOutDir.SetFont(&m_font);
        m_editOutDir.SetWindowText(m_outputDir);

        m_btnBrowseDir.Create(L"更改目录", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(r.right - 90, dirY, r.right - 10, dirY + btnH), this, ID_BTN_BROWSE);
        m_btnBrowseDir.SetFont(&m_font);

        bool configLoaded = false;
        CFile file;
        if (file.Open(m_configPath, CFile::modeRead)) {
            int len = (int)file.GetLength();
            if (len > 0) {
                char* buf = new char[len + 1];
                file.Read(buf, len);
                buf[len] = 0;
                char* start = buf;
                if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
                    start += 3;
                }
                int wLen = MultiByteToWideChar(CP_UTF8, 0, start, -1, NULL, 0);
                if (wLen > 0) {
                    wchar_t* wBuf = new wchar_t[wLen + 1];
                    MultiByteToWideChar(CP_UTF8, 0, start, -1, wBuf, wLen);
                    wBuf[wLen] = 0;
                    m_editNamesInput.SetWindowText(wBuf);
                    configLoaded = true;
                    delete[] wBuf;
                }
                delete[] buf;
            }
            file.Close();
        }

        if (configLoaded) {
            UpdatePlayersFromUI();
        }
        else {
            SyncDataToInputBox();
            RefreshDisplay();
            WriteScoreToFile();
        }
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
            memDC.StretchBlt(m_previewRect.left, m_previewRect.top, m_previewRect.Width(), m_previewRect.Height(), CDC::FromHandle(hBmpDC), 0, 0, m_w, m_h, SRCCOPY);
            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }
    }

    Draw(memDC);
    dc.BitBlt(0, 0, topHalf.Width(), topHalf.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
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
        {
            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (m_bmp) {
                HDC hDC = ::GetDC(NULL);
                HDC hSrc = CreateCompatibleDC(hDC);
                HDC hDst = CreateCompatibleDC(hDC);
                if (!m_historyBmps[m_historyIdx]) {
                    m_historyBmps[m_historyIdx] = CreateCompatibleBitmap(hDC, m_w, m_h);
                }
                HGDIOBJ os = SelectObject(hSrc, m_bmp);
                HGDIOBJ od = SelectObject(hDst, m_historyBmps[m_historyIdx]);
                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
                SelectObject(hSrc, os);
                SelectObject(hDst, od);
                DeleteDC(hSrc);
                DeleteDC(hDst);
                ::ReleaseDC(NULL, hDC);
                m_historyIdx = (m_historyIdx + 1) % 25;
            }
        }

        {
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
    }
}