#pragma once
#include "pch.h"
#include <afxwin.h>
#include <afxcmn.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <future>
#include <winhttp.h>
#include "NameMatcher.hpp"

#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

struct AliasData {
    CString name;
    int kills = 0;
    int deaths = 0;
    int currentStreak = 0;
    int akCount = 0;
};

struct PlayerData {
    CString name;
    int team = 0;
    std::vector<AliasData> aliases;
    int kills = 0;
    int deaths = 0;
    int currentStreak = 0;
    int akCount = 0;
};

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
    DECLARE_MESSAGE_MAP()

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    CString RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);
    void UpdatePlayersFromUI();
    void FilterLivePlatformPrefixes();
    void SyncDataToInputBox();
    void WriteScoreToFile();
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();

    // ========== 【智能进程保活机制】 ==========
    void EnsureOcrRunning();

private:
    HBITMAP m_bmp;
    int m_w, m_h;
    BOOL m_bIsRunning;
    BOOL m_bCanTrigger;
    BOOL m_bCanTriggerTeamScore;
    bool m_bPendingTeamScoreWin;

    int m_totalScoreRed;
    int m_totalScoreBlue;
    int m_lastKillerTeam;
    bool m_bFlipSides;

    CRect m_previewRect;
    std::vector<CPoint> m_selectPts;

    CStatic         m_status;
    CRichEditCtrl   m_editNamesInput;
    CRichEditCtrl   m_editOcrResult;
    CButton         m_btnStart;
    CButton         m_btnApply;
    CButton         m_btnReset;
    CButton         m_chkFlip;
    CFont           m_font;

    PlayerData m_players[8];
    CNameMatcher m_matcher;
    std::vector<RecentEvent> m_recentEvents;

    HBITMAP m_historyBmps[6];
    int m_historyIdx;

    std::mutex m_dataMutex;
    std::mutex m_debugMutex;
    CString m_debugOcrResult;

    CString m_ocrExePath; // 固定的本地 Umi-OCR 路径

    ULONG_PTR m_gdiplusToken;
    HINTERNET m_hHttpSession;
    HINTERNET m_hHttpConnect;

    std::mutex m_launchMutex;
    DWORD m_lastLaunchOcrTime;
};