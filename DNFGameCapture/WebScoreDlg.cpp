#include "pch.h"
#include "resource.h"  // 👈 【关键修复】：告诉代码去这里找对话框的 ID
#include "DNFGameCaptureDlg.h" // 替换为你实际的工程头文件
#include "WebScoreDlg.h"
#include <WebView2EnvironmentOptions.h>

void WriteMatchLog(const CString& logLine);

namespace {
    // 参考图1的紧凑 CSS 视口尺寸。窗口外框会按当前系统边框自动反推。
    constexpr int kCompactClientWidth = 1140;
    constexpr int kExpandedClientWidth = 1400;
    constexpr int kReferenceClientHeight = 480;
    constexpr int kAppearanceExtraClientHeight = 300;
    constexpr int kBroadcasterPreviewClientHeight = 760;
    constexpr int kPlayerIdentityClientHeight = 720;
    constexpr double kTargetVisualScale = 1.25;
    constexpr COLORREF kWebViewBackgroundColor = RGB(20, 24, 30);
    constexpr UINT kWebViewLoadingControlId = 0x7F01;
    constexpr UINT_PTR kWebViewRetryTimerId = 0x7F12;

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
    ON_WM_TIMER()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_WM_CLOSE() // 🚨 【新增】：绑定点 X 的消息
END_MESSAGE_MAP()


BOOL CWebScoreDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    m_webViewBackgroundBrush.CreateSolidBrush(kWebViewBackgroundColor);
    m_webViewLoadingFont.CreatePointFont(110, L"Microsoft YaHei");
    CRect loadingRect;
    GetClientRect(&loadingRect);
    m_webViewLoadingLabel.Create(
        L"正在加载计分界面...",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        loadingRect,
        this,
        kWebViewLoadingControlId);
    m_webViewLoadingLabel.SetFont(&m_webViewLoadingFont);
    SetWebViewLoadingState(true, L"正在加载计分界面...");

    // 🚨 动态读取版本号并设置窗口标题
    CString title;
    title.Format(L"DNF点将计分器 - v%s", CURRENT_VERSION);
    SetWindowText(title);

    InitWebView2();
    ApplyFixedWindowHeight();

    return TRUE;
}

BOOL CWebScoreDlg::OnEraseBkgnd(CDC* pDC)
{
    if (!pDC) return TRUE;
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(&rect, kWebViewBackgroundColor);
    return TRUE;
}

HBRUSH CWebScoreDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH brush = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
    if (pWnd && pWnd->GetSafeHwnd() == m_webViewLoadingLabel.GetSafeHwnd()) {
        pDC->SetTextColor(RGB(225, 230, 238));
        pDC->SetBkColor(kWebViewBackgroundColor);
        pDC->SetBkMode(OPAQUE);
        return static_cast<HBRUSH>(m_webViewBackgroundBrush.GetSafeHandle());
    }
    return brush;
}

void CWebScoreDlg::LayoutWebViewLoadingLabel(int cx, int cy)
{
    if (!m_webViewLoadingLabel.GetSafeHwnd()) return;
    // 失败/重试提示只占底部一小块，不能盖住已经绘制完成的计分板。
    const int labelWidth = (std::min)(520, (std::max)(240, cx - 24));
    const int labelHeight = (std::min)(32, (std::max)(24, cy - 12));
    const int left = (std::max)(12, (cx - labelWidth) / 2);
    const int top = (std::max)(8, cy - labelHeight - 10);
    m_webViewLoadingLabel.MoveWindow(left, top, labelWidth, labelHeight, FALSE);
    if (m_webViewLoadingLabel.IsWindowVisible()) {
        m_webViewLoadingLabel.SetWindowPos(
            &wndTop, left, top, labelWidth, labelHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void CWebScoreDlg::SetWebViewLoadingState(bool loading, const wchar_t* message)
{
    if (!m_webViewLoadingLabel.GetSafeHwnd()) return;
    if (message && *message) m_webViewLoadingLabel.SetWindowText(message);
    m_webViewPageReady = !loading;
    if (loading) {
        const CString loadingText = message ? message : L"";
        const bool showLoadingMessage =
            loadingText.Find(L"失败") >= 0 ||
            loadingText.Find(L"重试") >= 0 ||
            loadingText.Find(L"错误") >= 0 ||
            loadingText.Find(L"请检查") >= 0;
        CRect rect;
        GetClientRect(&rect);
        LayoutWebViewLoadingLabel(rect.Width(), rect.Height());
        m_webViewLoadingLabel.ShowWindow(showLoadingMessage ? SW_SHOW : SW_HIDE);
        if (showLoadingMessage) m_webViewLoadingLabel.BringWindowToTop();
    }
    else {
        m_webViewLoadingLabel.ShowWindow(SW_HIDE);
    }
}

bool CWebScoreDlg::SwitchToWebViewRecoveryDataFolder(HRESULT result)
{
    if (result != HRESULT_FROM_WIN32(ERROR_BUSY) ||
        m_webViewUsingFallbackDataFolder) {
        return false;
    }

    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = ::GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
    if (length == 0 || length >= std::size(tempPath)) return false;

    CString folder(tempPath, static_cast<int>(length));
    folder.TrimRight(L"\\/");
    CString suffix;
    suffix.Format(L"\\DNFGameCapture-WebView2-%lu-%llu",
        static_cast<unsigned long>(::GetCurrentProcessId()),
        static_cast<unsigned long long>(::GetTickCount64()));
    folder += suffix;

    if (!::CreateDirectoryW(folder, nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    m_webViewUserDataFolder = folder;
    m_webViewUsingFallbackDataFolder = true;
    WriteMatchLog(L"[WebView启动] 检测到用户数据目录忙（0x800700AA），切换备用目录：" + folder);
    return true;
}

void CWebScoreDlg::ScheduleWebViewRetry(const CString& reason)
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;

    const UINT delayMs = (std::min)(5000u,
        500u + m_webViewInitAttempt * 500u);
    KillTimer(kWebViewRetryTimerId);
    m_webViewRetryScheduled = true;
    SetTimer(kWebViewRetryTimerId, delayMs, nullptr);

    CString line;
    line.Format(L"[WebView启动] %s；将在 %u ms 后重试，第 %u 次。",
        reason.GetString(), delayMs, m_webViewInitAttempt + 1);
    WriteMatchLog(line);
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
    if (!m_hWnd || !::IsWindow(m_hWnd) || m_webviewController ||
        m_webViewInitInFlight) {
        return;
    }

    m_webViewInitInFlight = true;
    ++m_webViewInitAttempt;
    m_webViewInitStartedAt = ::GetTickCount64();
    m_webViewControllerStartedAt = 0;

    // 不使用 WebView2 的系统默认用户数据目录，避免和 Edge 或其他
    // WebView2 进程争用同一个 profile，导致首次环境创建长时间等待。
    if (m_webViewUserDataFolder.IsEmpty()) {
        wchar_t tempPath[MAX_PATH] = {};
        const DWORD length = ::GetTempPathW(
            static_cast<DWORD>(std::size(tempPath)), tempPath);
        if (length > 0 && length < std::size(tempPath)) {
            CString folder(tempPath, static_cast<int>(length));
            folder.TrimRight(L"\\/");
            folder += L"\\DNFGameCapture-WebView2-Profile";
            if (::CreateDirectoryW(folder, nullptr) ||
                ::GetLastError() == ERROR_ALREADY_EXISTS) {
                m_webViewUserDataFolder = folder;
                WriteMatchLog(L"[WebView启动] 使用应用专用用户数据目录：" + folder);
            }
        }
    }

    LPCWSTR userDataFolder = m_webViewUserDataFolder.IsEmpty()
        ? nullptr : m_webViewUserDataFolder.GetString();
    const HRESULT environmentRequestResult =
        CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

                // 🚨【关键修复 1】：拦截空指针和失败状态
                if (FAILED(result) || env == nullptr) {
                    m_webViewInitInFlight = false;
                    CString timing;
                    timing.Format(L"[WebView启动] 环境创建失败耗时=%llu ms。",
                        static_cast<unsigned long long>(::GetTickCount64() - m_webViewInitStartedAt));
                    WriteMatchLog(timing);
                    CString reason;
                    reason.Format(L"WebView2环境创建失败；hr=0x%08X。",
                        static_cast<unsigned int>(result));
                    const bool switchedToRecovery =
                        SwitchToWebViewRecoveryDataFolder(result);
                    WriteMatchLog(L"[WebView启动] " + reason);
                    CString failureMessage;
                    failureMessage.Format(L"计分界面正在重试（环境 0x%08X，备用目录%s）...",
                        static_cast<unsigned int>(result),
                        switchedToRecovery ? L"已切换" : L"未切换");
                    SetWebViewLoadingState(true, failureMessage.GetString());
                    ScheduleWebViewRetry(reason);
                    return S_OK;
                }

                m_webViewControllerStartedAt = ::GetTickCount64();
                CString environmentTiming;
                environmentTiming.Format(L"[WebView启动] 环境创建回调耗时=%llu ms。",
                    static_cast<unsigned long long>(m_webViewControllerStartedAt - m_webViewInitStartedAt));
                WriteMatchLog(environmentTiming);

                const HRESULT controllerRequestResult = env->CreateCoreWebView2Controller(m_hWnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {

                            // 🚨【关键修复 2】：控制器拦截
                            if (FAILED(result) || controller == nullptr) {
                                m_webViewInitInFlight = false;
                                CString reason;
                                reason.Format(L"WebView2控制器创建失败；hr=0x%08X。",
                                    static_cast<unsigned int>(result));
                                const bool switchedToRecovery =
                                    SwitchToWebViewRecoveryDataFolder(result);
                                WriteMatchLog(L"[WebView启动] " + reason);
                                CString failureMessage;
                                failureMessage.Format(L"计分界面正在重试（控制器 0x%08X，备用目录%s）...",
                                    static_cast<unsigned int>(result),
                                    switchedToRecovery ? L"已切换" : L"未切换");
                                SetWebViewLoadingState(true, failureMessage.GetString());
                                ScheduleWebViewRetry(reason);
                                return S_OK;
                            }

                            m_webviewController = controller;
                            m_webview.Reset();
                            const HRESULT coreWebViewResult =
                                m_webviewController->get_CoreWebView2(&m_webview);
                            if (FAILED(coreWebViewResult) || !m_webview) {
                                m_webviewController.Reset();
                                m_webViewInitInFlight = false;
                                CString reason;
                                reason.Format(L"获取CoreWebView2接口失败；hr=0x%08X。",
                                    static_cast<unsigned int>(coreWebViewResult));
                                WriteMatchLog(L"[WebView启动] " + reason);
                                CString failureMessage;
                                failureMessage.Format(L"计分界面正在重试（接口 0x%08X）...",
                                    static_cast<unsigned int>(coreWebViewResult));
                                SetWebViewLoadingState(true, failureMessage.GetString());
                                ScheduleWebViewRetry(reason);
                                return S_OK;
                            }

                            m_webViewInitInFlight = false;
                            m_webViewRetryScheduled = false;
                            KillTimer(kWebViewRetryTimerId);
                            CString timing;
                            timing.Format(L"[WebView启动] 环境和控制器创建耗时=%llu ms。",
                                static_cast<unsigned long long>(::GetTickCount64() - m_webViewInitStartedAt));
                            WriteMatchLog(timing);
                            CString controllerTiming;
                            controllerTiming.Format(L"[WebView启动] 控制器创建回调耗时=%llu ms。",
                                static_cast<unsigned long long>(::GetTickCount64() - m_webViewControllerStartedAt));
                            WriteMatchLog(controllerTiming);

                            // 调整网页大小铺满窗口
                            CRect rect;
                            GetClientRect(&rect);
                            m_webviewController->put_Bounds(rect);
                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_webviewController.As(&controller2)) && controller2) {
                                const COREWEBVIEW2_COLOR backgroundColor = { 0xFF, 0x14, 0x18, 0x1E };
                                controller2->put_DefaultBackgroundColor(backgroundColor);
                            }
                            // WebView2 控制器是后创建的子窗口，再次置顶保证加载提示不会被覆盖。
                            if (!m_webViewPageReady) m_webViewLoadingLabel.BringWindowToTop();
                            ApplyDpiNormalizedZoom();
                            WriteWebHostDiagnostics(L"WebView2控制器已创建");

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

                            EventRegistrationToken navigationToken;
                            m_webview->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success = FALSE;
                                        COREWEBVIEW2_WEB_ERROR_STATUS webErrorStatus =
                                            COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        if (args) {
                                            args->get_IsSuccess(&success);
                                            args->get_WebErrorStatus(&webErrorStatus);
                                        }
                                        if (!success) {
                                            CString line;
                                            line.Format(L"[WebView启动] 本地页面导航失败，WebErrorStatus=%d，保留加载提示。",
                                                static_cast<int>(webErrorStatus));
                                            WriteMatchLog(line);
                                            CString failureMessage;
                                            failureMessage.Format(L"计分界面加载失败（WebView错误 %d），请检查 web前端 文件后重试。",
                                                static_cast<int>(webErrorStatus));
                                            SetWebViewLoadingState(true, failureMessage.GetString());
                                        }
                                        else {
                                            // 页面导航成功后即使前端握手稍晚或丢失，也不能让原生
                                            // 加载层永久盖住已经绘制出的计分界面。page_ready 仍会
                                            // 继续负责桥接确认和首帧状态同步。
                                            WriteMatchLog(L"[WebView启动] 本地页面导航成功，解除原生加载覆盖层，等待前端page_ready。");
                                            if (!m_webViewPageReady) {
                                                SetWebViewLoadingState(false);
                                            }
                                        }
                                        return S_OK;
                                    }).Get(), &navigationToken);

                            // 加载本地网页
                            wchar_t exePath[MAX_PATH];
                            GetModuleFileName(NULL, exePath, MAX_PATH);
                            CString pagePath = exePath;
                            pagePath = pagePath.Left(pagePath.ReverseFind(L'\\') + 1) + L"web前端\\index.html";
                            CString pageDir = pagePath.Left(pagePath.ReverseFind(L'\\'));
                            CString pageUri;
                            Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
                            const HRESULT mappingQueryResult = m_webview.As(&webView3);
                            HRESULT mappingResult = E_NOINTERFACE;
                            if (SUCCEEDED(mappingQueryResult) && webView3) {
                                mappingResult = webView3->SetVirtualHostNameToFolderMapping(
                                    L"appassets.example", pageDir.GetString(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }
                            if (SUCCEEDED(mappingResult)) {
                                pageUri = L"http://appassets.example/index.html";
                            }
                            else {
                                pageUri = L"file:///";
                                CString normalizedPagePath = pagePath;
                                normalizedPagePath.Replace(L'\\', L'/');
                                pageUri += normalizedPagePath;
                                CString mappingLog;
                                mappingLog.Format(L"[WebView启动] 虚拟主机映射失败；hr=0x%08X，回退file URI。",
                                    static_cast<unsigned int>(mappingResult));
                                WriteMatchLog(mappingLog);
                            }
                            WriteMatchLog(L"[WebView启动] 正在导航本地页面：" + pageUri);
                            const HRESULT navigateResult = m_webview->Navigate(pageUri.GetString());
                            if (FAILED(navigateResult)) {
                                CString line;
                                line.Format(L"[WebView启动] Navigate调用失败；hr=0x%08X；路径=%s。",
                                    static_cast<unsigned int>(navigateResult), pagePath.GetString());
                                WriteMatchLog(line);
                                SetWebViewLoadingState(true,
                                    L"计分界面启动失败，请关闭后重新打开。 ");
                            }

                            return S_OK;
                        }).Get());
                if (FAILED(controllerRequestResult)) {
                    m_webViewInitInFlight = false;
                    const bool switchedToRecovery =
                        SwitchToWebViewRecoveryDataFolder(controllerRequestResult);
                    CString reason;
                    reason.Format(L"请求创建WebView2控制器失败；hr=0x%08X。",
                        static_cast<unsigned int>(controllerRequestResult));
                    WriteMatchLog(L"[WebView启动] " + reason);
                    CString failureMessage;
                    failureMessage.Format(L"计分界面正在重试（控制器请求 0x%08X，备用目录%s）...",
                        static_cast<unsigned int>(controllerRequestResult),
                        switchedToRecovery ? L"已切换" : L"未切换");
                    SetWebViewLoadingState(true, failureMessage.GetString());
                    ScheduleWebViewRetry(reason);
                }
                return S_OK;
            }).Get());
    if (FAILED(environmentRequestResult)) {
        m_webViewInitInFlight = false;
        const bool switchedToRecovery =
            SwitchToWebViewRecoveryDataFolder(environmentRequestResult);
        CString reason;
        reason.Format(L"请求创建WebView2环境失败；hr=0x%08X。",
            static_cast<unsigned int>(environmentRequestResult));
        WriteMatchLog(L"[WebView启动] " + reason);
        CString failureMessage;
        failureMessage.Format(L"计分界面正在重试（环境请求 0x%08X，备用目录%s）...",
            static_cast<unsigned int>(environmentRequestResult),
            switchedToRecovery ? L"已切换" : L"未切换");
        SetWebViewLoadingState(true, failureMessage.GetString());
        ScheduleWebViewRetry(reason);
    }
}

// 窗口拉伸时，网页跟着拉伸
void CWebScoreDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (m_webviewController != nullptr) {
		CRect bounds(0, 0, cx, cy);
		m_webviewController->put_Bounds(bounds);
        if (!m_webZoomCalibrated) ApplyDpiNormalizedZoom();
	}
    LayoutWebViewLoadingLabel(cx, cy);
}

void CWebScoreDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kWebViewRetryTimerId) {
        KillTimer(kWebViewRetryTimerId);
        m_webViewRetryScheduled = false;
        if (!m_webviewController && !m_webViewInitInFlight) {
            SetWebViewLoadingState(true, L"正在重新连接计分界面...");
            InitWebView2();
        }
        return;
    }

    CDialogEx::OnTimer(nIDEvent);
}

// 暴露给主窗口的方法：向网页发数据
bool CWebScoreDlg::SendStateToWeb(const CString& jsonStr)
{
	if (m_webview == nullptr) {
		const ULONGLONG now = ::GetTickCount64();
		if (m_lastWebMessageFailure != E_POINTER ||
		    now - m_lastWebMessageFailureTick >= 2000) {
			WriteMatchLog(L"[WebView桥] 消息未发送：WebView2 页面对象尚未创建。");
			m_lastWebMessageFailure = E_POINTER;
			m_lastWebMessageFailureTick = now;
		}
		return false;
	}

	HRESULT hr = m_webview->PostWebMessageAsJson(jsonStr.GetString());
	if (FAILED(hr)) {
		const ULONGLONG now = ::GetTickCount64();
		if (hr != m_lastWebMessageFailure ||
		    now - m_lastWebMessageFailureTick >= 2000) {
			CString line;
			line.Format(L"[WebView桥] PostWebMessageAsJson失败；hr=0x%08X；UTF-16长度=%d；窗口句柄=%p。",
				static_cast<unsigned int>(hr), jsonStr.GetLength(), m_hWnd);
			WriteMatchLog(line);
			m_lastWebMessageFailure = hr;
			m_lastWebMessageFailureTick = now;
		}
		return false;
	}
	else if (!m_webMessageSuccessLogged) {
		WriteMatchLog(L"[WebView桥] PostWebMessageAsJson首次发送成功。");
		m_webMessageSuccessLogged = true;
	}
	return true;
}

bool CWebScoreDlg::CopyWindowImageToClipboard(CString& errorMsg)
{
    errorMsg.Empty();

    HWND hwnd = GetSafeHwnd();
    if (!hwnd || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd) || ::IsIconic(hwnd)) {
        errorMsg = L"截图复制失败，请确认 Web 计分窗口未最小化。";
        return false;
    }

    RECT rc = {};
    if (!::GetWindowRect(hwnd, &rc)) {
        errorMsg = L"截图复制失败：无法读取 Web 计分窗口位置。";
        return false;
    }

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
        errorMsg = L"截图复制失败：Web 计分窗口尺寸异常。";
        return false;
    }

    HDC hScreen = ::GetDC(NULL);
    if (!hScreen) {
        errorMsg = L"截图复制失败：无法获取屏幕画面。";
        return false;
    }

    HDC hMem = ::CreateCompatibleDC(hScreen);
    HBITMAP hBmp = hMem ? ::CreateCompatibleBitmap(hScreen, width, height) : NULL;
    if (!hMem || !hBmp) {
        if (hBmp) ::DeleteObject(hBmp);
        if (hMem) ::DeleteDC(hMem);
        ::ReleaseDC(NULL, hScreen);
        errorMsg = L"截图复制失败：无法创建截图缓冲区。";
        return false;
    }

    HGDIOBJ oldBmp = ::SelectObject(hMem, hBmp);
    BOOL copied = ::BitBlt(hMem, 0, 0, width, height, hScreen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    ::SelectObject(hMem, oldBmp);
    ::DeleteDC(hMem);
    ::ReleaseDC(NULL, hScreen);

    if (!copied) {
        ::DeleteObject(hBmp);
        errorMsg = L"截图复制失败：无法抓取 Web 计分窗口画面。";
        return false;
    }

    if (!::OpenClipboard(hwnd)) {
        ::DeleteObject(hBmp);
        errorMsg = L"截图复制失败：系统剪贴板正被其他程序占用。";
        return false;
    }

    ::EmptyClipboard();
    if (!::SetClipboardData(CF_BITMAP, hBmp)) {
        ::CloseClipboard();
        ::DeleteObject(hBmp);
        errorMsg = L"截图复制失败：无法写入系统剪贴板。";
        return false;
    }

    ::CloseClipboard();
    return true;
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
        if (!m_webZoomCalibrated) ApplyDpiNormalizedZoom();
        return;
    }
    SetWindowPos(nullptr, newX, newY, targetWindowW, targetWindowH, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!m_webZoomCalibrated) ApplyDpiNormalizedZoom();
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
    m_currentWebZoom = GetDpiNormalizedWebZoom(m_hWnd);
    m_webviewController->put_ZoomFactor(m_currentWebZoom);
}

bool CWebScoreDlg::CalibrateZoomFromWebMetrics(int innerWidth, int innerHeight, const CString& reason)
{
    if (m_webviewController == nullptr || innerWidth <= 0 || innerHeight <= 0) return false;

    double actualZoom = m_currentWebZoom > 0.0 ? m_currentWebZoom : GetDpiNormalizedWebZoom(m_hWnd);
    double controllerZoom = 0.0;
    if (SUCCEEDED(m_webviewController->get_ZoomFactor(&controllerZoom)) && controllerZoom > 0.0) {
        actualZoom = controllerZoom;
    }

    const int referenceClientWidth = GetReferenceClientWidth();
    const double widthRatio = static_cast<double>(innerWidth) /
        static_cast<double>(referenceClientWidth);
    const double heightRatio = static_cast<double>(innerHeight) / static_cast<double>(kReferenceClientHeight);
    double measuredRatio = widthRatio;
    if (heightRatio > 0.0 && heightRatio < measuredRatio) measuredRatio = heightRatio;
    if (measuredRatio <= 0.0) return false;

    double correctedZoom = actualZoom * measuredRatio;
    if (correctedZoom < 0.50) correctedZoom = 0.50;
    if (correctedZoom > 2.00) correctedZoom = 2.00;

    if (fabs(correctedZoom - actualZoom) < 0.015) return false;

    CString line;
    line.Format(L"[Web布局诊断][C++] 自动校正WebViewZoom：原因=%s；JS inner=%dx%d；目标CSS=%dx%d；原Zoom=%.3f；新Zoom=%.3f；说明=按前端实测视口反推，修正系统/兼容性DPI未被GetDpiForWindow捕获的额外缩放。",
        reason.GetString(),
        innerWidth, innerHeight,
        referenceClientWidth, kReferenceClientHeight,
        actualZoom, correctedZoom);
    WriteMatchLog(line);

    m_currentWebZoom = correctedZoom;
    m_webZoomCalibrated = true;
    m_webviewController->put_ZoomFactor(correctedZoom);
    return true;
}

void CWebScoreDlg::WriteWebHostDiagnostics(const CString& reason)
{
    if (!m_hWnd) return;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileName(NULL, exePath, MAX_PATH);

    CRect clientRect;
    CRect windowRect;
    GetClientRect(&clientRect);
    GetWindowRect(&windowRect);

    double zoom = GetDpiNormalizedWebZoom(m_hWnd);
    if (m_webviewController != nullptr) {
        double actualZoom = 0.0;
        if (SUCCEEDED(m_webviewController->get_ZoomFactor(&actualZoom)) && actualZoom > 0.0) {
            zoom = actualZoom;
        }
    }

    CString line;
    line.Format(L"[Web布局诊断][C++] 原因=%s；exe版本=%s；exe路径=%s；窗口=%dx%d；client=%dx%d；DPI缩放=%.3f；WebViewZoom=%.3f；布局期望CSS=%dx%d；目标视觉缩放=%.3f。",
        reason.GetString(),
        CURRENT_VERSION,
        exePath,
        windowRect.Width(), windowRect.Height(),
        clientRect.Width(), clientRect.Height(),
        GetDpiScaleForWindow(m_hWnd),
        zoom,
        GetReferenceClientWidth(), kReferenceClientHeight,
        kTargetVisualScale);
    WriteMatchLog(line);
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
    ApplyExpandedWindowSize();
    WriteWebHostDiagnostics(L"固定Web窗口尺寸已应用");
}

void CWebScoreDlg::ApplyExpandedWindowSize()
{
    int targetClientHeight = kReferenceClientHeight;
    if (m_appearanceExpanded) {
        targetClientHeight = max(targetClientHeight,
            kReferenceClientHeight + kAppearanceExtraClientHeight);
    }
    if (m_broadcasterPreviewExpanded) {
        targetClientHeight = max(targetClientHeight, kBroadcasterPreviewClientHeight);
    }
    if (m_playerIdentityExpanded) {
        targetClientHeight = max(targetClientHeight, kPlayerIdentityClientHeight);
    }

    ResizeWindowForClientSize(
        ScaleCssSizeToNativePixels(GetReferenceClientWidth(), kTargetVisualScale),
        ScaleCssSizeToNativePixels(targetClientHeight, kTargetVisualScale));
}

int CWebScoreDlg::GetReferenceClientWidth() const
{
    return m_consolePanelExpanded ? kExpandedClientWidth : kCompactClientWidth;
}

void CWebScoreDlg::SetAppearancePanelExpanded(bool expanded)
{
    if (m_appearanceExpanded == expanded) return;
    m_appearanceExpanded = expanded;

    ApplyExpandedWindowSize();
    WriteWebHostDiagnostics(m_appearanceExpanded ? L"外观面板打开扩高" : L"外观面板关闭还原");
}

void CWebScoreDlg::SetBroadcasterPreviewExpanded(bool expanded)
{
    if (m_broadcasterPreviewExpanded == expanded) return;
    m_broadcasterPreviewExpanded = expanded;

    ApplyExpandedWindowSize();
    WriteWebHostDiagnostics(m_broadcasterPreviewExpanded ?
        L"主播预览打开扩高" : L"主播预览关闭还原");
}

void CWebScoreDlg::SetConsolePanelExpanded(bool expanded)
{
    if (m_consolePanelExpanded == expanded) return;
    m_consolePanelExpanded = expanded;

    ApplyExpandedWindowSize();
    WriteWebHostDiagnostics(m_consolePanelExpanded ?
        L"C++日志面板打开扩宽" : L"C++日志面板关闭还原");
}

void CWebScoreDlg::SetPlayerIdentityPanelExpanded(bool expanded)
{
    if (m_playerIdentityExpanded == expanded) return;
    m_playerIdentityExpanded = expanded;

    ApplyExpandedWindowSize();
    WriteWebHostDiagnostics(m_playerIdentityExpanded ?
        L"选手身份面板打开扩高" : L"选手身份面板关闭还原");
}
