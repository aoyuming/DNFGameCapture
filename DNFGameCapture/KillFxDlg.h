#pragma once

#include "resource.h"
#include "DnfDialogClass.h"
#include <wrl.h>
#include <WebView2.h>
#include <functional>

// Fullscreen, fully transparent, click-through window that only renders the
// kill effects (kill.html?mode=fx). Meant to be captured by live-streaming
// tools (e.g. Douyin Live Companion "window" source) and stretched over the scene.
class CKillFxDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CKillFxDlg)

public:
    explicit CKillFxDlg(CWnd* pParent = nullptr);
    virtual ~CKillFxDlg();

    void ShowOverlay(int scalePercent);
    void HideOverlay();
    void SetOnUserClose(std::function<void()> callback) { m_onUserClose = std::move(callback); }
    bool IsOverlayVisible() const { return m_overlayVisible; }

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_WEB_SCORE_DIALOG };
#endif

protected:
    virtual BOOL OnInitDialog();
    virtual void OnCancel();
    virtual void OnOK();
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg LRESULT OnDisplayChange(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_webviewController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;
    int m_scalePercent = 100;
    bool m_overlayVisible = false;
    std::function<void()> m_onUserClose;

    void InitWebView2();
    void PlaceOnPrimaryMonitor();
    void ApplyScale();
};
