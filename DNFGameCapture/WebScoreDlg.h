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
	void SendStateToWeb(const CString& jsonStr);

	// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WEB_SCORE_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持
	virtual BOOL OnInitDialog();
	afx_msg void OnSize(UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()

private:
	// WebView2 核心指针
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webviewController;
	Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;

	void InitWebView2();
};