#include "pch.h"
#include "KillDisplayDlg.h"
#include "DNFGameCaptureDlg.h"
#include <WebView2EnvironmentOptions.h>

namespace {
    constexpr int kKillDisplayClientWidth = 900;
    constexpr int kKillDisplayClientHeight = 360;
    constexpr int kKillDisplayMinWidth = 460;
    constexpr int kKillDisplayMinHeight = 180;
    constexpr COLORREF kKillDisplayTransparentKey = RGB(1, 2, 3);
    constexpr BYTE kKillDisplayLayeredAlpha = 245;
    constexpr wchar_t kKillDisplayWindowTitle[] = L"DNF Kill Display - DNF\u51FB\u6740\u5C55\u793A\u7A97\u53E3";
    constexpr wchar_t kKillDisplayUrl[] = L"http://127.0.0.1:18777/kill.html";
}

IMPLEMENT_DYNAMIC(CKillDisplayDlg, CDialogEx)

CKillDisplayDlg::CKillDisplayDlg(CWnd* pParent)
    : CDialogEx(IDD_WEB_SCORE_DIALOG, pParent)
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
    SetWindowPos(nullptr, 120, 120, kKillDisplayClientWidth, kKillDisplayClientHeight,
        SWP_NOACTIVATE | SWP_FRAMECHANGED);

    InitWebView2();
    ResizeWindowForClientSize(kKillDisplayClientWidth, kKillDisplayClientHeight);

    return TRUE;
}

void CKillDisplayDlg::OnClose()
{
    ShowWindow(SW_HIDE);
}

void CKillDisplayDlg::OnCancel()
{
    ShowWindow(SW_HIDE);
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
    lpMMI->ptMinTrackSize.x = kKillDisplayMinWidth;
    lpMMI->ptMinTrackSize.y = kKillDisplayMinHeight;
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

void CKillDisplayDlg::HandleWebMessage(const CString& message)
{
    if (message.Find(L"cmd_kill_window_resize") >= 0) {
        BeginWindowResize();
        return;
    }
    if (message.Find(L"cmd_kill_window_drag") >= 0) {
        BeginWindowDrag();
        return;
    }
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
