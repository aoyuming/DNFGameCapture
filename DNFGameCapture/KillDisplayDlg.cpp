#include "pch.h"
#include "KillDisplayDlg.h"
#include "DNFGameCaptureDlg.h"
#include <WebView2EnvironmentOptions.h>
#include <algorithm>

namespace {
    constexpr int kKillDisplayClientWidth = 900;
    constexpr int kKillDisplayClientHeight = 360;
    constexpr int kKillDisplayEditExtraHeight = 250;
    constexpr int kKillDisplayEditMinHeight = 590;
    constexpr int kKillDisplayMinWidth = 460;
    constexpr int kKillDisplayMinHeight = 180;
    constexpr COLORREF kKillDisplayTransparentKey = RGB(1, 2, 3);
    constexpr BYTE kKillDisplayLayeredAlpha = 245;
    constexpr wchar_t kKillDisplayWindowSection[] = L"KillDisplayWindow";
    constexpr wchar_t kKillDisplayWindowTitle[] = L"DNF Kill Display - DNF\u51FB\u6740\u5C55\u793A\u7A97\u53E3";
    constexpr wchar_t kKillDisplayUrl[] = L"http://127.0.0.1:18777/kill.html";

    UINT GetWindowDpi(HWND hwnd)
    {
        UINT dpi = 96;
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32 && hwnd) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
                ::GetProcAddress(user32, "GetDpiForWindow"));
            if (getDpiForWindow) dpi = getDpiForWindow(hwnd);
        }
        return dpi == 0 ? 96 : dpi;
    }

    int ScaleForDpi(int logicalValue, UINT dpi)
    {
        return ::MulDiv(logicalValue, static_cast<int>(dpi), 96);
    }

    int UnscaleForDpi(int physicalValue, UINT dpi)
    {
        return ::MulDiv(physicalValue, 96, static_cast<int>(dpi));
    }

    void ClampRectToMonitorWorkArea(CRect& rect)
    {
        HMONITOR monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = { sizeof(info) };
        if (!monitor || !::GetMonitorInfo(monitor, &info)) return;

        const int workWidth = info.rcWork.right - info.rcWork.left;
        const int workHeight = info.rcWork.bottom - info.rcWork.top;
        const int width = (std::min)(rect.Width(), workWidth);
        const int height = (std::min)(rect.Height(), workHeight);
        const int left = (std::max)(info.rcWork.left,
            (std::min)(rect.left, info.rcWork.right - width));
        const int top = (std::max)(info.rcWork.top,
            (std::min)(rect.top, info.rcWork.bottom - height));
        rect.SetRect(left, top, left + width, top + height);
    }

    void NotifyKillDisplayVisibilityChanged(CWnd* wnd)
    {
        CWnd* parent = wnd ? wnd->GetParent() : nullptr;
        if (!parent) parent = AfxGetMainWnd();
        if (parent && ::IsWindow(parent->GetSafeHwnd())) {
            parent->PostMessage(WM_KILL_DISPLAY_VISIBILITY_CHANGED, 0, 0);
        }
    }
}

IMPLEMENT_DYNAMIC(CKillDisplayDlg, CDialogEx)

CKillDisplayDlg::CKillDisplayDlg(const CString& iniPath, CWnd* pParent)
    : CDialogEx(IDD_WEB_SCORE_DIALOG, pParent), m_iniPath(iniPath)
{
}

CKillDisplayDlg::~CKillDisplayDlg()
{
}

void CKillDisplayDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CKillDisplayDlg, CDialogEx)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_WM_LBUTTONDOWN()
    ON_WM_ERASEBKGND()
    ON_WM_GETMINMAXINFO()
    ON_WM_EXITSIZEMOVE()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CKillDisplayDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetWindowText(kKillDisplayWindowTitle);

    const DWORD removeStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
    const DWORD addStyle = WS_POPUP;
    const DWORD removeExStyle = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE;
    const DWORD addExStyle = WS_EX_APPWINDOW | WS_EX_LAYERED;
    ModifyStyle(removeStyle, addStyle);
    ModifyStyleEx(removeExStyle, addExStyle);
    SetLayeredWindowAttributes(kKillDisplayTransparentKey, kKillDisplayLayeredAlpha, LWA_COLORKEY | LWA_ALPHA);
    RestoreWindowRect();

    InitWebView2();

    return TRUE;
}

void CKillDisplayDlg::OnClose()
{
    SetEditModeWindowExpanded(false);
    SaveWindowRect();
    ShowWindow(SW_HIDE);
    NotifyKillDisplayVisibilityChanged(this);
}

void CKillDisplayDlg::OnCancel()
{
    OnClose();
}

void CKillDisplayDlg::OnOK()
{
}

void CKillDisplayDlg::InitWebView2()
{
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) {
                    MessageBox(L"WebView2 runtime failed to load. Please install Microsoft Edge WebView2 Runtime.", L"WebView2 Missing", MB_ICONERROR);
                    return S_OK;
                }

                env->CreateCoreWebView2Controller(m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || controller == nullptr) return S_OK;

                            m_webviewController = controller;
                            m_webviewController->get_CoreWebView2(&m_webview);

                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_webviewController.As(&controller2)) && controller2) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            CRect rect;
                            GetClientRect(&rect);
                            m_webviewController->put_Bounds(rect);

                            if (m_webview) {
                                EventRegistrationToken token = {};
                                m_webview->add_WebMessageReceived(
                                    Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR message = nullptr;
                                            args->get_WebMessageAsJson(&message);
                                            HandleWebMessage(CString(message ? message : L""));
                                            if (message) CoTaskMemFree(message);
                                            return S_OK;
                                        }).Get(), &token);
                                EventRegistrationToken navigationToken = {};
                                m_webview->add_NavigationCompleted(
                                    Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                            BOOL success = FALSE;
                                            if (args && SUCCEEDED(args->get_IsSuccess(&success)) && success) {
                                                CWnd* parent = GetParent();
                                                if (!parent) parent = AfxGetMainWnd();
                                                if (parent && ::IsWindow(parent->GetSafeHwnd())) {
                                                    parent->PostMessage(WM_KILL_DISPLAY_READY, 0, 0);
                                                }
                                            }
                                            return S_OK;
                                        }).Get(), &navigationToken);
                                m_webview->Navigate(kKillDisplayUrl);
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void CKillDisplayDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    if (m_webviewController != nullptr) {
        CRect bounds(0, 0, cx, cy);
        m_webviewController->put_Bounds(bounds);
    }
}

void CKillDisplayDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    CDialogEx::OnLButtonDown(nFlags, point);
    BeginWindowDrag();
}

BOOL CKillDisplayDlg::OnEraseBkgnd(CDC* pDC)
{
    if (!pDC) return TRUE;
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, kKillDisplayTransparentKey);
    return TRUE;
}

void CKillDisplayDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CDialogEx::OnGetMinMaxInfo(lpMMI);
    if (!lpMMI) return;
    const UINT dpi = GetWindowDpi(m_hWnd);
    lpMMI->ptMinTrackSize.x = ScaleForDpi(kKillDisplayMinWidth, dpi);
    lpMMI->ptMinTrackSize.y = ScaleForDpi(kKillDisplayMinHeight, dpi);
}

void CKillDisplayDlg::OnExitSizeMove()
{
    SaveWindowRect();
    CDialogEx::OnExitSizeMove();
}

void CKillDisplayDlg::OnDestroy()
{
    SaveWindowRect();
    CDialogEx::OnDestroy();
}

void CKillDisplayDlg::ResizeWindowForClientSize(int targetClientW, int targetClientH)
{
    if (!m_hWnd || targetClientW <= 0 || targetClientH <= 0) return;

    CRect windowRect;
    CRect clientRect;
    GetWindowRect(&windowRect);
    GetClientRect(&clientRect);

    const int frameW = max(0, windowRect.Width() - clientRect.Width());
    const int frameH = max(0, windowRect.Height() - clientRect.Height());
    SetWindowPos(nullptr, windowRect.left, windowRect.top,
        targetClientW + frameW, targetClientH + frameH,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void CKillDisplayDlg::RestoreWindowRect()
{
    if (!m_hWnd) return;

    const UINT dpi = GetWindowDpi(m_hWnd);
    const int logicalWidth = (std::max)(kKillDisplayMinWidth,
        static_cast<int>(GetPrivateProfileInt(kKillDisplayWindowSection, L"Width",
            kKillDisplayClientWidth, m_iniPath)));
    const int logicalHeight = (std::max)(kKillDisplayMinHeight,
        static_cast<int>(GetPrivateProfileInt(kKillDisplayWindowSection, L"Height",
            kKillDisplayClientHeight, m_iniPath)));
    const int defaultPosition = ScaleForDpi(120, dpi);
    const int x = GetPrivateProfileInt(kKillDisplayWindowSection, L"X",
        defaultPosition, m_iniPath);
    const int y = GetPrivateProfileInt(kKillDisplayWindowSection, L"Y",
        defaultPosition, m_iniPath);

    SetWindowPos(nullptr, x, y,
        ScaleForDpi(logicalWidth, dpi), ScaleForDpi(logicalHeight, dpi),
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ResizeWindowForClientSize(ScaleForDpi(logicalWidth, dpi),
        ScaleForDpi(logicalHeight, dpi));

    CRect rect;
    GetWindowRect(&rect);
    ClampRectToMonitorWorkArea(rect);
    SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void CKillDisplayDlg::SaveWindowRect()
{
    if (!m_hWnd || m_iniPath.IsEmpty() || IsIconic()) return;

    CRect rect;
    if (m_editModeExpanded && !m_windowRectBeforeEdit.IsRectEmpty()) {
        rect = m_windowRectBeforeEdit;
    }
    else {
        GetWindowRect(&rect);
    }
    if (rect.IsRectEmpty()) return;

    const UINT dpi = GetWindowDpi(m_hWnd);
    CString value;
    value.Format(L"%d", rect.left);
    WritePrivateProfileString(kKillDisplayWindowSection, L"X", value, m_iniPath);
    value.Format(L"%d", rect.top);
    WritePrivateProfileString(kKillDisplayWindowSection, L"Y", value, m_iniPath);
    value.Format(L"%d", (std::max)(kKillDisplayMinWidth,
        UnscaleForDpi(rect.Width(), dpi)));
    WritePrivateProfileString(kKillDisplayWindowSection, L"Width", value, m_iniPath);
    value.Format(L"%d", (std::max)(kKillDisplayMinHeight,
        UnscaleForDpi(rect.Height(), dpi)));
    WritePrivateProfileString(kKillDisplayWindowSection, L"Height", value, m_iniPath);
}

void CKillDisplayDlg::HandleWebMessage(const CString& message)
{
    if (message.Find(L"cmd_kill_window_close") >= 0) {
        SetEditModeWindowExpanded(false);
        ShowWindow(SW_HIDE);
        NotifyKillDisplayVisibilityChanged(this);
        return;
    }
    if (message.Find(L"cmd_kill_window_edit_mode") >= 0) {
        SetEditModeWindowExpanded(message.Find(L"\"enabled\":true") >= 0);
        return;
    }
    if (message.Find(L"cmd_kill_window_resize") >= 0) {
        BeginWindowResize();
        return;
    }
    if (message.Find(L"cmd_kill_window_drag") >= 0) {
        BeginWindowDrag();
        return;
    }
}

void CKillDisplayDlg::SetEditModeWindowExpanded(bool expanded)
{
    if (!m_hWnd || expanded == m_editModeExpanded) return;

    if (expanded) {
        GetWindowRect(&m_windowRectBeforeEdit);
        const UINT dpi = GetWindowDpi(m_hWnd);
        const int targetHeight = max(m_windowRectBeforeEdit.Height() +
            ScaleForDpi(kKillDisplayEditExtraHeight, dpi),
            ScaleForDpi(kKillDisplayEditMinHeight, dpi));
        SetWindowPos(nullptr, m_windowRectBeforeEdit.left, m_windowRectBeforeEdit.top,
            m_windowRectBeforeEdit.Width(), targetHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!m_windowRectBeforeEdit.IsRectEmpty()) {
        SetWindowPos(nullptr, m_windowRectBeforeEdit.left, m_windowRectBeforeEdit.top,
            m_windowRectBeforeEdit.Width(), m_windowRectBeforeEdit.Height(),
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    m_editModeExpanded = expanded;
}

void CKillDisplayDlg::BeginWindowDrag()
{
    if (!m_hWnd) return;
    ReleaseCapture();
    SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void CKillDisplayDlg::BeginWindowResize()
{
    if (!m_hWnd) return;
    ReleaseCapture();
    SendMessage(WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, 0);
}
