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
#include <map>
#include "WGCCapture.h"

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

// 定义你当前软件的版本号，以及你服务器上 update.txt 的网址
#define CURRENT_VERSION L"1.3.2"
#define UPDATE_CHECK_URL L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/update.txt" // 【！！！请换成你自己的网址！！！】

#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

// ==========================================
// 【新增】：历史回溯截图的宏定义
// ==========================================
#define MAX_HISTORY_FRAMES 20      // 历史缓存的总帧数（决定了最多能回溯多少张图）
#define HISTORY_INTERVAL_MS 1000   // 截图的时间间隔(毫秒)，1000代表每秒1张

#define WM_UPDATE_OCR_DROPDOWNS (WM_USER + 100)
#define WM_TRAY_MESSAGE         (WM_USER + 101) // 托盘图标消息
#define WM_UPDATE_ALL_UI        (WM_USER + 105)// 【新增】：跨线程刷新 UI 专属消息

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

// ==========================================
// 【新增】：自定义编辑框类，底层暴力拦截按键
// ==========================================
class CQuickAddEdit : public CEdit {
protected:
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        // 1. 拦截键盘按下
        if (message == WM_KEYDOWN && wParam == VK_RETURN) {
            if (GetKeyState(VK_CONTROL) < 0) {
                // 【Ctrl + 回车】：纯换行
                ReplaceSel(L"\r\n", TRUE);
                return 0; // 吞掉消息
            }
            else {
                // 【纯回车】：给父窗口（主界面）发送“点击了添加按钮 (ID: 1022)”的模拟指令
                GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(1022, BN_CLICKED), 0);
                return 0; // 吞掉消息，坚决不换行
            }
        }
        // 2. 拦截字符输入残影
        if (message == WM_CHAR && wParam == VK_RETURN) {
            return 0; // 吞掉多余的换行符
        }

        // 其他按键正常放行
        return CEdit::WindowProc(message, wParam, lParam);
    }
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

    CButton m_chkFlip;
    CButton m_btnHelp;  // 【新增】：帮助按钮
    afx_msg void OnBnClickedHelp(); // 【新增】：点击说明事件

    // 【新增】：跨线程刷新 UI 响应函数
    afx_msg LRESULT OnUpdateAllUI(WPARAM wParam, LPARAM lParam);
    std::vector<CString> m_autoExpandedNodes; // 【新增】：记忆刚才修改过，需要临时展开3秒的主号

private:
    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    OcrResultData RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);
    void FilterLivePlatformPrefixes();
    void WriteScoreToFile();
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();
    void EnsureOcrRunning();
    void SaveConfigToFile();

    // 托盘图标初始化与清理
    void InitTrayIcon();
    void RemoveTrayIcon();
    void DoRealExit();

    // --- 新增响应函数：---
    afx_msg void OnBnClickedQuickAdd();
    afx_msg void OnRClickTree(NMHDR* pNMHDR, LRESULT* pResult);
    void SyncDataToTree();
    void LoadConfigFromFile(); // 新版读取配置

    afx_msg void OnEditSetFocus();
    afx_msg void OnEditKillFocus();
    // 找个 afx_msg 区域放下
    afx_msg void OnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnCustomDrawTree(NMHDR* pNMHDR, LRESULT* pResult);
    // DNFGameCaptureDlg.h
// 处理控件颜色的消息函数
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

    // 【新增】:系统版本与权限检测
    bool IsWindows10OrGreater();
    bool IsRunningAsAdmin();
    bool RelaunchAsAdmin(); // 权限检测与自动提权
    bool IsBitmapBlank(HBITMAP hBmp, int w, int h);

    int m_nBlankFrameCount = 0;
    bool m_bAlreadyPrompted = false;

    const int ID_CMB_CAPTURE_ENGINE = 1030; // 捕获引擎选择下拉框 ID
    CComboBox m_cmbCaptureEngine;           // 捕获引擎选择下拉框
    int m_nCaptureEngineChoice = 0;         // 0=自动, 1=WGC, 2=PrintWindow

private:

    WGCCapture* m_pWGC = nullptr;
    bool m_bUseWGC = false;   // 是否使用 WGC 模式

    std::map<CString, CString> m_aliasDB;        // 本地小号数据库
    void LoadAliasDB();                          // 加载数据库
    void SaveAliasDB();                          // 保存数据库
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

    // --- 新增以下控件：---
    CComboBox m_cmbTeamSelect; // 选择红蓝队
    CQuickAddEdit     m_editQuickAdd;  // 顶部单行快速输入框
    CButton   m_btnQuickAdd;   // 添加按钮
    CTreeCtrl m_treePlayers;   // 树状展示列表

    CStatic         m_status;
    CRichEditCtrl   m_editOcrResult;
    CRichEditCtrl   m_editVisualLogs;

    CButton         m_btnStart;
    CButton         m_btnApply;
    CButton         m_btnReset;
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

    HBITMAP m_historyBmps[MAX_HISTORY_FRAMES];
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

    bool m_bIsAuthValid;            // 记录当前授权是否有效
    CButton m_btnInputKey;          // 新增的“输入授权码”按钮
    afx_msg void OnBnClickedInputKey(); // 按钮的点击事件

    bool m_bIsTrial;        // 记录当前是否处于“试用”模式
    long long m_trialEnd;   // 记录试用结束的时间戳

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

    void ClearPreview(); // 清空预览画面

public:

    // 【新增下面这一行】：全局快捷键响应函数
    afx_msg void OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2);

    // 手动触发击杀测试函数 (这个保留)
    void ManualTriggerKill(int killSide);

    afx_msg void OnChangeEditNamesInput();
    afx_msg void OnCbnSelchangeCaptureEngine();
};