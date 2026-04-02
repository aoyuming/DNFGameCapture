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
// 全局变量与线程锁定义
// ============================================================================
std::mutex g_bmpMutex;

struct VisualLogMsg {
    CString text;
    COLORREF color;
};
std::deque<VisualLogMsg> g_visualLogs; // 浮动日志队列
std::mutex g_visualLogMutex;

const float WINDOW_SCALE = 1.6f;
const int ID_BTN_START = 1005;
const int ID_BTN_APPLY = 1006;
const int ID_CHK_FLIP = 1007;
const int ID_BTN_RESET = 1008;
const int ID_BTN_BROWSE = 1013;
const int ID_EDIT_DIR = 1014;

// 计分板触发颜色检测坐标点
struct ScorePointF { float x; float y; };
ScorePointF g_scorePts[16] = {
    { 0.1594f, 0.0348f }, { 0.1922f, 0.0377f }, { 0.1761f, 0.1116f }, { 0.1957f, 0.1138f },
    { 0.2738f, 0.1127f }, { 0.2925f, 0.1127f }, { 0.3714f, 0.1104f }, { 0.3902f, 0.1138f },
    { 0.8105f, 0.0338f }, { 0.8457f, 0.0372f }, { 0.6085f, 0.1104f }, { 0.6281f, 0.1116f },
    { 0.7050f, 0.1127f }, { 0.7242f, 0.1127f }, { 0.8019f, 0.1127f }, { 0.8214f, 0.1116f },
};

// ============================================================================
// 全局辅助函数
// ============================================================================
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
// 一机一码授权验证系统实现
// ============================================================================
CString CDNFGameCaptureDlg::GetMachineID() {
    DWORD volSerial = 0;
    GetVolumeInformation(L"C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0);
    CString hwid;
    hwid.Format(L"%08X", volSerial);
    return hwid;
}

CString CDNFGameCaptureDlg::GenerateKey(CString machineID) {
    long long val = wcstoll(machineID, NULL, 16);
    val ^= 0x5AA55AA5;
    CString key;
    key.Format(L"DNF-%08llX-KEY", val);
    return key;
}

bool CDNFGameCaptureDlg::CheckLicense() {
    CString hwid = GetMachineID();
    CString expectedKey = GenerateKey(hwid);

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    bool bAuthorized = false;
    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        char buf[256] = { 0 };
        file.Read(buf, 255);
        file.Close();

        CString inputKey(buf);
        inputKey.Trim();
        if (inputKey == expectedKey) {
            bAuthorized = true;
        }
    }

    if (!bAuthorized) {
        if (::OpenClipboard(NULL)) {
            ::EmptyClipboard();
            size_t size = (hwid.GetLength() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
            memcpy(GlobalLock(hMem), hwid.GetBuffer(), size);
            GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
            ::CloseClipboard();
        }
        CString msg;
        msg.Format(L"软件未授权！\r\n\r\n您的机器码是： %s\r\n\r\n(机器码已自动复制)\r\n请将机器码发给作者，获取 license.txt 并放入软件同目录。", hwid);
        MessageBox(msg, L"授权拦截", MB_ICONERROR | MB_OK);
    }
    return bAuthorized;
}

// ============================================================================
// 初始化与窗口过程
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
#ifndef _DEBUG
    if (!CheckLicense()) {
        exit(0);
    }
#endif

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
    else {
        m_hHttpConnect = NULL;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString appDir = exePath;
    int pos = appDir.ReverseFind(L'\\');
    if (pos != -1) {
        appDir = appDir.Left(pos + 1);
    }

    m_ocrExePath = appDir + L"Umi-OCR.exe";
    m_configPath = appDir + L"players_config.txt";
    m_iniPath = appDir + L"config.ini";

    wchar_t dirBuf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"OutputDir", appDir, dirBuf, MAX_PATH, m_iniPath);
    m_outputDir = dirBuf;
    if (m_outputDir.Right(1) == L"\\") {
        m_outputDir.TrimRight(L"\\");
    }

    for (int i = 0; i < 25; i++) {
        m_historyBmps[i] = NULL;
    }
    AfxInitRichEdit2();

    for (int i = 0; i < 8; i++) {
        m_players[i].kills = 0;
        m_players[i].deaths = 0;
        m_players[i].currentStreak = 0;
        m_players[i].akCount = 0;
    }

    m_players[0].name = L"白羽"; m_players[0].team = 0; m_players[0].aliases.push_back({ L"抖音FSN白羽", 0, 1, 0, 0 });
    m_players[1].name = L"大崩"; m_players[1].team = 0; m_players[1].aliases.push_back({ L"流年兮", 0, 1, 0, 0 });
    m_players[2].name = L"夏法"; m_players[2].team = 0;
    m_players[3].name = L"逍遥"; m_players[3].team = 0;
    m_players[4].name = L"老王"; m_players[4].team = 1; m_players[4].aliases.push_back({ L"旋律", 4, 3, 0, 0 });
    m_players[5].name = L"夜风"; m_players[5].team = 1;
    m_players[6].name = L"二海"; m_players[6].team = 1; m_players[6].aliases.push_back({ L"疯疯熊冲鸭", 0, 0, 0, 0 });
    m_players[7].name = L"九哥"; m_players[7].team = 1; m_players[7].aliases.push_back({ L"米叹米叹", 0, 0, 0, 0 });

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CreateEx(0, cls, L"DNF击杀统计 - 终极后台运行版", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100, (int)(750 * WINDOW_SCALE), (int)(730 * WINDOW_SCALE), NULL, NULL);

    InitTrayIcon();
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    RemoveTrayIcon();
    if (m_bmp) {
        ::DeleteObject(m_bmp);
    }
    for (int i = 0; i < 25; i++) {
        if (m_historyBmps[i]) {
            ::DeleteObject(m_historyBmps[i]);
        }
    }
    if (m_hDebugOcrBmp[0]) {
        ::DeleteObject(m_hDebugOcrBmp[0]);
    }
    if (m_hDebugOcrBmp[1]) {
        ::DeleteObject(m_hDebugOcrBmp[1]);
    }

    {
        std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
        for (auto& r : m_ocrRecordsLeft) {
            if (r.hBmp) DeleteObject(r.hBmp);
        }
        for (auto& r : m_ocrRecordsRight) {
            if (r.hBmp) DeleteObject(r.hBmp);
        }
    }

    if (m_hHttpConnect) {
        WinHttpCloseHandle(m_hHttpConnect);
    }
    if (m_hHttpSession) {
        WinHttpCloseHandle(m_hHttpSession);
    }
    GdiplusShutdown(m_gdiplusToken);
}

// ============================================================================
// 托盘与后台机制
// ============================================================================
void CDNFGameCaptureDlg::InitTrayIcon() {
    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = GetSafeHwnd();
    m_nid.uID = 10001;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_MESSAGE;

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    m_nid.hIcon = ExtractIcon(AfxGetInstanceHandle(), exePath, 0);
    if (!m_nid.hIcon) {
        m_nid.hIcon = AfxGetApp()->LoadStandardIcon(IDI_APPLICATION);
    }

    wcscpy_s(m_nid.szTip, L"DNF击杀统计 - 运行中");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void CDNFGameCaptureDlg::RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
}

void CDNFGameCaptureDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if ((nID & 0xFFF0) == SC_CLOSE || (nID & 0xFFF0) == SC_MINIMIZE) {
        ShowWindow(SW_HIDE);
        return;
    }
    CWnd::OnSysCommand(nID, lParam);
}

LRESULT CDNFGameCaptureDlg::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
        ShowWindow(SW_SHOW);
        ShowWindow(SW_RESTORE);
        SetForegroundWindow();
    }
    else if (lParam == WM_RBUTTONUP) {
        CPoint pt;
        GetCursorPos(&pt);
        CMenu menu;
        menu.CreatePopupMenu();
        menu.AppendMenu(MF_STRING, 101, L"显示面板");
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, 102, L"完全退出");
        SetForegroundWindow();
        int cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, this);
        if (cmd == 101) {
            ShowWindow(SW_SHOW);
            ShowWindow(SW_RESTORE);
            SetForegroundWindow();
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

// ============================================================================
// 按钮事件与文件存储
// ============================================================================
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

void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) {
            r.push_back(m_players[i]);
        }
        else {
            b.push_back(m_players[i]);
        }
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
        int tk = p.kills, td = p.deaths, tak = p.akCount;
        for (auto& a : p.aliases) {
            tk += a.kills;
            td += a.deaths;
            tak += a.akCount;
        }
        CString s;
        s.Format(L"%s%02d/%02d", p.name.GetString(), tk, td);
        if (tak == 1) {
            s += L" A";
        }
        else if (tak > 1) {
            s.AppendFormat(L" A%d", tak);
        }
        return s;
        };

    FILE* fKL = NULL;
    if (_wfopen_s(&fKL, pathLeft, L"wt, ccs=UTF-8") == 0 && fKL) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_full(lT[i]);
            if (!ls.IsEmpty()) {
                fwprintf(fKL, L"%s\n", ls.GetString());
            }
        }
        fclose(fKL);
    }

    FILE* fKR = NULL;
    if (_wfopen_s(&fKR, pathRight, L"wt, ccs=UTF-8") == 0 && fKR) {
        for (int i = 0; i < 4; i++) {
            CString rs = gs_full(rT[i]);
            if (!rs.IsEmpty()) {
                fwprintf(fKR, L"%s\n", rs.GetString());
            }
        }
        fclose(fKR);
    }

    auto gs_kill_only = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L"");
        int tk = p.kills, tak = p.akCount;
        for (auto& a : p.aliases) {
            tk += a.kills;
            tak += a.akCount;
        }
        CString s;
        s.Format(L"%s%02d", p.name.GetString(), tk);
        if (tak == 1) {
            s += L" A";
        }
        else if (tak > 1) {
            s.AppendFormat(L"A%d", tak);
        }
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


void CDNFGameCaptureDlg::EnsureOcrRunning() {
    std::lock_guard<std::mutex> lock(m_launchMutex);
    DWORD now = GetTickCount();
    if (now - m_lastLaunchOcrTime < 10000) return;

    m_lastLaunchOcrTime = now;
    if (GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) return;

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = m_ocrExePath.GetString();
    sei.nShow = SW_SHOWMINNOACTIVE;
    ShellExecuteEx(&sei);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) {
    return TRUE;
}

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
    UpdatePlayersFromUI();
    m_status.SetWindowText(L"修改已生效！");
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
            for (auto& a : m_players[i].aliases) {
                a.kills = 0;
                a.deaths = 0;
                a.currentStreak = 0;
                a.akCount = 0;
            }
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
        SetTimer(2, 13000, NULL);
    }
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

    std::vector<HBITMAP> historyClones;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        for (int i = 1; i <= 25; i++) {
            int idx = (m_historyIdx - i + 25) % 25;
            if (m_historyBmps[idx]) {
                HDC hDC = ::GetDC(NULL);
                HDC hSrc = CreateCompatibleDC(hDC);
                HDC hDst = CreateCompatibleDC(hDC);
                HBITMAP clone = CreateCompatibleBitmap(hDC, m_w, m_h);
                HGDIOBJ os = SelectObject(hSrc, m_historyBmps[idx]);
                HGDIOBJ od = SelectObject(hDst, clone);
                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
                SelectObject(hSrc, os);
                SelectObject(hDst, od);
                DeleteDC(hSrc);
                DeleteDC(hDst);
                ::ReleaseDC(NULL, hDC);
                historyClones.push_back(clone);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
        for (auto& r : m_ocrRecordsLeft) if (r.hBmp) DeleteObject(r.hBmp);
        for (auto& r : m_ocrRecordsRight) if (r.hBmp) DeleteObject(r.hBmp);
        m_ocrRecordsLeft.clear();
        m_ocrRecordsRight.clear();
        m_viewIndexLeft = -1;
        m_viewIndexRight = -1;
    }
    PostMessage(WM_UPDATE_OCR_DROPDOWNS, 1, 0);

    for (size_t i = 0; i < historyClones.size(); i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved)) break;

        HBITMAP hSnapshot = historyClones[i];
        std::future<OcrResultData> futKiller, futDead;

        if (!killerResolved) {
            futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        }
        if (!deadResolved) {
            futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);
        }

        OcrResultData resK = { L"", NULL }, resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get();
        if (futDead.valid()) resD = futDead.get();

        auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller, int& outBestP, int& outBestA, int& frameScore) -> bool {
            frameScore = -2;
            if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1) return false;

            CString logMsg;
            logMsg.Format(L"▶ [%s] 第%d帧 OCR提取: \"%s\"", isKiller ? L"找杀手" : L"找死者", i + 1, (LPCTSTR)ocrResult);
            WriteMatchLog(logMsg);
            { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ logMsg, RGB(255, 215, 0) }); }

            int maxS = -2, bestP = -1, bestA = -1, bestRealLen = 0;
            std::wstring bestN = L"";

            m_dataMutex.lock();
            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                int teamPenalty = 0;
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) teamPenalty = 20;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) teamPenalty = 20;

                int curScore = m_matcher.GetMatchScore(m_players[p].name.GetString(), ocrResult.GetString());
                if (curScore == -1) { maxS = -1; break; }
                curScore -= teamPenalty;

                std::wstring curBestN = m_players[p].name.GetString();
                int curBestA = -1, curRealLen = m_players[p].name.GetLength();

                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    int as = m_matcher.GetMatchScore(m_players[p].aliases[a].name.GetString(), ocrResult.GetString());
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
                    maxS = curScore;
                    bestP = p;
                    bestA = curBestA;
                    bestN = curBestN;
                    bestRealLen = curRealLen;
                }
            }
            m_dataMutex.unlock();

            frameScore = maxS;

            if (maxS == -1) {
                CString jobLog = L"  └ [⚠️职业干扰] 检测到职业名，跳过比对...";
                WriteMatchLog(jobLog);
                { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ jobLog, RGB(180, 180, 180) }); }
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
                successLog.Format(L"  └ [✔匹配] 指向: %s (得分:%d, 达标:%d)", (LPCTSTR)finalName, maxS, passLine);
                WriteMatchLog(successLog);
                { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ successLog, (m_players[bestP].team == 0) ? RGB(255, 120, 120) : RGB(120, 180, 255) }); }

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[bestP].team;
                else lockedDeadTeam = m_players[bestP].team;
                m_dataMutex.unlock();
            }
            else {
                CString failLog;
                failLog.Format(L"  └ [✖失败] 最高 %d 分，未及格 %d 分", maxS, passLine);
                WriteMatchLog(failLog);
                { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ failLog, RGB(180, 180, 180) }); }
            }
            return false;
            };

        int kScore = -2, dScore = -2;
        bool kIsJob = processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore);
        bool dIsJob = processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore);

        OcrResultData& resL = killerIsLeft ? resK : resD;
        OcrResultData& resR = killerIsLeft ? resD : resK;
        int scoreL = killerIsLeft ? kScore : dScore;
        int scoreR = killerIsLeft ? dScore : kScore;
        bool isJobL = killerIsLeft ? kIsJob : dIsJob;
        bool isJobR = killerIsLeft ? dIsJob : kIsJob;
        bool resolvedL = killerIsLeft ? killerResolved : deadResolved;
        bool resolvedR = killerIsLeft ? deadResolved : killerResolved;
        CString finalNameL = killerIsLeft ? finalKillerName : finalDeadName;
        CString finalNameR = killerIsLeft ? finalDeadName : finalKillerName;

        {
            std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
            if (resL.hBmp) {
                CString shortT = resL.text;
                if (shortT.GetLength() > 8) shortT = shortT.Left(8) + L"..";
                CString lbl;
                if (resolvedL && scoreL == -2) lbl.Format(L"第%d帧 [已锁: %s]", i + 1, (LPCTSTR)finalNameL);
                else if (isJobL) lbl.Format(L"第%d帧 [屏蔽职业]", i + 1);
                else if (shortT.IsEmpty()) lbl.Format(L"第%d帧 [空/无识别]", i + 1);
                else lbl.Format(L"第%d帧 %s(%d分)", i + 1, (LPCTSTR)shortT, scoreL);
                m_ocrRecordsLeft.push_back({ resL.hBmp, lbl });
            }
            if (resR.hBmp) {
                CString shortT = resR.text;
                if (shortT.GetLength() > 8) shortT = shortT.Left(8) + L"..";
                CString lbl;
                if (resolvedR && scoreR == -2) lbl.Format(L"第%d帧 [已锁: %s]", i + 1, (LPCTSTR)finalNameR);
                else if (isJobR) lbl.Format(L"第%d帧 [屏蔽职业]", i + 1);
                else if (shortT.IsEmpty()) lbl.Format(L"第%d帧 [空/无识别]", i + 1);
                else lbl.Format(L"第%d帧 %s(%d分)", i + 1, (LPCTSTR)shortT, scoreR);
                m_ocrRecordsRight.push_back({ resR.hBmp, lbl });
            }
        }

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult.Format(L"【时光倒流帧 %d/25】当前锁定 - 杀:%s 亡:%s",
                (int)(i + 1), killerResolved ? finalKillerName : L"未定", deadResolved ? finalDeadName : L"未定");
        }

        PostMessage(WM_UPDATE_OCR_DROPDOWNS, 0, 0);
        InvalidateRect(&m_previewRect, FALSE);
        UpdateWindow();
    }

    if (!killerResolved && globalKillerBestP != -1 && globalKillerBestScore >= (globalKillerPassLine - 20) && globalKillerBestScore >= 35) {
        killerResolved = true; killerBestP = globalKillerBestP; killerBestA = globalKillerBestA; finalKillerName = globalKillerName;
        CString fallbackLog; fallbackLog.Format(L"  └ [⚠️降级录取] 勉强认出杀手: %s (得分:%d)", (LPCTSTR)finalKillerName, globalKillerBestScore);
        WriteMatchLog(fallbackLog);
        { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ fallbackLog, RGB(255, 165, 0) }); }
    }
    if (!deadResolved && globalDeadBestP != -1 && globalDeadBestScore >= (globalDeadPassLine - 20) && globalDeadBestScore >= 35) {
        deadResolved = true; deadBestP = globalDeadBestP; deadBestA = globalDeadBestA; finalDeadName = globalDeadName;
        CString fallbackLog; fallbackLog.Format(L"  └ [⚠️降级录取] 勉强认出死者: %s (得分:%d)", (LPCTSTR)finalDeadName, globalDeadBestScore);
        WriteMatchLog(fallbackLog);
        { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ fallbackLog, RGB(255, 165, 0) }); }
    }
    if ((!killerResolved || !deadResolved) && (killerResolved || deadResolved)) {
        CString timeoutLog;
        timeoutLog.Format(L"  └ [⌛回溯完毕] 历史帧中未找到另一方，进行单边结算兜底 (杀手:%s, 死者:%s)", killerResolved ? finalKillerName : L"未定", deadResolved ? finalDeadName : L"未定");
        WriteMatchLog(timeoutLog);
        { std::lock_guard<std::mutex> lk(g_visualLogMutex); g_visualLogs.push_back({ timeoutLog, RGB(255, 140, 0) }); }
    }

    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();
        bool isDuplicate = false;

        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 5000)) {
                isDuplicate = true; break;
            }
        }

        if (!isDuplicate) {
            m_recentEvents.push_back({ finalKillerName, finalDeadName, now });
            m_recentEvents.erase(std::remove_if(m_recentEvents.begin(), m_recentEvents.end(), [&](const RecentEvent& ev) {
                return now - ev.time > 10000;
                }), m_recentEvents.end());

            auto addEventLog = [&](const CString& msg, COLORREF color) {
                WriteMatchLog(msg);
                std::lock_guard<std::mutex> lk(g_visualLogMutex);
                g_visualLogs.push_back({ msg, color });
                };

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++) {
                    if (p != killerBestP) {
                        m_players[p].currentStreak = 0;
                        for (auto& a : m_players[p].aliases) a.currentStreak = 0;
                    }
                }

                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                CString actionLog;

                if (killerBestA != -1) {
                    m_players[killerBestP].aliases[killerBestA].kills++;
                    m_players[killerBestP].aliases[killerBestA].currentStreak++;

                    actionLog.Format(L"⚔ [击杀] 小号 [%s] 拿下一击！当前连杀: %d", (LPCTSTR)m_players[killerBestP].aliases[killerBestA].name, m_players[killerBestP].aliases[killerBestA].currentStreak);
                    addEventLog(actionLog, teamColor);

                    if (m_players[killerBestP].aliases[killerBestA].currentStreak == 4) {
                        m_players[killerBestP].aliases[killerBestA].akCount++;
                        m_players[killerBestP].aliases[killerBestA].currentStreak = 0;

                        actionLog.Format(L"🌟 [AK宣告] 恐怖如斯！小号 [%s] 完成一次 AK！", (LPCTSTR)m_players[killerBestP].aliases[killerBestA].name);
                        addEventLog(actionLog, RGB(255, 215, 0));
                    }
                    m_players[killerBestP].currentStreak = 0;
                    for (int a = 0; a < m_players[killerBestP].aliases.size(); a++) {
                        if (a != killerBestA) m_players[killerBestP].aliases[a].currentStreak = 0;
                    }
                }
                else {
                    m_players[killerBestP].kills++;
                    m_players[killerBestP].currentStreak++;

                    actionLog.Format(L"⚔ [击杀] 玩家 [%s] 拿下一击！当前连杀: %d", (LPCTSTR)m_players[killerBestP].name, m_players[killerBestP].currentStreak);
                    addEventLog(actionLog, teamColor);

                    if (m_players[killerBestP].currentStreak == 4) {
                        m_players[killerBestP].akCount++;
                        m_players[killerBestP].currentStreak = 0;

                        actionLog.Format(L"🌟 [AK宣告] 恐怖如斯！玩家 [%s] 完成一次 AK！", (LPCTSTR)m_players[killerBestP].name);
                        addEventLog(actionLog, RGB(255, 215, 0));
                    }
                    for (auto& a : m_players[killerBestP].aliases) a.currentStreak = 0;
                }
                m_lastKillerTeam = m_players[killerBestP].team;
            }

            if (deadResolved && deadBestP != -1) {
                if (deadBestA != -1) m_players[deadBestP].aliases[deadBestA].deaths++;
                else m_players[deadBestP].deaths++;
            }

            if (m_bPendingTeamScoreWin) {
                m_bPendingTeamScoreWin = false;
                CString scoreLog;
                if (m_lastKillerTeam == 0) {
                    m_totalScoreRed++;
                    scoreLog.Format(L"🏆 [结算] 绝杀！红队拿下本局！当前大比分 红 %d : %d 蓝", m_totalScoreRed, m_totalScoreBlue);
                    addEventLog(scoreLog, RGB(255, 50, 50));
                }
                else if (m_lastKillerTeam == 1) {
                    m_totalScoreBlue++;
                    scoreLog.Format(L"🏆 [结算] 绝杀！蓝队拿下本局！当前大比分 红 %d : %d 蓝", m_totalScoreRed, m_totalScoreBlue);
                    addEventLog(scoreLog, RGB(50, 150, 255));
                }
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
OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex) {
    OcrResultData ret = { L"", NULL };
    if (!m_hHttpConnect) return ret;

    RECT r_game = (nAreaIndex == 0) ?
        RECT{ (long)(m_w * 0.190f), (long)(m_h * 0.004f), (long)(m_w * 0.360f), (long)(m_h * 0.040f) } :
        RECT{ (long)(m_w * 0.655f), (long)(m_h * 0.004f), (long)(m_w * 0.815f), (long)(m_h * 0.040f) };

    int sw = r_game.right - r_game.left;
    int sh = r_game.bottom - r_game.top;
    int scale = 2;
    int pad = 30;
    int dstW = sw * scale + pad * 2;
    int dstH = sh * scale + pad * 2;

    HDC hSrcDC = CreateCompatibleDC(NULL);
    HDC hDstDC = CreateCompatibleDC(NULL);
    HBITMAP hDstBmp = CreateCompatibleBitmap(GetDC()->GetSafeHdc(), dstW, dstH);

    SelectObject(hSrcDC, hTargetBmp);
    SelectObject(hDstDC, hDstBmp);

    RECT bgRect = { 0, 0, dstW, dstH };
    HBRUSH hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hDstDC, &bgRect, hWhiteBrush);
    DeleteObject(hWhiteBrush);

    SetStretchBltMode(hDstDC, HALFTONE);
    StretchBlt(hDstDC, pad, pad, sw * scale, sh * scale, hSrcDC, r_game.left, r_game.top, sw, sh, SRCCOPY);

    BITMAP bm;
    GetObject(hDstBmp, sizeof(BITMAP), &bm);
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB } };
    std::vector<BYTE> px(bm.bmWidth * bm.bmHeight * 4);
    GetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);

    for (size_t i = 0; i < px.size(); i += 4) {
        int g = (px[i + 2] * 299 + px[i + 1] * 587 + px[i] * 114) / 1000;
        px[i] = px[i + 1] = px[i + 2] = (g > 90) ? 0 : 255;
    }
    SetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);

    ret.hBmp = (HBITMAP)CopyImage(hDstBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    IStream* pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    {
        Bitmap b(hDstBmp, NULL);
        CLSID pngClsid;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
        b.Save(pStream, &pngClsid, NULL);
    }

    HGLOBAL hMem = NULL;
    GetHGlobalFromStream(pStream, &hMem);
    LPVOID pData = GlobalLock(hMem);
    SIZE_T nSize = GlobalSize(hMem);
    DWORD dBase64Len = 0;

    CryptBinaryToStringA((const BYTE*)pData, (DWORD)nSize, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &dBase64Len);
    std::string base64Str(dBase64Len, '\0');
    CryptBinaryToStringA((const BYTE*)pData, (DWORD)nSize, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &base64Str[0], &dBase64Len);

    GlobalUnlock(hMem);
    pStream->Release();
    DeleteObject(hDstBmp);
    DeleteDC(hSrcDC);
    DeleteDC(hDstDC);

    if (!base64Str.empty() && base64Str.back() == '\0') {
        base64Str.pop_back();
    }

    std::string jsonPayload = "{\"base64\": \"" + base64Str + "\"}";
    CString res = L"";
    HINTERNET hRequest = WinHttpOpenRequest(m_hHttpConnect, L"POST", L"/api/ocr", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)jsonPayload.c_str(), (DWORD)jsonPayload.length(), (DWORD)jsonPayload.length(), 0);

        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }

        if (!bResults) {
            EnsureOcrRunning();
        }
        else {
            std::string responseStr;
            DWORD dwSize = 0, dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
                std::vector<char> buffer(dwSize + 1, 0);
                if (WinHttpReadData(hRequest, (LPVOID)buffer.data(), dwSize, &dwDownloaded)) {
                    responseStr.append(buffer.data(), dwDownloaded);
                }
            } while (dwSize > 0);

            size_t searchPos = 0;
            while ((searchPos = responseStr.find("\"text\"", searchPos)) != std::string::npos) {
                size_t colonPos = responseStr.find(":", searchPos);
                if (colonPos == std::string::npos) break;

                size_t startQuote = responseStr.find("\"", colonPos);
                if (startQuote == std::string::npos) break;

                size_t endQuote = responseStr.find("\"", startQuote + 1);
                while (endQuote != std::string::npos && responseStr[endQuote - 1] == '\\') {
                    endQuote = responseStr.find("\"", endQuote + 1);
                }

                if (endQuote != std::string::npos && endQuote > startQuote) {
                    std::string text = responseStr.substr(startQuote + 1, endQuote - startQuote - 1);
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
                    if (wideLen > 0) {
                        std::vector<wchar_t> wideBuf(wideLen + 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wideBuf.data(), wideLen);
                        res += wideBuf.data();
                    }
                }
                searchPos = (endQuote != std::string::npos) ? endQuote : searchPos + 6;
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    else {
        EnsureOcrRunning();
    }

    res.Replace(L"\\n", L"");
    res.Replace(L"\\r", L"");
    res.Replace(L"\\\"", L"");

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

    res.Replace(L"\r", L"");
    res.Replace(L"\n", L"");
    res.Trim();
    if (res.Find(L"无") != -1 || res.Find(L"没有") != -1 || res.Find(L"无法") != -1) {
        res = L"";
    }
    ret.text = res;
    return ret;
}

// ============================================================================
// UI 与数据解析
// ============================================================================
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

            for (auto& a : m_players[i].aliases) {
                if (a.kills > 0 || a.deaths > 0 || a.akCount > 0) {
                    l.AppendFormat(L" [%s]=%d/%d", a.name, a.kills, a.deaths);
                    if (a.akCount == 1) l += L" A";
                    else if (a.akCount > 1) l.AppendFormat(L" A%d", a.akCount);
                }
            }
            ap(l + L"\r\n", color);
        }
        };

    renderTeam(0, 4, RGB(220, 0, 0), L"【红队】\r\n");
    renderTeam(4, 8, RGB(0, 0, 220), L"【蓝队】\r\n");

    m_editNamesInput.SetSel(cr);
    SaveConfigToFile();
}

void CDNFGameCaptureDlg::UpdatePlayersFromUI() {
    std::lock_guard<std::mutex> dataLock(m_dataMutex);
    CString text;
    m_editNamesInput.GetWindowText(text);
    int start = 0;
    PlayerData old[8];
    for (int i = 0; i < 8; i++) old[i] = m_players[i];

    int cT = -1, rI = 0, bI = 4;

    for (int i = 0; i < 8; i++) {
        m_players[i].name = L"";
        m_players[i].aliases.clear();
        m_players[i].kills = 0;
        m_players[i].deaths = 0;
        m_players[i].currentStreak = old[i].currentStreak;
        m_players[i].akCount = 0;
        m_players[i].team = (i < 4 ? 0 : 1);
    }

    while (start < text.GetLength()) {
        int newlinePos = text.Find(L'\n', start);
        CString line = (newlinePos != -1) ? text.Mid(start, newlinePos - start) : text.Mid(start);
        start = (newlinePos != -1) ? newlinePos + 1 : (int)text.GetLength();
        line.Remove(L'\r');
        line.Trim();

        if (line.IsEmpty() ||
            line.Find(L"===") != -1 ||
            line.Find(L"💡") != -1 ||
            line.Find(L"1. 分队") != -1 ||
            line.Find(L"2. 绑定") != -1 ||
            line.Find(L"3. 手动") != -1 ||
            line.Find(L"4. 手动") != -1) {
            continue;
        }

        if (line.Find(L"红") != -1 && line.Find(L"蓝") != -1 && line.Find(L":") != -1 && line.Find(L"【") == -1) {
            int rP = line.Find(L"红"), bP = line.Find(L"蓝");
            if (rP < bP) {
                CString m = line.Mid(rP + 1, bP - rP - 1);
                int c = m.Find(L":");
                if (c != -1) {
                    m_totalScoreRed = _wtoi(m.Left(c));
                    m_totalScoreBlue = _wtoi(m.Mid(c + 1));
                }
            }
            continue;
        }

        if (line.Find(L"【红队】") != -1 || line == L"红队" || line == L"红队:") { cT = 0; continue; }
        if (line.Find(L"【蓝队】") != -1 || line == L"蓝队" || line == L"蓝队:") { cT = 1; continue; }

        if (cT == -1) continue;

        int pI = (cT == 0 ? rI : bI);
        if ((cT == 0 && rI >= 4) || (cT == 1 && bI >= 8)) continue;

        int eP = line.FindOneOf(L"=＝");
        CString namePart = (eP != -1 ? line.Left(eP) : line);
        namePart.Trim();
        int fP = namePart.FindOneOf(L"(（");

        if (fP != -1) {
            m_players[pI].name = namePart.Left(fP);
            m_players[pI].name.Trim();
            CString aR = namePart.Mid(fP);
            int cur = 0;
            while (true) {
                CString tempStr = aR.Mid(cur);
                int L_rel = tempStr.FindOneOf(L"(（");
                int R_rel = tempStr.FindOneOf(L")）");
                if (L_rel == -1 || R_rel == -1) break;

                int L = cur + L_rel, R = cur + R_rel;
                CString aN = aR.Mid(L + 1, R - L - 1);
                aN.Trim();

                if (!aN.IsEmpty()) {
                    int ok = 0, od = 0, os = 0, oak = 0;
                    for (auto& oa : old[pI].aliases) {
                        if (oa.name == aN) {
                            ok = oa.kills; od = oa.deaths; os = oa.currentStreak; oak = oa.akCount; break;
                        }
                    }
                    m_players[pI].aliases.push_back({ aN, ok, od, os, oak });
                }
                cur = R + 1;
            }
        }
        else {
            m_players[pI].name = namePart;
        }

        if (eP != -1) {
            CString sP = line.Mid(eP + 1);
            sP.Trim();
            int sB = sP.Find(L'[');
            CString mS = (sB != -1 ? sP.Left(sB) : sP);
            mS.Trim();
            int aPos = mS.Find(L'A');

            if (aPos != -1) {
                CString akStr = mS.Mid(aPos + 1);
                m_players[pI].akCount = akStr.IsEmpty() ? 1 : _wtoi(akStr);
                mS = mS.Left(aPos);
            }
            else {
                m_players[pI].akCount = 0;
            }

            int sl = mS.FindOneOf(L"/-");
            if (sl != -1) {
                m_players[pI].kills = _wtoi(mS.Left(sl));
                m_players[pI].deaths = _wtoi(mS.Mid(sl + 1));
            }
            else {
                m_players[pI].kills = _wtoi(mS);
            }

            int bP = line.Find(L'[');
            while (bP != -1) {
                int eb = line.Find(L']', bP);
                if (eb != -1) {
                    CString t = line.Mid(bP + 1, eb - bP - 1);
                    t.Trim();
                    CString rL = line.Mid(eb + 1);
                    int sE = rL.FindOneOf(L"=＝");
                    if (sE != -1) {
                        CString v = rL.Mid(sE + 1);
                        v.Trim();
                        int nB = v.Find(L'[');
                        if (nB != -1) v = v.Left(nB);

                        int aPosAlias = v.Find(L'A'), parsedAK = 0;
                        if (aPosAlias != -1) {
                            CString akStr = v.Mid(aPosAlias + 1);
                            parsedAK = akStr.IsEmpty() ? 1 : _wtoi(akStr);
                            v = v.Left(aPosAlias);
                        }

                        int sl2 = v.FindOneOf(L"/-");
                        int tk = (sl2 != -1 ? _wtoi(v.Left(sl2)) : _wtoi(v));
                        int td = (sl2 != -1 ? _wtoi(v.Mid(sl2 + 1)) : 0);

                        for (auto& al : m_players[pI].aliases) {
                            if (al.name == t) {
                                al.kills = tk; al.deaths = td; al.akCount = parsedAK; break;
                            }
                        }
                    }
                }
                bP = line.Find(L'[', eb != -1 ? eb : bP + 1);
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

void CDNFGameCaptureDlg::AppendResultText(const CString& t, COLORREF c) {
    int l = m_editOcrResult.GetWindowTextLength();
    m_editOcrResult.SetSel(l, l);
    CHARFORMAT cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = c;
    m_editOcrResult.SetSelectionCharFormat(cf);
    m_editOcrResult.ReplaceSel(t);
}

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");
    CString sS;
    sS.Format(L"============= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
    AppendResultText(sS, RGB(0, 100, 0));
    AppendResultText(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0));
    AppendResultText(L"------------------------------------------\r\n", RGB(150, 150, 150));

    std::vector<int> rI, bI;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) rI.push_back(i); else bI.push_back(i);
    }

    std::vector<int>& lIdx = m_bFlipSides ? bI : rI;
    std::vector<int>& rIdx = m_bFlipSides ? rI : bI;
    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0);
    COLORREF rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    for (size_t i = 0; i < max(lIdx.size(), rIdx.size()); i++) {
        CString lT = L"";
        if (i < lIdx.size()) {
            int p = lIdx[i], tk = m_players[p].kills, td = m_players[p].deaths, tak = m_players[p].akCount;
            for (auto& a : m_players[p].aliases) { tk += a.kills; td += a.deaths; tak += a.akCount; }
            lT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, tk, td);
            if (tak == 1) lT += L" A"; else if (tak > 1) lT.AppendFormat(L" A%d", tak);
        }
        AppendResultText(lT, lC);

        int curW = GetVisualWidth(lT);
        for (int s = 0; s < (32 - curW); s++) {
            AppendResultText(L" ", 0);
        }

        CString rT = L"";
        if (i < rIdx.size()) {
            int p = rIdx[i], tk = m_players[p].kills, td = m_players[p].deaths, tak = m_players[p].akCount;
            for (auto& a : m_players[p].aliases) { tk += a.kills; td += a.deaths; tak += a.akCount; }
            rT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, tk, td);
            if (tak == 1) rT += L" A"; else if (tak > 1) rT.AppendFormat(L" A%d", tak);
            rT += L"\r\n";
        }
        else {
            rT = L"\r\n";
        }
        AppendResultText(rT, rC);
    }
}


// ============================================================================
// 绘制与界面响应
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
        CRect textRect(0, 0, 0, 0);
        dc.DrawText(h, &textRect, DT_LEFT | DT_TOP | DT_CALCRECT);
        CRect cr(m_previewRect.left + 15, m_previewRect.bottom - 25 - textRect.Height(), m_previewRect.left + 15 + textRect.Width(), m_previewRect.bottom - 25);
        cr.InflateRect(8, 8);
        dc.FillSolidRect(&cr, RGB(25, 25, 25));
        dc.DrawText(h, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(of);
    }

    HBITMAP hShowL = NULL, hShowR = NULL;
    {
        std::lock_guard<std::mutex> lkBmp(m_ocrRecordMutex);
        if (m_viewIndexLeft >= 0 && m_viewIndexLeft < (int)m_ocrRecordsLeft.size()) {
            hShowL = m_ocrRecordsLeft[m_viewIndexLeft].hBmp;
        }
        else if (!m_ocrRecordsLeft.empty()) {
            hShowL = m_ocrRecordsLeft.back().hBmp;
        }

        if (m_viewIndexRight >= 0 && m_viewIndexRight < (int)m_ocrRecordsRight.size()) {
            hShowR = m_ocrRecordsRight[m_viewIndexRight].hBmp;
        }
        else if (!m_ocrRecordsRight.empty()) {
            hShowR = m_ocrRecordsRight.back().hBmp;
        }
    }

    HBITMAP arrToDraw[2] = { hShowL, hShowR };
    int curDrawY = m_previewRect.bottom - 20;
    int targetDrawW = max(180, m_previewRect.Width() / 4);

    for (int i = 1; i >= 0; i--) {
        if (arrToDraw[i]) {
            BITMAP bmOcr;
            GetObject(arrToDraw[i], sizeof(BITMAP), &bmOcr);
            int srcW = (int)(bmOcr.bmWidth * 0.70);
            int srcH = bmOcr.bmHeight;
            int srcX = (i == 0) ? 0 : (bmOcr.bmWidth - srcW);
            int drawW = targetDrawW;
            int drawH = (int)((float)srcH / srcW * drawW);
            curDrawY -= drawH;

            int itemStartX = m_previewRect.right - 15 - drawW;
            HDC hMemDC = CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = SelectObject(hMemDC, arrToDraw[i]);
            COLORREF borderColor = (i == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);

            dc.FillSolidRect(itemStartX - 2, curDrawY - 2, drawW + 4, drawH + 4, borderColor);
            dc.SetStretchBltMode(HALFTONE);
            dc.StretchBlt(itemStartX, curDrawY, drawW, drawH, CDC::FromHandle(hMemDC), srcX, 0, srcW, srcH, SRCCOPY);

            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(borderColor);
            CFont fMini;
            fMini.CreatePointFont(90, L"微软雅黑");
            CFont* ofMini = dc.SelectObject(&fMini);
            dc.TextOut(itemStartX, curDrawY - 18, i == 0 ? L"左侧提取区" : L"右侧提取区");
            dc.SelectObject(ofMini);

            curDrawY -= 25;
            SelectObject(hMemDC, oldBmp);
            DeleteDC(hMemDC);
        }
    }
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