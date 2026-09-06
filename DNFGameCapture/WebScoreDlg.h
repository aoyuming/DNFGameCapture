#pragma once
#include "resource.h"  // 👈 【关键修复】：告诉代码去这里找对话框的 ID
#include <wrl.h>
#include <WebView2.h>



// 定义一个发给主窗口的自定义消息，改成 + 888 防冲突
#define WM_WEB_CMD_RECEIVED (WM_USER + 888)

class CWebScoreDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CWebScoreDlg)

public:
	CWebScoreDlg(CWnd* pParent = nullptr);   // 标准构造函数
	virtual ~CWebScoreDlg();

	// 暴露给主窗口的方法：主窗口调用它，把数据发给网页
	bool SendStateToWeb(const CString& jsonStr);
	void ApplyFixedWindowHeight();
	void SetAppearancePanelExpanded(bool expanded);
	void SetBroadcasterPreviewExpanded(bool expanded);
	void SetConsolePanelExpanded(bool expanded);
	void SetPlayerIdentityPanelExpanded(bool expanded);
	void ResizeWindowToSize(int targetWindowW, int targetWindowH);
	void ResizeWindowForClientSize(int targetClientW, int targetClientH);
	void WriteWebHostDiagnostics(const CString& reason);
	bool CalibrateZoomFromWebMetrics(int innerWidth, int innerHeight, const CString& reason);
	bool CopyWindowImageToClipboard(CString& errorMsg);
	void SetWebViewLoadingState(bool loading, const wchar_t* message = nullptr);

	// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WEB_SCORE_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持
	virtual BOOL OnInitDialog();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	// 🚨 【新增】：拦截各种会导致窗口消失的按键和动作
	virtual void OnCancel();
	virtual void OnOK();
	afx_msg void OnClose();

	DECLARE_MESSAGE_MAP()

private:
	// WebView2 核心指针
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webviewController;
	Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;
	int m_baseWindowWidth = 0;
	int m_baseWindowHeight = 0;
	bool m_appearanceExpanded = false;
	bool m_broadcasterPreviewExpanded = false;
	bool m_consolePanelExpanded = false;
	bool m_playerIdentityExpanded = false;
	bool m_initialWindowSizeApplied = false;
	double m_currentWebZoom = 0.0;
	bool m_webZoomCalibrated = false;
	bool m_webViewPageReady = false;
	HRESULT m_lastWebMessageFailure = S_OK;
	ULONGLONG m_lastWebMessageFailureTick = 0;
	bool m_webMessageSuccessLogged = false;
	CStatic m_webViewLoadingLabel;
	CFont m_webViewLoadingFont;
	CBrush m_webViewBackgroundBrush;
	CString m_webViewUserDataFolder;
	bool m_webViewUsingFallbackDataFolder = false;
	bool m_webViewInitInFlight = false;
	bool m_webViewRetryScheduled = false;
	unsigned int m_webViewInitAttempt = 0;
	ULONGLONG m_webViewInitStartedAt = 0;
	ULONGLONG m_webViewControllerStartedAt = 0;

	void InitWebView2();
	bool SwitchToWebViewRecoveryDataFolder(HRESULT result);
	void ScheduleWebViewRetry(const CString& reason);
	void LayoutWebViewLoadingLabel(int cx, int cy);
	void ApplyDpiNormalizedZoom();
	void ApplyExpandedWindowSize();
	int GetReferenceClientWidth() const;
};
