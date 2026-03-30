#pragma once
#include "pch.h"
#include <afxwin.h>
#include <Shellapi.h>
#include <gdiplus.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// 游戏窗口标题（根据实际情况修改）
#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_ERROR 5
#define COLOR_BLUE RGB(0,0,255)
#define COLOR_RED  RGB(255,0,0)

struct FixedPoint
{
    int x, y;
};

class CDNFGameCaptureDlg : public CWnd
{
public:
    CDNFGameCaptureDlg();
    ~CDNFGameCaptureDlg();

protected:
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy); // 新增窗口大小改变消息
    DECLARE_MESSAGE_MAP()

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    CString RunOCR(int nAreaIndex);

private:
    HBITMAP m_bmp;
    int m_w, m_h;
    BOOL m_bCanOcr;
    CPoint m_colorPts[4];
    CRect m_previewRect;
    CStatic m_status;
    CEdit m_editOcrResult;
};