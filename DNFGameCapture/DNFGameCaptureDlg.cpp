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
const int ID_CHK_FLIP = 1007;

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
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip)
END_MESSAGE_MAP()

CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    m_bmp = NULL; m_w = m_h = 0;
    m_bIsRunning = FALSE; m_bCanTrigger = TRUE; m_bCanTriggerTeamScore = TRUE;
    m_historyIdx = 0;
    m_totalScoreRed = 0; m_totalScoreBlue = 0; m_lastKillerTeam = -1; m_bFlipSides = false;
    for (int i = 0; i < 6; i++) m_historyBmps[i] = NULL;
    AfxInitRichEdit2();

    m_players[0].name = L"温柔（刀仞刀)"; m_players[0].team = 0;
    m_players[1].name = L"发哥（卫敢）"; m_players[1].team = 0;
    m_players[2].name = L"旋律（王大枪）"; m_players[2].team = 0;
    m_players[3].name = L"虎牙直播堕落"; m_players[3].team = 0;
    m_players[4].name = L"堕落不如我"; m_players[4].team = 1;
    m_players[5].name = L"桐镜"; m_players[5].team = 1;
    m_players[6].name = L"BBQ"; m_players[6].team = 1;
    m_players[7].name = L"翠花"; m_players[7].team = 1;

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CreateEx(0, cls, L"DNF击杀统计 - 并发无漏版", WS_OVERLAPPEDWINDOW, 100, 100, (int)(850 * WINDOW_SCALE), (int)(750 * WINDOW_SCALE), NULL, NULL);
}

#include <ctime> // 确保文件顶部有这个头文件

void WriteMatchLog(const CString& logLine) {
    CFile file;
    // 使用 CFile 以二进制模式追加，手动处理编码
    if (file.Open(L"match_debug.log", CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite)) {
        if (file.GetLength() == 0) {
            // 如果是新文件，写入 UTF-8 BOM 头 (EF BB BF)
            unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            file.Write(bom, 3);
        }
        file.SeekToEnd();

        // 获取时间
        time_t now = time(0);
        tm t;
        localtime_s(&t, &now);
        CString fullLine;
        fullLine.Format(L"[%02d:%02d:%02d] %s\r\n", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)logLine);

        // 将 Unicode (UTF-16) 转换为 UTF-8 字符串
        std::string utf8Line = CW2A(fullLine, CP_UTF8);
        file.Write(utf8Line.c_str(), (UINT)utf8Line.length());
        file.Close();
    }
}


CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    if (m_bmp) ::DeleteObject(m_bmp);
    for (int i = 0; i < 6; i++) if (m_historyBmps[i]) ::DeleteObject(m_historyBmps[i]);
}

void CDNFGameCaptureDlg::OnClose() {
    m_bIsRunning = FALSE; KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4);
    DestroyWindow(); PostQuitMessage(0);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) { return TRUE; }

void CDNFGameCaptureDlg::OnBnClickedStart() {
    if (!m_bIsRunning) {
        UpdatePlayersFromUI(); m_bIsRunning = TRUE;
        m_btnStart.SetWindowText(L"停止监控");
        SetTimer(1, 50, NULL); SetTimer(3, 1000, NULL);
        m_status.SetWindowText(L"监控中...");
    }
    else {
        m_bIsRunning = FALSE; KillTimer(1); KillTimer(3);
        m_btnStart.SetWindowText(L"开始监控"); m_status.SetWindowText(L"已停止");
    }
}

void CDNFGameCaptureDlg::OnBnClickedApply() { UpdatePlayersFromUI(); m_status.SetWindowText(L"修改已生效！"); }
void CDNFGameCaptureDlg::OnBnClickedFlip() { m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED); WriteScoreToFile(); RefreshDisplay(); }

void CDNFGameCaptureDlg::Capture() {
    HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
    if (!hGame) return;
    std::lock_guard<std::mutex> lock(g_bmpMutex);
    RECT rc; ::GetClientRect(hGame, &rc);
    m_w = rc.right - rc.left; m_h = rc.bottom - rc.top;
    if (m_w <= 0 || m_h <= 0) return;
    if (!m_bmp) {
        HDC hdc = ::GetDC(hGame); m_bmp = ::CreateCompatibleBitmap(hdc, m_w, m_h); ::ReleaseDC(hGame, hdc);
    }
    HDC hGameDC = ::GetDC(hGame); HDC hMem = ::CreateCompatibleDC(hGameDC);
    HGDIOBJ old = ::SelectObject(hMem, m_bmp);
    ::PrintWindow(hGame, hMem, 2);
    CRect client; GetClientRect(&client);
    m_previewRect = client; m_previewRect.bottom -= (int)(290 * WINDOW_SCALE);
    CClientDC dc(this); dc.SetStretchBltMode(HALFTONE);
    dc.StretchBlt(m_previewRect.left, m_previewRect.top, m_previewRect.Width(), m_previewRect.Height(), CDC::FromHandle(hMem), 0, 0, m_w, m_h, SRCCOPY);
    Draw(dc);
    ::SelectObject(hMem, old); ::DeleteDC(hMem); ::ReleaseDC(hGame, hGameDC);
}

void CDNFGameCaptureDlg::CheckColorTrigger() {
    if (!m_bmp || !m_bIsRunning) return;
    COLORREF c_k[4], c_t[16];
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        HDC hMem = ::CreateCompatibleDC(NULL); HGDIOBJ old = ::SelectObject(hMem, m_bmp);
        c_k[0] = ::GetPixel(hMem, (int)(m_w * 0.187f), (int)(m_h * 0.036f));
        c_k[1] = ::GetPixel(hMem, (int)(m_w * 0.157f), (int)(m_h * 0.034f));
        c_k[2] = ::GetPixel(hMem, (int)(m_w * 0.840f), (int)(m_h * 0.039f));
        c_k[3] = ::GetPixel(hMem, (int)(m_w * 0.810f), (int)(m_h * 0.039f));
        for (int i = 0; i < 16; i++) c_t[i] = ::GetPixel(hMem, (int)(m_w * g_scorePts[i].x), (int)(m_h * g_scorePts[i].y));
        ::SelectObject(hMem, old); ::DeleteDC(hMem);
    }
    auto eq = [](COLORREF a, COLORREF b) { return abs(GetRValue(a) - GetRValue(b)) < 25 && abs(GetGValue(a) - GetGValue(b)) < 25 && abs(GetBValue(a) - GetBValue(b)) < 25; };
    auto mk = [&](int p1, int p2) { return (eq(c_k[p1], COLOR_BLUE) && eq(c_k[p2], COLOR_RED)) || (eq(c_k[p1], COLOR_RED) && eq(c_k[p2], COLOR_BLUE)); };
    auto mt = [&](int p1, int p2) { return (eq(c_t[p1], COLOR_BLUE) && eq(c_t[p2], COLOR_RED)) || (eq(c_t[p1], COLOR_RED) && eq(c_t[p2], COLOR_BLUE)); };

    if ((mt(0, 1) && mt(2, 3) && mt(4, 5) && mt(6, 7) || mt(8, 9) && mt(10, 11) && mt(12, 13) && mt(14, 15)) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex); // 保护大比分修改
            if (m_lastKillerTeam == 0) m_totalScoreRed++; else if (m_lastKillerTeam == 1) m_totalScoreBlue++;
            SyncDataToInputBox(); RefreshDisplay(); WriteScoreToFile();
        }
        SetTimer(4, 10000, NULL);
    }

    // ... 前面的颜色获取和 eq, mk, mt 判定逻辑保持不变 ...

        // ============ 大比分（团灭）判定 ============
    if ((mt(0, 1) && mt(2, 3) && mt(4, 5) && mt(6, 7) || mt(8, 9) && mt(10, 11) && mt(12, 13) && mt(14, 15)) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex); // 保护大比分修改
            if (m_lastKillerTeam == 0) m_totalScoreRed++; else if (m_lastKillerTeam == 1) m_totalScoreBlue++;
            SyncDataToInputBox(); RefreshDisplay(); WriteScoreToFile();
        }
        // 【核心修改】：大比分冷却时间改为 2 分钟 (120000 毫秒)
        SetTimer(4, 120000, NULL);
    }

    // ============ 单人击杀判定 ============
    if ((mk(0, 1) || mk(2, 3)) && m_bCanTrigger) {
        m_bCanTrigger = FALSE;
        int killSide = mk(0, 1) ? 0 : 1;
        std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
        // 【核心修改】：击杀冷却时间改为 13 秒 (000 毫秒)
        SetTimer(2, 13000, NULL);
    }
}

void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide) {
    int killerArea = (triggerSide == 0) ? 1 : 0; int deadArea = triggerSide;
    bool killerResolved = false; bool deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";

    // 用于暂存本线程识别出的目标ID，最终统一结算去重
    int killerBestP = -1, killerBestA = -1;
    int deadBestP = -1, deadBestA = -1;

    int lockedKillerTeam = -1;
    int lockedDeadTeam = -1;

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

        auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName, bool isKiller, int& outBestP, int& outBestA) {
            if (resolved || ocrResult.IsEmpty()) return;

            // --- 关键日志：记录 OCR 抓到了什么 ---
            WriteMatchLog(L"------------------------------------");
            WriteMatchLog(L"【新一轮匹配】OCR识别结果: " + ocrResult + (isKiller ? L" (寻找杀手)" : L" (寻找死者)"));

            int maxS = -1;
            int bestP = -1;
            int bestA = -1;
            std::wstring bestN = L"";

            m_dataMutex.lock();
            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                // 阵营过滤日志（可选）
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam) continue;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam) continue;

                // 1. 匹配主名字
                int curScore = m_matcher.GetMatchScore(m_players[p].name.GetString(), ocrResult.GetString());
                std::wstring curBestN = m_players[p].name.GetString();
                int curBestA = -1;

                // 2. 匹配小号
                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    int as = m_matcher.GetMatchScore(m_players[p].aliases[a].name.GetString(), ocrResult.GetString());
                    if (as > curScore) {
                        curScore = as;
                        curBestN = m_players[p].aliases[a].name.GetString();
                        curBestA = (int)a;
                    }
                }

                // --- 关键日志：记录每个角色的得分 ---
                CString scoreDetail;
                scoreDetail.Format(L"  检查选手: %s, 最高匹配分: %d (匹配项: %s)", m_players[p].name, curScore, curBestN.c_str());
                WriteMatchLog(scoreDetail);

                if (curScore > maxS) {
                    maxS = curScore;
                    bestP = p;
                    bestA = curBestA;
                    bestN = curBestN;
                }
            }
            m_dataMutex.unlock();

            // --- 最终判定下限：30分 ---
            if (bestP != -1 && maxS >= 30) {
                resolved = true;
                finalName = bestN.c_str();
                outBestP = bestP;
                outBestA = bestA;

                WriteMatchLog(L"  >> [成功] 最终匹配到: " + finalName + L" (分数: " + std::to_wstring(maxS).c_str() + L")");

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[bestP].team;
                else lockedDeadTeam = m_players[bestP].team;
                m_dataMutex.unlock();
            }
            else {
                WriteMatchLog(L"  XX [失败] 最高分未达到30分下限");
            }
            };

        processMatch(killerStr, killerResolved, finalKillerName, true, killerBestP, killerBestA);
        processMatch(deadStr, deadResolved, finalDeadName, false, deadBestP, deadBestA);

        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            if (isHistory) m_debugOcrResult.Format(L"【时光倒流：%d秒前】\n杀:%s 亡:%s", secondsAgo, killerStr, deadStr);
            else m_debugOcrResult.Format(L"【实时重试：第%d次】\n杀:%s 亡:%s", i - (int)historyClones.size(), killerStr, deadStr);
            m_debugMatchDetails = L"";
            if (killerResolved) m_debugMatchDetails.AppendFormat(L"\n★★★ 击杀：[%s]", finalKillerName);
            if (deadResolved) m_debugMatchDetails.AppendFormat(L"\n☠☠☠ 阵亡：[%s]", finalDeadName);
        }
        InvalidateRect(&m_previewRect, FALSE);

        if (!killerResolved || !deadResolved) std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    }

    // ============ 【终极防御】：双向成对去重结算系统 ============
    // 线程循环完毕后，统一评估并提交战绩，绝不重复算分！
    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();
        bool isDuplicate = false;

        // 检查记忆库：如果这 5 秒内，这对冤家（相同的杀手+死者）已经发生过命案，说明这是 UI 残留！
        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 5000)) {
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate) {
            // 这是全新的击杀，记入记忆库！
            RecentEvent rev; rev.killer = finalKillerName; rev.dead = finalDeadName; rev.time = now;
            m_recentEvents.push_back(rev);

            // 清理10秒前的陈旧记忆
            m_recentEvents.erase(std::remove_if(m_recentEvents.begin(), m_recentEvents.end(), [&](const RecentEvent& ev) {
                return now - ev.time > 10000;
                }), m_recentEvents.end());

            // 兑现分数
            if (killerResolved && killerBestP != -1) {
                if (killerBestA != -1) m_players[killerBestP].aliases[killerBestA].kills++; else m_players[killerBestP].kills++;
                m_lastKillerTeam = m_players[killerBestP].team;
            }
            if (deadResolved && deadBestP != -1) {
                if (deadBestA != -1) m_players[deadBestP].aliases[deadBestA].deaths++; else m_players[deadBestP].deaths++;
            }

            SyncDataToInputBox();
            RefreshDisplay();
            WriteScoreToFile();
        }
    }

    for (HBITMAP hb : historyClones) DeleteObject(hb);
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

    BITMAP bm; GetObject(hDstBmp, sizeof(BITMAP), &bm);
    BITMAPINFO bmi = { 0 }; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = bm.bmWidth; bmi.bmiHeader.biHeight = -bm.bmHeight;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> px(bm.bmWidth * bm.bmHeight * 4);
    GetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);
    for (size_t i = 0; i < px.size(); i += 4) {
        int g = (px[i + 2] * 299 + px[i + 1] * 587 + px[i] * 114) / 1000; int inv = 255 - g;
        inv = (inv < 140) ? 0 : 255; px[i] = px[i + 1] = px[i + 2] = inv;
    }
    SetDIBits(hDstDC, hDstBmp, 0, bm.bmHeight, px.data(), &bmi, DIB_RGB_COLORS);

    DWORD t = GetTickCount(); CString base = L"E:\\Umi-OCR_Paddle_v2.1.5\\", png, txt;
    png.Format(L"%stmp_%u.png", base, t); txt.Format(L"%stmp_%u.txt", base, t);
    ULONG_PTR gpt; GdiplusStartupInput gpi; GdiplusStartup(&gpt, &gpi, NULL);
    { Bitmap b(hDstBmp, NULL); CLSID id; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &id); b.Save(png, &id, NULL); }
    GdiplusShutdown(gpt); DeleteObject(hDstBmp); DeleteDC(hSrcDC); DeleteDC(hDstDC);
    CString exePath = base + L"Umi-OCR.exe";
    SHELLEXECUTEINFO sei = { sizeof(sei) }; sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open"; sei.lpFile = exePath.GetString();
    CString args; args.Format(L"--path \"%s\" --output \"%s\"", (LPCTSTR)png, (LPCTSTR)txt); sei.lpParameters = args.GetString(); sei.nShow = SW_HIDE;
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
    CHARRANGE cr; m_editNamesInput.GetSel(cr); m_editNamesInput.SetWindowText(L"");
    auto ap = [&](const CString& t, COLORREF c, bool b = false) {
        int l = m_editNamesInput.GetWindowTextLength(); m_editNamesInput.SetSel(l, l);
        CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.crTextColor = c; cf.dwEffects = b ? CFE_BOLD : 0; m_editNamesInput.SetSelectionCharFormat(cf); m_editNamesInput.ReplaceSel(t);
        };
    ap(L"============= 总比分 =============\r\n", RGB(150, 150, 150), true);
    CString sL; sL.Format(L"               红 %d  :  %d 蓝\r\n\r\n", m_totalScoreRed, m_totalScoreBlue); ap(sL, RGB(0, 150, 0), true);
    ap(L"【红队】\r\n", RGB(220, 0, 0), true);
    for (int i = 0; i < 4; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        CString l = L"  " + m_players[i].name; for (auto& a : m_players[i].aliases) l += L"(" + a.name + L")";
        l.AppendFormat(L" = %d/%d", m_players[i].kills, m_players[i].deaths);
        for (auto& a : m_players[i].aliases) if (a.kills > 0 || a.deaths > 0) l.AppendFormat(L" [%s]=%d/%d", a.name, a.kills, a.deaths);
        ap(l + L"\r\n", RGB(220, 0, 0));
    }
    ap(L"\r\n【蓝队】\r\n", RGB(0, 0, 220), true);
    for (int i = 4; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        CString l = L"  " + m_players[i].name; for (auto& a : m_players[i].aliases) l += L"(" + a.name + L")";
        l.AppendFormat(L" = %d/%d", m_players[i].kills, m_players[i].deaths);
        for (auto& a : m_players[i].aliases) if (a.kills > 0 || a.deaths > 0) l.AppendFormat(L" [%s]=%d/%d", a.name, a.kills, a.deaths);
        ap(l + L"\r\n", RGB(0, 0, 220));
    }
    m_editNamesInput.SetSel(cr);
}

void CDNFGameCaptureDlg::UpdatePlayersFromUI() {
    std::lock_guard<std::mutex> dataLock(m_dataMutex); // 【安全更新】阻止解析冲突
    CString text; m_editNamesInput.GetWindowText(text); int start = 0;
    PlayerData old[8]; for (int i = 0; i < 8; i++) old[i] = m_players[i];
    int cT = 0, rI = 0, bI = 4;
    for (int i = 0; i < 8; i++) { m_players[i].name = L""; m_players[i].aliases.clear(); m_players[i].kills = 0; m_players[i].deaths = 0; m_players[i].team = (i < 4 ? 0 : 1); }

    while (start < text.GetLength()) {
        int newlinePos = text.Find(L'\n', start);
        CString line = (newlinePos != -1) ? text.Mid(start, newlinePos - start) : text.Mid(start);
        start = (newlinePos != -1) ? newlinePos + 1 : (int)text.GetLength();
        line.Remove(L'\r'); line.Trim();

        if (line.IsEmpty() || line.Find(L"===") != -1) continue;

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

        // 【修复1】：之前把这里也命名成 nP 导致冲突，现在改为 namePart
        CString namePart = (eP != -1 ? line.Left(eP) : line);
        namePart.Trim();

        int fP = namePart.FindOneOf(L"(（");
        if (fP != -1) {
            m_players[pI].name = namePart.Left(fP); m_players[pI].name.Trim();
            CString aR = namePart.Mid(fP);
            int cur = 0;
            while (true) {
                // 【修复2】：CString::FindOneOf 不支持第二个参数，改用 Mid 截取后查找
                CString tempStr = aR.Mid(cur);
                int L_rel = tempStr.FindOneOf(L"(（");
                int R_rel = tempStr.FindOneOf(L")）");

                if (L_rel == -1 || R_rel == -1) break;

                // 还原回绝对坐标
                int L = cur + L_rel;
                int R = cur + R_rel;

                CString aN = aR.Mid(L + 1, R - L - 1); aN.Trim();
                if (!aN.IsEmpty()) {
                    int ok = 0, od = 0;
                    for (auto& oa : old[pI].aliases) {
                        if (oa.name == aN) { ok = oa.kills; od = oa.deaths; break; }
                    }
                    m_players[pI].aliases.push_back({ aN, ok, od });
                }
                cur = R + 1;
            }
        }
        else {
            m_players[pI].name = namePart;
        }

        if (eP != -1) {
            CString sP = line.Mid(eP + 1); sP.Trim();
            int sB = sP.Find(L'[');
            CString mS = (sB != -1 ? sP.Left(sB) : sP); mS.Trim();
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
                    CString t = line.Mid(bP + 1, eb - bP - 1); t.Trim();
                    CString rL = line.Mid(eb + 1);
                    int sE = rL.FindOneOf(L"=＝");
                    if (sE != -1) {
                        CString v = rL.Mid(sE + 1); v.Trim();
                        int nB = v.Find(L'[');
                        if (nB != -1) v = v.Left(nB);
                        int sl2 = v.FindOneOf(L"/-");
                        int tk = (sl2 != -1 ? _wtoi(v.Left(sl2)) : _wtoi(v));
                        int td = (sl2 != -1 ? _wtoi(v.Mid(sl2 + 1)) : 0);
                        for (auto& al : m_players[pI].aliases) {
                            if (al.name == t) { al.kills = tk; al.deaths = td; break; }
                        }
                    }
                }
                bP = line.Find(L'[', eb != -1 ? eb : bP + 1);
            }
        }
        else {
            m_players[pI].kills = old[pI].kills;
            m_players[pI].deaths = old[pI].deaths;
        }

        if (cT == 0) rI++; else bI++;
    }
    SyncDataToInputBox(); WriteScoreToFile(); RefreshDisplay();
}
void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) { if (m_players[i].name.IsEmpty()) continue; if (m_players[i].team == 0) r.push_back(m_players[i]); else b.push_back(m_players[i]); }
    while (r.size() < 4) r.push_back({ L"",{},0,0,0 }); while (b.size() < 4) b.push_back({ L"",{},0,0,1 });
    std::vector<PlayerData>& lT = m_bFlipSides ? b : r; std::vector<PlayerData>& rT = m_bFlipSides ? r : b;
    FILE* fS = NULL; if (_wfopen_s(&fS, L"C:\\比分.txt", L"wt, ccs=UTF-8") == 0 && fS) {
        fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue); fclose(fS);
    }
    FILE* fK = NULL; if (_wfopen_s(&fK, L"C:\\人头.txt", L"wt, ccs=UTF-8") == 0 && fK) {
        for (int i = 0; i < 4; i++) {
            auto gs = [](PlayerData& p) { if (p.name.IsEmpty()) return CString(L""); int tk = p.kills, td = p.deaths; for (auto& a : p.aliases) { tk += a.kills;td += a.deaths; } CString s; s.Format(L"%s %d/%d", p.name.GetString(), tk, td); return s; };
            CString ls = gs(lT[i]), rs = gs(rT[i]); if (!ls.IsEmpty() || !rs.IsEmpty()) fwprintf(fK, L"%s\t\t%s\n", ls.GetString(), rs.GetString());
        } fclose(fK);
    }
}

void CDNFGameCaptureDlg::AppendResultText(const CString& t, COLORREF c) {
    int l = m_editOcrResult.GetWindowTextLength(); m_editOcrResult.SetSel(l, l);
    CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = c;
    m_editOcrResult.SetSelectionCharFormat(cf); m_editOcrResult.ReplaceSel(t);
}

int GetVisualWidth(const CString& s) { int w = 0; for (int i = 0; i < s.GetLength(); i++) w += (s[i] >= 0x4E00 && s[i] <= 0x9FFF) ? 2 : 1; return w; }

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");
    CString sS; sS.Format(L"============= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
    AppendResultText(sS, RGB(0, 100, 0));
    AppendResultText(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0));
    AppendResultText(L"------------------------------------------\r\n", RGB(150, 150, 150));
    std::vector<int> rI, bI; for (int i = 0; i < 8; i++) { if (m_players[i].name.IsEmpty())continue; if (m_players[i].team == 0) rI.push_back(i); else bI.push_back(i); }
    std::vector<int>& lIdx = m_bFlipSides ? bI : rI; std::vector<int>& rIdx = m_bFlipSides ? rI : bI;
    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0), rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);
    size_t rows = max(lIdx.size(), rIdx.size());
    for (size_t i = 0; i < rows; i++) {
        CString lT = L""; if (i < lIdx.size()) { int p = lIdx[i]; int tk = m_players[p].kills, td = m_players[p].deaths; for (auto& a : m_players[p].aliases) { tk += a.kills;td += a.deaths; } lT.Format(L"%s : %d/%d", (LPCTSTR)m_players[p].name, tk, td); }
        AppendResultText(lT, lC); int curW = GetVisualWidth(lT); for (int s = 0; s < (32 - curW); s++) AppendResultText(L" ", 0);
        CString rT = L""; if (i < rIdx.size()) { int p = rIdx[i]; int tk = m_players[p].kills, td = m_players[p].deaths; for (auto& a : m_players[p].aliases) { tk += a.kills;td += a.deaths; } rT.Format(L"%s : %d/%d\r\n", (LPCTSTR)m_players[p].name, tk, td); }
        else rT = L"\r\n";
        AppendResultText(rT, rC);
    }
}

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;
    CPen p1(PS_SOLID, 2, RGB(255, 0, 0)); dc.SelectObject(&p1); dc.SelectStockObject(NULL_BRUSH);
    float pX[4] = { 0.187f,0.157f,0.840f,0.810f }, pY[4] = { 0.036f,0.034f,0.039f,0.039f };
    for (int i = 0; i < 4; i++) dc.Ellipse(m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) - 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) - 5, m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) + 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) + 5);
    CPen p3(PS_SOLID, 2, RGB(255, 165, 0)); dc.SelectObject(&p3);
    for (int i = 0; i < 16; i++) dc.Ellipse(m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) - 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) - 5, m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) + 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) + 5);

    CString h; { std::lock_guard<std::mutex> lk(m_debugMutex); h = m_debugOcrResult + L"\n" + m_debugMatchDetails; }
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
        if (m_selectPts.size() >= 16) m_selectPts.clear();
        m_selectPts.push_back(CPoint((int)(((float)(point.x - m_previewRect.left) / m_previewRect.Width()) * 10000.0f), (int)(((float)(point.y - m_previewRect.top) / m_previewRect.Height()) * 10000.0f)));
        InvalidateRect(&m_previewRect, FALSE);
        if (m_selectPts.size() == 16) {
            CString res = L"ScorePointF g_scorePts[16] = {\r\n";
            for (int i = 0; i < 16; i++) { CString t; t.Format(L"    { %.4ff, %.4ff },\r\n", m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f); res += t; }
            res += L"};\r\n"; m_editOcrResult.SetWindowText(res); MessageBox(L"坐标已采集，见右侧框。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnPaint() {
    CPaintDC dc(this); CRect r; GetClientRect(&r);
    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(105, L"微软雅黑"); int uY = r.bottom - (int)(280 * WINDOW_SCALE); int hW = (r.right - 30) / 2;
        m_editNamesInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL, CRect(10, uY, 10 + hW, r.bottom - 80), this, 1001);
        m_editNamesInput.SetFont(&m_font);
        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, CRect(10 + hW + 10, uY, r.right - 10, r.bottom - 80), this, 1002);
        m_editOcrResult.SetFont(&m_font);
        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, r.bottom - 75, 10 + hW, r.bottom - 40), this, ID_BTN_START);
        m_btnApply.Create(L"应用修改", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10 + hW + 10, r.bottom - 75, r.right - 10, r.bottom - 40), this, ID_BTN_APPLY);
        m_chkFlip.Create(L"翻转红蓝输出 (勾选则蓝左红右)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(10, r.bottom - 30, 260, r.bottom - 10), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font); m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(270, r.bottom - 30, r.right, r.bottom), this, 1003);
        SyncDataToInputBox();
    }
    Draw(dc);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nID) {
    if (nID == 1 && m_bIsRunning) { Capture(); CheckColorTrigger(); }
    else if (nID == 2) { m_bCanTrigger = TRUE; KillTimer(2); }
    else if (nID == 4) { m_bCanTriggerTeamScore = TRUE; KillTimer(4); }
    else if (nID == 3 && m_bIsRunning) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
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