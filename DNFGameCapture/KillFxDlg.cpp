#include "pch.h"
#include "KillFxDlg.h"
#include <WebView2EnvironmentOptions.h>
#include <algorithm>

namespace {
    // Same color key as the kill display window: pixels of this color become transparent.
    constexpr COLORREF kKillFxTransparentKey = RGB(1, 2, 3);
    // The effect layer is designed for a ~360 CSS px tall viewport (the kill window is 278).
    // RasterizationScale maps that design height onto the physical monitor height.
    constexpr double kKillFxDesignHeight = 360.0;
    constexpr wchar_t kKillFxWindowTitle[] = L"DNF Kill FX Fullscreen - DNF\u5168\u5C4F\u51FB\u6740\u7279\u6548";
    constexpr wchar_t kKillFxUrl[] = L"http://127.0.0.1:18777/kill.html?mode=fx";
    // Separate profile so this window can use its own browser flags: keep rendering and timers
    // running while the window sits behind the game (it is never topmost).
    constexpr wchar_t kKillFxBrowserArgs[] =
        L"--disable-features=CalculateNativeWinOcclusion "
        L"--disable-backgrounding-occluded-windows "
        L"--disable-renderer-backgrounding "
        L"--disable-background-timer-throttling";

    CString KillFxUserDataFolder()
    {
        wchar_t tempPath[MAX_PATH] = {};
        const DWORD length = ::GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
        if (length == 0 || length >= std::size(tempPath)) return CString();
        CString folder(tempPath, static_cast<int>(length));
        folder.TrimRight(L"\\/");
        folder += L"\\DNFGameCapture-WebView2-KillFx";
        if (::CreateDirectoryW(folder, nullptr) || ::GetLastError() == ERROR_ALREADY_EXISTS) return folder;
        return CString();
    }
}

IMPLEMENT_DYNAMIC(CKillFxDlg, CDialogEx)

CKillFxDlg::CKillFxDlg(CWnd* pParent)
    : CDialogEx(IDD_WEB_SCORE_DIALOG, pParent)
{
}

CKillFxDlg::~CKillFxDlg()
{
}

BEGIN_MESSAGE_MAP(CKillFxDlg, CDialogEx)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_DISPLAYCHANGE, &CKillFxDlg::OnDisplayChange)
END_MESSAGE_MAP()

BOOL CKillFxDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowText(kKillFxWindowTitle);

    const DWORD removeStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
        WS_BORDER | WS_DLGFRAME | WS_THICKFRAME;
    const DWORD removeExStyle = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE;
    // LAYERED + color key = fully transparent background; TRANSPARENT = mouse clicks pass
    // through; NOACTIVATE = never steals focus; APPWINDOW = listed by capture tools.
    // The window is kept at the BOTTOM of the z-order so it never covers the game on the
    // player's own screen; the streaming tool captures it separately and layers it over DNF.
    const DWORD addExStyle = WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    ModifyStyle(removeStyle, WS_POPUP);
    ModifyStyleEx(removeExStyle, addExStyle);
    SetLayeredWindowAttributes(kKillFxTransparentKey, 255, LWA_COLORKEY);
    PlaceOnPrimaryMonitor();

    InitWebView2();
    return TRUE;
}

void CKillFxDlg::OnClose()
{
    HideOverlay();
    if (m_onUserClose) m_onUserClose();
}

void CKillFxDlg::OnCancel()
{
    OnClose();
}

void CKillFxDlg::OnOK()
{
}

void CKillFxDlg::PlaceOnPrimaryMonitor()
{
    if (!m_hWnd) return;
    HMONITOR monitor = ::MonitorFromPoint(CPoint(0, 0), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = { sizeof(info) };
    CRect rect(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
    if (monitor && ::GetMonitorInfo(monitor, &info)) rect = info.rcMonitor;
    // Always 16:9 (the live canvas ratio), even on 16:10 / 21:9 / 4:3 screens:
    // take the largest 16:9 rectangle that fits the monitor and center it.
    int width = rect.Width();
    int height = MulDiv(width, 9, 16);
    if (height > rect.Height()) {
        height = rect.Height();
        width = MulDiv(height, 16, 9);
    }
    const int left = rect.left + (rect.Width() - width) / 2;
    const int top = rect.top + (rect.Height() - height) / 2;
    SetWindowPos(&wndBottom, left, top, width, height,
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void CKillFxDlg::ApplyScale()
{
    if (!m_webviewController || !m_hWnd) return;
    CRect rect;
    GetClientRect(&rect);
    if (rect.Height() <= 0) return;

    double scale = rect.Height() / kKillFxDesignHeight * (m_scalePercent / 100.0);
    scale = (std::max)(0.5, (std::min)(8.0, scale));

    // RasterizationScale is per controller (unlike ZoomFactor, it never leaks into the
    // kill display window that shares the same origin).
    Microsoft::WRL::ComPtr<ICoreWebView2Controller3> controller3;
    if (SUCCEEDED(m_webviewController.As(&controller3)) && controller3) {
        controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
        controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        controller3->put_RasterizationScale(scale);
    }
    m_webviewController->put_Bounds(rect);
}

void CKillFxDlg::ShowOverlay(int scalePercent)
{
    if (!m_hWnd) return;
    m_scalePercent = (std::max)(50, (std::min)(200, scalePercent));
    PlaceOnPrimaryMonitor();
    ShowWindow(SW_SHOWNOACTIVATE);
    SetWindowPos(&wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (m_webviewController) {
        m_webviewController->put_IsVisible(TRUE);
        ApplyScale();
    }
    m_overlayVisible = true;
}

void CKillFxDlg::HideOverlay()
{
    // Hidden WebView => document.hidden, so the page stops tracking and rendering.
    if (m_webviewController) m_webviewController->put_IsVisible(FALSE);
    if (m_hWnd) ShowWindow(SW_HIDE);
    m_overlayVisible = false;
}

void CKillFxDlg::InitWebView2()
{
    const CString userDataFolder = KillFxUserDataFolder();
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (options) options->put_AdditionalBrowserArguments(kKillFxBrowserArgs);
    CreateCoreWebView2EnvironmentWithOptions(nullptr,
        userDataFolder.IsEmpty() ? nullptr : userDataFolder.GetString(), options.Get(),
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr || !::IsWindow(m_hWnd)) return S_OK;

                env->CreateCoreWebView2Controller(m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || controller == nullptr || !::IsWindow(m_hWnd)) return S_OK;

                            m_webviewController = controller;
                            m_webviewController->get_CoreWebView2(&m_webview);

                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_webviewController.As(&controller2)) && controller2) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            if (m_webview) {
                                Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                                if (SUCCEEDED(m_webview->get_Settings(&settings)) && settings) {
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_IsZoomControlEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                }
                            }

                            ApplyScale();
                            m_webviewController->put_IsVisible(m_overlayVisible ? TRUE : FALSE);
                            if (m_webview) m_webview->Navigate(kKillFxUrl);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void CKillFxDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    ApplyScale();
}

BOOL CKillFxDlg::OnEraseBkgnd(CDC* pDC)
{
    if (!pDC) return TRUE;
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, kKillFxTransparentKey);
    return TRUE;
}

LRESULT CKillFxDlg::OnDisplayChange(WPARAM, LPARAM)
{
    if (m_overlayVisible) {
        PlaceOnPrimaryMonitor();
        ApplyScale();
    }
    return 0;
}
