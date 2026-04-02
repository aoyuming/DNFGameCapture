#pragma once
#include "pch.h"
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <future>
#include <winhttp.h>
#include "NameMatcher.hpp"

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

// 定义你当前软件的版本号，以及你服务器上 update.txt 的网址
#define CURRENT_VERSION L"1.0.3"
#define UPDATE_CHECK_URL L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/update.txt" // 【！！！请换成你自己的网址！！！】

#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

#define WM_UPDATE_OCR_DROPDOWNS (WM_USER + 100)
#define WM_TRAY_MESSAGE         (WM_USER + 101) // 托盘图标消息

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

struct OcrResultData {
    CString text;
    HBITMAP hBmp;
};

struct OcrRecord {
    HBITMAP hBmp;
    CString displayText;
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
    afx_msg void OnBnClickedBrowseDir();

    // 系统指令拦截与托盘消息处理
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg LRESULT OnTrayMessage(WPARAM wParam, LPARAM lParam);

    afx_msg LRESULT OnUpdateOcrDropdowns(WPARAM wParam, LPARAM lParam);
    afx_msg void OnCbnSelchangeLeft();
    afx_msg void OnCbnSelchangeRight();

    DECLARE_MESSAGE_MAP()

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    OcrResultData RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);
    void UpdatePlayersFromUI();
    void FilterLivePlatformPrefixes();
    void SyncDataToInputBox();
    void WriteScoreToFile();
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();
    void EnsureOcrRunning();
    void SaveConfigToFile();

    // 托盘图标初始化与清理
    void InitTrayIcon();
    void RemoveTrayIcon();
    void DoRealExit();

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
    CRichEditCtrl   m_editVisualLogs;

    CButton         m_btnStart;
    CButton         m_btnApply;
    CButton         m_btnReset;
    CButton         m_chkFlip;
    CButton         m_btnBrowseDir;
    CEdit           m_editOutDir;
    CFont           m_font;

    CComboBox       m_cmbLeft;
    CComboBox       m_cmbRight;
    std::vector<OcrRecord> m_ocrRecordsLeft;
    std::vector<OcrRecord> m_ocrRecordsRight;
    std::mutex      m_ocrRecordMutex;
    int             m_viewIndexLeft;
    int             m_viewIndexRight;

    PlayerData m_players[8];
    CNameMatcher m_matcher;
    std::vector<RecentEvent> m_recentEvents;

    HBITMAP m_historyBmps[25];
    int m_historyIdx;

    std::mutex m_dataMutex;
    std::mutex m_debugMutex;
    CString m_debugOcrResult;

    HBITMAP m_hDebugOcrBmp[2];
    std::mutex m_ocrBmpMutex;

    CString m_ocrExePath;
    CString m_configPath;
    CString m_iniPath;
    CString m_outputDir;

    ULONG_PTR m_gdiplusToken;
    HINTERNET m_hHttpSession;
    HINTERNET m_hHttpConnect;
    NOTIFYICONDATA m_nid;

    // 【新版授权系统】
    void CheckTrialAndLicense();
    bool VerifyKey(CString inputKey, CString machineID);
    // 【新增下面这一行】：
    CString CheckCloudBinding(CString key, CString hwid);
    CString GetMachineID();

    // 【新增】：仅在 Debug 模式下编译的调试输出函数
    void OutputDebugAuthInfo();

    // 【新增】远程更新系统
    void CheckForUpdates(bool bSilent);
    void DownloadAndApplyUpdate(CString downloadUrl);

    std::mutex m_launchMutex;
    DWORD m_lastLaunchOcrTime;

    // 【新增】：用于防止多开的互斥体句柄
    HANDLE m_hSingleInstanceMutex;

public:

    // 【新增下面这一行】：全局快捷键响应函数
    afx_msg void OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2);

    // 手动触发击杀测试函数 (这个保留)
    void ManualTriggerKill(int killSide);
};