#pragma once

#include "resource.h"
#include <wrl.h>
#include <WebView2.h>

class CKillDisplayDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CKillDisplayDlg)

public:
    CKillDisplayDlg(CWnd* pParent = nullptr);
    virtual ~CKillDisplayDlg();

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
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);

    DECLARE_MESSAGE_MAP()

private:
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webviewController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;

    void InitWebView2();
    void ResizeWindowForClientSize(int targetClientW, int targetClientH);
    void HandleWebMessage(const CString& message);
    void BeginWindowDrag();
    void BeginWindowResize();
};
