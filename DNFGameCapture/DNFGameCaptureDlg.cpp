#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <shellapi.h>    // ShellExecute需要
#include <Gdiplus.h>     // GDI+需要
#pragma comment(lib, "Gdiplus.lib")
using namespace Gdiplus;

// 新增：定义缩放倍数常量，方便后续调整
const float WINDOW_SCALE = 1.6f;

CDNFGameCaptureDlg::CDNFGameCaptureDlg()
{
    m_bmp = NULL;
    m_w = 0;
    m_h = 0;
    m_bCanOcr = TRUE;

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
        ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));

    // 【修改点1】窗口初始尺寸放大1.4倍（原820x650 → 820*1.4=1148，650*1.4=910）
    int initWidth = (int)(820 * WINDOW_SCALE);
    int initHeight = (int)(650 * WINDOW_SCALE);
    CreateEx(0, cls, L"DNF颜色触发OCR识别", WS_OVERLAPPEDWINDOW,
        100, 100, initWidth, initHeight, NULL, NULL);
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg()
{
    if (m_bmp)
        ::DeleteObject(m_bmp);
}

BEGIN_MESSAGE_MAP(CDNFGameCaptureDlg, CWnd)
    ON_WM_TIMER()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CLOSE()
END_MESSAGE_MAP()

void CDNFGameCaptureDlg::OnClose()
{
    KillTimer(1);
    KillTimer(2);
    DestroyWindow();
    PostQuitMessage(0);
}

CString CDNFGameCaptureDlg::RunOCR(int nAreaIndex)
{
    // 全局锁：修复退出时临界区未销毁的问题
    static CRITICAL_SECTION g_cs;
    static bool cs_init = false;
    if (!cs_init)
    {
        InitializeCriticalSection(&g_cs);
        cs_init = true;
        // 程序退出时销毁临界区，避免内存泄漏
        atexit([]() { DeleteCriticalSection(&g_cs); });
    }
    EnterCriticalSection(&g_cs);

    // 文件名：保留动态命名，但后续延迟删除避免冲突
    SYSTEMTIME st;
    GetLocalTime(&st);
    CString fname;
    fname.Format(L"ocr_%d_%d_%02d%02d%02d",
        GetCurrentProcessId(), nAreaIndex,
        st.wMinute, st.wSecond, st.wMilliseconds);

    CString base = L"E:\\Umi-OCR_Paddle_v2.1.5\\";
    CString png = base + fname + L".png";
    CString txt = base + fname + L".txt";

    // ========== 核心修改1：先计算预览窗口的坐标映射（游戏坐标→预览窗口坐标） ==========
    // 预览窗口的宽高（已在Capture中赋值）
    int previewW = m_previewRect.Width();
    int previewH = m_previewRect.Height();
    RECT r_game; // 游戏原始坐标区域
    if (nAreaIndex == 0)
    {
        r_game.left = (long)(m_w * 0.197f);
        r_game.top = (long)(m_h * 0.004f);
        r_game.right = (long)(m_w * 0.349f);
        r_game.bottom = (long)(m_h * 0.035f);
    }
    else
    {
        r_game.left = (long)(m_w * 0.660f);
        r_game.top = (long)(m_h * 0.004f);
        r_game.right = (long)(m_w * 0.803f);
        r_game.bottom = (long)(m_h * 0.035f);
    }
    // 转换为预览窗口的坐标（拉伸后的坐标）
    RECT r_preview;
    r_preview.left = m_previewRect.left + (long)((float)(r_game.left) / m_w * previewW);
    r_preview.top = m_previewRect.top + (long)((float)(r_game.top) / m_h * previewH);
    r_preview.right = m_previewRect.left + (long)((float)(r_game.right) / m_w * previewW);
    r_preview.bottom = m_previewRect.top + (long)((float)(r_game.bottom) / m_h * previewH);

    int w = r_preview.right - r_preview.left;
    int h = r_preview.bottom - r_preview.top;
    CString res = L"none";

    // 提前校验：预览窗口尺寸有效、当前窗口有效
    if (w > 0 && h > 0 && previewW > 0 && previewH > 0 && IsWindow(m_hWnd))
    {
        // ========== 核心修改2：从预览窗口（当前Dlg）获取DC，而非游戏窗口 ==========
        HDC hPreviewDC = ::GetDC(m_hWnd); // 当前Dlg的DC（预览窗口）
        if (hPreviewDC == NULL)
        {
            LeaveCriticalSection(&g_cs);
            return res;
        }

        // 2. 创建兼容DC和位图（基于预览窗口DC）
        HDC memdc = CreateCompatibleDC(hPreviewDC);
        HBITMAP bmp = CreateCompatibleBitmap(hPreviewDC, w, h);
        if (memdc == NULL || bmp == NULL)
        {
            ::ReleaseDC(m_hWnd, hPreviewDC);
            LeaveCriticalSection(&g_cs);
            return res;
        }

        HGDIOBJ old = SelectObject(memdc, bmp);
        // 截图：从预览窗口DC截取对应区域
        BitBlt(memdc, 0, 0, w, h, hPreviewDC, r_preview.left, r_preview.top, SRCCOPY);
        SelectObject(memdc, old); // 恢复旧对象

        // ========== 核心修复2：GDI+操作完成后再释放Bitmap ==========
        ULONG_PTR gpt = 0;
        GdiplusStartupInput gpi = { 0 };
        Status gdiStatus = GdiplusStartup(&gpt, &gpi, NULL);
        if (gdiStatus == Ok)
        {
            CLSID id;
            CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &id);

            // Bitmap对象持有bmp，此时绝对不能释放bmp！
            Bitmap bitmap(bmp, NULL);
            Status saveRet = bitmap.Save(png, &id, NULL);
            if (saveRet != Ok)
            {
                CString s = L"❌ PNG保存失败，GDI+状态码：";
                OutputDebugString(s);
            }
        }

        // ========== 核心修复3：GDI+用完后，再释放HBITMAP/DC ==========
        DeleteObject(bmp);    // 此时Bitmap已销毁，可安全释放
        DeleteDC(memdc);      // 释放DC
        ::ReleaseDC(m_hWnd, hPreviewDC); // 释放预览窗口DC
        GdiplusShutdown(gpt);

        // 等待PNG文件生成（带超时，避免死等）
        DWORD dwPngWaitStart = GetTickCount();
        BOOL pngExist = FALSE;
        while (GetTickCount() - dwPngWaitStart < 3000)
        {
            if (GetFileAttributes(png) != INVALID_FILE_ATTRIBUTES)
            {
                pngExist = TRUE;
                Sleep(200); // 确保文件完全写入
                break;
            }
            Sleep(100);
        }
        if (!pngExist)
        {
            LeaveCriticalSection(&g_cs);
            return res;
        }

        // 调用OCR：检查程序是否存在
        CString ocrExe = base + L"Umi-OCR.exe";
        if (GetFileAttributes(ocrExe) == INVALID_FILE_ATTRIBUTES)
        {
            res = L"OCR程序不存在";
            LeaveCriticalSection(&g_cs);
            return res;
        }

        CString param;
        param.Format(L"--path \"%s\" --output \"%s\"", png, txt);
        HINSTANCE hInst = ShellExecute(NULL, L"open", ocrExe, param, base, SW_HIDE);
        if ((int)hInst <= 32)
        {
            res = L"OCR调用失败";
            LeaveCriticalSection(&g_cs);
            return res;
        }

        // 等待TXT生成（超时5秒）
        DWORD dwTxtWaitStart = GetTickCount();
        BOOL txtExist = FALSE;
        while (GetTickCount() - dwTxtWaitStart < 5000)
        {
            if (GetFileAttributes(txt) != INVALID_FILE_ATTRIBUTES)
            {
                txtExist = TRUE;
                Sleep(300); // 确保OCR写完文件
                break;
            }
            Sleep(200);
        }
        if (!txtExist)
        {
            LeaveCriticalSection(&g_cs);
            return res;
        }

        // 读取TXT：避免内存越界
        FILE* f = NULL;
        if (_wfopen_s(&f, txt, L"rb") == 0 && f != NULL)
        {
            char buf[4096] = { 0 }; // 初始化全0
            size_t readLen = fread(buf, 1, 4000, f);
            fclose(f);

            if (readLen > 0)
            {
                buf[4000] = '\0'; // 强制加结束符
                int len = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
                if (len > 0)
                {
                    wchar_t* wbuf = new wchar_t[len + 1](); // 多开1位存\0
                    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, len);
                    res = wbuf;
                    delete[] wbuf; // 释放内存
                }
            }
        }

        // 延迟删除文件（避免OCR还在占用）
        Sleep(500);
        DeleteFile(png);
        DeleteFile(txt);
    }

    LeaveCriticalSection(&g_cs);
    return res;
}

void CDNFGameCaptureDlg::Capture()
{
    HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
    if (!hGame)
    {
        m_status.SetWindowText(L"未找到游戏");
        return;
    }

    RECT rc;
    ::GetClientRect(hGame, &rc);
    m_w = rc.right - rc.left;
    m_h = rc.bottom - rc.top;

    if (m_w <= 0 || m_h <= 0)
        return;

    if (!m_bmp)
    {
        HDC hdc = ::GetDC(hGame);
        m_bmp = ::CreateCompatibleBitmap(hdc, m_w, m_h);
        ::ReleaseDC(hGame, hdc);
    }

    HDC hGameDC = ::GetDC(hGame);
    HDC hMem = ::CreateCompatibleDC(hGameDC);
    HGDIOBJ old = ::SelectObject(hMem, m_bmp);
    ::PrintWindow(hGame, hMem, 2);

    CRect r;
    GetClientRect(&r);
    // 【修改点2】预览区域高度适配放大后的窗口（原减30 → 按比例减30*1.4=42）
    r.bottom -= (int)(30 * WINDOW_SCALE);

    CClientDC dc(this);
    dc.SetStretchBltMode(HALFTONE);
    dc.StretchBlt(r.left, r.top, r.Width(), r.Height(),
        CDC::FromHandle(hMem), 0, 0, m_w, m_h, SRCCOPY);

    ::SelectObject(hMem, old);
    ::DeleteDC(hMem);
    ::ReleaseDC(hGame, hGameDC);

    m_colorPts[0].x = (int)(m_w * 0.187f);
    m_colorPts[0].y = (int)(m_h * 0.036f);
    m_colorPts[1].x = (int)(m_w * 0.157f);
    m_colorPts[1].y = (int)(m_h * 0.034f);
    m_colorPts[2].x = (int)(m_w * 0.840f);
    m_colorPts[2].y = (int)(m_h * 0.039f);
    m_colorPts[3].x = (int)(m_w * 0.810f);
    m_colorPts[3].y = (int)(m_h * 0.039f);
    // 保存预览窗口坐标（用于后续截图）
    GetClientRect(&m_previewRect);
    // 【修改点3】预览区域底部适配放大后的窗口
    m_previewRect.bottom -= (int)(30 * WINDOW_SCALE); // 减去状态栏高度
}

void CDNFGameCaptureDlg::CheckColorTrigger()
{
    if (!m_bmp || m_w <= 0)
        return;

    HDC hMem = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(hMem, m_bmp);

    auto get = [&](int i) {
        return ::GetPixel(hMem, m_colorPts[i].x, m_colorPts[i].y);
        };

    COLORREF c0 = get(0);
    COLORREF c1 = get(1);
    COLORREF c2 = get(2);
    COLORREF c3 = get(3);

    ::SelectObject(hMem, old);
    ::DeleteDC(hMem);

    auto eq = [](COLORREF a, COLORREF b) {
        return abs(GetRValue(a) - GetRValue(b)) < 6 &&
            abs(GetGValue(a) - GetGValue(b)) < 6 &&
            abs(GetBValue(a) - GetBValue(b)) < 6;
        };

    bool g1 = eq(c0, COLOR_BLUE) && eq(c1, COLOR_RED);
    bool g2 = eq(c2, COLOR_BLUE) && eq(c3, COLOR_RED);

    static bool ouce = FALSE;
    if ((g1 || g2) && m_bCanOcr)
    {
        ouce = TRUE;
        m_bCanOcr = FALSE;
        m_status.SetWindowText(L"触发成功 → 正在OCR识别...");
        MessageBeep(MB_OK);

        RECT r1;
        r1.left = (LONG)(m_w * 0.197f);
        r1.top = (LONG)(m_h * 0.001f);
        r1.right = (LONG)(m_w * 0.349f);
        r1.bottom = (LONG)(m_h * 0.032f);

        RECT r2;
        r2.left = (LONG)(m_w * 0.660f);
        r2.top = (LONG)(m_h * 0.000f);
        r2.right = (LONG)(m_w * 0.803f);
        r2.bottom = (LONG)(m_h * 0.032f);

        CString s1 = RunOCR(0); // 识别区域1
        Sleep(300);
        CString s2 = RunOCR(1); // 识别区域2

        // 【修复点】Format占位符匹配（原少一个%s）
        CString show;
        show.Format(L"[区域1]\r\n%s\r\n\r\n[区域2]\r\n%s", s1, s2);
        m_editOcrResult.SetWindowText(show);

        SetTimer(2, 2000, NULL);
    }
}

void CDNFGameCaptureDlg::Draw(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    // 【修改点4】绘图区域底部适配放大后的窗口
    r.bottom -= (int)(30 * WINDOW_SCALE);
    if (m_w <= 0) return;

    // 缩放比例基于放大后的预览区域，自动适配1.4倍窗口
    float sx = (float)r.Width() / m_w;
    float sy = (float)r.Height() / m_h;

    CPen p(PS_SOLID, 2, RGB(255, 0, 0));
    dc.SelectObject(p);
    dc.SelectStockObject(NULL_BRUSH);
    for (int i = 0; i < 4; i++)
    {
        int x = r.left + (int)(m_colorPts[i].x * sx);
        int y = r.top + (int)(m_colorPts[i].y * sy);
        dc.Ellipse(x - 5, y - 5, x + 5, y + 5);
    }

    CPen p2(PS_SOLID, 2, RGB(0, 255, 255));
    dc.SelectObject(p2);

    int l1 = r.left + (int)(m_w * 0.197f * sx);
    int t1 = r.top + (int)(m_h * 0.004f * sy);
    int r1 = r.left + (int)(m_w * 0.349f * sx);
    int b1 = r.top + (int)(m_h * 0.035f * sy);
    dc.Rectangle(l1, t1, r1, b1);

    int l2 = r.left + (int)(m_w * 0.660f * sx);
    int t2 = r.top + (int)(m_h * 0.004f * sy);
    int r2 = r.left + (int)(m_w * 0.803f * sx);
    int b2 = r.top + (int)(m_h * 0.035f * sy);

    dc.Rectangle(l2, t2, r2, b2);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 2)
    {
        m_bCanOcr = TRUE;
        KillTimer(2);
        return;
    }

    Capture();
    CheckColorTrigger();
    Invalidate(FALSE);
}

void CDNFGameCaptureDlg::OnPaint()
{
    CPaintDC dc(this);
    if (!m_status.m_hWnd)
    {
        CRect r;
        GetClientRect(&r);
        // 【修改点5】状态栏位置适配放大后的窗口
        r.top = r.bottom - (int)(30 * WINDOW_SCALE);
        m_status.Create(L"监控中", WS_CHILD | WS_VISIBLE | SS_CENTER, r, this, 0);

        // 【修改点6】OCR结果编辑框位置/尺寸适配放大后的窗口（原500→500*1.4=700，810→810*1.4=1134，640→640*1.4=896）
        int editTop = (int)(500 * WINDOW_SCALE);
        int editRight = (int)(810 * WINDOW_SCALE);
        int editBottom = (int)(640 * WINDOW_SCALE);
        CRect r2(10, editTop, editRight, editBottom);
        m_editOcrResult.Create(ES_MULTILINE | WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOVSCROLL, r2, this, 0);

        SetTimer(1, 30, NULL);
    }
    Draw(dc);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC)
{
    return TRUE;
}