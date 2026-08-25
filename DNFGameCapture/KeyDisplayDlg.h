#pragma once

#include "resource.h"
#include <wrl.h>
#include <WebView2.h>

class CKeyDisplayDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CKeyDisplayDlg)

public:
    CKeyDisplayDlg(const CString& iniPath, CWnd* pParent = nullptr);
    virtual ~CKeyDisplayDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_WEB_SCORE_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnCancel();
    virtual void OnOK();
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnExitSizeMove();
    afx_msg void OnDestroy();

    DECLARE_MESSAGE_MAP()

private:
    CString m_iniPath;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webviewController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;

    void InitWebView2();
    void HandleWebMessage(const CString& message);
    void BeginWindowDrag();
    void BeginWindowResize();
    void SaveWindowRect();
};
