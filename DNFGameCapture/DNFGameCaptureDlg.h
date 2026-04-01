#pragma once
#include "pch.h"
#include <afxwin.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>    // <--- 请务必加上这一行
#include "NameMatcher.hpp"

// 游戏窗口标题及常量定义
#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

// 别名数据结构
struct AliasData {
    CString name;
    int kills = 0;
    int deaths = 0;
    int currentStreak = 0; // 当前连杀数
    int akCount = 0;       // AK次数
};

// 玩家主数据结构
struct PlayerData {
    CString name;
    int team = 0;
    std::vector<AliasData> aliases;
    int kills = 0;
    int deaths = 0;
    int currentStreak = 0; // 当前连杀数
    int akCount = 0;       // AK次数
};

// 最近事件记录（用于防重复）
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
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnBnClickedStart();
    afx_msg void OnBnClickedApply();
    afx_msg void OnBnClickedFlip();
    afx_msg void OnBnClickedReset();
    afx_msg void OnBnClickedBrowseOcr();
    DECLARE_MESSAGE_MAP()

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    CString RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);

    void UpdatePlayersFromUI();
    void FilterLivePlatformPrefixes(); // 智能过滤直播词缀
    void SyncDataToInputBox();
    void WriteScoreToFile();
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();

private:
    HBITMAP m_bmp;
    int m_w, m_h;
    BOOL m_bIsRunning;
    BOOL m_bCanTrigger;
    BOOL m_bCanTriggerTeamScore;
    bool m_bPendingTeamScoreWin; // 异步大比分结算标记

    int m_totalScoreRed;
    int m_totalScoreBlue;
    int m_lastKillerTeam;
    bool m_bFlipSides;

    CRect m_previewRect;
    std::vector<CPoint> m_selectPts;

    CStatic m_status;
    CRichEditCtrl m_editNamesInput;
    CRichEditCtrl m_editOcrResult;
    CButton m_btnStart;
    CButton m_btnApply;
    CButton m_btnReset;
    CButton m_chkFlip;
    CButton m_btnBrowseOcr;
    CEdit   m_editOcrPath;
    CFont   m_font;

    PlayerData m_players[8];
    CNameMatcher m_matcher;
    std::vector<RecentEvent> m_recentEvents;

    HBITMAP m_historyBmps[6];
    int m_historyIdx;

    std::mutex m_dataMutex;
    std::mutex m_debugMutex;
    CString m_debugOcrResult;
    CString m_ocrExePath;
};