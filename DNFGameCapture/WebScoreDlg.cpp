#include "pch.h"
#include "resource.h"  // 👈 【关键修复】：告诉代码去这里找对话框的 ID
#include "DNFGameCaptureDlg.h" // 替换为你实际的工程头文件
#include "WebScoreDlg.h"
#include <WebView2EnvironmentOptions.h>

namespace {
    // 参考图1的紧凑 CSS 视口尺寸。窗口外框会按当前系统边框自动反推。
    constexpr int kReferenceClientWidth = 720;
    constexpr int kReferenceClientHeight = 560;
    constexpr double kTargetVisualScale = 1.25;

    double GetDpiScaleForWindow(HWND hwnd)
    {
        UINT dpi = 96;
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32 && hwnd) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
                ::GetProcAddress(user32, "GetDpiForWindow"));
            if (getDpiForWindow) dpi = getDpiForWindow(hwnd);
        }
        if (dpi == 0) dpi = 96;
        return static_cast<double>(dpi) / 96.0;
    }

    double GetDpiNormalizedWebZoom(HWND hwnd)
    {
        double zoom = kTargetVisualScale / GetDpiScaleForWindow(hwnd);
        if (zoom < 0.50) zoom = 0.50;
        if (zoom > 2.00) zoom = 2.00;
        return zoom;
    }

    int ScaleCssSizeToNativePixels(int cssSize, double visualScale)
    {
        return static_cast<int>(cssSize * visualScale + 0.5);
    }
}

IMPLEMENT_DYNAMIC(CWebScoreDlg, CDialogEx)

CWebScoreDlg::CWebScoreDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WEB_SCORE_DIALOG, pParent)
{
}

CWebScoreDlg::~CWebScoreDlg()
{
}

void CWebScoreDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CWebScoreDlg, CDialogEx)
	ON_WM_SIZE()
    ON_WM_CLOSE() // 🚨 【新增】：绑定点 X 的消息
END_MESSAGE_MAP()


BOOL CWebScoreDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 🚨 动态读取版本号并设置窗口标题
    CString title;
    title.Format(L"DNF点将计分器 - v%s", CURRENT_VERSION);
    SetWindowText(title);

    InitWebView2();
    ApplyFixedWindowHeight();

    return TRUE;
}

// ==========================================
// 🚨 窗口生命周期控制：彻底禁止 Web 窗口销毁，改为隐藏
// ==========================================
void CWebScoreDlg::OnClose() {
    ShowWindow(SW_HIDE); // 点 X 只是隐藏
}

void CWebScoreDlg::OnCancel() {
    ShowWindow(SW_HIDE); // 按 Esc 只是隐藏
}

void CWebScoreDlg::OnOK() {
    // 拦截回车键，什么都不做，防止意外关闭
}

void CWebScoreDlg::InitWebView2()
{
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

                // 🚨【关键修复 1】：拦截空指针和失败状态
                if (FAILED(result) || env == nullptr) {
                    MessageBox(L"WebView2 运行环境加载失败！\r\n请检查您的电脑是否已安装 Microsoft Edge WebView2 运行时。", L"组件缺失", MB_ICONERROR);
                    return S_OK;
                }

                env->CreateCoreWebView2Controller(m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {

                            // 🚨【关键修复 2】：控制器拦截
                            if (FAILED(result) || controller == nullptr) return S_OK;

                            m_webviewController = controller;
                            m_webviewController->get_CoreWebView2(&m_webview);

                            // 调整网页大小铺满窗口
                            CRect rect;
                            GetClientRect(&rect);
                            m_webviewController->put_Bounds(rect);
                            ApplyDpiNormalizedZoom();

                            // 接收来自 JS 的 JSON 数据
                            EventRegistrationToken token;
                            m_webview->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [this](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {

                                    LPWSTR message;
                                    args->get_WebMessageAsJson(&message);

                                    CString* pJsonStr = new CString(message);

                                    // 🚨 【核心修复 2】：使用全局绝对定位获取主窗口，绝不迷路！
                                    CWnd* pMainWnd = AfxGetMainWnd();
                                    if (pMainWnd != nullptr) {
                                        pMainWnd->PostMessage(WM_WEB_CMD_RECEIVED, 0, (LPARAM)pJsonStr);
                                    }
                                    else {
                                        // 如果连主窗口都找不到，弹窗报警，防止内存泄漏
                                        MessageBox(L"找不到主程序窗口，消息发送失败！", L"致命错误", MB_ICONERROR);
                                        delete pJsonStr;
                                    }

                                    CoTaskMemFree(message);
                                    return S_OK;
                                }).Get(), &token);

                            // 加载本地网页
                            wchar_t exePath[MAX_PATH];
                            GetModuleFileName(NULL, exePath, MAX_PATH);
                            CString path = exePath;
                            path = path.Left(path.ReverseFind(L'\\') + 1) + L"web前端\\index.html";
                            m_webview->Navigate(path);

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

// 窗口拉伸时，网页跟着拉伸
void CWebScoreDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_webviewController != nullptr) {
		CRect bounds(0, 0, cx, cy);
		m_webviewController->put_Bounds(bounds);
        ApplyDpiNormalizedZoom();
	}
}

// 暴露给主窗口的方法：向网页发数据
void CWebScoreDlg::SendStateToWeb(const CString& jsonStr)
{
	if (m_webview != nullptr) {
		m_webview->PostWebMessageAsJson(jsonStr.GetString());
	}
}

void CWebScoreDlg::ResizeWindowToSize(int targetWindowW, int targetWindowH)
{
    if (!m_hWnd || targetWindowW <= 0 || targetWindowH <= 0) return;

    CRect windowRect;
    GetWindowRect(&windowRect);

    MONITORINFO mi = { sizeof(mi) };
    HMONITOR monitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
    if (!::GetMonitorInfo(monitor, &mi)) return;

    CRect work(mi.rcWork);
    int maxWindowW = work.Width() - 24;
    int maxWindowH = work.Height() - 24;
    if (targetWindowW > maxWindowW) targetWindowW = maxWindowW;
    if (targetWindowH > maxWindowH) targetWindowH = maxWindowH;
    if (targetWindowW < 640) targetWindowW = 640;
    if (targetWindowH < 420) targetWindowH = 420;

    int newX = windowRect.left;
    if (newX + targetWindowW > work.right) newX = work.right - targetWindowW;
    if (newX < work.left) newX = work.left;

    int newY = windowRect.top;
    if (newY + targetWindowH > work.bottom) newY = work.bottom - targetWindowH;
    if (newY < work.top) newY = work.top;

    if (abs(windowRect.Width() - targetWindowW) < 2 && abs(windowRect.Height() - targetWindowH) < 2) {
        ApplyDpiNormalizedZoom();
        return;
    }
    SetWindowPos(nullptr, newX, newY, targetWindowW, targetWindowH, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyDpiNormalizedZoom();
}

void CWebScoreDlg::ResizeWindowForClientSize(int targetClientW, int targetClientH)
{
    if (!m_hWnd || targetClientW <= 0 || targetClientH <= 0) return;

    CRect windowRect;
    CRect clientRect;
    GetWindowRect(&windowRect);
    GetClientRect(&clientRect);

    const int frameW = max(0, windowRect.Width() - clientRect.Width());
    const int frameH = max(0, windowRect.Height() - clientRect.Height());
    ResizeWindowToSize(targetClientW + frameW, targetClientH + frameH);
}

void CWebScoreDlg::ApplyDpiNormalizedZoom()
{
    if (m_webviewController == nullptr) return;
    // 以 Windows 125% 缩放下的旧版图1为视觉基准。
    m_webviewController->put_ZoomFactor(GetDpiNormalizedWebZoom(m_hWnd));
}

void CWebScoreDlg::ApplyFixedWindowHeight()
{
    CRect windowRect;
    GetWindowRect(&windowRect);
    if (m_baseWindowWidth <= 0 || m_baseWindowHeight <= 0) {
        m_baseWindowWidth = windowRect.Width();
        m_baseWindowHeight = windowRect.Height();
    }

    if (m_initialWindowSizeApplied) return;
    m_initialWindowSizeApplied = true;

    // 窗口按目标视觉基准缩放一次即可；WebView2 Zoom 已经负责抵消系统 DPI。
    ResizeWindowForClientSize(
        ScaleCssSizeToNativePixels(kReferenceClientWidth, kTargetVisualScale),
        ScaleCssSizeToNativePixels(kReferenceClientHeight, kTargetVisualScale));
}
