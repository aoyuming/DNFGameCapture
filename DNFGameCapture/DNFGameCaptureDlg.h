#pragma once
#include "pch.h"
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include <future>
#include <winhttp.h>
#include "NameMatcher.hpp"
#include "TemporalIdentityMatcher.hpp" // 【新增】：固定红框时间窗身份融合匹配
#include <map>
#include "WGCCapture.h"
#include "CameraCapture.h" // 【新增】
#include <deque>
#include "WebScoreDlg.h"
#include "json.hpp"

struct ScorePointF {
    float x = 0.0f;
    float y = 0.0f;
};

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

// 定义你当前软件的版本号，以及你服务器上 update.txt 的网址  
#define CURRENT_VERSION L"3.8.0"    //当前版本号
#define BRIDGE_VERSION  L"2.3.4" //桥接更新版本号
#define UPDATE_CHECK_URL_V1 L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/update.txt"//第一版单EXE更新版本地址
#define UPDATE_CHECK_URL_V2 L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/update_v2.txt"

#define DNF_WINDOW_NAME L"地下城与勇士：创新世纪"
#define COLOR_BLUE      RGB(0,0,255)
#define COLOR_RED       RGB(255,0,0)

// =========================================
// 🚨 【新增】：核心检测与冷却时间配置宏 (单位:毫秒)
// 如果你调高倍速看录像，请务必等比例缩小这些时间！
// 比如 2倍速：25000 -> 12500，240 -> 120
// ==========================================
#define POLL_COLOR_INTERVAL     240    // 画面颜色轮询间隔 (默认240ms，倍速太快容易漏掉大X，可改小如 100)
#define COOLDOWN_KILL_TRIGGER   10000   // 普通击杀：10秒
#define COOLDOWN_ROUND_END      35000    // 整局结束：35秒
#define COOLDOWN_TEAM_SCORE     60000 // 队伍覆灭大比分防抖冷却时间 (默认120000ms = 2分钟)
#define DUP_KILL_LIMIT_TIME     60000  // 重复击杀判定拦截时间 (默认20000ms = 60秒内同一个人死两次不计)
#define DUP_KILL_CLEAN_TIME     25000  // 战绩历史清理时间 (必须比 DUP_KILL_LIMIT_TIME 大一点)

// ==========================================
// 【新增】：历史回溯截图的宏定义
// ==========================================
#define MAX_HISTORY_FRAMES 10      // 历史缓存的总帧数（决定了最多能回溯多少张图）
#define HISTORY_INTERVAL_MS 1000   // 截图的时间间隔(毫秒)，1000代表每秒1张

#define WM_UPDATE_OCR_DROPDOWNS (WM_USER + 100)
#define WM_TRAY_MESSAGE         (WM_USER + 101) // 托盘图标消息
#define WM_UPDATE_ALL_UI        (WM_USER + 105)// 【新增】：跨线程刷新 UI 专属消息
#define WM_CLOUD_AUTH_FAIL      (WM_USER + 106) // 【新增】：云端授权失败专属消息
#define WM_UPDATE_AUTH_TIME     (WM_USER + 107) // 【新增】：同步云端到期时间
#define WM_OCR_SERVICE_FAIL     (WM_USER + 108) // 【新增】：Umi-OCR 离线/恢复失败，停止监控
#define WM_OCR_START_RESULT     (WM_USER + 109) // 【新增】：Umi-OCR 启动流程完成
#define WM_OCR_RECOVER_RESULT   (WM_USER + 110) // 【新增】：Umi-OCR 运行中恢复完成

// =========================================================
// 【编译环境切换开关】
// 设为 0：开启云端测试模式（捕获全屏幕桌面，用于云端播放录像测试）
// 设为 1：正式发布模式（精准捕获 DNF 窗口，用于发给用户的正式版）
// =========================================================
#define ENABLE_CLOUD_TEST_MODE 0

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
    int id = 0;
    CString killer;
    CString dead;
    DWORD time;
    CString timeText;
    int triggerSide = -1;
    int killerIdx = -1;
    int deadIdx = -1;
    int killerTeam = -1;
    int deadTeam = -1;
    int redScoreDelta = 0;
    int blueScoreDelta = 0;
    int akDelta = 0;
    bool statsApplied = false;
    bool undone = false;
    CString status;
    CString ocrSummary;
    CString candidateSummary;
    CString algorithmName;
    CString snapshotPath;
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

public:
    // 将主窗口的数据广播给 Web
    void BroadcastStateToWeb();

protected:
    // 接收新窗口发来的 Web 指令
    afx_msg LRESULT OnWebCmdReceived(WPARAM wParam, LPARAM lParam);

protected:
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnClose();
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
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
    // ⬇️ 【新增】：列表框点击事件
    afx_msg void OnLbnSelchangeRecentPlayers();

    DECLARE_MESSAGE_MAP()

    CButton m_chkFlip;
    CButton m_btnHelp;  // 【新增】：帮助按钮
    afx_msg void OnBnClickedHelp(); // 【新增】：点击说明事件

    // 【新增】：跨线程刷新 UI 响应函数
    afx_msg LRESULT OnUpdateAllUI(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCloudAuthFail(WPARAM wParam, LPARAM lParam); // 【新增】
    afx_msg LRESULT OnOcrServiceFail(WPARAM wParam, LPARAM lParam); // 【新增】：OCR 服务恢复失败时停止监控并提醒
    afx_msg LRESULT OnOcrStartResult(WPARAM wParam, LPARAM lParam); // 【新增】：OCR 启动完成回调
    afx_msg LRESULT OnOcrRecoverResult(WPARAM wParam, LPARAM lParam); // 【新增】：OCR 运行中恢复完成回调

    std::vector<CString> m_autoExpandedNodes; // 【新增】：记忆刚才修改过，需要临时展开3秒的主号

    // 🚨 C++ 战场级查重引擎：检查某个即将上场的主号及其小号，是否与场上现有的选手冲突
    CString CheckFieldConflict(const CString& newMain, const std::vector<CString>& extraAliases, int excludeIdx);

private:

    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc);
    OcrResultData RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex);

    void DoRetryMatchingTask(int triggerSide);

    // =========================================================
    // 【新增】：固定红框身份融合辅助接口
    // 说明：实现放在 DNFGameCaptureDlg.cpp 中调用即可。
    // - 左框：ID + 大区
    // - 右框：大区 + ID
    // - 职业帧进入缓存，不再当 ID 直接匹配
    // =========================================================
    void UpdateIdentityPanelCache(int areaIndex, const CString& rawOcrText);
    std::vector<TDnfCandidateIdentity> BuildIdentityCandidatesForPanel(TDnfPanelSide side);
    TDnfPanelMatchResult MatchIdentityPanel(TDnfPanelSide side);
    void NotifyIdentityKillConfirmed(int deadTeam, const CString& deadName);
    void NotifyIdentityRoundReset(const CString& reason);
    void AddReviewEvent(const RecentEvent& ev);
    bool ToggleReviewEvent(int eventId);

    void FilterLivePlatformPrefixes();
    void WriteScoreToFile();
    CString GetPickSeatLabelForIndex(int index) const;
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();
    bool EnsureOcrRunning(bool forceRestart = false);
    bool ProbeOcrServiceReady();
    void StartMonitoringAfterOcrReady();
    void BeginOcrServiceBootstrap();
    void BeginOcrServiceRecovery(bool probeBeforePending = false);
    void SetOcrStartupPendingUI(bool pending);
    bool RefreshOcrExePathFromRunningProcess(bool persistToIni);
    void SaveConfigToFile();

    // 托盘图标初始化与清理
    void InitTrayIcon();
    void RemoveTrayIcon();
    void DoRealExit();

    // --- 新增响应函数：---
    afx_msg void OnBnClickedQuickAdd();
    afx_msg void OnRClickTree(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg BOOL OnInitDialog();
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
    bool TryAutoCropBlackBars(HBITMAP& hBmp, int& w, int& h, const wchar_t* sourceTag);
    void ResetFrameHistory();

    int m_nBlankFrameCount = 0;
    bool m_bAlreadyPrompted = false;

    const int ID_CMB_DEATH_ALGORITHM = 1034; // 死亡X算法选择下拉框 ID
    CComboBox m_cmbDeathAlgorithm;           // 死亡X算法选择：0=大X颜色个数判断，1=打补丁红蓝判断
    int m_nDeathAlgorithmChoice = 0;         // 0=大X颜色个数判断，1=打补丁红蓝判断
    bool EnsureDeathPatchInstalled();        // 打补丁红蓝判断启动前自动安装 NPK
    bool FindDnfImagePacks2Folder(CString& outDir); // 搜索地下城与勇士\ImagePacks2

    const int ID_CMB_CAPTURE_ENGINE = 1030; // 捕获引擎选择下拉框 ID
    CComboBox m_cmbCaptureEngine;           // 捕获引擎选择下拉框
    int m_nCaptureEngineChoice = 0;         // 0=自动, 1=WGC, 2=PrintWindow

    const int ID_CMB_TARGET_WINDOW = 1031; // 【新增】：目标选择下拉框
    CComboBox m_cmbTargetWindow;
    afx_msg void OnCbnDropdownTargetWindow(); // 下拉展开时刷新列表
    afx_msg void OnCbnCloseupTargetWindow();
    // 【新增】：去标题栏复选框
    CButton m_chkCropTitle;
    CButton m_chkAutoCropBlackBars;
    afx_msg void OnBnClickedAutoCropBlackBars();

    void RefreshTargetList(); // 刷新目标列表的方法
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam); // 遍历窗口的回调函数
    CameraCapture* m_pCamera = nullptr; // 摄像头引擎实例
    std::atomic<bool> m_bWGCInitPending{ false };  // WGC 正在后台初始化中
    HWND m_hWGCInitTarget = NULL;                  // 正在初始化的目标窗口
    afx_msg LRESULT OnWGCInitDone(WPARAM wParam, LPARAM lParam);
    std::atomic<int> m_nWGCInitGeneration{ 0 };  // WGC 初始化代际计数

    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnBnClickedDeathXCalibrate();
    afx_msg void OnBnClickedDeathXSave();
    afx_msg void OnBnClickedDeathXCancel();
    afx_msg void OnBnClickedDeathXDefault();
    void EnterDeathXCalibrationMode();
    void ExitDeathXCalibrationMode(bool restoreSnapshot);
    void SaveDeathXCalibrationToIni();
    void LoadDeathXCalibrationFromIni();
    void ApplyDefaultDeathXPoints();
    void SnapshotDeathXCalibration();
    void ResetDeathXStableState();
    void EnsureBackgroundTimersStarted();
    void ResetMatchCooldownState(const CString& reason);
    ScorePointF GetDeathXPoint(int logicalIdx) const;
    void SetDeathXPoint(int logicalIdx, ScorePointF pt);
    void SelectDeathXPoint(int logicalIdx);
    bool MoveSelectedDeathXPointByPixels(int dx, int dy);
    bool HandleDeathXCalibrationKey(UINT vk);
    int HitTestDeathXPoint(CPoint point) const;
    CPoint DeathXPointToClient(ScorePointF pt) const;
    ScorePointF ClientToDeathXPoint(CPoint point) const;
    void UpdateDeathXCalibrationButtons();
    virtual BOOL PreTranslateMessage(MSG* pMsg);

    // 【新增】：强制居中的自定义 MessageBox
    int ShowCenteredMsgBox(LPCTSTR lpszText, LPCTSTR lpszCaption = NULL, UINT nType = MB_OK);
    int ShowUpdateConfirmDialog(const CString& serverVersion, const CString& currentVersion, const CString& visibleUpdateLog);


    void ClearPreview(); // 清空预览画面

    // 【新增】：根据进程名杀后台的黑科技函数
    void KillProcessByName(const CString& processName);

    CWebScoreDlg* m_pWebDlg; // 新窗口的指针


private:

    WGCCapture* m_pWGC = nullptr;
    bool m_bUseWGC = false;   // 是否使用 WGC 模式
    HWND m_cachedGameHwnd = NULL; // 最近一次成功抓到的游戏窗口句柄
    // 🚨【新增】：WGC 线程安全延迟销毁器
    void SafeDeleteWGC();

    std::map<CString, CString> m_aliasDB;        // 本地小号数据库
    void LoadAliasDB();                          // 加载数据库
    void SaveAliasDB();                          // 保存数据库
    void SaveAliasDB(bool mergeActivePlayers);
    std::string BuildAliasDbJsonPayload(int& mainCount, int& pairCount) const;
    void LoadAliasCloudDeleteBaseline();
    void SaveAliasCloudDeleteBaseline() const;
    void SetAliasCloudDeleteBaselineFromPublicPlayers(const nlohmann::json& players);
    nlohmann::json BuildAliasCloudDeleteScopeJson() const;
    HBITMAP m_bmp;
    int m_w, m_h;
    BOOL m_bIsRunning;
    BOOL m_bCanTrigger;
    BOOL m_bCanTriggerTeamScore;
    bool m_bPendingTeamScoreWin;
    bool m_deathXStableState[8] = {};
    int m_deathXStableOn[8] = {};
    int m_deathXStableOff[8] = {};
    ScorePointF m_deathXPoints[8] = {};
    ScorePointF m_deathXSnapshotPoints[8] = {};
    bool m_bDeathXCustomPoints = false;
    bool m_bDeathXCalibrationMode = false;
    int m_selectedDeathXPoint = -1;
    int m_dragDeathXPoint = -1;
    bool m_bDraggingDeathXPoint = false;

    int m_totalScoreRed;
    int m_totalScoreBlue;
    int m_lastKillerTeam;
    bool m_bFlipSides;

    CRect m_previewRect;

    // --- 新增以下控件：---
    CComboBox m_cmbTeamSelect; // 选择红蓝队
    CQuickAddEdit     m_editQuickAdd;  // 顶部单行快速输入框
    CButton   m_btnQuickAdd;   // 添加按钮

    CStatic         m_status;
    CRichEditCtrl   m_editOcrResult;
    CRichEditCtrl   m_editVisualLogs;

    CButton         m_btnStart;
    CButton         m_btnApply;
    CButton         m_btnReset;
    CButton         m_btnBrowseDir;
    CButton         m_btnDeathXCalibrate;
    CButton         m_btnDeathXSave;
    CButton         m_btnDeathXCancel;
    CButton         m_btnDeathXDefault;
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
    CTemporalIdentityMatcher m_identityMatcher; // 【新增】：左/右固定红框 ID+大区+职业 时间窗融合缓存
    std::mutex m_identityMutex;                 // 【新增】：保护身份缓存，RunOCR 并发线程会同时写入左右框
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
    bool m_bOutputSeatLabelToKillFile = false;
    bool m_bRedPickFirst = true;

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
    // 🚨 【新增】：标记是否是用户手动点击的授权验证
    bool m_bIsManualAuthCheck = false;

    // 【新增】：云端授权回调变量与消息
    long long m_keyDuration = 0;     // 存放解析出的时长
    long long m_cloudExpireTime = 0; // 存放云端返回的绝对到期时间

    // 把下面这两行覆盖原来的声明
    bool VerifyKey(CString inputKey, CString machineID);
    CString CheckCloudBinding(CString key, CString hwid, long long duration, long long& outExpTime);
    bool BeginLicenseCloudCheck(const CString& inputKey, bool manualCheck);
    CString SubmitAliasDbForReview(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString DirectSyncAliasDbToCloud(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString SyncAliasDbFromCloud();
    std::string FilterAliasDbPayloadForReview(const std::string& aliasDbPayload, int& mainCount, int& pairCount, int& containedNakedAliasCount) const;
    void ResetAliasDbCloudBaseline();
    void AutoSubmitAliasDbIfDirty();
    CString SubmitAliasDbSnapshotIfDirty(bool saveBeforeBuild = true);
    afx_msg LRESULT OnUpdateAuthTime(WPARAM wParam, LPARAM lParam); // 【新增消息】

    CString GetMachineID();
    std::string m_aliasDbCloudBaselinePayload;
    std::string m_aliasDbLastSubmittedPayload;
    bool m_bAliasDirectMode = false;
    std::vector<CString> m_aliasDbPendingDeleteMains;
    std::vector<CString> m_aliasCloudDeleteBaselineMains;
    std::map<CString, CString> m_aliasCloudBaselinePlayers;

    // 【新增】：仅在 Debug 模式下编译的调试输出函数
    void OutputDebugAuthInfo();

    // 【新增】远程更新系统
    void CheckForUpdates(bool bSilent);
    void DownloadAndApplyUpdate(CString downloadUrl);


    std::mutex m_launchMutex;
    DWORD m_lastLaunchOcrTime;
    std::atomic<bool> m_bOcrStartPending{ false };
    std::atomic<DWORD> m_ocrStartRequestId{ 0 };
    std::atomic<bool> m_bOcrHealthCheckPending{ false };
    std::atomic<bool> m_bOcrRecoveryPending{ false };
    std::atomic<DWORD> m_ocrRecoveryRequestId{ 0 };

    // 【新增】：用于防止多开的互斥体句柄
    HANDLE m_hSingleInstanceMutex;


    CTreeCtrl m_treePlayers;   // 树状展示列表

    // ⬇️ 【新增】：常用选手名单列表框
    CListBox  m_listRecentPlayers;

    // ⬇️ 【新增】：用来保存最近使用记录的数据结构
    struct RecentPlayerRecord {
        CString mainName;
        std::vector<CString> aliases;
    };
    std::deque<RecentPlayerRecord> m_recentPlayerRecords;
    std::mutex m_recentRecordsMutex;

    // ⬇️ 【新增】：刷新列表框的核心函数
    void UpdateAndRefreshRecentList();
public:

    // 【新增下面这一行】：全局快捷键响应函数
    afx_msg void OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2);

    // 手动触发击杀测试函数 (这个保留)
    void ManualTriggerKill(int killSide);

    afx_msg void OnChangeEditNamesInput();
    afx_msg void OnCbnSelchangeDeathAlgorithm();
    afx_msg void OnCbnSelchangeCaptureEngine();
};
