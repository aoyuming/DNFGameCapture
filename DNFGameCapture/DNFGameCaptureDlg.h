#pragma once
#include "pch.h"
#include <afxwin.h>
#include <afxcmn.h>   
#include <thread>
#include <vector>
#include <mutex>
#include "NameMatcher.hpp"

// 游戏窗口标题及常量定义
#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

struct AliasData {
    CString name;
    int kills = 0;
    int deaths = 0;
};

struct PlayerData {
    CString name;
    std::vector<AliasData> aliases;
    int kills = 0;
    int deaths = 0;
    int team = 0;
};

// ============ 新增：近期击杀事件记录（用于智能去重） ============
struct RecentEvent {
    CString killer;
    CString dead;
    DWORD time;
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
    afx_msg void OnBnClickedStart();
    afx_msg void OnBnClickedApply();
    afx_msg void OnBnClickedFlip();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    DECLARE_MESSAGE_MAP()

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    CString RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);
    void UpdatePlayersFromUI();
    void RefreshDisplay();
    void WriteScoreToFile();
    void AppendResultText(const CString& text, COLORREF color);
    void SyncDataToInputBox();

private:
    HBITMAP m_bmp;
    int m_w, m_h;
    BOOL m_bIsRunning;

    BOOL m_bCanTrigger;
    BOOL m_bCanTriggerTeamScore;

    HBITMAP m_historyBmps[6];
    int m_historyIdx;

    CPoint m_colorPts[4];
    CRect m_previewRect;

    CStatic m_status;
    CRichEditCtrl m_editNamesInput;
    CRichEditCtrl m_editOcrResult;

    CButton m_btnStart;
    CButton m_btnApply;
    CButton m_chkFlip;

    CFont   m_font;

    PlayerData m_players[8];
    CNameMatcher m_matcher;

    CString m_debugOcrResult;
    CString m_debugMatchDetails;
    std::mutex m_debugMutex;

    std::vector<CPoint> m_selectPts;

    int m_totalScoreRed;
    int m_totalScoreBlue;
    int m_lastKillerTeam;

    bool m_bFlipSides;

    // ============ 新增：多线程数据保护锁与记忆库 ============
    std::vector<RecentEvent> m_recentEvents;
    std::mutex m_dataMutex; // 全局保护玩家数据，彻底防崩溃

    CButton m_btnReset;
    afx_msg void OnBnClickedReset();

    CButton m_btnBrowseOcr;   // 选择OCR路径的按钮
    CEdit m_editOcrPath;      // 显示OCR路径的文本框
    CString m_ocrExePath;     // 存储OCR的实际路径

    afx_msg void OnBnClickedBrowseOcr(); // 按钮点击事件声明
};