#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <shellapi.h>
#include <Gdiplus.h>
#include <string>
#include <mutex>
#include <ctime>
#include <deque>

#pragma comment(lib, "Gdiplus.lib")
using namespace Gdiplus;

// ============================================================================
// 全局变量与配置区
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
const int ID_BTN_BROWSE_OCR = 1009;
const int IDC_EDIT_OCR_PATH = 1010;

struct ScorePointF {
    float x;
    float y;
};

ScorePointF g_scorePts[16] = {
    { 0.1594f, 0.0348f }, { 0.1922f, 0.0377f },
    { 0.1761f, 0.1116f }, { 0.1957f, 0.1138f },
    { 0.2738f, 0.1127f }, { 0.2925f, 0.1127f },
    { 0.3714f, 0.1104f }, { 0.3902f, 0.1138f },
    { 0.8105f, 0.0338f }, { 0.8457f, 0.0372f },
    { 0.6085f, 0.1104f }, { 0.6281f, 0.1116f },
    { 0.7050f, 0.1127f }, { 0.7242f, 0.1127f },
    { 0.8019f, 0.1127f }, { 0.8214f, 0.1116f },
};

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
    ON_BN_CLICKED(ID_BTN_BROWSE_OCR, OnBnClickedBrowseOcr)
END_MESSAGE_MAP()


// ============================================================================
// 构造与析构
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    m_bmp = NULL;
    m_w = 0;
    m_h = 0;
    m_bIsRunning = FALSE;
    m_bCanTrigger = TRUE;
    m_bCanTriggerTeamScore = TRUE;
    m_historyIdx = 0;
    m_bPendingTeamScoreWin = false;

    m_totalScoreRed = 0;
    m_totalScoreBlue = 1;
    m_lastKillerTeam = -1;
    m_bFlipSides = false;

    for (int i = 0; i < 6; i++) {
        m_historyBmps[i] = NULL;
    }

    AfxInitRichEdit2();

    for (int i = 0; i < 8; i++) {
        m_players[i].kills = 0;
        m_players[i].deaths = 0;
        m_players[i].currentStreak = 0;
        m_players[i].akCount = 0;
    }

    m_players[0].name = L"白羽";
    m_players[0].team = 0;
    m_players[0].aliases.push_back({ L"抖音FSN白羽", 0, 1, 0, 0 });

    m_players[1].name = L"大崩";
    m_players[1].team = 0;
    m_players[1].aliases.push_back({ L"流年兮", 0, 1, 0, 0 });

    m_players[2].name = L"夏法";
    m_players[2].team = 0;

    m_players[3].name = L"逍遥";
    m_players[3].team = 0;
    m_players[3].deaths = 1;

    m_players[4].name = L"老王";
    m_players[4].team = 1;
    m_players[4].aliases.push_back({ L"旋律", 4, 3, 0, 0 });
    m_players[4].kills = 12;
    m_players[4].deaths = 6;

    m_players[5].name = L"夜风";
    m_players[5].team = 1;

    m_players[6].name = L"二海";
    m_players[6].team = 1;
    m_players[6].aliases.push_back({ L"疯疯熊冲鸭", 0, 0, 0, 0 });

    m_players[7].name = L"九哥";
    m_players[7].team = 1;
    m_players[7].aliases.push_back({ L"米叹米叹", 0, 0, 0, 0 });

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString confPath = exePath;
    int pos = confPath.ReverseFind(L'\\');
    if (pos != -1) {
        confPath = confPath.Left(pos + 1);
    }
    confPath += L"config.ini";

    wchar_t ocrBuf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"OcrPath", L"E:\\Umi-OCR_Paddle_v2.1.5\\Umi-OCR.exe", ocrBuf, MAX_PATH, confPath);
    m_ocrExePath = ocrBuf;

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CreateEx(0, cls, L"DNF击杀统计 - 完美终极版",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100,
        (int)(750 * WINDOW_SCALE), (int)(650 * WINDOW_SCALE),
        NULL, NULL);
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    if (m_bmp) {
        ::DeleteObject(m_bmp);
    }
    for (int i = 0; i < 6; i++) {
        if (m_historyBmps[i]) {
            ::DeleteObject(m_historyBmps[i]);
        }
    }
}


// ============================================================================
// 辅助功能函数
// ============================================================================

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

int GetVisualWidth(const CString& s) {
    int w = 0;
    for (int i = 0; i < s.GetLength(); i++) {
        w += (s[i] >= 0x4E00 && s[i] <= 0x9FFF) ? 2 : 1;
    }
    return w;
}


// ============================================================================
// UI 事件响应处理
// ============================================================================

void CDNFGameCaptureDlg::OnClose() {
    m_bIsRunning = FALSE;
    KillTimer(1);
    KillTimer(2);
    KillTimer(3);
    KillTimer(4);
    DestroyWindow();
    PostQuitMessage(0);
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
        SetTimer(3, 1000, NULL);
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
    if (MessageBox(L"确定要将所有人的【击杀/阵亡/连杀/AK】以及【总比分】全部归零吗？", L"确认归零", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);

        m_totalScoreRed = 0;
        m_totalScoreBlue = 0;

        for (int i = 0; i < 8; i++) {
            m_players[i].kills = 0;
            m_players[i].deaths = 0;
            m_players[i].currentStreak = 0;
            m_players[i].akCount = 0;
            for (auto& alias : m_players[i].aliases) {
                alias.kills = 0;
                alias.deaths = 0;
                alias.currentStreak = 0;
                alias.akCount = 0;
            }
        }

        SyncDataToInputBox();
        RefreshDisplay();
        WriteScoreToFile();

        m_status.SetWindowText(L"战绩已全部归零！");
    }
}

void CDNFGameCaptureDlg::OnBnClickedBrowseOcr() {
    CFileDialog dlg(TRUE, L"exe", L"Umi-OCR.exe", OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, L"Executable Files (*.exe)|*.exe|All Files (*.*)|*.*||", this);
    if (dlg.DoModal() == IDOK) {
        m_ocrExePath = dlg.GetPathName();
        if (m_editOcrPath.m_hWnd) {
            m_editOcrPath.SetWindowText(m_ocrExePath);
        }

        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        CString confPath = exePath;
        int pos = confPath.ReverseFind(L'\\');
        if (pos != -1) {
            confPath = confPath.Left(pos + 1);
        }
        confPath += L"config.ini";

        WritePrivateProfileString(L"Settings", L"OcrPath", m_ocrExePath, confPath);
        m_status.SetWindowText(L"OCR路径已保存！");
    }
}


// ============================================================================
// 核心捕获与渲染逻辑
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

    int splitY = client.bottom - (int)(340 * WINDOW_SCALE);
    if (splitY < 100) splitY = 100;
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

    // ========== 检测大比分条件 ==========
    if ((mt(0, 1) && mt(2, 3) && mt(4, 5) && mt(6, 7) || mt(8, 9) && mt(10, 11) && mt(12, 13) && mt(14, 15)) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex);
            m_bPendingTeamScoreWin = true;
        }

        CString logMsg = L"★ [触发] 检测到团灭/结束特效，挂起等待最后击杀者结算...";
        WriteMatchLog(logMsg);
        {
            std::lock_guard<std::mutex> lk(g_visualLogMutex);
            g_visualLogs.push_back({ logMsg, RGB(255, 140, 0) });
            if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
        }

        SetTimer(4, 120000, NULL);
    }

    // ========== 检测单人击杀条件 ==========
    if ((mk(0, 1) || mk(2, 3)) && m_bCanTrigger) {
        m_bCanTrigger = FALSE;
        int killSide = mk(0, 1) ? 0 : 1;
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
        SetTimer(2, 13000, NULL);
    }
}


// ============================================================================
// OCR 重试与核心匹配逻辑
// ============================================================================

void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide) {
    int killerArea = (triggerSide == 0) ? 1 : 0;
    int deadArea = triggerSide;

    bool killerResolved = false;
    bool deadResolved = false;

    CString finalKillerName = L"待定";
    CString finalDeadName = L"待定";

    int killerBestP = -1, killerBestA = -1;
    int deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    // OCR 精度差时的降级容错记录本
    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999;
    CString globalKillerName = L"";

    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999;
    CString globalDeadName = L"";

    std::vector<HBITMAP> historyClones;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        for (int i = 1; i <= 6; i++) {
            int idx = (m_historyIdx - i + 6) % 6;
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
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.clear();
    }

    for (int i = 1; i <= 12; i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved)) break;

        HBITMAP hSnapshot = NULL;
        bool isHistory = false;
        int secondsAgo = i;

        if (i <= (int)historyClones.size()) {
            hSnapshot = historyClones[i - 1];
            isHistory = true;
            secondsAgo = i;
        }
        else {
            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (m_bmp) {
                HDC hDC = ::GetDC(NULL);
                HDC hSrc = CreateCompatibleDC(hDC);
                HDC hDst = CreateCompatibleDC(hDC);

                hSnapshot = CreateCompatibleBitmap(hDC, m_w, m_h);
                HGDIOBJ os = SelectObject(hSrc, m_bmp);
                HGDIOBJ od = SelectObject(hDst, hSnapshot);

                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);

                SelectObject(hSrc, os);
                SelectObject(hDst, od);
                DeleteDC(hSrc);
                DeleteDC(hDst);
                ::ReleaseDC(NULL, hDC);
            }
        }

        if (!hSnapshot) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        CString killerStr = L"";
        CString deadStr = L"";

        if (!killerResolved) {
            killerStr = RunOCR_Internal(hSnapshot, killerArea);
        }

        // 死者名字极速消失，只允许在前6次(历史快照)中读取，防止读到刚下场的下一位选手
        if (!deadResolved) {
            if (isHistory || i <= 6) {
                deadStr = RunOCR_Internal(hSnapshot, deadArea);
            }
        }

        if (!isHistory) DeleteObject(hSnapshot);

        auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller, int& outBestP, int& outBestA) {
            if (resolved || ocrResult.IsEmpty()) return;

            if (ocrResult.Find(L"No text in OCR result") != -1) return;

            CString logMsg;
            logMsg.Format(L"▶ [%s] OCR提取: \"%s\"", isKiller ? L"找杀手" : L"找死者", (LPCTSTR)ocrResult);
            WriteMatchLog(logMsg);
            {
                std::lock_guard<std::mutex> lk(g_visualLogMutex);
                g_visualLogs.push_back({ logMsg, RGB(255, 215, 0) });
                if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
            }

            int maxS = -1;
            int bestP = -1;
            int bestA = -1;
            int bestRealLen = 0;
            std::wstring bestN = L"";

            m_dataMutex.lock();
            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                int teamPenalty = 0;
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) teamPenalty = 20;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) teamPenalty = 20;

                int curScore = m_matcher.GetMatchScore(m_players[p].name.GetString(), ocrResult.GetString()) - teamPenalty;
                std::wstring curBestN = m_players[p].name.GetString();
                int curBestA = -1;
                int curRealLen = m_players[p].name.GetLength();

                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    int as = m_matcher.GetMatchScore(m_players[p].aliases[a].name.GetString(), ocrResult.GetString()) - teamPenalty;
                    if (as > curScore) {
                        curScore = as;
                        curBestN = m_players[p].aliases[a].name.GetString();
                        curBestA = (int)a;
                        curRealLen = m_players[p].aliases[a].name.GetLength();
                    }
                }

                if (curScore > maxS || (curScore == maxS && maxS > 0 && curRealLen > bestRealLen)) {
                    maxS = curScore;
                    bestP = p;
                    bestA = curBestA;
                    bestN = curBestN;
                    bestRealLen = curRealLen;
                }
            }
            m_dataMutex.unlock();

            int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);

            if (bestP != -1) {
                if (isKiller && maxS > globalKillerBestScore) {
                    globalKillerBestScore = maxS;
                    globalKillerBestP = bestP;
                    globalKillerBestA = bestA;
                    globalKillerPassLine = passLine;
                    globalKillerName = bestN.c_str();
                }
                else if (!isKiller && maxS > globalDeadBestScore) {
                    globalDeadBestScore = maxS;
                    globalDeadBestP = bestP;
                    globalDeadBestA = bestA;
                    globalDeadPassLine = passLine;
                    globalDeadName = bestN.c_str();
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

                {
                    std::lock_guard<std::mutex> lk(g_visualLogMutex);
                    COLORREF c = (m_players[bestP].team == 0) ? RGB(255, 120, 120) : RGB(120, 180, 255);
                    g_visualLogs.push_back({ successLog, c });
                    if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
                }

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[bestP].team;
                else lockedDeadTeam = m_players[bestP].team;
                m_dataMutex.unlock();
            }
            else {
                CString failLog;
                failLog.Format(L"  └ [✖失败] 最高 %d 分，未及格 %d 分", maxS, passLine);
                WriteMatchLog(failLog);
                {
                    std::lock_guard<std::mutex> lk(g_visualLogMutex);
                    g_visualLogs.push_back({ failLog, RGB(180, 180, 180) });
                    if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
                }
            }
            };

        processMatch(killerStr, killerResolved, finalKillerName, true, killerBestP, killerBestA);
        processMatch(deadStr, deadResolved, finalDeadName, false, deadBestP, deadBestA);

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            if (isHistory) {
                m_debugOcrResult.Format(L"【时光倒流快照】当前锁定 - 杀:%s 亡:%s",
                    killerResolved ? finalKillerName : L"未定",
                    deadResolved ? finalDeadName : L"未定");
            }
            else {
                m_debugOcrResult.Format(L"【实时重试第%d次】当前锁定 - 杀:%s 亡:%s",
                    i - (int)historyClones.size(),
                    killerResolved ? finalKillerName : L"未定",
                    deadResolved ? finalDeadName : L"未定");
            }
        }

        InvalidateRect(&m_previewRect, FALSE);

        if (!killerResolved || !deadResolved) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1800));
        }
    }

    // 12次重试耗尽后的 OCR 降级容错机制
    if (!killerResolved && globalKillerBestP != -1) {
        if (globalKillerBestScore >= (globalKillerPassLine - 20) && globalKillerBestScore >= 35) {
            killerResolved = true;
            killerBestP = globalKillerBestP;
            killerBestA = globalKillerBestA;
            finalKillerName = globalKillerName;

            CString fallbackLog;
            fallbackLog.Format(L"  └ [⚠️降级录取] 勉强认出杀手: %s (得分:%d, 原线:%d)", (LPCTSTR)finalKillerName, globalKillerBestScore, globalKillerPassLine);
            WriteMatchLog(fallbackLog);
            std::lock_guard<std::mutex> lk(g_visualLogMutex);
            g_visualLogs.push_back({ fallbackLog, RGB(255, 165, 0) });
            if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
        }
    }

    if (!deadResolved && globalDeadBestP != -1) {
        if (globalDeadBestScore >= (globalDeadPassLine - 20) && globalDeadBestScore >= 35) {
            deadResolved = true;
            deadBestP = globalDeadBestP;
            deadBestA = globalDeadBestA;
            finalDeadName = globalDeadName;

            CString fallbackLog;
            fallbackLog.Format(L"  └ [⚠️降级录取] 勉强认出死者: %s (得分:%d, 原线:%d)", (LPCTSTR)finalDeadName, globalDeadBestScore, globalDeadPassLine);
            WriteMatchLog(fallbackLog);
            std::lock_guard<std::mutex> lk(g_visualLogMutex);
            g_visualLogs.push_back({ fallbackLog, RGB(255, 165, 0) });
            if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
        }
    }

    // 判断最终结果并写入数据结构
    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();
        bool isDuplicate = false;

        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 5000)) {
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate) {
            RecentEvent rev;
            rev.killer = finalKillerName;
            rev.dead = finalDeadName;
            rev.time = now;
            m_recentEvents.push_back(rev);

            m_recentEvents.erase(std::remove_if(m_recentEvents.begin(), m_recentEvents.end(), [&](const RecentEvent& ev) {
                return now - ev.time > 10000;
                }), m_recentEvents.end());

            auto addEventLog = [&](const CString& msg, COLORREF color) {
                WriteMatchLog(msg);
                std::lock_guard<std::mutex> lk(g_visualLogMutex);
                g_visualLogs.push_back({ msg, color });
                if (g_visualLogs.size() > 10) g_visualLogs.pop_front();
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

                    actionLog.Format(L"⚔ [击杀] 小号 [%s] 拿下一击！当前连杀: %d",
                        (LPCTSTR)m_players[killerBestP].aliases[killerBestA].name,
                        m_players[killerBestP].aliases[killerBestA].currentStreak);
                    addEventLog(actionLog, teamColor);

                    if (m_players[killerBestP].aliases[killerBestA].currentStreak == 4) {
                        m_players[killerBestP].aliases[killerBestA].akCount++;
                        m_players[killerBestP].aliases[killerBestA].currentStreak = 0;

                        actionLog.Format(L"🌟 [AK宣告] 恐怖如斯！小号 [%s] 完成一次 AK！累计 AK: %d 次",
                            (LPCTSTR)m_players[killerBestP].aliases[killerBestA].name,
                            m_players[killerBestP].aliases[killerBestA].akCount);
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

                    actionLog.Format(L"⚔ [击杀] 玩家 [%s] 拿下一击！当前连杀: %d",
                        (LPCTSTR)m_players[killerBestP].name,
                        m_players[killerBestP].currentStreak);
                    addEventLog(actionLog, teamColor);

                    if (m_players[killerBestP].currentStreak == 4) {
                        m_players[killerBestP].akCount++;
                        m_players[killerBestP].currentStreak = 0;

                        actionLog.Format(L"🌟 [AK宣告] 恐怖如斯！玩家 [%s] 完成一次 AK！累计 AK: %d 次",
                            (LPCTSTR)m_players[killerBestP].name,
                            m_players[killerBestP].akCount);
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
// 图像处理及外部 OCR 调度
// ============================================================================

CString CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex) {
    RECT r_game;
    if (nAreaIndex == 0) {
        r_game = { (long)(m_w * 0.190f), (long)(m_h * 0.004f), (long)(m_w * 0.360f), (long)(m_h * 0.040f) };
    }
    else {
        r_game = { (long)(m_w * 0.655f), (long)(m_h * 0.004f), (long)(m_w * 0.815f), (long)(m_h * 0.040f) };
    }

    int sw = r_game.right - r_game.left;
    int sh = r_game.bottom - r_game.top;

    HDC hSrcDC = CreateCompatibleDC(NULL);
    HDC hDstDC = CreateCompatibleDC(NULL);
    HBITMAP hDstBmp = CreateCompatibleBitmap(GetDC()->GetSafeHdc(), sw * 2, sh * 2);

    SelectObject(hSrcDC, hTargetBmp);
    SelectObject(hDstDC, hDstBmp);
    SetStretchBltMode(hDstDC, HALFTONE);
    StretchBlt(hDstDC, 0, 0, sw * 2, sh * 2, hSrcDC, r_game.left, r_game.top, sw, sh, SRCCOPY);

    BITMAP bm;
    GetObject(hDstBmp, sizeof(BITMAP), &bm);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> px(bm.bmWidth * bm.bmHeight * 4);
    GetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);

    for (size_t i = 0; i < px.size(); i += 4) {
        int g = (px[i + 2] * 299 + px[i + 1] * 587 + px[i] * 114) / 1000;
        int inv = 255 - g;
        inv = (inv < 140) ? 0 : 255;
        px[i] = px[i + 1] = px[i + 2] = inv;
    }
    SetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);

    DWORD t = GetTickCount();

    CString exePath = m_ocrExePath;
    if (exePath.IsEmpty()) {
        exePath = L"E:\\Umi-OCR_Paddle_v2.1.5\\Umi-OCR.exe";
    }

    int lastSlash = exePath.ReverseFind(L'\\');
    CString base = exePath.Left(lastSlash + 1);

    CString png, txt;
    png.Format(L"%stmp_%u.png", base, t);
    txt.Format(L"%stmp_%u.txt", base, t);

    ULONG_PTR gpt;
    GdiplusStartupInput gpi;
    GdiplusStartup(&gpt, &gpi, NULL);
    {
        Bitmap b(hDstBmp, NULL);
        CLSID id;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &id);
        b.Save(png, &id, NULL);
    }
    GdiplusShutdown(gpt);
    DeleteObject(hDstBmp);
    DeleteDC(hSrcDC);
    DeleteDC(hDstDC);

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = exePath.GetString();

    CString args;
    args.Format(L"--path \"%s\" --output \"%s\"", (LPCTSTR)png, (LPCTSTR)txt);
    sei.lpParameters = args.GetString();
    sei.nShow = SW_HIDE;

    if (ShellExecuteEx(&sei)) {
        WaitForSingleObject(sei.hProcess, 4000);
        CloseHandle(sei.hProcess);
    }

    CString res = L"";
    FILE* f = NULL;
    Sleep(50);

    if (_wfopen_s(&f, txt, L"rb") == 0 && f) {
        char buf[1024] = { 0 };
        fread(buf, 1, 1024, f);
        fclose(f);

        int wl = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
        if (wl > 1) {
            wchar_t* wb = new wchar_t[wl];
            MultiByteToWideChar(CP_UTF8, 0, buf, -1, wb, wl);
            res = wb;
            delete[] wb;
        }
    }

    _wunlink(png);
    _wunlink(txt);
    res.Replace(L"\r", L"");
    res.Replace(L"\n", L"");

    return res;
}


// ============================================================================
// 智能过滤直播平台前缀/后缀 (当检测到多人带有相同词缀时触发)
// ============================================================================
void CDNFGameCaptureDlg::FilterLivePlatformPrefixes() {
    std::vector<CString> keywords = {
        L"FSN", L"TV", L"直播", L"抖音", L"快手", L"斗鱼", L"虎牙",
        L"B站", L"BILIBILI", L"企鹅", L"熊猫", L"战旗"
    };

    for (const CString& kw : keywords) {
        int count = 0;

        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            bool foundInPlayer = false;

            CString upperName = m_players[i].name;
            upperName.MakeUpper();
            CString upperKw = kw;
            upperKw.MakeUpper();

            if (upperName.Find(upperKw) != -1) {
                foundInPlayer = true;
            }
            else {
                for (const auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name;
                    upperAlias.MakeUpper();
                    if (upperAlias.Find(upperKw) != -1) {
                        foundInPlayer = true;
                        break;
                    }
                }
            }
            if (foundInPlayer) count++;
        }

        if (count >= 2) {
            CString upperKw = kw;
            upperKw.MakeUpper();

            for (int i = 0; i < 8; i++) {
                if (m_players[i].name.IsEmpty()) continue;

                CString upperName = m_players[i].name;
                upperName.MakeUpper();
                int pos = upperName.Find(upperKw);
                while (pos != -1) {
                    m_players[i].name.Delete(pos, kw.GetLength());
                    m_players[i].name.Trim(L"-_. ");
                    upperName = m_players[i].name;
                    upperName.MakeUpper();
                    pos = upperName.Find(upperKw);
                }

                for (auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name;
                    upperAlias.MakeUpper();
                    int apos = upperAlias.Find(upperKw);
                    while (apos != -1) {
                        a.name.Delete(apos, kw.GetLength());
                        a.name.Trim(L"-_. ");
                        upperAlias = a.name;
                        upperAlias.MakeUpper();
                        apos = upperAlias.Find(upperKw);
                    }
                }
            }
        }
    }
}


// ============================================================================
// 数据、UI 同步与文件输出模块
// ============================================================================

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

    ap(L"============= 💡 操 作 说 明 =============\r\n", RGB(150, 150, 150), true);
    ap(L"* 玩家填写格式举例：老王(旋律)(电次)..()里面人头死亡累计到老王上面\r\n", RGB(150, 150, 150));
    ap(L"* 可以[老王]=击杀数/阵亡数或者(电次)=击杀数/阵亡数，在点应用即可修改\r\n", RGB(150, 150, 150));
    ap(L"========================================\r\n", RGB(150, 150, 150), true);

    CString sL;
    sL.Format(L"               红 %d  :  %d 蓝\r\n", m_totalScoreRed, m_totalScoreBlue);
    ap(sL, RGB(0, 150, 0), true);

    ap(L"【红队】\r\n", RGB(220, 0, 0), true);
    for (int i = 0; i < 4; i++) {
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
        ap(l + L"\r\n", RGB(220, 0, 0));
    }

    ap(L"【蓝队】\r\n", RGB(0, 0, 220), true);
    for (int i = 4; i < 8; i++) {
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
        ap(l + L"\r\n", RGB(0, 0, 220));
    }

    m_editNamesInput.SetSel(cr);
}

void CDNFGameCaptureDlg::UpdatePlayersFromUI() {
    std::lock_guard<std::mutex> dataLock(m_dataMutex);

    CString text;
    m_editNamesInput.GetWindowText(text);
    int start = 0;

    PlayerData old[8];
    for (int i = 0; i < 8; i++) {
        old[i] = m_players[i];
    }

    int cT = 0, rI = 0, bI = 4;

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

        if (line.IsEmpty() || line.Find(L"===") != -1 || line.Find(L"💡") != -1 || line.Find(L"*") != -1) {
            continue;
        }

        if (line.Find(L"红") != -1 && line.Find(L"蓝") != -1 && line.Find(L":") != -1) {
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

        if (line.Find(L"红队") != -1) { cT = 0; continue; }
        if (line.Find(L"蓝队") != -1) { cT = 1; continue; }

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

                int L = cur + L_rel;
                int R = cur + R_rel;

                CString aN = aR.Mid(L + 1, R - L - 1);
                aN.Trim();

                if (!aN.IsEmpty()) {
                    int ok = 0, od = 0, os = 0, oak = 0;
                    for (auto& oa : old[pI].aliases) {
                        if (oa.name == aN) {
                            ok = oa.kills;
                            od = oa.deaths;
                            os = oa.currentStreak;
                            oak = oa.akCount;
                            break;
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

                        int aPosAlias = v.Find(L'A');
                        int parsedAK = 0;
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
                                al.kills = tk;
                                al.deaths = td;
                                al.akCount = parsedAK;
                                break;
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

        if (cT == 0) rI++;
        else bI++;
    }

    FilterLivePlatformPrefixes();

    SyncDataToInputBox();
    WriteScoreToFile();
    RefreshDisplay();
}

void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;

        if (m_players[i].team == 0) r.push_back(m_players[i]);
        else b.push_back(m_players[i]);
    }

    // 补齐到4人，防止数组越界
    while (r.size() < 4) r.push_back({ L"",0,{},0,0,0,0 });
    while (b.size() < 4) b.push_back({ L"",1,{},0,0,0,0 });

    std::vector<PlayerData>& lT = m_bFlipSides ? b : r;
    std::vector<PlayerData>& rT = m_bFlipSides ? r : b;

    // 1. 写入总比分
    FILE* fS = NULL;
    if (_wfopen_s(&fS, L"C:\\比分.txt", L"wt, ccs=UTF-8") == 0 && fS) {
        fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
        fclose(fS);
    }

    // 提取带“阵亡”的完整格式（保留给旧版左右人头文件用）
    auto gs_full = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L"");
        int tk = p.kills, td = p.deaths, tak = p.akCount;
        for (auto& a : p.aliases) { tk += a.kills; td += a.deaths; tak += a.akCount; }
        CString s;
        // 【修改点】：将 tk 的 %d 改为了 %02d (阵亡 td 也顺便改为了 %02d 保证排版整齐)
        s.Format(L"%s %d/%d", p.name.GetString(), tk, td);
        if (tak >= 1)  s.AppendFormat(L"A%d", tak);
        return s;
        };

    FILE* fKL = NULL;
    if (_wfopen_s(&fKL, L"C:\\左侧人头.txt", L"wt, ccs=UTF-8") == 0 && fKL) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_full(lT[i]);
            if (!ls.IsEmpty()) fwprintf(fKL, L"%s\n", ls.GetString());
        }
        fclose(fKL);
    }

    FILE* fKR = NULL;
    if (_wfopen_s(&fKR, L"C:\\右侧人头.txt", L"wt, ccs=UTF-8") == 0 && fKR) {
        for (int i = 0; i < 4; i++) {
            CString rs = gs_full(rT[i]);
            if (!rs.IsEmpty()) fwprintf(fKR, L"%s\n", rs.GetString());
        }
        fclose(fKR);
    }

    // ====================================================================
    // 2. 写入新增的对齐版 “击杀.txt” (只含击杀和AK，无阵亡)
    // ====================================================================
    auto gs_kill_only = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L"");
        int tk = p.kills, tak = p.akCount;
        for (auto& a : p.aliases) { tk += a.kills; tak += a.akCount; }
        CString s;

        // 【修改点】：将 %d 改为了 %02d，让击杀数始终占两位
        s.Format(L"%s%02d", p.name.GetString(), tk);

        if(tak >= 1) s.AppendFormat(L"A%d", tak);
        return s;
        };

    FILE* fKill = NULL;
    if (_wfopen_s(&fKill, L"C:\\击杀.txt", L"wt, ccs=UTF-8") == 0 && fKill) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_kill_only(lT[i]);
            CString rs = gs_kill_only(rT[i]);

            // 如果左右两边都没人，跳过空行
            if (ls.IsEmpty() && rs.IsEmpty()) continue;

            // ===== 计算动态空格进行严格对齐 =====
            int targetWidth = 9;
            int currentWidth = GetVisualWidth(ls);
            int pad = targetWidth - currentWidth;

            if (pad < 1) pad = 1;
            CString spaces(L' ', pad);
            fwprintf(fKill, L"%s%s%s\n", ls.GetString(), spaces.GetString(), rs.GetString());
        }
        fclose(fKill);
    }
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
    sS.Format(L"============= 总比分  %d : %d =============\r\n",
        m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed,
        m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
    AppendResultText(sS, RGB(0, 100, 0));

    AppendResultText(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0));
    AppendResultText(L"------------------------------------------\r\n", RGB(150, 150, 150));

    std::vector<int> rI, bI;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) rI.push_back(i);
        else bI.push_back(i);
    }

    std::vector<int>& lIdx = m_bFlipSides ? bI : rI;
    std::vector<int>& rIdx = m_bFlipSides ? rI : bI;

    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0);
    COLORREF rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    size_t rows = max(lIdx.size(), rIdx.size());
    for (size_t i = 0; i < rows; i++) {
        CString lT = L"";
        if (i < lIdx.size()) {
            int p = lIdx[i];
            int tk = m_players[p].kills, td = m_players[p].deaths, tak = m_players[p].akCount;
            for (auto& a : m_players[p].aliases) {
                tk += a.kills;
                td += a.deaths;
                tak += a.akCount;
            }
            lT.Format(L"%s : %d/%d", (LPCTSTR)m_players[p].name, tk, td);
            if (tak == 1) lT += L" A";
            else if (tak > 1) lT.AppendFormat(L" A%d", tak);
        }

        AppendResultText(lT, lC);

        int curW = GetVisualWidth(lT);
        for (int s = 0; s < (32 - curW); s++) {
            AppendResultText(L" ", 0);
        }

        CString rT = L"";
        if (i < rIdx.size()) {
            int p = rIdx[i];
            int tk = m_players[p].kills, td = m_players[p].deaths, tak = m_players[p].akCount;
            for (auto& a : m_players[p].aliases) {
                tk += a.kills;
                td += a.deaths;
                tak += a.akCount;
            }
            rT.Format(L"%s : %d/%d", (LPCTSTR)m_players[p].name, tk, td);
            if (tak == 1) rT += L" A";
            else if (tak > 1) rT.AppendFormat(L" A%d", tak);
            rT += L"\r\n";
        }
        else {
            rT = L"\r\n";
        }

        AppendResultText(rT, rC);
    }
}


// ============================================================================
// 画面内部的附加视觉绘制系统
// ============================================================================

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;

    CPen p1(PS_SOLID, 2, RGB(255, 0, 0));
    dc.SelectObject(&p1);
    dc.SelectStockObject(NULL_BRUSH);

    float pX[4] = { 0.187f, 0.157f, 0.840f, 0.810f };
    float pY[4] = { 0.036f, 0.034f, 0.039f, 0.039f };
    for (int i = 0; i < 4; i++) {
        dc.Ellipse(m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) - 5,
            m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) - 5,
            m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) + 5,
            m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) + 5);
    }

    CPen p3(PS_SOLID, 2, RGB(255, 165, 0));
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

        CFont f;
        f.CreatePointFont(105, L"黑体");
        CFont* of = dc.SelectObject(&f);

        CRect r(m_previewRect.left + 15, m_previewRect.top + 15, m_previewRect.right - 15, m_previewRect.bottom - 15);
        CRect cr = r;
        dc.DrawText(h, &cr, DT_LEFT | DT_TOP | DT_CALCRECT);
        cr.InflateRect(8, 8);

        dc.FillSolidRect(&cr, RGB(25, 25, 25));
        dc.DrawText(h, &cr, DT_LEFT | DT_TOP);
        dc.SelectObject(of);
    }

    std::lock_guard<std::mutex> lkLog(g_visualLogMutex);
    if (!g_visualLogs.empty()) {
        dc.SetBkMode(TRANSPARENT);
        CFont f2;
        f2.CreatePointFont(100, L"微软雅黑");
        CFont* of2 = dc.SelectObject(&f2);

        int lineH = 20;
        int totalH = g_visualLogs.size() * lineH + 10;
        CRect bgRect(m_previewRect.left + 15, m_previewRect.bottom - totalH - 15, m_previewRect.right - 15, m_previewRect.bottom - 15);

        dc.FillSolidRect(&bgRect, RGB(35, 35, 35));
        int curY = bgRect.top + 5;

        for (const auto& log : g_visualLogs) {
            dc.SetTextColor(log.color);
            dc.TextOut(bgRect.left + 10, curY, log.text);
            curY += lineH;
        }
        dc.SelectObject(of2);
    }
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;

    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= 16) {
            m_selectPts.clear();
        }

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
            res += L"};\r\n";
            m_editOcrResult.SetWindowText(res);
            MessageBox(L"坐标已采集，见右侧框。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnPaint() {
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

    int splitY = r.bottom - (int)(340 * WINDOW_SCALE);
    if (splitY < 100) splitY = 100;

    CRect topHalf(0, 0, r.right, splitY);
    CRect uiRect(0, splitY, r.right, r.bottom);

    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(95, L"微软雅黑");

        int row1_Y = splitY + 5;
        int halfW = (r.right - 30) / 2;

        m_btnBrowseOcr.Create(L"选择 Umi-OCR", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, row1_Y, 110, row1_Y + 25), this, ID_BTN_BROWSE_OCR);
        m_btnBrowseOcr.SetFont(&m_font);

        m_editOcrPath.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, CRect(120, row1_Y, halfW + 10, row1_Y + 25), this, IDC_EDIT_OCR_PATH);
        m_editOcrPath.SetFont(&m_font);
        m_editOcrPath.SetWindowText(m_ocrExePath);

        m_chkFlip.Create(L"翻转红蓝(蓝左红右)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(halfW + 20, row1_Y, halfW + 200, row1_Y + 25), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font);

        m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(halfW + 210, row1_Y + 4, r.right - 10, row1_Y + 25), this, 1003);
        m_status.SetFont(&m_font);

        int row2_Y = row1_Y + 30;
        int row2_Bottom = r.bottom - (int)(45 * WINDOW_SCALE);

        m_editNamesInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL, CRect(10, row2_Y, 10 + halfW, row2_Bottom), this, 1001);
        m_editNamesInput.SetFont(&m_font);

        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(20 + halfW, row2_Y, r.right - 10, row2_Bottom), this, 1002);
        m_editOcrResult.SetFont(&m_font);

        int btnY = row2_Bottom + 8;
        int btnH = (int)(28 * WINDOW_SCALE);
        int bW = (r.right - 40) / 3;

        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, btnY, 10 + bW, btnY + btnH), this, ID_BTN_START);
        m_btnStart.SetFont(&m_font);

        m_btnApply.Create(L"应用修改", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(20 + bW, btnY, 20 + bW * 2, btnY + btnH), this, ID_BTN_APPLY);
        m_btnApply.SetFont(&m_font);

        m_btnReset.Create(L"战绩归零", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(30 + bW * 2, btnY, r.right - 10, btnY + btnH), this, ID_BTN_RESET);
        m_btnReset.SetFont(&m_font);

        SyncDataToInputBox();
        RefreshDisplay();
        WriteScoreToFile();
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
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            if (m_historyBmps[m_historyIdx]) {
                DeleteObject(m_historyBmps[m_historyIdx]);
            }

            HDC hDC = ::GetDC(NULL);
            HDC hSrc = CreateCompatibleDC(hDC);
            HDC hDst = CreateCompatibleDC(hDC);

            m_historyBmps[m_historyIdx] = CreateCompatibleBitmap(hDC, m_w, m_h);
            HGDIOBJ os = SelectObject(hSrc, m_bmp);
            HGDIOBJ od = SelectObject(hDst, m_historyBmps[m_historyIdx]);

            BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);

            SelectObject(hSrc, os);
            SelectObject(hDst, od);
            DeleteDC(hSrc);
            DeleteDC(hDst);
            ::ReleaseDC(NULL, hDC);

            m_historyIdx = (m_historyIdx + 1) % 6;
        }
    }
}