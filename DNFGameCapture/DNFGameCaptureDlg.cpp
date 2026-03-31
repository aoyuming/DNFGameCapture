#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <shellapi.h>
#include <Gdiplus.h>
#include <string>
#include <mutex>

#pragma comment(lib, "Gdiplus.lib")
using namespace Gdiplus;

std::mutex g_bmpMutex;

const float WINDOW_SCALE = 1.6f;
const int ID_BTN_START = 1005;
const int ID_BTN_APPLY = 1006;
const int ID_CHK_FLIP = 1007; // 翻转复选框ID

struct ScorePointF { float x, y; };
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
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip) // 绑定翻转事件
END_MESSAGE_MAP()

CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    m_bmp = NULL;
    m_w = m_h = 0;
    m_bIsRunning = FALSE;
    m_bCanTrigger = TRUE;
    m_bCanTriggerTeamScore = TRUE;
    m_bMatchingInProgress = false;
    m_historyIdx = 0;
    m_totalScoreRed = 0;
    m_totalScoreBlue = 0;
    m_lastKillerTeam = -1;
    m_bFlipSides = false; // 默认不翻转（红左蓝右）
    for (int i = 0; i < 6; i++) m_historyBmps[i] = NULL;
    AfxInitRichEdit2();

    m_players[0].name = L"王大枪"; m_players[0].aliases.push_back({ L"大枪小号",0,0 }); m_players[0].team = 0;
    m_players[1].name = L"堕落"; m_players[1].team = 0;
    m_players[2].name = L"Scul"; m_players[2].aliases.push_back({ L"小S",0,0 }); m_players[2].aliases.push_back({ L"S哥",0,0 }); m_players[2].team = 0;
    m_players[3].name = L"卫敢"; m_players[3].aliases.push_back({ L"敢敢",0,0 }); m_players[3].team = 0;

    m_players[4].name = L"59"; m_players[4].aliases.push_back({ L"59小号",0,0 }); m_players[4].team = 1;
    m_players[5].name = L"45"; m_players[5].team = 1;
    m_players[6].name = L"夜风"; m_players[6].team = 1;
    m_players[7].name = L"好好"; m_players[7].team = 1;

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CreateEx(0, cls, L"DNF击杀统计 - 导播专用版", WS_OVERLAPPEDWINDOW,
        100, 100, (int)(850 * WINDOW_SCALE), (int)(750 * WINDOW_SCALE), NULL, NULL);
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    if (m_bmp) ::DeleteObject(m_bmp);
    for (int i = 0; i < 6; i++) if (m_historyBmps[i]) ::DeleteObject(m_historyBmps[i]);
}

void CDNFGameCaptureDlg::OnClose() {
    m_bIsRunning = FALSE;
    KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4);
    DestroyWindow(); PostQuitMessage(0);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) { return TRUE; }

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
        KillTimer(1); KillTimer(3);
        m_btnStart.SetWindowText(L"开始监控");
        m_status.SetWindowText(L"已停止");
    }
}

void CDNFGameCaptureDlg::OnBnClickedApply() {
    UpdatePlayersFromUI();
    m_status.SetWindowText(L"修改已实时生效！");
}

// 核心翻转逻辑
void CDNFGameCaptureDlg::OnBnClickedFlip() {
    m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED);
    WriteScoreToFile(); // 更新文件输出
    RefreshDisplay();   // 刷新UI显示
}

void CDNFGameCaptureDlg::Capture() {
    HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
    if (!hGame) return;
    std::lock_guard<std::mutex> lock(g_bmpMutex);
    RECT rc; ::GetClientRect(hGame, &rc);
    m_w = rc.right - rc.left; m_h = rc.bottom - rc.top;
    if (m_w <= 0 || m_h <= 0) return;
    if (!m_bmp) {
        HDC hdc = ::GetDC(hGame);
        m_bmp = ::CreateCompatibleBitmap(hdc, m_w, m_h);
        ::ReleaseDC(hGame, hdc);
    }
    HDC hGameDC = ::GetDC(hGame); HDC hMem = ::CreateCompatibleDC(hGameDC);
    HGDIOBJ old = ::SelectObject(hMem, m_bmp);
    ::PrintWindow(hGame, hMem, 2);
    CRect client; GetClientRect(&client);
    m_previewRect = client; m_previewRect.bottom -= (int)(290 * WINDOW_SCALE);
    CClientDC dc(this); dc.SetStretchBltMode(HALFTONE);
    dc.StretchBlt(m_previewRect.left, m_previewRect.top, m_previewRect.Width(), m_previewRect.Height(),
        CDC::FromHandle(hMem), 0, 0, m_w, m_h, SRCCOPY);
    Draw(dc);
    ::SelectObject(hMem, old); ::DeleteDC(hMem); ::ReleaseDC(hGame, hGameDC);
}

void CDNFGameCaptureDlg::CheckColorTrigger() {
    if (!m_bmp || !m_bIsRunning) return;

    COLORREF c_kill[4];
    COLORREF c_team[16];

    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        HDC hMem = ::CreateCompatibleDC(NULL);
        HGDIOBJ old = ::SelectObject(hMem, m_bmp);

        c_kill[0] = ::GetPixel(hMem, (int)(m_w * 0.187f), (int)(m_h * 0.036f));
        c_kill[1] = ::GetPixel(hMem, (int)(m_w * 0.157f), (int)(m_h * 0.034f));
        c_kill[2] = ::GetPixel(hMem, (int)(m_w * 0.840f), (int)(m_h * 0.039f));
        c_kill[3] = ::GetPixel(hMem, (int)(m_w * 0.810f), (int)(m_h * 0.039f));

        for (int i = 0; i < 16; i++) {
            c_team[i] = ::GetPixel(hMem, (int)(m_w * g_scorePts[i].x), (int)(m_h * g_scorePts[i].y));
        }
        ::SelectObject(hMem, old);
        ::DeleteDC(hMem);
    }

    auto eq = [](COLORREF a, COLORREF b) {
        return abs(GetRValue(a) - GetRValue(b)) < 25 &&
            abs(GetGValue(a) - GetGValue(b)) < 25 &&
            abs(GetBValue(a) - GetBValue(b)) < 25;
        };

    auto matchKillPair = [&](int p1, int p2) { return (eq(c_kill[p1], COLOR_BLUE) && eq(c_kill[p2], COLOR_RED)) || (eq(c_kill[p1], COLOR_RED) && eq(c_kill[p2], COLOR_BLUE)); };
    auto matchTeamPair = [&](int p1, int p2) { return (eq(c_team[p1], COLOR_BLUE) && eq(c_team[p2], COLOR_RED)) || (eq(c_team[p1], COLOR_RED) && eq(c_team[p2], COLOR_BLUE)); };

    bool leftTeamWipe = matchTeamPair(0, 1) && matchTeamPair(2, 3) && matchTeamPair(4, 5) && matchTeamPair(6, 7);
    bool rightTeamWipe = matchTeamPair(8, 9) && matchTeamPair(10, 11) && matchTeamPair(12, 13) && matchTeamPair(14, 15);

    if ((leftTeamWipe || rightTeamWipe) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;

        if (m_lastKillerTeam == 0) m_totalScoreRed++;
        else if (m_lastKillerTeam == 1) m_totalScoreBlue++;

        SyncDataToInputBox();
        RefreshDisplay();

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult = L"【队伍结算】检测到16点满足！已根据决斗者阵营加大比分！";
            m_debugMatchDetails = L"";
        }
        InvalidateRect(&m_previewRect, FALSE);
        SetTimer(4, 10000, NULL);
    }

    bool leftKill = matchKillPair(0, 1);
    bool rightKill = matchKillPair(2, 3);

    if ((leftKill || rightKill) && m_bCanTrigger && !m_bMatchingInProgress) {
        m_bCanTrigger = FALSE;
        m_bMatchingInProgress = true;
        int killTriggerSide = leftKill ? 0 : 1;

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult.Format(L"【人头击杀】[%s]侧判定死亡(4点满足)，提取历史记录...", killTriggerSide == 0 ? L"左" : L"右");
            m_debugMatchDetails = L"";
        }
        InvalidateRect(&m_previewRect, FALSE);
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killTriggerSide).detach();
        SetTimer(2, 4800, NULL);
    }
}

void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide) {
    int killerArea = (triggerSide == 0) ? 1 : 0; int deadArea = triggerSide;
    bool killerResolved = false; bool deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";

    // ============ 新增：阵营互斥锁 ============
    int lockedKillerTeam = -1; // 已确认的击杀者阵营
    int lockedDeadTeam = -1;   // 已确认的阵亡者阵营

    std::vector<HBITMAP> historyClones;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        for (int i = 1; i <= 6; i++) {
            int idx = (m_historyIdx - i + 6) % 6;
            if (m_historyBmps[idx]) {
                HDC hDC = ::GetDC(NULL); HDC hSrc = CreateCompatibleDC(hDC); HDC hDst = CreateCompatibleDC(hDC);
                HBITMAP clone = CreateCompatibleBitmap(hDC, m_w, m_h);
                HGDIOBJ os = SelectObject(hSrc, m_historyBmps[idx]); HGDIOBJ od = SelectObject(hDst, clone);
                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
                SelectObject(hSrc, os); SelectObject(hDst, od);
                DeleteDC(hSrc); DeleteDC(hDst); ::ReleaseDC(NULL, hDC);
                historyClones.push_back(clone);
            }
        }
    }

    for (int i = 1; i <= 12; i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved)) break;
        HBITMAP hSnapshot = NULL; bool isHistory = false; int secondsAgo = 0;

        if (i <= (int)historyClones.size()) { hSnapshot = historyClones[i - 1]; isHistory = true; secondsAgo = i; }
        else {
            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (m_bmp) {
                HDC hDC = ::GetDC(NULL); HDC hSrc = CreateCompatibleDC(hDC); HDC hDst = CreateCompatibleDC(hDC);
                hSnapshot = CreateCompatibleBitmap(hDC, m_w, m_h);
                HGDIOBJ os = SelectObject(hSrc, m_bmp); HGDIOBJ od = SelectObject(hDst, hSnapshot);
                BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
                SelectObject(hSrc, os); SelectObject(hDst, od);
                DeleteDC(hSrc); DeleteDC(hDst); ::ReleaseDC(NULL, hDC);
            }
        }

        if (!hSnapshot) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        CString killerStr = L"", deadStr = L"";
        if (!killerResolved) killerStr = RunOCR_Internal(hSnapshot, killerArea);
        if (!deadResolved) deadStr = RunOCR_Internal(hSnapshot, deadArea);
        if (!isHistory) DeleteObject(hSnapshot);

        // ============ 核心重构：更加严格的匹配机制 ============
        auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller) {
            if (resolved || ocrResult.IsEmpty()) return;
            std::wstring ocrW = ocrResult.GetString();

            // 【防误判 1】剔除过于零碎的 OCR 垃圾识别（通常是技能特效）
            if (ocrW.length() < 2) return;

            int maxS = 0, bestP = -1, bestA = -1; std::wstring bestN = L"";

            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                // 【防误判 2】阵营互斥逻辑：杀手和死者绝对不可能在同一个队！
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) continue;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) continue;

                std::wstring pN = m_players[p].name.GetString();
                int s = m_matcher.GetMatchScore(pN, ocrW);
                // 提高绝对包含的权重，防止把 “A” 错认为 “ABC”
                if (ocrW.find(pN) != std::wstring::npos) s += 50;

                int curPBestS = s; int curPBestA = -1; std::wstring curPBestN = pN;
                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    std::wstring aN = m_players[p].aliases[a].name.GetString();
                    int as = m_matcher.GetMatchScore(aN, ocrW);
                    if (ocrW.find(aN) != std::wstring::npos) as += 50;
                    if (as > curPBestS) { curPBestS = as; curPBestN = aN; curPBestA = (int)a; }
                }

                // 【防误判 3】全面提高触发阈值（要求更高的相似度）
                int threshold = (curPBestN.length() <= 2) ? 75 : (curPBestN.length() <= 4 ? 60 : 45);

                if (curPBestS >= threshold && curPBestS > maxS) {
                    maxS = curPBestS; bestP = p; bestA = curPBestA; bestN = curPBestN;
                }
            }

            if (bestP != -1) {
                if (isKiller) {
                    if (bestA != -1) m_players[bestP].aliases[bestA].kills++; else m_players[bestP].kills++;
                    m_lastKillerTeam = m_players[bestP].team;
                    lockedKillerTeam = m_players[bestP].team; // 锁定杀手阵营，死者绝不可能是这队的
                }
                else {
                    if (bestA != -1) m_players[bestP].aliases[bestA].deaths++; else m_players[bestP].deaths++;
                    lockedDeadTeam = m_players[bestP].team;   // 锁定死者阵营，杀手绝不可能是这队的
                }
                resolved = true; finalName = bestN.c_str();
                SyncDataToInputBox();
                RefreshDisplay();
            }
            };

        processMatch(killerStr, killerResolved, finalKillerName, true);
        processMatch(deadStr, deadResolved, finalDeadName, false);

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            if (isHistory) m_debugOcrResult.Format(L"【时光倒流：%d秒前】\n杀:%s 亡:%s", secondsAgo, killerStr, deadStr);
            else m_debugOcrResult.Format(L"【实时重试：第%d次】\n杀:%s 亡:%s", i - (int)historyClones.size(), killerStr, deadStr);
            m_debugMatchDetails = L"";
            if (killerResolved) m_debugMatchDetails.AppendFormat(L"\n★★★ 击杀：[%s]", finalKillerName);
            if (deadResolved) m_debugMatchDetails.AppendFormat(L"\n☠☠☠ 阵亡：[%s]", finalDeadName);
        }
        InvalidateRect(&m_previewRect, FALSE);

        // 如果依然没有找齐双方，等待一段时间继续回溯下一帧
        if (!killerResolved || !deadResolved) std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    }
    for (HBITMAP hb : historyClones) DeleteObject(hb);
    m_bMatchingInProgress = false;
}


CString CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex) {
    RECT r_game;
    if (nAreaIndex == 0) r_game = { (long)(m_w * 0.190f), (long)(m_h * 0.004f), (long)(m_w * 0.360f), (long)(m_h * 0.040f) };
    else r_game = { (long)(m_w * 0.655f), (long)(m_h * 0.004f), (long)(m_w * 0.815f), (long)(m_h * 0.040f) };
    int sw = r_game.right - r_game.left, sh = r_game.bottom - r_game.top;
    HDC hSrcDC = CreateCompatibleDC(NULL), hDstDC = CreateCompatibleDC(NULL);
    HBITMAP hDstBmp = CreateCompatibleBitmap(GetDC()->GetSafeHdc(), sw * 2, sh * 2);
    SelectObject(hSrcDC, hTargetBmp); SelectObject(hDstDC, hDstBmp);
    SetStretchBltMode(hDstDC, HALFTONE); StretchBlt(hDstDC, 0, 0, sw * 2, sh * 2, hSrcDC, r_game.left, r_game.top, sw, sh, SRCCOPY);
    DWORD t = GetTickCount(); int rd = rand() % 1000;
    CString base = L"E:\\Umi-OCR_Paddle_v2.1.5\\", png, txt;
    png.Format(L"%stmp_%u_%d.png", base, t, rd); txt.Format(L"%stmp_%u_%d.txt", base, t, rd);
    ULONG_PTR gpt; GdiplusStartupInput gpi; GdiplusStartup(&gpt, &gpi, NULL);
    { Bitmap b(hDstBmp, NULL); CLSID id; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &id); b.Save(png, &id, NULL); }
    GdiplusShutdown(gpt); DeleteObject(hDstBmp); DeleteDC(hSrcDC); DeleteDC(hDstDC);
    SHELLEXECUTEINFO sei = { sizeof(sei) }; sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.lpVerb = L"open";
    CString exePath = base + L"Umi-OCR.exe"; sei.lpFile = exePath.GetString();
    CString args; args.Format(L"--path \"%s\" --output \"%s\"", (LPCTSTR)png, (LPCTSTR)txt);
    sei.lpParameters = args.GetString(); sei.nShow = SW_HIDE;
    if (ShellExecuteEx(&sei)) { WaitForSingleObject(sei.hProcess, 4000); CloseHandle(sei.hProcess); }
    CString res = L""; FILE* f = NULL; Sleep(50);
    if (_wfopen_s(&f, txt, L"rb") == 0 && f) {
        char buf[1024] = { 0 }; fread(buf, 1, 1024, f); fclose(f);
        int wl = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
        if (wl > 1) { wchar_t* wb = new wchar_t[wl]; MultiByteToWideChar(CP_UTF8, 0, buf, -1, wb, wl); res = wb; delete[] wb; }
    }
    _wunlink(png); _wunlink(txt); res.Replace(L"\r", L""); res.Replace(L"\n", L""); return res;
}

void CDNFGameCaptureDlg::SyncDataToInputBox() {
    CHARRANGE crSel;
    m_editNamesInput.GetSel(crSel);

    m_editNamesInput.SetWindowText(L"");

    auto appendColorText = [&](const CString& text, COLORREF color, bool bold = false) {
        int len = m_editNamesInput.GetWindowTextLength();
        m_editNamesInput.SetSel(len, len);
        CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.crTextColor = color;
        cf.dwEffects = bold ? CFE_BOLD : 0;
        m_editNamesInput.SetSelectionCharFormat(cf);
        m_editNamesInput.ReplaceSel(text);
        };

    appendColorText(L"============= 总比分 =============\r\n", RGB(150, 150, 150), true);
    CString scoreLine; scoreLine.Format(L"               红 %d  :  %d 蓝\r\n\r\n", m_totalScoreRed, m_totalScoreBlue);
    appendColorText(scoreLine, RGB(0, 150, 0), true);

    appendColorText(L"【红队】\r\n", RGB(220, 0, 0), true);
    for (int i = 0; i < 4; i++) {
        if (m_players[i].name.IsEmpty()) { appendColorText(L"\r\n", RGB(0, 0, 0)); continue; }
        CString line = L"  " + m_players[i].name;
        for (auto& al : m_players[i].aliases) line += L"(" + al.name + L")";
        line.AppendFormat(L" = %d/%d", m_players[i].kills, m_players[i].deaths);
        for (auto& al : m_players[i].aliases) {
            if (al.kills > 0 || al.deaths > 0) line.AppendFormat(L" [%s]=%d/%d", al.name, al.kills, al.deaths);
        }
        line += L"\r\n";
        appendColorText(line, RGB(220, 0, 0));
    }

    appendColorText(L"\r\n【蓝队】\r\n", RGB(0, 0, 220), true);
    for (int i = 4; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) { appendColorText(L"\r\n", RGB(0, 0, 0)); continue; }
        CString line = L"  " + m_players[i].name;
        for (auto& al : m_players[i].aliases) line += L"(" + al.name + L")";
        line.AppendFormat(L" = %d/%d", m_players[i].kills, m_players[i].deaths);
        for (auto& al : m_players[i].aliases) {
            if (al.kills > 0 || al.deaths > 0) line.AppendFormat(L" [%s]=%d/%d", al.name, al.kills, al.deaths);
        }
        line += L"\r\n";
        appendColorText(line, RGB(0, 0, 220));
    }

    m_editNamesInput.SetSel(crSel);
}

void CDNFGameCaptureDlg::UpdatePlayersFromUI() {
    CString text; m_editNamesInput.GetWindowText(text);
    int start = 0;
    PlayerData oldData[8]; for (int i = 0; i < 8; i++) oldData[i] = m_players[i];

    int currentTeam = 0;
    int redIdx = 0, blueIdx = 4;

    for (int i = 0; i < 8; i++) {
        m_players[i].name = L""; m_players[i].aliases.clear();
        m_players[i].kills = 0; m_players[i].deaths = 0;
        m_players[i].team = (i < 4) ? 0 : 1;
    }

    while (start < text.GetLength()) {
        int newlinePos = text.Find(L'\n', start);
        CString line = (newlinePos != -1) ? text.Mid(start, newlinePos - start) : text.Mid(start);
        start = (newlinePos != -1) ? newlinePos + 1 : (int)text.GetLength();
        line.Remove(L'\r'); line.Trim();

        if (line.IsEmpty() || line.Find(L"===") != -1) continue;

        if (line.Find(L"红") != -1 && line.Find(L"蓝") != -1 && line.Find(L":") != -1) {
            int rPos = line.Find(L"红"), bPos = line.Find(L"蓝");
            if (rPos < bPos) {
                CString midStr = line.Mid(rPos + 1, bPos - rPos - 1);
                int colon = midStr.Find(L":");
                if (colon != -1) {
                    m_totalScoreRed = _wtoi(midStr.Left(colon));
                    m_totalScoreBlue = _wtoi(midStr.Mid(colon + 1));
                }
            }
            continue;
        }

        if (line == L"【红队】" || line == L"[红队]" || line == L"红队") { currentTeam = 0; continue; }
        if (line == L"【蓝队】" || line == L"[蓝队]" || line == L"蓝队") { currentTeam = 1; continue; }

        int pIdx = (currentTeam == 0) ? redIdx : blueIdx;
        if (currentTeam == 0 && redIdx >= 4) continue;
        if (currentTeam == 1 && blueIdx >= 8) continue;

        int eqPos = line.FindOneOf(L"=＝");
        CString namePart = (eqPos != -1) ? line.Left(eqPos) : line;
        namePart.Trim();

        int firstP = namePart.FindOneOf(L"(（");
        if (firstP != -1) {
            m_players[pIdx].name = namePart.Left(firstP); m_players[pIdx].name.Trim();
            CString aliasRaw = namePart.Mid(firstP); int cur = 0;
            while (true) {
                CString rem = aliasRaw.Mid(cur);
                int L = rem.FindOneOf(L"(（"); if (L == -1) break;
                int R = rem.FindOneOf(L")）"); if (R == -1) break;
                CString aN = rem.Mid(L + 1, R - L - 1); aN.Trim();
                if (!aN.IsEmpty()) {
                    int ok = 0, od = 0;
                    for (auto& oa : oldData[pIdx].aliases) if (oa.name == aN) { ok = oa.kills; od = oa.deaths; break; }
                    m_players[pIdx].aliases.push_back({ aN, ok, od });
                }
                cur += R + 1;
            }
        }
        else {
            m_players[pIdx].name = namePart;
        }

        if (eqPos != -1) {
            CString scorePart = line.Mid(eqPos + 1); scorePart.Trim();
            int firstBracket = scorePart.Find(L'[');
            CString mainScoreStr = (firstBracket != -1) ? scorePart.Left(firstBracket) : scorePart;
            mainScoreStr.Trim();
            int slash = mainScoreStr.FindOneOf(L"/\\-");
            if (slash != -1) {
                m_players[pIdx].kills = _wtoi(mainScoreStr.Left(slash));
                m_players[pIdx].deaths = _wtoi(mainScoreStr.Mid(slash + 1));
            }
            else {
                m_players[pIdx].kills = _wtoi(mainScoreStr);
            }

            int bPos = line.Find(L'[');
            while (bPos != -1) {
                int eb = line.Find(L']', bPos);
                if (eb != -1) {
                    CString target = line.Mid(bPos + 1, eb - bPos - 1); target.Trim();
                    CString remLine = line.Mid(eb + 1);
                    int subEq = remLine.FindOneOf(L"=＝");
                    if (subEq != -1) {
                        CString v = remLine.Mid(subEq + 1); v.Trim();
                        int nextB = v.Find(L'['); if (nextB != -1) v = v.Left(nextB);
                        int s = v.FindOneOf(L"/\\-");
                        int tk = (s != -1) ? _wtoi(v.Left(s)) : _wtoi(v);
                        int td = (s != -1) ? _wtoi(v.Mid(s + 1)) : 0;
                        for (auto& al : m_players[pIdx].aliases) {
                            if (al.name == target) { al.kills = tk; al.deaths = td; break; }
                        }
                    }
                }
                bPos = line.Find(L'[', eb != -1 ? eb : bPos + 1);
            }
        }
        else {
            m_players[pIdx].kills = oldData[pIdx].kills;
            m_players[pIdx].deaths = oldData[pIdx].deaths;
        }

        if (currentTeam == 0) redIdx++; else blueIdx++;
    }

    SyncDataToInputBox();
    WriteScoreToFile();
    RefreshDisplay();
}

// ====================== 核心革新：独立纯净排版的文件输出 ======================
void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> red, blue;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) red.push_back(m_players[i]);
        else blue.push_back(m_players[i]);
    }
    while (red.size() < 4) red.push_back({ L"", {}, 0, 0, 0 });
    while (blue.size() < 4) blue.push_back({ L"", {}, 0, 0, 1 });

    // 根据是否翻转选项，决定左右队伍的归属
    std::vector<PlayerData>& leftTeam = m_bFlipSides ? blue : red;
    std::vector<PlayerData>& rightTeam = m_bFlipSides ? red : blue;

    // 1. 写 比分.txt (纯净的 x-x 格式)
    FILE* fScore = NULL;
    if (_wfopen_s(&fScore, L"C:\\比分.txt", L"wt, ccs=UTF-8") == 0 && fScore) {
        if (m_bFlipSides) fwprintf(fScore, L"%d-%d\n", m_totalScoreBlue, m_totalScoreRed);
        else fwprintf(fScore, L"%d-%d\n", m_totalScoreRed, m_totalScoreBlue);
        fclose(fScore);
    }

    // 2. 写 人头.txt (去除全部多余文字，纯"人名 击杀/死亡"对齐排版)
    FILE* fKills = NULL;
    if (_wfopen_s(&fKills, L"C:\\人头.txt", L"wt, ccs=UTF-8") == 0 && fKills) {
        for (int i = 0; i < 4; ++i) {
            auto getS = [](PlayerData& p) {
                if (p.name.IsEmpty()) return CString(L"");
                int tk = p.kills, td = p.deaths;
                for (auto& a : p.aliases) { tk += a.kills; td += a.deaths; }
                CString s; s.Format(L"%s %d/%d", p.name.GetString(), tk, td);
                return s;
                };
            CString leftStr = getS(leftTeam[i]);
            CString rightStr = getS(rightTeam[i]);

            // 如果某一行左右都没人，则直接跳过避免输出空行
            if (leftStr.IsEmpty() && rightStr.IsEmpty()) continue;

            fwprintf(fKills, L"%s\t\t%s\n", leftStr.GetString(), rightStr.GetString());
        }
        fclose(fKills);
    }
}

void CDNFGameCaptureDlg::AppendResultText(const CString& text, COLORREF color) {
    int len = m_editOcrResult.GetWindowTextLength(); m_editOcrResult.SetSel(len, len);
    CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = color;
    m_editOcrResult.SetSelectionCharFormat(cf); m_editOcrResult.ReplaceSel(text);
}

int GetVisualWidth(const CString& str) {
    int width = 0;
    for (int i = 0; i < str.GetLength(); i++) {
        unsigned short c = str[i];
        if (c >= 0x4E00 && c <= 0x9FFF) width += 2;
        else width += 1;
    }
    return width;
}

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");

    int leftScore = m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed;
    int rightScore = m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue;

    CString scoreStr;
    scoreStr.Format(L"============= 总比分  %d : %d =============\r\n", leftScore, rightScore);
    AppendResultText(scoreStr, RGB(0, 100, 0));

    // 根据翻转状态动态显示表头
    CString titleLeft = m_bFlipSides ? L"蓝 队 选 手" : L"红 队 选 手";
    CString titleRight = m_bFlipSides ? L"红 队 选 手" : L"蓝 队 选 手";
    CString headerLine; headerLine.Format(L"%-25s%s\r\n", titleLeft.GetString(), titleRight.GetString());

    AppendResultText(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0));
    AppendResultText(L"------------------------------------------\r\n", RGB(150, 150, 150));

    std::vector<int> redIdx, blueIdx;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) redIdx.push_back(i);
        else blueIdx.push_back(i);
    }

    std::vector<int>& leftIdx = m_bFlipSides ? blueIdx : redIdx;
    std::vector<int>& rightIdx = m_bFlipSides ? redIdx : blueIdx;

    COLORREF leftColor = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0);
    COLORREF rightColor = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    const int COLUMN_VISUAL_WIDTH = 32;
    size_t rowCount = max(leftIdx.size(), rightIdx.size());

    for (size_t i = 0; i < rowCount; i++) {
        CString leftText = L"";
        if (i < leftIdx.size()) {
            int p = leftIdx[i];
            int tk = m_players[p].kills, td = m_players[p].deaths;
            for (const auto& alt : m_players[p].aliases) { tk += alt.kills; td += alt.deaths; }
            leftText.Format(L"%s : %d/%d", (LPCTSTR)m_players[p].name, tk, td);
        }

        AppendResultText(leftText, leftColor);
        int currentW = GetVisualWidth(leftText);
        CString spaces = L"";
        for (int s = 0; s < (COLUMN_VISUAL_WIDTH - currentW); s++) spaces += L" ";
        AppendResultText(spaces, RGB(0, 0, 0));

        if (i < rightIdx.size()) {
            int p = rightIdx[i];
            int tk = m_players[p].kills, td = m_players[p].deaths;
            for (const auto& alt : m_players[p].aliases) { tk += alt.kills; td += alt.deaths; }
            CString rightText;
            rightText.Format(L"%s : %d/%d\r\n", (LPCTSTR)m_players[p].name, tk, td);
            AppendResultText(rightText, rightColor);
        }
        else {
            AppendResultText(L"\r\n", RGB(0, 0, 0));
        }

        std::vector<int> leftV, rightV;
        if (i < leftIdx.size()) {
            for (size_t a = 0; a < m_players[leftIdx[i]].aliases.size(); a++)
                if (m_players[leftIdx[i]].aliases[a].kills > 0 || m_players[leftIdx[i]].aliases[a].deaths > 0) leftV.push_back((int)a);
        }
        if (i < rightIdx.size()) {
            for (size_t a = 0; a < m_players[rightIdx[i]].aliases.size(); a++)
                if (m_players[rightIdx[i]].aliases[a].kills > 0 || m_players[rightIdx[i]].aliases[a].deaths > 0) rightV.push_back((int)a);
        }

        size_t maxAliasRow = max(leftV.size(), rightV.size());
        for (size_t ar = 0; maxAliasRow > 0 && ar < maxAliasRow; ar++) {
            CString lA = L"";
            if (ar < leftV.size()) {
                int p = leftIdx[i]; int a = leftV[ar];
                lA.Format(L"  └ %s : %d/%d", (LPCTSTR)m_players[p].aliases[a].name, m_players[p].aliases[a].kills, m_players[p].aliases[a].deaths);
            }
            AppendResultText(lA, RGB(128, 128, 128));
            int lW = GetVisualWidth(lA);
            CString lS = L"";
            for (int s = 0; s < (COLUMN_VISUAL_WIDTH - lW); s++) lS += L" ";
            AppendResultText(lS, RGB(0, 0, 0));

            if (ar < rightV.size()) {
                int p = rightIdx[i]; int a = rightV[ar];
                CString rA;
                rA.Format(L"  └ %s : %d/%d\r\n", (LPCTSTR)m_players[p].aliases[a].name, m_players[p].aliases[a].kills, m_players[p].aliases[a].deaths);
                AppendResultText(rA, RGB(128, 128, 128));
            }
            else {
                AppendResultText(L"\r\n", RGB(0, 0, 0));
            }
        }
    }
}

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;

    CPen p1(PS_SOLID, 2, RGB(255, 0, 0));
    dc.SelectObject(&p1);
    dc.SelectStockObject(NULL_BRUSH);
    float ptsX[4] = { 0.187f, 0.157f, 0.840f, 0.810f };
    float ptsY[4] = { 0.036f, 0.034f, 0.039f, 0.039f };
    for (int i = 0; i < 4; i++) {
        int cx = m_previewRect.left + (int)(ptsX[i] * m_previewRect.Width());
        int cy = m_previewRect.top + (int)(ptsY[i] * m_previewRect.Height());
        dc.Ellipse(cx - 5, cy - 5, cx + 5, cy + 5);
    }

    CPen p3(PS_SOLID, 2, RGB(255, 165, 0));
    dc.SelectObject(&p3);
    for (int i = 0; i < 16; i++) {
        int cx = m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width());
        int cy = m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height());
        dc.Ellipse(cx - 5, cy - 5, cx + 5, cy + 5);
    }

    CPen p2(PS_SOLID, 2, RGB(0, 255, 0));
    CBrush b2(RGB(0, 255, 0));
    dc.SelectObject(&p2);
    dc.SelectObject(&b2);
    for (size_t i = 0; i < m_selectPts.size(); i++) {
        int cx = m_previewRect.left + (int)((m_selectPts[i].x / 10000.0f) * m_previewRect.Width());
        int cy = m_previewRect.top + (int)((m_selectPts[i].y / 10000.0f) * m_previewRect.Height());
        dc.Ellipse(cx - 4, cy - 4, cx + 4, cy + 4);

        CString num; num.Format(L"%d", i + 1);
        dc.SetTextColor(RGB(0, 255, 0));
        dc.SetBkMode(TRANSPARENT);
        CFont numFont; numFont.CreatePointFont(90, L"Arial");
        CFont* oldF = dc.SelectObject(&numFont);
        dc.TextOutW(cx + 6, cy - 6, num);
        dc.SelectObject(oldF);
    }

    CString h; { std::lock_guard<std::mutex> lk(m_debugMutex); if (!m_debugOcrResult.IsEmpty()) { h = m_debugOcrResult + L"\n" + m_debugMatchDetails; } }
    if (!h.IsEmpty()) {
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(0, 255, 0));
        CFont f; f.CreatePointFont(105, L"黑体"); CFont* of = dc.SelectObject(&f);
        CRect r(m_previewRect.left + 15, m_previewRect.top + 15, m_previewRect.right - 15, m_previewRect.bottom - 15);
        CRect cr = r; dc.DrawText(h, &cr, DT_LEFT | DT_TOP | DT_CALCRECT);
        int th = cr.Height(); CRect tr = cr; tr.bottom = m_previewRect.bottom - 15; tr.top = tr.bottom - th;
        CRect br = tr; br.InflateRect(8, 8); dc.FillSolidRect(&br, RGB(25, 25, 25));
        dc.DrawText(h, &tr, DT_LEFT | DT_TOP); dc.SelectObject(of);
    }
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;

    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= 16) {
            m_selectPts.clear();
        }

        float ratioX = (float)(point.x - m_previewRect.left) / m_previewRect.Width();
        float ratioY = (float)(point.y - m_previewRect.top) / m_previewRect.Height();

        m_selectPts.push_back(CPoint((int)(ratioX * 10000.0f), (int)(ratioY * 10000.0f)));
        InvalidateRect(&m_previewRect, FALSE);

        if (m_selectPts.size() == 16) {
            CString result = L"// 自动生成的 16 个比分判定点坐标(比例)：\r\n";
            result += L"struct ScorePointF { float x, y; };\r\n";
            result += L"ScorePointF g_scorePts[16] = {\r\n";
            for (int i = 0; i < 16; i++) {
                CString temp;
                temp.Format(L"    { %.4ff, %.4ff }, // 第 %d 个点\r\n",
                    m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f, i + 1);
                result += temp;
            }
            result += L"};\r\n";

            m_editOcrResult.SetWindowText(result);
            MessageBox(L"16个坐标已采集完毕！\n请从右侧结果框中复制生成的代码并发给我。", L"采集成功");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnPaint() {
    CPaintDC dc(this); CRect r; GetClientRect(&r);
    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(105, L"微软雅黑");
        int uY = r.bottom - (int)(280 * WINDOW_SCALE); int hW = (r.right - 30) / 2;

        m_editNamesInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL, CRect(10, uY, 10 + hW, r.bottom - 80), this, 1001);
        m_editNamesInput.SetFont(&m_font);

        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(10 + hW + 10, uY, r.right - 10, r.bottom - 80), this, 1002);
        m_editOcrResult.SetFont(&m_font);

        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, r.bottom - 75, 10 + hW, r.bottom - 40), this, ID_BTN_START);
        m_btnApply.Create(L"应用修改", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10 + hW + 10, r.bottom - 75, r.right - 10, r.bottom - 40), this, ID_BTN_APPLY);

        // 新增复选框：翻转红蓝输出选项
        m_chkFlip.Create(L"翻转红蓝输出 (勾选则蓝队在左，红队在右)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(10, r.bottom - 30, 260, r.bottom - 10), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font);
        if (m_bFlipSides) m_chkFlip.SetCheck(BST_CHECKED);

        m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(270, r.bottom - 30, r.right, r.bottom), this, 1003);

        SyncDataToInputBox();
    }
    Draw(dc);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == 1 && m_bIsRunning) { Capture(); CheckColorTrigger(); }
    else if (nIDEvent == 2) { m_bCanTrigger = TRUE; KillTimer(2); }
    else if (nIDEvent == 4) { m_bCanTriggerTeamScore = TRUE; KillTimer(4); }
    else if (nIDEvent == 3 && m_bIsRunning) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp && m_w > 0 && m_h > 0) {
            if (m_historyBmps[m_historyIdx]) DeleteObject(m_historyBmps[m_historyIdx]);
            HDC hDC = ::GetDC(NULL); HDC hSrc = CreateCompatibleDC(hDC); HDC hDst = CreateCompatibleDC(hDC);
            m_historyBmps[m_historyIdx] = CreateCompatibleBitmap(hDC, m_w, m_h);
            HGDIOBJ os = SelectObject(hSrc, m_bmp); HGDIOBJ od = SelectObject(hDst, m_historyBmps[m_historyIdx]);
            BitBlt(hDst, 0, 0, m_w, m_h, hSrc, 0, 0, SRCCOPY);
            SelectObject(hSrc, os); SelectObject(hDst, od);
            DeleteDC(hSrc); DeleteDC(hDst); ::ReleaseDC(NULL, hDC);
            m_historyIdx = (m_historyIdx + 1) % 6;
        }
    }
}