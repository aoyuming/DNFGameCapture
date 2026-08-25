#include "pch.h"
#include "KeyDisplayDlg.h"
#include "DNFGameCaptureDlg.h"

namespace {
    constexpr int kDefaultWidth = 800;
    constexpr int kDefaultHeight = 240;
    constexpr int kMinWidth = 320;
    constexpr int kMinHeight = 96;
    constexpr COLORREF kTransparentKey = RGB(1, 2, 3);
    constexpr wchar_t kWindowTitle[] = L"DNF Key Display - DNF\u6309\u952e\u6620\u5c04\u7a97\u53e3";
    constexpr wchar_t kWindowUrl[] = L"http://127.0.0.1:18777/keys.html?shell=1";

    void NotifyVisibilityChanged(CWnd* wnd)
    {
        CWnd* parent = wnd ? wnd->GetParent() : nullptr;
        if (!parent) parent = AfxGetMainWnd();
        if (parent && ::IsWindow(parent->GetSafeHwnd())) {
            parent->PostMessage(WM_KEY_DISPLAY_VISIBILITY_CHANGED, 0, 0);
        }
    }
}

IMPLEMENT_DYNAMIC(CKeyDisplayDlg, CDialogEx)

CKeyDisplayDlg::CKeyDisplayDlg(const CString& iniPath, CWnd* pParent)
    : CDialogEx(IDD_WEB_SCORE_DIALOG, pParent), m_iniPath(iniPath)
{
}

CKeyDisplayDlg::~CKeyDisplayDlg()
{
}

void CKeyDisplayDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CKeyDisplayDlg, CDialogEx)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_GETMINMAXINFO()
    ON_WM_EXITSIZEMOVE()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CKeyDisplayDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(kWindowTitle);

    const DWORD removeStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
        WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
    const DWORD removeExStyle = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE;
    ModifyStyle(removeStyle, WS_POPUP);
    ModifyStyleEx(removeExStyle, WS_EX_APPWINDOW | WS_EX_LAYERED);
    SetLayeredWindowAttributes(kTransparentKey, 255, LWA_COLORKEY | LWA_ALPHA);

    const int x = GetPrivateProfileInt(L"KeyDisplayWindow", L"X", 160, m_iniPath);
    const int y = GetPrivateProfileInt(L"KeyDisplayWindow", L"Y", 160, m_iniPath);
    const int width = max(kMinWidth, GetPrivateProfileInt(L"KeyDisplayWindow", L"Width", kDefaultWidth, m_iniPath));
    const int height = max(kMinHeight, GetPrivateProfileInt(L"KeyDisplayWindow", L"Height", kDefaultHeight, m_iniPath));
    SetWindowPos(nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED);

    InitWebView2();
    return TRUE;
}

void CKeyDisplayDlg::OnClose()
{
    SaveWindowRect();
    ShowWindow(SW_HIDE);
    NotifyVisibilityChanged(this);
}

void CKeyDisplayDlg::OnCancel()
{
    OnClose();
}

void CKeyDisplayDlg::OnOK()
{
}

void CKeyDisplayDlg::InitWebView2()
{
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) return S_OK;
                env->CreateCoreWebView2Controller(m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT createResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(createResult) || controller == nullptr) return S_OK;
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
                                m_webview->Navigate(kWindowUrl);
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void CKeyDisplayDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    if (m_webviewController) {
        m_webviewController->put_Bounds(CRect(0, 0, cx, cy));
    }
}

BOOL CKeyDisplayDlg::OnEraseBkgnd(CDC* pDC)
{
    if (!pDC) return TRUE;
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, kTransparentKey);
    return TRUE;
}

void CKeyDisplayDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CDialogEx::OnGetMinMaxInfo(lpMMI);
    if (!lpMMI) return;
    lpMMI->ptMinTrackSize.x = kMinWidth;
    lpMMI->ptMinTrackSize.y = kMinHeight;
}

void CKeyDisplayDlg::OnExitSizeMove()
{
    SaveWindowRect();
    CDialogEx::OnExitSizeMove();
}

void CKeyDisplayDlg::OnDestroy()
{
    SaveWindowRect();
    CDialogEx::OnDestroy();
}

void CKeyDisplayDlg::HandleWebMessage(const CString& message)
{
    if (message.Find(L"cmd_key_window_close") >= 0) {
        OnClose();
    }
    else if (message.Find(L"cmd_key_window_resize") >= 0) {
        BeginWindowResize();
    }
    else if (message.Find(L"cmd_key_window_drag") >= 0) {
        BeginWindowDrag();
    }
}

void CKeyDisplayDlg::BeginWindowDrag()
{
    ReleaseCapture();
    SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void CKeyDisplayDlg::BeginWindowResize()
{
    ReleaseCapture();
    SendMessage(WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, 0);
}

void CKeyDisplayDlg::SaveWindowRect()
{
    if (!m_hWnd || IsIconic()) return;
    CRect rect;
    GetWindowRect(&rect);
    CString value;
    value.Format(L"%d", rect.left);
    WritePrivateProfileString(L"KeyDisplayWindow", L"X", value, m_iniPath);
    value.Format(L"%d", rect.top);
    WritePrivateProfileString(L"KeyDisplayWindow", L"Y", value, m_iniPath);
    value.Format(L"%d", rect.Width());
    WritePrivateProfileString(L"KeyDisplayWindow", L"Width", value, m_iniPath);
    value.Format(L"%d", rect.Height());
    WritePrivateProfileString(L"KeyDisplayWindow", L"Height", value, m_iniPath);
}
