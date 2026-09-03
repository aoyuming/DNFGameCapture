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
#include <condition_variable>
#include <future>
#include <memory>
#include <winhttp.h>
#include "NameMatcher.hpp"
#include "TemporalIdentityMatcher.hpp" // 【新增】：固定红框时间窗身份融合匹配
#include <map>
#include "WGCCapture.h"
#include "CameraCapture.h" // 【新增】
#include <deque>
#include "WebScoreDlg.h"
#include "KillDisplayDlg.h"
#include "KeyDisplayDlg.h"
#include "KeyMappingHook.h"
#include "KeyMappingLanService.h"
#include "CloudMatchClient.h"
#include "CloudMatchStatusDisplay.h"
#include "AliasDbAutoSyncPolicy.h"
#include "json.hpp"

void DnfReleaseSingleInstanceMutex() noexcept;

struct ScorePointF {
    float x = 0.0f;
    float y = 0.0f;
};

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

// 定义你当前软件的版本号，以及你服务器上 update.txt 的网址  
#define CURRENT_VERSION L"5.0.2"    //当前版本号
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
#define WM_KILL_DISPLAY_VISIBILITY_CHANGED (WM_USER + 111) // 击杀展示窗口显示/隐藏后同步 Web 按钮状态
#define WM_KEY_DISPLAY_VISIBILITY_CHANGED (WM_USER + 113)
#define WM_KEY_MAPPING_LAN_CHANGED (WM_USER + 114)
#define WM_KEY_MAPPING_TEAM_SYNC (WM_USER + 115)
#define WM_CAPTURE_SOURCE_SWITCH_DONE (WM_USER + 116)
#define WM_CAMERA_LIST_READY (WM_USER + 117)
#define WM_ALIAS_AUTO_SYNC_RESULT (WM_USER + 118)

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
    bool cloudSynced = false;
};

struct OcrResultData {
    CString text;
    HBITMAP hBmp;
};

struct OcrRecord {
    HBITMAP hBmp;
    CString displayText;
};

struct KeyMappingSlot {
    UINT vk = 0;
    CString label;
    CString color = L"#00E5FF";
    int opacity = 42;
};

inline constexpr int KEY_MAPPING_SLOT_COUNT = 14;

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
    std::string BuildKillDisplayStatePayload();
    std::string BuildKeyMappingStatePayload();
    bool SaveKillDisplaySettingsPayload(const std::string& requestBody, std::string& responseBody);

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
    afx_msg LRESULT OnKillDisplayVisibilityChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnKeyDisplayVisibilityChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnKeyMappingLanChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnKeyMappingTeamSync(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAliasDbAutoSyncResult(WPARAM wParam, LPARAM lParam);

    std::vector<CString> m_autoExpandedNodes; // 【新增】：记忆刚才修改过，需要临时展开3秒的选手

    // 🚨 C++ 战场级查重引擎：检查某个即将上场的选手及其游戏ID，是否与场上现有的选手冲突
    CString CheckFieldConflict(const CString& newMain, const std::vector<CString>& extraAliases, int excludeIdx);

private:

    void Capture();
    void CheckColorTrigger();
    void Draw(CDC& dc, HBITMAP previewFrame, int previewW, int previewH);
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
    void AddReviewEventUnlocked(const RecentEvent& ev);
    bool MergeCloudRecentEvents(const nlohmann::json& events,
        const std::string& sourceName);
    bool ToggleReviewEvent(int eventId);

    void FilterLivePlatformPrefixes();
    bool WriteScoreToFile();
    nlohmann::json BuildSharedWebMatchSnapshotJson();
    nlohmann::json DnfBuildKillDisplayStateJson();
    nlohmann::json DnfBuildSharedWebStateJson();
    void OpenKillDisplayWindow();
    void HideKillDisplayWindow();
    void ToggleKillDisplayWindow();
    bool IsKillDisplayWindowVisible() const;
    CString GetKillDisplayObsUrl() const;
    nlohmann::json BuildKeyMappingSettingsJson();
    void LoadKeyMappingSettings();
    void SaveKeyMappingSettings();
    bool SetKeyMappingEnabled(bool enabled, CString& errorMessage);
    void PollKeyMappingState();
    void LoadKeyMappingLanSettings();
    void SaveKeyMappingLanSettings();
    void SetKeyMappingLanRole(KeyMappingLanRole role);
    void NotifyKeyMappingAdminRequired(const CString& reason);
    CString GetKeyMappingLanDeviceName() const;
    std::string BuildTeamSyncSnapshotPayload();
    std::string BuildTeamSyncSnapshotPayloadUnlocked();
    void LoadCloudMatchSettings();
    bool SaveCloudMatchSettings();
    bool SaveCloudMatchSettingsForRoomIdentity(const std::string& roomIdOverride,
        const CString& broadcasterNameOverride);
    bool SaveCloudMatchRevision();
    bool HasAuthorizedCloudMatchEndpoint() const;
    void DisableCloudMatchForAuthorization(const CString& reason);
    void StartSavedCloudMatchSession();
    void BeginCloudRoomJoin(const std::string& roomId, const CString& broadcasterName);
    void BeginCloudDeviceRegistration();
    bool BeginCloudRoomRestore(const CString& reason);
    void CancelCloudRoomJoin(const CString& reason);
    void HandleCloudMatchSnapshotUploadResult(const nlohmann::json& event);
    void HandleCloudMatchMessage(std::string message);
    bool RejectLocalMatchEditWhileRealtime();
    bool IsCloudReverseSyncBlocked(const std::string& targetDeviceId) const;
    void InvalidateCloudMatchSyncPreview(bool clearMembers = true);
    void InvalidateCloudMatchSyncUndo();
    void BeginCloudMatchComparisonRequest();
    void HandleCloudMatchComparisonResult(const nlohmann::json& event);
    void HandleCloudMatchSnapshotResult(const nlohmann::json& event);
    void HandleUnifiedCloudSnapshot(const nlohmann::json& event, bool realtime);
    void RequestUnifiedCloudDirectory();
    void QueueCloudMatchSyncedUpload(const std::string& sourceDeviceId,
        std::uint64_t sourceRevision);
    void PollCloudMatch();
    CloudMatchDisplayStatus BuildCloudMatchDisplayStatusSnapshot(
        const CloudMatchStatusSnapshot& cloudStatus) const;
    void RefreshCloudMatchStatusDisplay(
        const CloudMatchStatusSnapshot& cloudStatus);
    void OnMatchStateChanged(std::string matchPayload, const char* source);
    void MarkMatchMutation();
    void MarkCloudMatchOcrStateChanged(std::string matchPayload);
    std::string BuildCloudMatchSnapshotPayload(const std::string& matchPayload,
        std::uint64_t clientRevision, const std::string& changeSource,
        CString* errorMessage = nullptr) const;
    void SendCloudRoomPromptIfNeeded();
    bool ValidateTeamSyncSnapshot(const nlohmann::json& snapshot, CString& errorMessage) const;
    bool ApplyTeamSyncSnapshot(const nlohmann::json& snapshot, bool createBackup,
        CString& errorMessage, bool automatic = false, bool preserveLocalFlip = false);
    bool RefreshAfterTeamSyncApply();
    void ClearTeamSyncState();
    void OpenKeyDisplayWindow();
    void HideKeyDisplayWindow();
    void ToggleKeyDisplayWindow();
    bool IsKeyDisplayWindowVisible() const;
    CString GetPickSeatLabelForIndex(int index) const;
    void AppendResultText(const CString& t, COLORREF c);
    void RefreshDisplay();
    void StartOcrSupervisor();
    void StopOcrSupervisor();
    void RequestOcrSupervisorWork();
    void OcrSupervisorLoop();
    bool WarmupOcrEngine();
    void RestartOcrProcessForRecovery();
    bool IsTrackedOcrProcessAlive();
    void CloseTrackedOcrProcess();
    bool EnsureOcrRunning(bool forceRestart = false);
    bool ProbeOcrServiceReady();
    void StartMonitoringAfterOcrReady();
    void BeginOcrServiceBootstrap();
    void BeginOcrServiceRecovery(bool probeBeforePending = false);
    void SetOcrStartupPendingUI(bool pending);
    bool RefreshOcrExePathFromRunningProcess(bool persistToIni);
    void StartOcrMatchingTask(int triggerSide);
    void EndOcrMatchingTask();
    void StopOcrMatchingTasks();
    void WaitForOcrMatchingTasks();
    bool RegisterOcrSupervisorRequest(HINTERNET hRequest);
    bool ReleaseOcrSupervisorRequest(HINTERNET hRequest);
    void CancelOcrSupervisorRequest();
    bool SaveConfigToFile();

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
    void RequestBlackBarCropDetection(const CString& reason);
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
    CString m_lastTargetWindowName;
    afx_msg void OnCbnDropdownTargetWindow(); // 下拉展开时刷新列表
    afx_msg void OnCbnCloseupTargetWindow();
    // 【新增】：去标题栏复选框
    CButton m_chkCropTitle;
    CButton m_btnCropBlackBars;
    afx_msg void OnBnClickedAutoCropBlackBars();
    bool m_blackBarCropPending = true;
    bool m_blackBarCropLocked = false;
    int m_blackBarCropSourceW = 0;
    int m_blackBarCropSourceH = 0;
    int m_blackBarCropLeft = 0;
    int m_blackBarCropTop = 0;
    int m_blackBarCropRight = 0;
    int m_blackBarCropBottom = 0;
    CString m_blackBarCropRequestReason = L"首次捕获";

    void RefreshTargetList(); // 刷新目标列表的方法
    CString GetSelectedTargetWindowLabel();
    void SaveSelectedTargetWindowName();
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam); // 遍历窗口的回调函数
    CameraCapture* m_pCamera = nullptr; // 摄像头引擎实例
    std::atomic<bool> m_bWGCInitPending{ false };  // WGC 正在后台初始化中
    HWND m_hWGCInitTarget = NULL;                  // 正在初始化的目标窗口
    afx_msg LRESULT OnWGCInitDone(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCaptureSourceSwitchDone(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraListReady(WPARAM wParam, LPARAM lParam);
    std::atomic<int> m_nWGCInitGeneration{ 0 };  // WGC 初始化代际计数
    std::atomic<unsigned int> m_captureSwitchGeneration{ 0 };
    std::atomic<bool> m_captureSwitchPending{ false };
    ULONGLONG m_captureSwitchStartedAt = 0;
    struct CaptureSwitchJob {
        WGCCapture* wgc = nullptr;
        CameraCapture* camera = nullptr;
        HWND notifyWindow = nullptr;
        unsigned int generation = 0;
        bool notifyWhenDone = false;
    };
    std::thread m_captureSwitchWorker;
    std::mutex m_captureSwitchMutex;
    std::condition_variable m_captureSwitchCv;
    std::deque<CaptureSwitchJob> m_captureSwitchQueue;
    bool m_captureSwitchWorkerStopping = false;
    void StartCaptureSwitchWorker();
    void StopCaptureSwitchWorker();
    void EnqueueCaptureSwitchJob(CaptureSwitchJob job);
    void QueueCaptureSourceSwitch();
    void SafeDeleteCamera();
    void BeginCameraEnumeration();
    std::mutex m_cameraListMutex;
    std::vector<std::wstring> m_cachedCameraNames;
    std::atomic<bool> m_cameraEnumerationPending{ false };

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
    CKillDisplayDlg* m_pKillDisplayDlg = nullptr; // OBS/直播伴侣击杀展示窗口
    CKeyDisplayDlg* m_pKeyDisplayDlg = nullptr;
    CString m_webFrontDir;
    bool m_bKillDisplayHttpReady = false;
    CString m_killDisplayHttpError;


private:

    WGCCapture* m_pWGC = nullptr;
    bool m_bUseWGC = false;   // 是否使用 WGC 模式
    HWND m_cachedGameHwnd = NULL; // 最近一次成功抓到的游戏窗口句柄
    // 🚨【新增】：WGC 线程安全延迟销毁器
    void SafeDeleteWGC();

    std::map<CString, CString> m_aliasDB;        // 本地游戏ID数据库
    void LoadAliasDB();                          // 加载数据库
    bool SaveAliasDB();                          // 保存数据库
    bool SaveAliasDB(bool mergeActivePlayers);
    std::string BuildAliasDbJsonPayload(int& mainCount, int& pairCount) const;
    std::string BuildAliasDbAppendPayload(int& mainCount, int& pairCount) const;
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

    KeyMappingSlot m_keyMappingSlots[KEY_MAPPING_SLOT_COUNT];
    std::mutex m_keyMappingMutex;
    std::atomic<unsigned int> m_keyMappingLocalMask{ 0 };
    std::atomic<unsigned int> m_keyMappingActiveMask{ 0 };
    std::atomic<bool> m_keyMappingEnabled{ false };
    CKeyMappingHook m_keyMappingHook;
    KeyMappingLanService m_keyMappingLanService;
    KeyMappingLanRole m_keyMappingLanRole = KeyMappingLanRole::standalone;
    unsigned short m_keyMappingLanPort = 18778;
    CString m_keyMappingLanPairCode;
    CString m_keyMappingLanClientPairCode;
    CString m_keyMappingLanServerAddress;
    CString m_keyMappingLanServerId;
    CString m_keyMappingLanDeviceId;
    CString m_keyMappingLanLastServerId;
    CString m_keyMappingLanLastStatus;
    bool m_keyMappingLanWasConnected = false;
    bool m_keyMappingAdminRequired = false;
    nlohmann::json m_teamSyncPendingSnapshot;
    nlohmann::json m_teamSyncLocalBaselineSnapshot;
    nlohmann::json m_teamSyncAppliedSnapshot;
    nlohmann::json m_teamSyncBackupSnapshot;
    bool m_teamSyncBackupAvailable = false;
    bool m_teamSyncAutoReceive = false;
    bool m_teamSyncAllowClientWrite = false;
    bool m_teamSyncAutoSend = false;
    std::uint64_t m_teamSyncLastAutoRevision = 0;
    CString m_teamSyncLastAutoResult;
    int m_teamSyncEventBoundaryId = 0;
    int m_teamSyncBackupEventBoundaryId = 0;

    CloudMatchClient m_cloudMatchClient;
    CString m_cloudMatchServerUrl;
    std::string m_cloudMatchDeviceId;
    std::string m_cloudMatchDeviceToken;
    bool m_cloudMatchTemporaryInstance = false;
    std::string m_cloudMatchRoomId;
    CString m_cloudMatchBroadcasterName;
    std::uint64_t m_cloudMatchClientRevision = 0;
    std::string m_cloudMatchPendingRoomId;
    CString m_cloudMatchPendingBroadcasterName;
    CString m_cloudMatchLastError;
    std::string m_cloudMatchLastObservedPayload;
    std::string m_cloudMatchPendingPayload;
    std::string m_cloudMatchPendingChangeSource = "manual";
    std::string m_cloudMatchInFlightPayload;
    std::string m_cloudMatchInFlightChangeSource;
    std::uint64_t m_cloudMatchInFlightRevision = 0;
    std::mutex m_cloudMatchSourceMutex;
    std::string m_cloudMatchExplicitOcrPayload;
    ULONGLONG m_cloudMatchUploadDueTick = 0;
    ULONGLONG m_cloudMatchUploadQueueResultDeadlineTick = 0;
    ULONGLONG m_cloudMatchLastObserveTick = 0;
    ULONGLONG m_cloudMatchJoinDeadlineTick = 0;
    ULONGLONG m_cloudMatchLeaveDeadlineTick = 0;
    bool m_cloudMatchUploadDirty = false;
    bool m_cloudMatchUploadInFlight = false;
    bool m_cloudMatchUploadRetryBlocked = false;
    int m_cloudMatchUploadTransientRetryCount = 0;
    bool m_cloudMatchRoomConfirmed = false;
    bool m_cloudMatchRestoring = false;
    bool m_cloudMatchJoining = false;
    bool m_cloudMatchRegistering = false;
    bool m_cloudMatchRenaming = false;
    bool m_cloudMatchLeaving = false;
    bool m_cloudMatchSkipPromptThisRun = false;
    bool m_cloudMatchPromptSent = false;
    bool m_cloudMatchWebReady = false;
    int m_cloudMatchRegistrationRetryCount = 0;
    bool m_cloudMatchUsingLicenseLease = false;
    bool m_cloudMatchLeaseRefreshAttempted = false;
    bool m_cloudMatchLeaseRefreshInFlight = false;
    ULONGLONG m_cloudMatchLeaseDisconnectedSinceTick = 0;

    bool m_cloudMatchSyncPanelOpen = false;
    bool m_cloudMatchSyncBusy = false;
    std::uint64_t m_cloudMatchSyncGeneration = 1;
    std::uint64_t m_cloudMatchSyncRequestSequence = 0;
    std::uint64_t m_cloudMatchSyncConnectionGeneration = 0;
    std::uint64_t m_cloudMatchSyncComparisonRoomRevision = 0;
    std::uint64_t m_cloudMatchSyncComparisonGeneratedAt = 0;
    std::uint64_t m_cloudMatchSyncComparisonTotalMembers = 0;
    std::uint64_t m_cloudMatchSyncComparisonBoundedMembers = 0;
    std::uint64_t m_cloudMatchSyncRequestConnectionGeneration = 0;
    std::size_t m_cloudMatchSyncComparisonPageCount = 0;
    bool m_cloudMatchSyncComparisonTruncated = false;
    bool m_cloudMatchSyncWasConnected = false;
    bool m_cloudMatchDisplayInitialized = false;
    CloudMatchDisplayState m_cloudMatchDisplayState =
        CloudMatchDisplayState::notJoined;
    CString m_cloudMatchDisplayText;
    std::string m_cloudMatchSyncComparisonRequestId;
    std::string m_cloudMatchSyncSnapshotRequestId;
    std::string m_cloudMatchSyncPreviewRequestId;
    std::string m_cloudMatchSyncComparisonCursor;
    std::string m_cloudMatchSyncComparisonToken;
    std::string m_cloudMatchSyncRequestRoomId;
    std::string m_cloudMatchSyncSelectedDeviceId;
    std::uint64_t m_cloudMatchSyncSelectedRevision = 0;
    bool m_cloudMatchSyncSelectedSwapped = false;
    nlohmann::json m_cloudMatchSyncMembers = nlohmann::json::array();
    nlohmann::json m_cloudMatchSyncGroups = nlohmann::json::array();
    nlohmann::json m_cloudMatchSyncPendingMembers = nlohmann::json::array();
    nlohmann::json m_cloudMatchSyncPendingGroups = nlohmann::json::array();
    std::string m_cloudMatchSyncConsensusDeviceId;
    nlohmann::json m_cloudMatchSyncPreview;
    nlohmann::json m_cloudMatchSyncPendingSnapshot;
    nlohmann::json m_cloudMatchSyncLocalBaseline;
    nlohmann::json m_cloudMatchSyncUndoBackup;
    nlohmann::json m_cloudMatchSyncUndoApplied;
    std::string m_cloudMatchSyncUndoAppliedHash;
    std::string m_cloudMatchSyncUndoRoomId;
    std::uint64_t m_cloudMatchSyncUndoConnectionGeneration = 0;
    std::uint64_t m_cloudMatchSyncUndoPostApplyEpoch = 0;
    int m_cloudMatchSyncUndoEventBoundaryId = 0;
    std::atomic<std::uint64_t> m_matchMutationEpoch{ 1 };
    CString m_cloudMatchSyncError;
    CString m_cloudMatchSyncLastResult;

    // Unified broadcaster pool state. Legacy room fields remain only for migration.
    nlohmann::json m_cloudBroadcasters = nlohmann::json::array();
    nlohmann::json m_cloudSyncHistory = nlohmann::json::object();
    nlohmann::json m_cloudRealtimeRelations = nlohmann::json::array();
    nlohmann::json m_cloudPreviewSnapshot = nlohmann::json::object();
    std::string m_cloudDirectoryRequestId;
    std::string m_cloudPreviewRequestId;
    std::string m_cloudSyncRecordRequestId;
    std::string m_cloudRealtimeRequestId;
    std::string m_cloudRealtimeTargetDeviceId;
    bool m_cloudRealtimeFollowing = false;
    ULONGLONG m_cloudRealtimeHeartbeatDueTick = 0;
    bool m_cloudCloudStateApplying = false;

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
    CStatic         m_cloudMatchStatus;
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
    CString m_webTheme = L"dark-esports";
    bool m_bOutputSeatLabelToKillFile = false;
    bool m_bRedPickFirst = false;

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
    std::uint64_t m_cloudAuthRequestGeneration = 0;

    // 【新增】：云端授权回调变量与消息
    long long m_keyDuration = 0;     // 存放解析出的时长
    long long m_cloudExpireTime = 0; // 存放云端返回的绝对到期时间

    // 把下面这两行覆盖原来的声明
    bool VerifyKey(CString inputKey, CString machineID);
    static CString CheckCloudBinding(CString key, CString hwid, long long duration,
        long long& outExpTime, CString& outCloudServerUrl);
    bool BeginLicenseCloudCheck(const CString& inputKey, bool manualCheck);
    bool TryActivateFromLicenseLease(const CString& normalizedKey,
        const CString& machineId, long long cardDuration);
    bool BeginLicenseLeaseEndpointRefresh();
    CString SubmitAliasDbForReview(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString DirectSyncAliasDbToCloud(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString SyncAliasDbFromCloud();
    std::string FilterAliasDbPayloadForReview(const std::string& aliasDbPayload, int& mainCount, int& pairCount, int& containedNakedAliasCount) const;
    void ResetAliasDbCloudBaseline();
    void AutoSubmitAliasDbIfDirty();
    CString SubmitAliasDbSnapshotIfDirty(bool saveBeforeBuild = true);
    void LoadAliasDbAutoSyncSettings();
    bool SaveAliasDbAutoSyncSettings() const;
    void MaybeStartAliasDbAutoSync(bool force = false);
    void StartAliasDbAutoSyncAttempt();
    bool MergePublicAliasDbForAutoSync(const nlohmann::json& players,
        CString& resultMessage);
    afx_msg LRESULT OnUpdateAuthTime(WPARAM wParam, LPARAM lParam); // 【新增消息】

    CString GetMachineID();
    std::string m_aliasDbCloudBaselinePayload;
    std::string m_aliasDbLastSubmittedPayload;
    bool m_bAliasDirectMode = false;
    std::vector<CString> m_aliasDbPendingDeleteMains;
    std::vector<CString> m_aliasCloudDeleteBaselineMains;
    std::map<CString, CString> m_aliasCloudBaselinePlayers;

    // Seven-day alias database sync is independent from match snapshot sync.
    bool m_aliasAutoSyncEnabled = true;
    bool m_aliasAutoSyncInFlight = false;
    bool m_aliasAutoSyncAttemptedThisRun = false;
    std::atomic<std::uint64_t> m_aliasAutoSyncGeneration{ 1 };
    std::int64_t m_aliasAutoSyncLastSuccessAt = 0;
    std::string m_aliasAutoSyncLastPushHash;
    CString m_aliasAutoSyncLastPushStatus = L"pending";
    CString m_aliasAutoSyncLastPushMessage;
    CString m_aliasAutoSyncLastPullStatus = L"failed";
    CString m_aliasAutoSyncLastPullMessage;
    CString m_aliasAutoSyncLastResult;
    bool m_aliasAutoSyncAppendSupported = false;
    bool m_aliasAutoSyncLastKnownAuthorized = false;
    std::shared_ptr<std::atomic<bool>> m_aliasAutoSyncLifetime;

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
    std::thread m_ocrSupervisorThread;
    std::mutex m_ocrSupervisorMutex;
    std::condition_variable m_ocrSupervisorCv;
    bool m_ocrSupervisorWake = false;
    std::atomic<bool> m_bOcrSupervisorStop{ false };
    std::atomic<bool> m_bOcrSupervisorStarted{ false };
    std::atomic<bool> m_bOcrServiceReady{ false };
    std::atomic<bool> m_bOcrEngineReady{ false };
    std::atomic<bool> m_bStartAfterOcrReady{ false };
    std::atomic<DWORD> m_ocrStartPendingSince{ 0 };
    std::atomic<DWORD> m_ocrRecoveryPendingSince{ 0 };
    std::atomic<bool> m_bOcrRecoveryResultPosted{ false };
    std::atomic<HWND> m_ocrSupervisorHwnd{ nullptr };
    HANDLE m_hOcrTrackedProcess = nullptr;
    DWORD m_ocrTrackedProcessId = 0;
    std::atomic<ULONGLONG> m_ocrProcessNotReadySince{ 0 };
    std::mutex m_ocrSupervisorRequestMutex;
    HINTERNET m_hOcrSupervisorRequest = nullptr;

    std::mutex m_ocrTaskMutex;
    std::condition_variable m_ocrTaskCv;
    std::size_t m_ocrTaskCount = 0;
    bool m_bOcrTaskStop = false;

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
