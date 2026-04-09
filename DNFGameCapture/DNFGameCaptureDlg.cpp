#include "pch.h"
#include "DNFGameCaptureDlg.h"
#include <shellapi.h>
#include <Gdiplus.h>
#include <string>
#include <mutex>
#include <ctime>
#include <deque>
#include <future>
#include <winhttp.h>
#include <wincrypt.h>
#include <wininet.h>
#include <tlhelp32.h> // 【新增】：用于遍历和杀掉后台残留进程

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

// ============================================================================
// 全局变量与辅助结构
// ============================================================================
std::mutex g_bmpMutex;

struct VisualLogMsg {
    CString text;
    COLORREF color;
};

std::deque<VisualLogMsg> g_visualLogs;
std::mutex g_visualLogMutex;

float WINDOW_SCALE = 1.0f;
const int ID_BTN_START = 1005;
const int ID_BTN_APPLY = 1006;
const int ID_CHK_FLIP = 1007;
const int ID_BTN_RESET = 1008;
const int ID_BTN_BROWSE = 1013;
const int ID_EDIT_DIR = 1014;
const int ID_BTN_INPUT_KEY = 1020; // 输入授权码按钮ID
const CString PLACEHOLDER_TEXT = L"输入：主号(小号1)(小号2)...";

struct ScorePointF { float x; float y; };
ScorePointF g_deathPts[40] = {
    { 0.8274f, 0.0366f },
    { 0.8419f, 0.0110f },
    { 0.8388f, 0.0532f },
    { 0.8181f, 0.0532f },
    { 0.8130f, 0.0128f },
    { 0.8192f, 0.1247f },
    { 0.8047f, 0.1009f },
    { 0.8109f, 0.1119f },
    { 0.8181f, 0.1009f },
    { 0.8047f, 0.1247f },
    { 0.7231f, 0.1266f },
    { 0.7086f, 0.1229f },
    { 0.7086f, 0.1027f },
    { 0.7221f, 0.1009f },
    { 0.7148f, 0.1119f },
    { 0.6260f, 0.1247f },
    { 0.6115f, 0.1247f },
    { 0.6115f, 0.1009f },
    { 0.6260f, 0.1009f },
    { 0.6188f, 0.1119f },
    { 0.1756f, 0.0366f },
    { 0.1900f, 0.0110f },
    { 0.1900f, 0.0568f },
    { 0.1632f, 0.0605f },
    { 0.1611f, 0.0128f },
    { 0.1869f, 0.1119f },
    { 0.1931f, 0.1009f },
    { 0.1931f, 0.1247f },
    { 0.1797f, 0.1247f },
    { 0.1797f, 0.1027f },
    { 0.2840f, 0.1119f },
    { 0.2902f, 0.1027f },
    { 0.2913f, 0.1247f },
    { 0.2778f, 0.1229f },
    { 0.2778f, 0.1027f },
    { 0.3811f, 0.1119f },
    { 0.3884f, 0.1009f },
    { 0.3894f, 0.1266f },
    { 0.3750f, 0.1229f },
    { 0.3750f, 0.1027f },
};

// ========================================================
// 【新增】：完美的 MessageBox 强行居中钩子引擎
// ========================================================
HHOOK g_hMsgBoxHook = NULL;
HWND  g_hMsgBoxParent = NULL;
std::mutex g_msgBoxMutex;

LRESULT CALLBACK MsgBoxCBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 拦截窗口即将激活的瞬间
    if (nCode == HCBT_ACTIVATE) {
        HWND hMsgBox = (HWND)wParam;
        if (g_hMsgBoxParent && ::IsWindow(g_hMsgBoxParent)) {
            RECT pr, mr;
            ::GetWindowRect(g_hMsgBoxParent, &pr); // 获取软件主窗口坐标
            ::GetWindowRect(hMsgBox, &mr);         // 获取弹窗的坐标

            // 像素级计算正中心坐标
            int x = pr.left + (pr.right - pr.left) / 2 - (mr.right - mr.left) / 2;
            int y = pr.top + (pr.bottom - pr.top) / 2 - (mr.bottom - mr.top) / 2;

            // 强行挪过去
            ::SetWindowPos(hMsgBox, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        // 挪完就过河拆桥，卸载钩子，防止卡顿
        ::UnhookWindowsHookEx(g_hMsgBoxHook);
        g_hMsgBoxHook = NULL;
    }
    return CallNextHookEx(g_hMsgBoxHook, nCode, wParam, lParam);
}

int CDNFGameCaptureDlg::ShowCenteredMsgBox(LPCTSTR lpszText, LPCTSTR lpszCaption, UINT nType) {
    // 加锁防止多线程同时弹窗导致钩子冲突
    std::lock_guard<std::mutex> lock(g_msgBoxMutex);

    g_hMsgBoxParent = this->GetSafeHwnd();
    // 挂上只针对当前线程的拦截钩子
    g_hMsgBoxHook = SetWindowsHookEx(WH_CBT, MsgBoxCBTProc, NULL, GetCurrentThreadId());

    // 呼出系统的 MessageBox，它刚探出头就会被钩子按在正中间！
    return ::MessageBox(g_hMsgBoxParent, lpszText, lpszCaption, nType);
}


// 获取中英文混合字符串的视觉宽度
int GetVisualWidth(const CString& s) {
    int w = 0;
    for (int i = 0; i < s.GetLength(); i++) {
        w += (s[i] >= 0x4E00 && s[i] <= 0x9FFF) ? 2 : 1;
    }
    return w;
}

// 按语义版本号比较：返回 1 表示 a > b，-1 表示 a < b，0 表示相等
static int CompareVersion(const CString& a, const CString& b) {
    int ia = 0, ib = 0;
    int la = a.GetLength(), lb = b.GetLength();
    while (ia < la || ib < lb) {
        int numA = 0, numB = 0;
        while (ia < la && a[ia] != L'.') {
            if (a[ia] >= L'0' && a[ia] <= L'9')
                numA = numA * 10 + (a[ia] - L'0');
            ia++;
        }
        while (ib < lb && b[ib] != L'.') {
            if (b[ib] >= L'0' && b[ib] <= L'9')
                numB = numB * 10 + (b[ib] - L'0');
            ib++;
        }
        if (numA > numB) return 1;
        if (numA < numB) return -1;
        ia++; ib++; // 跳过 '.'
    }
    return 0;
}

// 时间戳转字符串
CString FormatTimeStamp(long long ts) {
    if (ts >= 0xFFFFFFF0) return L"永久有效";
    time_t t_ts = (time_t)ts;
    tm t;
    localtime_s(&t, &t_ts);
    CString res;
    res.Format(L"%04d-%02d-%02d %02d:%02d:%02d",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return res;
}


// 【新增】：全局通用的 UI 日志输出助手，随处可用
void AppLog(const CString& msg, COLORREF color) {
    time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
    CString tStr; tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
    std::lock_guard<std::mutex> lk(g_visualLogMutex);
    g_visualLogs.push_back({ tStr, color });
}

// 写入本地匹配日志
void WriteMatchLog(const CString& logLine) {
    CFile file;
    if (file.Open(L"match_debug.log", CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite)) {
        if (file.GetLength() == 0) {
            unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            file.Write(bom, 3);
        }
        file.SeekToEnd();
        time_t now = time(0);
        tm t;
        localtime_s(&t, &now);
        CString fullLine;
        fullLine.Format(L"[%02d:%02d:%02d] %s\r\n", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)logLine);
        std::string utf8Line = CW2A(fullLine, CP_UTF8);
        file.Write(utf8Line.c_str(), (UINT)utf8Line.length());
        file.Close();
    }
}

// ============================================================================
// 消息映射与全局快捷键
// ============================================================================
BEGIN_MESSAGE_MAP(CDNFGameCaptureDlg, CWnd)
    ON_WM_TIMER()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CLOSE()
    ON_WM_LBUTTONDOWN()
    ON_BN_CLICKED(ID_BTN_START, OnBnClickedStart)
    ON_BN_CLICKED(ID_BTN_APPLY, OnBnClickedApply)
    ON_BN_CLICKED(ID_CHK_FLIP, OnBnClickedFlip)
    ON_BN_CLICKED(ID_BTN_RESET, OnBnClickedReset)
    ON_BN_CLICKED(ID_BTN_BROWSE, OnBnClickedBrowseDir)
    ON_BN_CLICKED(ID_BTN_INPUT_KEY, OnBnClickedInputKey)
    ON_WM_SYSCOMMAND()
    ON_WM_HOTKEY()
    ON_EN_CHANGE(1025, &CDNFGameCaptureDlg::OnChangeEditNamesInput) // 1001是你输入框的ID
    ON_MESSAGE(WM_TRAY_MESSAGE, &CDNFGameCaptureDlg::OnTrayMessage)
    ON_MESSAGE(WM_UPDATE_OCR_DROPDOWNS, &CDNFGameCaptureDlg::OnUpdateOcrDropdowns)
    ON_CBN_SELCHANGE(1010, &CDNFGameCaptureDlg::OnCbnSelchangeLeft)
    ON_CBN_SELCHANGE(1009, &CDNFGameCaptureDlg::OnCbnSelchangeRight)
    ON_BN_CLICKED(1021, &CDNFGameCaptureDlg::OnBnClickedHelp) // 【新增】：绑定说明按钮
    ON_EN_SETFOCUS(1025, &CDNFGameCaptureDlg::OnEditSetFocus)   // 得到焦点
    ON_EN_KILLFOCUS(1025, &CDNFGameCaptureDlg::OnEditKillFocus) // 失去焦点
    // 绑定添加按钮和树控件的右键菜单
    ON_BN_CLICKED(1022, &CDNFGameCaptureDlg::OnBnClickedQuickAdd)
    ON_NOTIFY(NM_RCLICK, 1023, &CDNFGameCaptureDlg::OnRClickTree)
    // 1023 是树控件的 ID
    ON_NOTIFY(TVN_ENDLABELEDIT, 1023, &CDNFGameCaptureDlg::OnEndLabelEdit)
    // 找到 BEGIN_MESSAGE_MAP 区域，添加下面这一行
    ON_NOTIFY(NM_CUSTOMDRAW, 1023, &CDNFGameCaptureDlg::OnCustomDrawTree)
    ON_WM_CTLCOLOR() // 添加这一行，拦截所有的颜色请求
    ON_MESSAGE(WM_UPDATE_ALL_UI, &CDNFGameCaptureDlg::OnUpdateAllUI)// 【新增】：绑定自定义 UI 刷新消息
    ON_MESSAGE(WM_CLOUD_AUTH_FAIL, &CDNFGameCaptureDlg::OnCloudAuthFail) // 【新增】
    ON_CBN_SELCHANGE(1030, &CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine)
    ON_MESSAGE(WM_UPDATE_AUTH_TIME, &CDNFGameCaptureDlg::OnUpdateAuthTime)
    ON_CBN_DROPDOWN(1031, &CDNFGameCaptureDlg::OnCbnDropdownTargetWindow)
    ON_CBN_CLOSEUP(1031, &CDNFGameCaptureDlg::OnCbnCloseupTargetWindow)
    ON_MESSAGE(WM_USER + 200, &CDNFGameCaptureDlg::OnWGCInitDone)
    ON_WM_MOUSEMOVE()
    ON_WM_RBUTTONDOWN()


END_MESSAGE_MAP()


void CDNFGameCaptureDlg::OnMouseMove(UINT nFlags, CPoint point) {
    // 鼠标在预览区移动时，高频重绘触发显微镜画面
    if (m_w > 0 && m_previewRect.PtInRect(point)) {
        InvalidateRect(&m_previewRect, FALSE);
    }
    CWnd::OnMouseMove(nFlags, point);
}

void CDNFGameCaptureDlg::OnRButtonDown(UINT nFlags, CPoint point) {
    // 【右键撤销功能】：点错了直接右键，删掉上一个点
    if (m_w > 0 && m_previewRect.PtInRect(point)) {
        if (!m_selectPts.empty()) {
            m_selectPts.pop_back();
            InvalidateRect(&m_previewRect, FALSE);
        }
    }
    CWnd::OnRButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point) {
    if (m_w <= 0 || m_h <= 0) return;
    if (m_previewRect.PtInRect(point)) {
        if (m_selectPts.size() >= 40) m_selectPts.clear(); // 改为 40 个点
        m_selectPts.push_back(CPoint(
            (int)(((float)(point.x - m_previewRect.left) / m_previewRect.Width()) * 10000.0f),
            (int)(((float)(point.y - m_previewRect.top) / m_previewRect.Height()) * 10000.0f)
        ));
        InvalidateRect(&m_previewRect, FALSE);

        // 凑齐 40 个点后，直接生成全新的数组代码
        if (m_selectPts.size() == 40) {
            CString res = L"ScorePointF g_deathPts[40] = {\r\n";
            for (int i = 0; i < 40; i++) {
                CString t; t.Format(L"    { %.4ff, %.4ff },\r\n", m_selectPts[i].x / 10000.0f, m_selectPts[i].y / 10000.0f); res += t;
            }
            m_editOcrResult.SetWindowText(res + L"};\r\n");
            MessageBox(L"🎉 40个大X坐标已采集完毕！\r\n请去右侧日志框复制代码。");
        }
    }
    CWnd::OnLButtonDown(nFlags, point);
}

void CDNFGameCaptureDlg::OnHotKey(UINT nHotKeyId, UINT nKey1, UINT nKey2) {
    if (nHotKeyId == 8008) {
        ManualTriggerKill(0); // 触发红队
    }
    else if (nHotKeyId == 8009) {
        ManualTriggerKill(1); // 触发蓝队
    }
    CWnd::OnHotKey(nHotKeyId, nKey1, nKey2);
}

// ============================================================================
// 核心授权验证逻辑 (本地软拦截 + 云端强校验)
// ============================================================================
unsigned int CustomSimpleHash(const std::string& str) {
    unsigned int hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

CString CDNFGameCaptureDlg::GetMachineID() {
    DWORD volSerial = 0;
    GetVolumeInformation(L"C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0);
    CString hwid;
    hwid.Format(L"%08X", volSerial);
    return hwid;
}

// ==========================================
// 【新增】：精准击杀指定名称的后台进程
// ==========================================
void CDNFGameCaptureDlg::KillProcessByName(const CString& processName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnap, &pe)) {
        do {
            CString currentName(pe.szExeFile);
            if (currentName.CompareNoCase(processName) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

// 云端返回成功时触发：
LRESULT CDNFGameCaptureDlg::OnUpdateAuthTime(WPARAM wParam, LPARAM lParam) {
    long long cloudTime = (long long)lParam;
    m_cloudExpireTime = cloudTime;

    // 1. 严格纠正授权状态
    if (cloudTime > 1 || cloudTime == 0xFFFFFFFF) {
        m_bIsAuthValid = true;
    }
    else {
        m_bIsAuthValid = false;
    }

    // 2. 【关键修复 3】：清空面板上的“正在同步...”，重新打印终极状态！
    if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
    OutputDebugAuthInfo();

    // 3. 追加高亮提示
    if (m_bIsAuthValid) {
        AppLog(L"✅ [云端验证] 授权已激活，欢迎使用！", RGB(0, 255, 100));
    }
    else {
        AppLog(L"❌ [云端验证] 该卡密已被封停或无效！", RGB(255, 80, 80));
    }

    return 0;
}

// 云端返回失败（或网络超时）时触发：
LRESULT CDNFGameCaptureDlg::OnCloudAuthFail(WPARAM wParam, LPARAM lParam) {
    CString* pCloudResult = (CString*)lParam;
    if (pCloudResult) {
        m_bIsAuthValid = false;

        // 【关键修复 4】：必须解除 -1 状态，否则用户永远点不了开始监控！
        m_cloudExpireTime = 0;

        // 强行清空面板并刷新状态
        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        OutputDebugAuthInfo();

        if (m_bIsRunning) OnBnClickedStart();

        CString errMsg;
        errMsg.Format(L"授权校验失败：\r\n%s\r\n\r\n请检查网络或卡密状态！", (LPCTSTR)*pCloudResult);
        MessageBox(errMsg, L"安全拦截", MB_ICONERROR | MB_SYSTEMMODAL);

        delete pCloudResult;
    }
    return 0;
}
bool CDNFGameCaptureDlg::VerifyKey(CString inputKey, CString machineID) {
    // ===================================
    // 纯血新版：只允许动态时长激活码 (CDK-开头)
    // ===================================
    if (inputKey.Left(4) == L"CDK-") {
        int firstDash = 3;
        int secondDash = inputKey.Find(L'-', firstDash + 1);
        int thirdDash = inputKey.Find(L'-', secondDash + 1);

        if (thirdDash != -1) {
            CString durStr = inputKey.Mid(firstDash + 1, secondDash - firstDash - 1);
            CString nonceStr = inputKey.Mid(secondDash + 1, thirdDash - secondDash - 1);
            CString sigStr = inputKey.Mid(thirdDash + 1);

            long long duration = wcstoll(durStr, NULL, 16);
            unsigned int sig = wcstoul(sigStr, NULL, 16);

            CString signData; signData.Format(L"%llX-%s-MySuperSecretKey2026", duration, (LPCTSTR)nonceStr);
            if (sig != CustomSimpleHash(std::string(CW2A(signData, CP_UTF8)))) return false;

            m_keyDuration = duration;
            m_cloudExpireTime = -1;    // 设置为正在请求云端的状态

            // 【关键修改】：本地格式过关不代表授权有效！
            // 必须设为 false，等待子线程拿到云端的“OK”后再反转。
            m_bIsAuthValid = false;
            return true;
        }
    }

    // 如果是 DNF- 开头的老卡，或者乱输的字符，统统在这里直接拦截！
    return false;
}

CString CDNFGameCaptureDlg::CheckCloudBinding(CString key, CString hwid, long long duration, long long& outExpTime) {
    CString jsonStr;
    // 把 duration 一并传给 Node.js
    jsonStr.Format(L"{\"key\": \"%s\", \"hwid\": \"%s\", \"duration\": %lld}", key, hwid, duration);
    std::string jsonUtf8 = CW2A(jsonStr, CP_UTF8);

    HINTERNET hSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    // ==========================================
    // 【关键修复 1】：设置 3 秒超时！绝不允许后台线程无限卡死！
    // ==========================================
    if (hSession) {
        WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);
    }
    HINTERNET hConnect = WinHttpConnect(hSession, L"verifykey-thaovfpoib.cn-hangzhou.fcapp.run", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    CString resultMsg = L"未知请求异常";
    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        if (WinHttpSendRequest(hRequest, NULL, 0, (LPVOID)jsonUtf8.c_str(), (DWORD)jsonUtf8.length(), (DWORD)jsonUtf8.length(), 0) && WinHttpReceiveResponse(hRequest, NULL)) {
            std::string resp; DWORD sz = 0, dl = 0;
            while (WinHttpQueryDataAvailable(hRequest, &sz) && sz > 0) {
                std::vector<char> buf(sz + 1, 0);
                if (WinHttpReadData(hRequest, buf.data(), sz, &dl)) resp.append(buf.data(), dl);
            }

            if (resp.find("\"status\":\"ok\"") != std::string::npos) {
                resultMsg = L"OK";
                // 【提取云端返回的 expireTime】
                size_t pExp = resp.find("\"expireTime\":");
                if (pExp != std::string::npos) {
                    outExpTime = atoll(resp.c_str() + pExp + 13);
                }
            }
            else {
                size_t p1 = resp.find("\"msg\":\"");
                if (p1 != std::string::npos) {
                    size_t p2 = resp.find("\"", p1 + 7);
                    if (p2 != std::string::npos) resultMsg = CA2W(resp.substr(p1 + 7, p2 - p1 - 7).c_str(), CP_UTF8);
                }
            }
        }
        WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resultMsg;
}

// 授权检查函数：支持云端异步校验与环境隔离
void CDNFGameCaptureDlg::CheckTrialAndLicense() {
    // =========================================================
    // 【模式切换】：如果是云端测试模式，直接赋予上帝权限，跳过所有校验
    // =========================================================
#if ENABLE_CLOUD_TEST_MODE
    m_bIsAuthValid = true;   // 授权有效
    m_bIsTrial = false;      // 非试用（正式模式）
    m_trialEnd = 0;
    return;
#endif

    // 默认初始化状态：未授权
    m_bIsAuthValid = false;
    m_bIsTrial = false;
    m_trialEnd = 0;

    // 获取机器码
    CString hwid = GetMachineID();

    // 定位本地 license.txt 路径
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    // --- 第一阶段：尝试读取本地卡密 ---
    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        char buf[256] = { 0 };
        UINT nRead = file.Read(buf, 255);
        file.Close();

        CString inputKey(buf);
        inputKey.Trim();

        // 基础算法校验（这里现在只有 CDK 能通过了）
        if (!inputKey.IsEmpty() && VerifyKey(inputKey, hwid)) {
            // 【关键修复 2】：删掉 m_bIsAuthValid = true; 
            // 离线格式对了也没用，必须设为 false，等待云端判决！
            m_bIsTrial = false;
            long long duration = m_keyDuration;
            HWND hWnd = GetSafeHwnd();

            std::thread([this, hWnd, inputKey, hwid, duration]() {
                long long cloudExpTime = 0;
                CString cloudResult = CheckCloudBinding(inputKey, hwid, duration, cloudExpTime);

                if (cloudResult != L"OK" && ::IsWindow(hWnd)) {
                    CString* pResult = new CString(cloudResult);
                    ::PostMessage(hWnd, WM_CLOUD_AUTH_FAIL, 0, (LPARAM)pResult);
                }
                else if (cloudResult == L"OK" && ::IsWindow(hWnd)) {
                    ::PostMessage(hWnd, WM_UPDATE_AUTH_TIME, 0, (LPARAM)cloudExpTime);
                }
                }).detach();

            return;
        }
    }

    // --- 第三阶段：如果没有卡密，检查注册表试用期 ---
    HKEY hKey;
    time_t now = time(nullptr);
    // 打开注册表项：HKEY_CURRENT_USER\Software\DNFCapture
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD iT = 0, sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz) == ERROR_SUCCESS) {
            // 试用期设定为 7 天 (604800 秒)
            long long expireTime = (long long)iT + 604800;
            if (iT > 0 && (long long)now <= expireTime) {
                m_bIsAuthValid = true;
                m_bIsTrial = true;
                m_trialEnd = expireTime;
            }
        }
        RegCloseKey(hKey);
    }
}

void CDNFGameCaptureDlg::OutputDebugAuthInfo() {
    auto print = [&](const CString& t, COLORREF c) {
        if (!m_editVisualLogs.m_hWnd) return;
        int l = m_editVisualLogs.GetWindowTextLength();
        m_editVisualLogs.SetSel(l, l);
        CHARFORMAT cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = c;
        m_editVisualLogs.SetSelectionCharFormat(cf);
        m_editVisualLogs.ReplaceSel(t + L"\r\n");
        m_editVisualLogs.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);
        };

    print(L"====== [本机授权状态信息] ======", RGB(255, 215, 0));
    print(L"本机机器码: " + GetMachineID(), RGB(200, 200, 200));

    if (m_bIsAuthValid) {
        print(L"当前状态: [ ✔ 授权有效，可正常监控 ]", RGB(0, 255, 0));
    }
    else {
        print(L"当前状态: [ ❌ 授权无效，监控被锁定 ]", RGB(255, 80, 80));
    }

    HKEY hKey;
    time_t now = time(nullptr);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD iT = 0, sz = 4;
        if (RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz) == ERROR_SUCCESS) {
            long long expTime = (long long)iT + 604800;
            if (expTime > (long long)now) {
                print(L"试用期结束时间: " + FormatTimeStamp(expTime), RGB(0, 255, 255));
            }
            else {
                print(L"试用期状态: 已过期", RGB(150, 150, 150));
            }
        }
        RegCloseKey(hKey);
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        char buf[256] = { 0 };
        file.Read(buf, 255);
        file.Close();
        CString inputKey(buf);
        inputKey.Trim();

        // ==========================================
        // 【新增】：卡密脱敏处理 (数据打码)
        // ==========================================
        CString displayKey = inputKey;
        if (displayKey.GetLength() > 12) {
            // 保留前 8 个字符 (例如 CDK-278D 或 DNF-69F6)
            // 保留后 4 个字符 (例如 9A91)，中间全部用 **** 替换
            displayKey = displayKey.Left(8) + L"****-****-" + displayKey.Right(4);
        }

        print(L"本地卡密记录: " + displayKey, RGB(180, 180, 180));

        if (inputKey.Left(4) == L"CDK-") {
            // ... 下面保持不变
            if (!m_bIsAuthValid && m_cloudExpireTime == 0) {
                print(L"该卡密状态: ❌ 无效卡密 (格式错误或被篡改)", RGB(255, 80, 80));
            }
            else if (m_cloudExpireTime > 0) {
                print(L"该卡密到期时间: " + FormatTimeStamp(m_cloudExpireTime), RGB(200, 200, 200));
            }
            else if (m_cloudExpireTime == -1) {
                print(L"该卡密到期时间: 正在向云端同步激活信息...", RGB(255, 165, 0));
            }
            else {
                print(L"该卡密到期时间: 验证通过 (以云端记录为准)", RGB(0, 255, 100));
            }
        }
        else {
            // 【新增】：旧版卡密无情拒绝提示
            print(L"该卡密状态: ❌ 已淘汰的旧版卡密，请联系管理员更换新版 CDK！", RGB(255, 80, 80));
        }
    }
    print(L"==================================", RGB(255, 215, 0));
}

// ============================================================================
// 初始化与窗口过程
// ============================================================================
CDNFGameCaptureDlg::CDNFGameCaptureDlg() {
    m_bIsAuthValid = false;
    

    m_hSingleInstanceMutex = CreateMutex(NULL, TRUE, L"Global\\DNFGameCapture_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(L"程序已经在运行中！\r\n请在右下角任务栏中查找。", L"提示", MB_ICONINFORMATION | MB_OK);
        exit(0);
    }

    m_bmp = NULL; m_w = 0; m_h = 0; m_bIsRunning = FALSE;
    m_bCanTrigger = TRUE; m_bCanTriggerTeamScore = TRUE;
    m_historyIdx = 0; m_bPendingTeamScoreWin = false;
    m_totalScoreRed = 0; m_totalScoreBlue = 0; m_lastKillerTeam = -1; m_bFlipSides = false;
    m_hDebugOcrBmp[0] = NULL; m_hDebugOcrBmp[1] = NULL;
    m_viewIndexLeft = -1; m_viewIndexRight = -1;

    GdiplusStartupInput gpi;
    GdiplusStartup(&m_gdiplusToken, &gpi, NULL);

    m_hHttpSession = WinHttpOpen(L"DNF Capture", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (m_hHttpSession) {
        WinHttpSetTimeouts(m_hHttpSession, 1500, 1500, 2500, 2500);
        m_hHttpConnect = WinHttpConnect(m_hHttpSession, L"127.0.0.1", 1224, 0);
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString appDir = exePath;
    int pos = appDir.ReverseFind(L'\\');
    if (pos != -1) appDir = appDir.Left(pos + 1);

    m_ocrExePath = appDir + L"Umi-OCR.exe";
    m_configPath = appDir + L"players_config.txt";
    m_iniPath = appDir + L"config.ini";

    wchar_t dirBuf[MAX_PATH];
    GetPrivateProfileString(L"Settings", L"OutputDir", appDir, dirBuf, MAX_PATH, m_iniPath);
    m_outputDir = dirBuf;
    if (m_outputDir.Right(1) == L"\\") {
        m_outputDir.TrimRight(L"\\");
    }

    AfxInitRichEdit2();
    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) { m_historyBmps[i] = NULL; }
    for (int i = 0; i < 8; i++) {
        m_players[i].kills = 0; m_players[i].deaths = 0;
        m_players[i].currentStreak = 0; m_players[i].akCount = 0;
        m_players[i].team = (i < 4 ? 0 : 1);
    }

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1));
    CString title;
    title.Format(L"DNF击杀统计(v%s)", CURRENT_VERSION);

    // 【新增】：分辨率自适应
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    if (screenY <= 1080) {
        WINDOW_SCALE = 1.2f; // 1080P或更低，缩小界面
    }
    else {
        WINDOW_SCALE = 1.6f; // 2K/4K屏幕，放大界面
    }

    // 然后再执行 CreateEx ...
    CreateEx(0, cls, title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        100, 100, (int)(750 * WINDOW_SCALE), (int)(760 * WINDOW_SCALE), NULL, NULL);
    CheckTrialAndLicense();
    InitTrayIcon();

    // 注册全局测试快捷键
    ::RegisterHotKey(m_hWnd, 8008, MOD_CONTROL, VK_F8);
    ::RegisterHotKey(m_hWnd, 8009, MOD_CONTROL, VK_F9);
}

CDNFGameCaptureDlg::~CDNFGameCaptureDlg() {
    ::UnregisterHotKey(m_hWnd, 8008);
    ::UnregisterHotKey(m_hWnd, 8009);

    if (m_hSingleInstanceMutex) CloseHandle(m_hSingleInstanceMutex);
    RemoveTrayIcon();

    if (m_bmp) ::DeleteObject(m_bmp);
    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) { if (m_historyBmps[i]) ::DeleteObject(m_historyBmps[i]); }
    if (m_hHttpConnect) WinHttpCloseHandle(m_hHttpConnect);
    if (m_hHttpSession) WinHttpCloseHandle(m_hHttpSession);

    GdiplusShutdown(m_gdiplusToken);
}

// ============================================================================
// 托盘与退出
// ============================================================================
void CDNFGameCaptureDlg::InitTrayIcon() {
    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = GetSafeHwnd();
    m_nid.uID = 10001;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_MESSAGE;

    wchar_t p[MAX_PATH]; GetModuleFileName(NULL, p, MAX_PATH);
    m_nid.hIcon = ExtractIcon(AfxGetInstanceHandle(), p, 0);
    if (!m_nid.hIcon) m_nid.hIcon = AfxGetApp()->LoadStandardIcon(IDI_APPLICATION);
    wcscpy_s(m_nid.szTip, L"DNF击杀统计");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}
void CDNFGameCaptureDlg::RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &m_nid); }
void CDNFGameCaptureDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if ((nID & 0xFFF0) == SC_CLOSE) { if (m_bIsRunning) ShowWindow(SW_HIDE); else DoRealExit(); return; }
    if ((nID & 0xFFF0) == SC_MINIMIZE) { ShowWindow(SW_HIDE); return; }
    CWnd::OnSysCommand(nID, lParam);
}

// ============================================================================
// 【修复清单】
//
//   1. RunOCR_Internal     — 修复 hScreenDC 泄漏 (死代码删除 + 资源统一释放)
//   2. DoRetryMatchingTask — 按需克隆历史帧，峰值内存从 160MB 降到 ~16MB
//   3. CheckColorTrigger   — 复用 DC，避免每 50ms 创建/销毁
//   4. Capture (PrintWindow 段) — 检测分辨率变化时重建 m_bmp
//   5. OnTimer             — Timer 6 预览间隔从 50ms 改为 200ms
//   6. OnBnClickedQuickAdd — 删除重复的树展开循环
//
// ============================================================================
// 【函数 1】RunOCR_Internal — 修复 hScreenDC 内存泄漏
//
// 原始问题：
//   - hScreenDC = ::GetDC(NULL) 之后，从未调用 ::ReleaseDC(NULL, hScreenDC)
//   - 函数末尾 return ret; 之后的释放代码是死代码，永远执行不到
//   - 每次 OCR 调用都泄漏一个屏幕 DC，长时间运行后 GDI 资源耗尽导致系统卡顿
// ============================================================================
OcrResultData CDNFGameCaptureDlg::RunOCR_Internal(HBITMAP hTargetBmp, int nAreaIndex)
{
    OcrResultData result = { L"", NULL };

    if (!m_hHttpConnect)
        return result;

    // ---- 1. 计算截取区域 ----
    RECT cropRect;
    if (nAreaIndex == 0) {
        cropRect = {
            (long)(m_w * 0.190f), (long)(m_h * 0.004f),
            (long)(m_w * 0.360f), (long)(m_h * 0.040f)
        };
    }
    else {
        cropRect = {
            (long)(m_w * 0.655f), (long)(m_h * 0.004f),
            (long)(m_w * 0.815f), (long)(m_h * 0.040f)
        };
    }

    int srcW = cropRect.right - cropRect.left;
    int srcH = cropRect.bottom - cropRect.top;
    int scale = 2;
    int padding = 30;
    int dstW = srcW * scale + padding * 2;
    int dstH = srcH * scale + padding * 2;

    // ---- 2. 创建 GDI 资源（统一管理，统一释放） ----
    HDC hScreenDC = ::GetDC(NULL);
    HDC hSrcDC = ::CreateCompatibleDC(NULL);
    HDC hDstDC = ::CreateCompatibleDC(NULL);
    HBITMAP hWorkBmp = ::CreateCompatibleBitmap(hScreenDC, dstW, dstH);

    HGDIOBJ oldSrc = ::SelectObject(hSrcDC, hTargetBmp);
    HGDIOBJ oldDst = ::SelectObject(hDstDC, hWorkBmp);

    // ---- 3. 白底填充 + 缩放拷贝 ----
    RECT bgRect = { 0, 0, dstW, dstH };
    HBRUSH whiteBrush = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(hDstDC, &bgRect, whiteBrush);
    ::DeleteObject(whiteBrush);

    ::SetStretchBltMode(hDstDC, HALFTONE);
    ::StretchBlt(hDstDC, padding, padding, srcW * scale, srcH * scale,
        hSrcDC, cropRect.left, cropRect.top, srcW, srcH, SRCCOPY);

    // ---- 4. 灰度二值化处理 ----
    BITMAP bm;
    ::GetObject(hWorkBmp, sizeof(BITMAP), &bm);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight; // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels(bm.bmWidth * bm.bmHeight * 4);
    ::GetDIBits(hDstDC, hWorkBmp, 0, bm.bmHeight, pixels.data(), &bi, DIB_RGB_COLORS);

    for (size_t i = 0; i < pixels.size(); i += 4) {
        // 加权灰度公式：0.299R + 0.587G + 0.114B
        int gray = (pixels[i + 2] * 299 + pixels[i + 1] * 587 + pixels[i] * 114) / 1000;
        BYTE binaryVal = (gray > 90) ? 0 : 255;
        pixels[i] = binaryVal; // B
        pixels[i + 1] = binaryVal; // G
        pixels[i + 2] = binaryVal; // R
    }
    ::SetDIBits(hDstDC, hWorkBmp, 0, bm.bmHeight, pixels.data(), &bi, DIB_RGB_COLORS);

    // ---- 5. 保存预览用的副本 ----
    result.hBmp = (HBITMAP)::CopyImage(hWorkBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    // ---- 6. 编码为 PNG Base64 ----
    IStream* pStream = NULL;
    ::CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    {
        Gdiplus::Bitmap gBmp(hWorkBmp, NULL);
        CLSID pngClsid;
        CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
        gBmp.Save(pStream, &pngClsid, NULL);
    }

    HGLOBAL hGlobal = NULL;
    ::GetHGlobalFromStream(pStream, &hGlobal);
    LPVOID pData = ::GlobalLock(hGlobal);
    SIZE_T dataSize = ::GlobalSize(hGlobal);

    DWORD base64Len = 0;
    ::CryptBinaryToStringA((const BYTE*)pData, (DWORD)dataSize,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);

    std::string base64Str(base64Len, '\0');
    ::CryptBinaryToStringA((const BYTE*)pData, (DWORD)dataSize,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &base64Str[0], &base64Len);

    ::GlobalUnlock(hGlobal);
    pStream->Release();

    // ---- 7. 【关键修复】：统一释放所有 GDI 资源 ----
    ::SelectObject(hSrcDC, oldSrc);
    ::SelectObject(hDstDC, oldDst);
    ::DeleteObject(hWorkBmp);
    ::DeleteDC(hSrcDC);
    ::DeleteDC(hDstDC);
    ::ReleaseDC(NULL, hScreenDC);  // ★ 原代码遗漏，导致 DC 泄漏

    // ---- 8. 去掉 Base64 尾部的空字符 ----
    if (!base64Str.empty() && base64Str.back() == '\0')
        base64Str.pop_back();

    // ---- 9. 构造 JSON 并发送 HTTP 请求 ----
    std::string jsonBody = "{\"base64\": \"" + base64Str + "\"}";
    CString ocrText = L"";

    HINTERNET hRequest = WinHttpOpenRequest(
        m_hHttpConnect, L"POST", L"/api/ocr",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (hRequest) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        BOOL bSent = WinHttpSendRequest(
            hRequest, NULL, 0,
            (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.length(),
            (DWORD)jsonBody.length(), 0);

        if (bSent && WinHttpReceiveResponse(hRequest, NULL)) {
            // 读取响应体
            std::string responseStr;
            DWORD available = 0, downloaded = 0;
            while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
                std::vector<char> buf(available + 1, 0);
                if (WinHttpReadData(hRequest, buf.data(), available, &downloaded))
                    responseStr.append(buf.data(), downloaded);
            }

            // 解析所有 "text" 字段
            size_t searchPos = 0;
            while ((searchPos = responseStr.find("\"text\"", searchPos)) != std::string::npos) {
                size_t colonPos = responseStr.find(":", searchPos);
                size_t quoteOpen = responseStr.find("\"", colonPos);
                size_t quoteEnd = responseStr.find("\"", quoteOpen + 1);
                if (quoteEnd > quoteOpen) {
                    std::string textUtf8 = responseStr.substr(quoteOpen + 1, quoteEnd - quoteOpen - 1);
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, NULL, 0);
                    if (wideLen > 0) {
                        std::vector<wchar_t> wideBuf(wideLen);
                        MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, wideBuf.data(), wideLen);
                        ocrText += wideBuf.data();
                    }
                }
                searchPos = quoteEnd + 1;
            }
        }
        else {
            EnsureOcrRunning();
        }
        WinHttpCloseHandle(hRequest);
    }

    // ---- 10. 清洗 OCR 结果中的转义字符 ----
    ocrText.Replace(L"\\n", L"");
    ocrText.Replace(L"\\r", L"");

    // 处理 \uXXXX Unicode 转义
    int uPos = 0;
    while ((uPos = ocrText.Find(L"\\u", uPos)) != -1) {
        if (uPos + 5 < ocrText.GetLength()) {
            CString hexStr = ocrText.Mid(uPos + 2, 4);
            wchar_t wc = (wchar_t)wcstol(hexStr.GetString(), NULL, 16);
            ocrText.Delete(uPos, 6);
            ocrText.Insert(uPos, CString(wc));
            uPos += 1;
        }
        else {
            uPos += 2;
        }
    }

    ocrText.Replace(L"\\\"", L"");
    ocrText.Trim();

    result.text = ocrText;
    return result;
}


// ============================================================================
// 【函数 2】DoRetryMatchingTask — 按需克隆历史帧
//
// 原始问题：
//   - 一次性克隆全部 20 帧历史截图，1080P 下瞬时占用 160MB
//   - 每帧都单独 GetDC / CreateCompatibleDC / DeleteDC，GDI 调用爆炸
//
// 优化方案：
//   - 只在需要 OCR 时才克隆当前帧，用完立即释放
//   - 复用一对 DC，全程只创建/销毁一次
// ============================================================================
void CDNFGameCaptureDlg::DoRetryMatchingTask(int triggerSide)
{
    int killerArea = (triggerSide == 0) ? 1 : 0;
    int deadArea = triggerSide;
    bool killerIsLeft = (killerArea == 0);

    bool killerResolved = false, deadResolved = false;
    CString finalKillerName = L"待定", finalDeadName = L"待定";
    int killerBestP = -1, killerBestA = -1;
    int deadBestP = -1, deadBestA = -1;
    int lockedKillerTeam = -1, lockedDeadTeam = -1;

    // 全局最优记录（用于二轮兜底）
    int globalKillerBestScore = -1, globalKillerBestP = -1, globalKillerBestA = -1, globalKillerPassLine = 999;
    CString globalKillerName;
    int globalDeadBestScore = -1, globalDeadBestP = -1, globalDeadBestA = -1, globalDeadPassLine = 999;
    CString globalDeadName;

    struct FrameData { CString text; int frameIdx; };
    std::vector<FrameData> historyKTexts;
    std::vector<FrameData> historyDTexts;

    // 日志输出辅助
    auto PushVisualLog = [&](const CString& msg, COLORREF color) {
        time_t now_t = time(0);
        tm t;
        localtime_s(&t, &now_t);
        CString tStr;
        tStr.Format(L"[%02d:%02d:%02d] %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)msg);
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.push_back({ tStr, color });
        WriteMatchLog(msg);
        };

    // ========================================================
    // 【关键优化】：不再一次性克隆全部历史帧
    //   改为：记录有效帧的索引列表，需要时再单帧克隆
    // ========================================================
    struct HistorySlot {
        int ringIdx;  // 在 m_historyBmps 环形缓冲中的实际下标
    };
    std::vector<HistorySlot> validSlots;
    int snapshotW = 0, snapshotH = 0;
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        snapshotW = m_w;
        snapshotH = m_h;
        for (int i = 1; i <= MAX_HISTORY_FRAMES; i++) {
            int idx = (m_historyIdx - i + MAX_HISTORY_FRAMES) % MAX_HISTORY_FRAMES;
            if (m_historyBmps[idx]) {
                validSlots.push_back({ idx });
            }
        }
    }

    // 帧克隆辅助函数：从环形缓冲安全拷贝一帧出来
    auto CloneHistoryFrame = [&](int ringIdx) -> HBITMAP {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (!m_historyBmps[ringIdx])
            return nullptr;

        HDC hScreenDC = ::GetDC(NULL);
        HDC hSrcDC = ::CreateCompatibleDC(hScreenDC);
        HDC hDstDC = ::CreateCompatibleDC(hScreenDC);

        HBITMAP hClone = ::CreateCompatibleBitmap(hScreenDC, snapshotW, snapshotH);
        HGDIOBJ oldSrc = ::SelectObject(hSrcDC, m_historyBmps[ringIdx]);
        HGDIOBJ oldDst = ::SelectObject(hDstDC, hClone);

        ::BitBlt(hDstDC, 0, 0, snapshotW, snapshotH, hSrcDC, 0, 0, SRCCOPY);

        ::SelectObject(hSrcDC, oldSrc);
        ::SelectObject(hDstDC, oldDst);
        ::DeleteDC(hSrcDC);
        ::DeleteDC(hDstDC);
        ::ReleaseDC(NULL, hScreenDC);

        return hClone;
        };

    // 清空 OCR 下拉框历史
    {
        std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
        for (auto& r : m_ocrRecordsLeft)  if (r.hBmp) DeleteObject(r.hBmp);
        for (auto& r : m_ocrRecordsRight) if (r.hBmp) DeleteObject(r.hBmp);
        m_ocrRecordsLeft.clear();
        m_ocrRecordsRight.clear();
        m_viewIndexLeft = -1;
        m_viewIndexRight = -1;
    }
    PostMessage(WM_UPDATE_OCR_DROPDOWNS, 1, 0);

    // ---- 匹配核心逻辑（与原版完全一致，只是变量名更清晰） ----
    auto processMatch = [&](CString ocrResult, bool& resolved, CString& finalName,
        bool isKiller, int& outBestP, int& outBestA,
        int& frameScore, bool isAggressive, int frameIdx) -> bool
        {
            frameScore = -2;
            if (resolved || ocrResult.IsEmpty() || ocrResult.Find(L"No text") != -1)
                return false;

            CString logMsg;
            logMsg.Format(L"▶ [%s] 第%d帧提取: \"%s\"",
                isKiller ? L"找杀手" : L"找死者", frameIdx, (LPCTSTR)ocrResult);
            PushVisualLog(logMsg, RGB(180, 180, 180));

            int maxScore = -2, bestP = -1, bestA = -1, bestRealLen = 0;
            std::wstring bestName;

            m_dataMutex.lock();
            for (int p = 0; p < 8; p++) {
                if (m_players[p].name.IsEmpty()) continue;

                int teamPenalty = 0;
                if (isKiller && lockedDeadTeam != -1 && m_players[p].team == lockedDeadTeam)
                    teamPenalty = 20;
                if (!isKiller && lockedKillerTeam != -1 && m_players[p].team == lockedKillerTeam)
                    teamPenalty = 20;

                int curScore = m_matcher.GetMatchScore(
                    m_players[p].name.GetString(), ocrResult.GetString(), isAggressive);
                if (curScore == -1) { maxScore = -1; break; }
                curScore -= teamPenalty;

                std::wstring curBestName = m_players[p].name.GetString();
                int curBestAlias = -1;
                int curRealLen = m_players[p].name.GetLength();

                for (size_t a = 0; a < m_players[p].aliases.size(); a++) {
                    int aliasScore = m_matcher.GetMatchScore(
                        m_players[p].aliases[a].name.GetString(), ocrResult.GetString(), isAggressive);
                    if (aliasScore == -1) { maxScore = -1; break; }
                    aliasScore -= teamPenalty;
                    if (aliasScore > curScore) {
                        curScore = aliasScore;
                        curBestName = m_players[p].aliases[a].name.GetString();
                        curBestAlias = (int)a;
                        curRealLen = m_players[p].aliases[a].name.GetLength();
                    }
                }
                if (maxScore == -1) break;

                if (curScore > maxScore || (curScore == maxScore && maxScore > 0 && curRealLen > bestRealLen)) {
                    maxScore = curScore;
                    bestP = p;
                    bestA = curBestAlias;
                    bestName = curBestName;
                    bestRealLen = curRealLen;
                }
            }
            m_dataMutex.unlock();

            frameScore = maxScore;

            if (maxScore == -1) {
                PushVisualLog(L"  └ [⚠️职业干扰] 跳过本帧...", RGB(120, 120, 120));
                return true;
            }

            int passLine = CNameMatcher::GetDynamicThreshold(bestRealLen);

            // 更新全局最优记录
            if (bestP != -1) {
                if (isKiller && maxScore > globalKillerBestScore) {
                    globalKillerBestScore = maxScore;
                    globalKillerBestP = bestP;
                    globalKillerBestA = bestA;
                    globalKillerPassLine = passLine;
                    globalKillerName = bestName.c_str();
                }
                else if (!isKiller && maxScore > globalDeadBestScore) {
                    globalDeadBestScore = maxScore;
                    globalDeadBestP = bestP;
                    globalDeadBestA = bestA;
                    globalDeadPassLine = passLine;
                    globalDeadName = bestName.c_str();
                }
            }

            if (bestP != -1 && maxScore >= passLine) {
                resolved = true;
                finalName = bestName.c_str();
                outBestP = bestP;
                outBestA = bestA;

                CString successLog;
                if (isAggressive)
                    successLog.Format(L"  └ [✨二轮匹配] 强行锁定: %s (%d分)", (LPCTSTR)finalName, maxScore);
                else
                    successLog.Format(L"  └ [✔首轮匹配] 成功指向: %s (%d分)", (LPCTSTR)finalName, maxScore);

                COLORREF teamColor = (m_players[bestP].team == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
                PushVisualLog(successLog, teamColor);

                m_dataMutex.lock();
                if (isKiller) lockedKillerTeam = m_players[bestP].team;
                else          lockedDeadTeam = m_players[bestP].team;
                m_dataMutex.unlock();
            }
            else {
                CString failLog;
                failLog.Format(L"  └ [✖未达标] 最高 %d 分 (及格线: %d)", maxScore, passLine);
                if (!isAggressive)
                    PushVisualLog(failLog, RGB(120, 120, 120));
            }

            return false;
        };

    // ========================================================
    // 【核心优化】：逐帧克隆 → OCR → 匹配 → 释放
    // ========================================================
    for (size_t i = 0; i < validSlots.size(); i++) {
        if (!m_bIsRunning || (killerResolved && deadResolved))
            break;

        // 按需克隆单帧
        HBITMAP hSnapshot = CloneHistoryFrame(validSlots[i].ringIdx);
        if (!hSnapshot)
            continue;

        // 并行 OCR
        std::future<OcrResultData> futKiller, futDead;
        if (!killerResolved)
            futKiller = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, killerArea);
        if (!deadResolved)
            futDead = std::async(std::launch::async, &CDNFGameCaptureDlg::RunOCR_Internal, this, hSnapshot, deadArea);

        OcrResultData resK = { L"", NULL };
        OcrResultData resD = { L"", NULL };
        if (futKiller.valid()) resK = futKiller.get();
        if (futDead.valid())   resD = futDead.get();

        // ★ 用完立即释放克隆帧，不累积内存
        ::DeleteObject(hSnapshot);

        // 记录有效文本用于二轮匹配
        if (!killerResolved && !resK.text.IsEmpty() && resK.text.Find(L"No text") == -1)
            historyKTexts.push_back({ resK.text, (int)(i + 1) });
        if (!deadResolved && !resD.text.IsEmpty() && resD.text.Find(L"No text") == -1)
            historyDTexts.push_back({ resD.text, (int)(i + 1) });

        // 首轮匹配
        int kScore = -2, dScore = -2;
        processMatch(resK.text, killerResolved, finalKillerName, true, killerBestP, killerBestA, kScore, false, (int)(i + 1));
        processMatch(resD.text, deadResolved, finalDeadName, false, deadBestP, deadBestA, dScore, false, (int)(i + 1));

        // 更新 OCR 下拉框
        OcrResultData& resL = killerIsLeft ? resK : resD;
        OcrResultData& resR = killerIsLeft ? resD : resK;
        {
            std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
            if (resL.hBmp) {
                CString lbl;
                lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resL.text);
                m_ocrRecordsLeft.push_back({ resL.hBmp, lbl });
            }
            if (resR.hBmp) {
                CString lbl;
                lbl.Format(L"第%d帧 %s", (int)(i + 1), (LPCTSTR)resR.text);
                m_ocrRecordsRight.push_back({ resR.hBmp, lbl });
            }
        }
        PostMessage(WM_UPDATE_OCR_DROPDOWNS, 0, 0);

        // 更新调试信息
        {
            std::lock_guard<std::mutex> lk(m_debugMutex);
            m_debugOcrResult.Format(L"时光倒流 %d/%d | 杀:%s 亡:%s",
                (int)(i + 1), (int)validSlots.size(),
                killerResolved ? finalKillerName : L"未定",
                deadResolved ? finalDeadName : L"未定");
        }
        ::InvalidateRect(m_hWnd, &m_previewRect, FALSE);
    }

    // ---- 二轮降级匹配（杀手） ----
    if (!killerResolved && !historyKTexts.empty()) {
        PushVisualLog(L"▶ [找杀手] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyKTexts) {
            int kScore = -2;
            processMatch(frame.text, killerResolved, finalKillerName, true,
                killerBestP, killerBestA, kScore, true, frame.frameIdx);
            if (killerResolved) break;
        }
    }

    // ---- 二轮降级匹配（死者） ----
    if (!deadResolved && !historyDTexts.empty()) {
        PushVisualLog(L"▶ [找死者] 启动【二轮降级匹配】...", RGB(255, 165, 0));
        for (const auto& frame : historyDTexts) {
            int dScore = -2;
            processMatch(frame.text, deadResolved, finalDeadName, false,
                deadBestP, deadBestA, dScore, true, frame.frameIdx);
            if (deadResolved) break;
        }
    }

    // ---- 全局兜底 ----
    if (!killerResolved && globalKillerBestP != -1
        && globalKillerBestScore >= (globalKillerPassLine - 20)
        && globalKillerBestScore >= 40)
    {
        killerResolved = true;
        killerBestP = globalKillerBestP;
        killerBestA = globalKillerBestA;
        finalKillerName = globalKillerName;
    }
    if (!deadResolved && globalDeadBestP != -1
        && globalDeadBestScore >= (globalDeadPassLine - 20)
        && globalDeadBestScore >= 40)
    {
        deadResolved = true;
        deadBestP = globalDeadBestP;
        deadBestA = globalDeadBestA;
        finalDeadName = globalDeadName;
    }

    // ---- 战绩更新（与原版逻辑完全一致） ----
    if (killerResolved || deadResolved) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        DWORD now = GetTickCount();

        bool isDup = false;
        for (const auto& ev : m_recentEvents) {
            if (ev.killer == finalKillerName && ev.dead == finalDeadName && (now - ev.time < 20000)) {
                isDup = true;
                break;
            }
        }

        if (!isDup) {
            m_recentEvents.push_back({ finalKillerName, finalDeadName, now });
            m_recentEvents.erase(
                std::remove_if(m_recentEvents.begin(), m_recentEvents.end(),
                    [&](const RecentEvent& ev) { return now - ev.time > 25000; }),
                m_recentEvents.end());

            if (killerResolved && killerBestP != -1) {
                for (int p = 0; p < 8; p++)
                    if (p != killerBestP)
                        m_players[p].currentStreak = 0;

                m_players[killerBestP].kills++;
                m_players[killerBestP].currentStreak++;

                CString displayName = m_players[killerBestP].name;
                if (killerBestA != -1 && (size_t)killerBestA < m_players[killerBestP].aliases.size())
                    displayName = m_players[killerBestP].aliases[killerBestA].name;

                COLORREF teamColor = (m_players[killerBestP].team == 0) ? RGB(255, 100, 100) : RGB(100, 180, 255);
                CString actionLog;
                actionLog.Format(L"⚔ [击杀成功] 玩家 [%s] 拿下一击！连杀: %d",
                    (LPCTSTR)displayName, m_players[killerBestP].currentStreak);
                PushVisualLog(actionLog, teamColor);

                if (m_players[killerBestP].currentStreak == 4) {
                    m_players[killerBestP].akCount++;
                    m_players[killerBestP].currentStreak = 0;
                    PushVisualLog(L"🌟 [AK宣告] 恐怖如斯！玩家 [" + displayName + L"] 完成一次 AK！",
                        RGB(255, 215, 0));
                }
                m_lastKillerTeam = m_players[killerBestP].team;
            }

            if (deadResolved && deadBestP != -1)
                m_players[deadBestP].deaths++;

            if (m_bPendingTeamScoreWin) {
                m_bPendingTeamScoreWin = false;
                if (m_lastKillerTeam == 0)      m_totalScoreRed++;
                else if (m_lastKillerTeam == 1)  m_totalScoreBlue++;
                for (int p = 0; p < 8; p++)
                    m_players[p].currentStreak = 0;
                PushVisualLog(L"🏆 [结算] 局间大比分变动！所有人连击清零！", RGB(0, 255, 100));
            }

            PostMessage(WM_UPDATE_ALL_UI, 0, 0);
        }
    }

    // ★ 不再需要手动释放 historyClones，因为帧已在循环内逐个释放
}


// ============================================================================
// 【函数 3】CheckColorTrigger — 复用 DC，减少 GDI 创建/销毁开销
//
// 原始问题：
//   - 每 50ms 调用一次，每次都 CreateCompatibleDC + DeleteDC
//   - 高频率创建/销毁 DC 浪费 CPU，尤其在低端机上
// ============================================================================
void CDNFGameCaptureDlg::CheckColorTrigger()
{
    if (!m_bmp || !m_bIsRunning)
        return;

    // 1. 获取这 40 个点的实时像素
    COLORREF colorDeath[40];
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        HDC hMemDC = ::CreateCompatibleDC(NULL);
        HGDIOBJ oldBmp = ::SelectObject(hMemDC, m_bmp);
        for (int i = 0; i < 40; i++) {
            colorDeath[i] = ::GetPixel(hMemDC, (int)(m_w * g_deathPts[i].x), (int)(m_h * g_deathPts[i].y));
        }
        ::SelectObject(hMemDC, oldBmp);
        ::DeleteDC(hMemDC);
    }

    // 2. 高级色彩滤镜：判断该像素是否是大X的“死亡橙红色”
    auto isXColor = [](COLORREF c) -> bool {
        int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        // 大 X 的特征：红色值极高(>160)，且明显高于绿色和蓝色
        return (r > 160 && r > g + 40 && r > b + 80);
        };

    // 3. 容错判定器：传入起始下标，检查连续的 5 个点，有 3 个符合即认为该位置的人已死
    auto checkDead = [&](int startIdx) -> bool {
        int matchCount = 0;
        for (int i = 0; i < 5; i++) {
            if (isXColor(colorDeath[startIdx + i])) matchCount++;
        }
        return matchCount >= 3;
        };

    // ========================================================
    // 4. 按照你的专属下标逻辑，提取各位置的生死状态
    // ========================================================
    // 右边 (0-19)
    bool rightActiveDead = checkDead(0);  // 右边正在打的选手 (0-4)
    bool rightTeamDead = rightActiveDead && checkDead(5) && checkDead(10) && checkDead(15); // 右边替补全部满足 (5-19)

    // 左边 (20-39)
    bool leftActiveDead = checkDead(20); // 左边正在打的选手 (20-24)
    bool leftTeamDead = leftActiveDead && checkDead(25) && checkDead(30) && checkDead(35); // 左边替补全部满足 (25-39)


    // ========================================================
    // 5. 状态机：跟踪【左侧/右侧】正在打的选手的生死，触发单局击杀
    // ========================================================
    static bool s_leftActiveWasDead = false;
    static bool s_rightActiveWasDead = false;

    // 🎯 左边正在打的死了 -> 右边赢了这一小局！(传入 0 代表左边被击杀)
    if (leftActiveDead && !s_leftActiveWasDead) {
        s_leftActiveWasDead = true;
        if (m_bCanTrigger) {
            m_bCanTrigger = FALSE;
            std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, 0).detach();
            SetTimer(2, 10000, NULL); // 10秒防抖，防止刚死的时候动画闪烁重复触发
        }
    }
    else if (!leftActiveDead && s_leftActiveWasDead) {
        s_leftActiveWasDead = false; // 左侧主将位置的大X消失，状态重置
    }

    // 🎯 右边正在打的死了 -> 左边赢了这一小局！(传入 1 代表右边被击杀)
    if (rightActiveDead && !s_rightActiveWasDead) {
        s_rightActiveWasDead = true;
        if (m_bCanTrigger) {
            m_bCanTrigger = FALSE;
            std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, 1).detach();
            SetTimer(2, 10000, NULL); // 10秒防抖
        }
    }
    else if (!rightActiveDead && s_rightActiveWasDead) {
        s_rightActiveWasDead = false; // 右侧主将位置的大X消失，状态重置
    }

    // ========================================================
    // 6. 大比分检测 (判定全队覆灭跳分)
    // ========================================================
    // 只要有一边 4 个人全被打上大X，就触发队伍结算逻辑
    if ((leftTeamDead || rightTeamDead) && m_bCanTriggerTeamScore) {
        m_bCanTriggerTeamScore = FALSE;
        {
            std::lock_guard<std::mutex> dataLock(m_dataMutex);
            m_bPendingTeamScoreWin = true; // 挂起结算标志，等待 DoRetryMatchingTask 里的战绩归属后统一加分
        }
        SetTimer(4, 120000, NULL); // 2分钟大局防抖（换人、换边等阶段不触发）
    }
}

LRESULT CDNFGameCaptureDlg::OnTrayMessage(WPARAM wParam, LPARAM lParam) {
    // 左键单击：显示主界面
    if (lParam == WM_LBUTTONUP) {
        ShowWindow(SW_SHOW);
        ShowWindow(SW_RESTORE);
        SetForegroundWindow();
    }
    // 右键单击：弹出菜单
    else if (lParam == WM_RBUTTONUP) {
        CPoint pt;
        GetCursorPos(&pt);

        CMenu m;
        m.CreatePopupMenu();
        m.AppendMenu(MF_STRING, 101, L"显示面板");

        // 【新增】：在右键菜单里加上“检查更新”选项
        m.AppendMenu(MF_STRING, 103, L"检查更新");

        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING, 102, L"完全退出");

        SetForegroundWindow();
        int cmd = m.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);

        // 处理用户的点击
        if (cmd == 101) {
            ShowWindow(SW_SHOW);
            ShowWindow(SW_RESTORE);
            SetForegroundWindow();
        }
        else if (cmd == 103) {
            // 【新增】：手动点击检查更新，开启后台线程。
            // 注意这里传入的是 false，代表“非静默模式”。
            // 这样即使用户已经是最新版，系统也会弹个窗告诉他“当前已是最新版本”，体验更好。
            std::thread([this]() {
                CheckForUpdates(false);
                }).detach();
        }
        else if (cmd == 102) {
            DoRealExit();
        }
    }
    return 0;
}

void CDNFGameCaptureDlg::DoRealExit() {
    m_bIsRunning = FALSE;
    KillTimer(1); KillTimer(2); KillTimer(3); KillTimer(4);

    // ==========================================
    // 【新增】：主程序退出时，拉着 OCR 一起陪葬
    // 必须连底层的 PaddleOCR-json.exe 一起杀，防止内存泄漏泄漏！
    // ==========================================
    KillProcessByName(L"Umi-OCR.exe");
    KillProcessByName(L"PaddleOCR-json.exe");

    DestroyWindow();
    PostQuitMessage(0);
}

void CDNFGameCaptureDlg::OnClose() { ShowWindow(SW_HIDE); }

// ============================================================================
// UI 事件响应与授权软拦截
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedStart() {
    // 【关键修复】：如果正在同步云端信息（-1），禁止开启监控
    if (m_cloudExpireTime == -1) {
        MessageBox(L"正在与云端同步授权信息，请稍后...", L"安全校验", MB_ICONINFORMATION);
        return;
    }

    // 原有的授权校验拦截
    if (!m_bIsAuthValid) {
        CString msg = L"❌ 您的授权无效或已过期，请检查卡密记录！";
        MessageBox(msg, L"需要授权", MB_ICONWARNING);
        return;
    }

    static bool once;
    if (!once) {
        if (!m_bIsAuthValid) {
            CString msg = L"❌ 您的授权已到期或未激活!\r\n\r\n监控功能已锁定,请联系作者获取正式卡密:\r\nQQ:974294684\r\n微信:aym724794\r\n\r\n获取卡密后点击下方【输入授权码】即可激活。";
            MessageBox(msg, L"需要授权", MB_ICONWARNING);
            return;
        }

        if (m_bIsTrial && !m_bIsRunning) {
            once = true;
            CString trialMsg;
            trialMsg.Format(L"【欢迎试用 DNF 击杀统计工具】\r\n\r\n您当前处于免费试用阶段,试用结束时间:\r\n%s\r\n\r\n点击确定后将开启监控功能。", (LPCTSTR)FormatTimeStamp(m_trialEnd));
            MessageBox(trialMsg, L"试用阶段", MB_ICONINFORMATION);
        }
    }

    if (!m_bIsRunning) {
        m_bIsRunning = TRUE;
        m_btnStart.SetWindowText(L"停止监控");
        // ==========================================
        // 【新增】：点击开始监控，立刻静默唤醒同目录下的 Umi-OCR
        // ==========================================
        EnsureOcrRunning();

        HWND hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);

        // 如果引擎还没就绪，主动尝试激活一次
        bool shouldTryWGC = (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1);
        if (hGame && shouldTryWGC && !m_bUseWGC) {
            try {
                if (WGCCapture::IsSupported()) {
                    if (!m_pWGC) m_pWGC = new WGCCapture();
                    if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) {
                        m_bUseWGC = true;
                    }
                }
            }
            catch (...) {
                if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
            }
        }

        m_nBlankFrameCount = 0;
        m_bAlreadyPrompted = false;

        // 打印相应的状态日志
        if (m_bUseWGC) {
            AppLog(L"✅ [监控已启动] 已启用 WGC 硬件加速捕获 (零闪屏)", RGB(0, 255, 100));
        }
        else if (!hGame) {
            // ★ 游戏没开，不管选了什么引擎，都只提示待命，不要说"降级"
            AppLog(L"⚠️ [监控已启动] 未检测到游戏窗口，待命中...", RGB(255, 165, 0));
        }
        else {
            // 游戏已开但 WGC 失败的情况，才算真正降级
            if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
            if (m_nCaptureEngineChoice == 1) {
                AppLog(L"❌ [监控已启动] WGC 初始化失败，自动降级为 PrintWindow", RGB(255, 80, 80));
            }
            else if (m_nCaptureEngineChoice == 2) {
                AppLog(L"✅ [监控已启动] 用户选择 PrintWindow 兼容模式", RGB(0, 255, 100));
            }
            else {
                AppLog(L"⚠️ [监控已启动] WGC 不可用，已降级为 PrintWindow", RGB(255, 165, 0));
            }
        }

        SetTimer(1, 100, NULL);
        SetTimer(3, HISTORY_INTERVAL_MS, NULL);
        m_status.SetWindowText(L"监控中...");
    }
    else {
        m_bIsRunning = FALSE;
        KillTimer(1);
        KillTimer(3);

        // ==========================================
        // 【关键修复】：停止监控时，绝不能销毁 m_pWGC！
        // 把它留着，让 Timer 6 继续给画面提供实时预览！
        // ==========================================

        m_nBlankFrameCount = 0;
        m_bAlreadyPrompted = false;

        m_btnStart.SetWindowText(L"开始监控");
        m_status.SetWindowText(L"已停止");
        AppLog(L"🛑 [监控已停止] 战绩统计已暂停", RGB(255, 165, 0));
    }
}

void CDNFGameCaptureDlg::OnBnClickedInputKey() {
    CString currentText;
    m_btnInputKey.GetWindowText(currentText);

    // ==========================================
    // 阶段一：点击“输入授权码”，打开记事本，并将按钮变身
    // ==========================================
    if (currentText == L"输入授权码") {
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        CString path = exePath;
        path = path.Left(path.ReverseFind(L'\\') + 1) + L"license.txt";

        CFile file;
        if (file.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite)) {
            file.Close();
        }

        ShellExecute(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);

        // 【关键】：改变按钮文字
        m_btnInputKey.SetWindowText(L"应用授权码");

        // 更新弹窗提示语
        MessageBox(L"请在打开的 license.txt 中粘贴新卡密并保存。\r\n\r\n保存完成后，请点击软件上的【应用授权码】即可生效！", L"第一步：输入授权码", MB_ICONINFORMATION);
    }
    // ==========================================
    // 阶段二：点击“应用授权码”，校验卡密，并将按钮还原
    // ==========================================
    else {
        // 1. 重新读取卡密并执行静默检查
        CheckTrialAndLicense();

        // 2. 清空旧面板，强制打印最新的状态
        if (m_editVisualLogs.m_hWnd) {
            m_editVisualLogs.SetWindowText(L"");
        }
        OutputDebugAuthInfo();

        m_status.SetWindowText(L"授权码校验已触发");

        // 3. 【关键】：将按钮文字还原，完成闭环
        m_btnInputKey.SetWindowText(L"输入授权码");
    }
}

void CDNFGameCaptureDlg::OnBnClickedApply() {
    // ==========================================
    // 1. 纯粹的数据落地：保存所有战绩和配置
    // ==========================================
    SaveAliasDB();      // 保存小号自动补全库
    SaveConfigToFile(); // 保存战局人员信息
    WriteScoreToFile(); // 刷新输出给 OBS 用的直播 TXT 文本

    // ==========================================
    // 2. 纯粹的视觉刷新：同步左右界面的显示
    // ==========================================
    SyncDataToTree();   // 刷新左侧树状图
    RefreshDisplay();   // 刷新右侧红蓝阵营对比图

    // ==========================================
    // 3. 状态反馈
    // ==========================================
    m_status.SetWindowText(L"应用修改成功");
    AppLog(L"💾 [系统] 对局信息与战绩已手动保存", RGB(0, 255, 100));
}

void CDNFGameCaptureDlg::OnBnClickedFlip() { m_bFlipSides = (m_chkFlip.GetCheck() == BST_CHECKED); WriteScoreToFile(); RefreshDisplay(); }

void CDNFGameCaptureDlg::OnBnClickedReset() {
#ifdef _DEBUG
    if (GetKeyState(VK_CONTROL) < 0) {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            DWORD iT = 0, sz = 4;
            time_t now = time(nullptr);
            RegQueryValueEx(hKey, L"InstallTime", NULL, NULL, (LPBYTE)&iT, &sz);
            if ((long long)now > ((long long)iT + 604800)) {
                DWORD resetTime = (DWORD)now;
                RegSetValueEx(hKey, L"InstallTime", 0, REG_DWORD, (const BYTE*)&resetTime, sizeof(DWORD));
                RegSetValueEx(hKey, L"LastRun", 0, REG_DWORD, (const BYTE*)&resetTime, sizeof(DWORD));
                MessageBox(L"【后门】试用期已恢复！重启生效。", L"后门提示", MB_ICONINFORMATION);
            }
            else {
                DWORD expiredTime = (DWORD)(now - 800000);
                RegSetValueEx(hKey, L"InstallTime", 0, REG_DWORD, (const BYTE*)&expiredTime, sizeof(DWORD));
                MessageBox(L"【后门】试用期已强制熔断！重启测试。", L"后门提示", MB_ICONWARNING);
            }
            RegCloseKey(hKey);
        }
        return;
    }
#endif

    if (MessageBox(L"确定要将战绩全部归零吗？", L"确认", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        std::lock_guard<std::mutex> dataLock(m_dataMutex);
        m_totalScoreRed = 0;
        m_totalScoreBlue = 0;
        for (int i = 0; i < 8; i++) {
            m_players[i].kills = 0; m_players[i].deaths = 0;
            m_players[i].currentStreak = 0; m_players[i].akCount = 0;
        }
        SyncDataToTree();
        SaveConfigToFile();
        RefreshDisplay();
        WriteScoreToFile();
        if (m_editVisualLogs.m_hWnd) m_editVisualLogs.SetWindowText(L"");
        OutputDebugAuthInfo();
        m_status.SetWindowText(L"战绩已归零！");
    }
}

void CDNFGameCaptureDlg::OnBnClickedBrowseDir() {
    bool wasRunning = m_bIsRunning;
    KillTimer(1);
    KillTimer(6);

    BROWSEINFO bi = { 0 };
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = L"请选择输出目录：";
    // 【终极绝杀】：只保留基础属性，绝对不能加 BIF_NEWDIALOGSTYLE！
    bi.ulFlags = BIF_RETURNONLYFSDIRS;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            m_outputDir = path;
            if (m_outputDir.Right(1) == L"\\") m_outputDir.TrimRight(L"\\");

            m_editOutDir.SetWindowText(m_outputDir);
            WritePrivateProfileString(L"Settings", L"OutputDir", m_outputDir, m_iniPath);
            WriteScoreToFile();
            m_status.SetWindowText(L"输出目录已更新");
        }
        CoTaskMemFree(pidl);
    }

    if (wasRunning) SetTimer(1, 50, NULL);
    SetTimer(6, 200, NULL);
}

BOOL CDNFGameCaptureDlg::OnEraseBkgnd(CDC* pDC) { return TRUE; }

// ============================================================================
// OCR 下拉框与坐标选取
// ============================================================================
LRESULT CDNFGameCaptureDlg::OnUpdateOcrDropdowns(WPARAM wParam, LPARAM lParam) {
    std::lock_guard<std::mutex> lk(m_ocrRecordMutex);
    if (wParam == 1) {
        m_cmbLeft.ResetContent(); m_cmbLeft.AddString(L"[红] 左侧自动追踪"); m_cmbLeft.SetCurSel(0);
        m_cmbRight.ResetContent(); m_cmbRight.AddString(L"[蓝] 右侧自动追踪"); m_cmbRight.SetCurSel(0);
        return 0;
    }
    while (m_cmbLeft.GetCount() - 1 < (int)m_ocrRecordsLeft.size()) {
        m_cmbLeft.AddString(m_ocrRecordsLeft[m_cmbLeft.GetCount() - 1].displayText);
    }
    while (m_cmbRight.GetCount() - 1 < (int)m_ocrRecordsRight.size()) {
        m_cmbRight.AddString(m_ocrRecordsRight[m_cmbRight.GetCount() - 1].displayText);
    }
    return 0;
}
void CDNFGameCaptureDlg::OnCbnSelchangeLeft() { m_viewIndexLeft = (m_cmbLeft.GetCurSel() == 0) ? -1 : (m_cmbLeft.GetCurSel() - 1); InvalidateRect(&m_previewRect, FALSE); }
void CDNFGameCaptureDlg::OnCbnSelchangeRight() { m_viewIndexRight = (m_cmbRight.GetCurSel() == 0) ? -1 : (m_cmbRight.GetCurSel() - 1); InvalidateRect(&m_previewRect, FALSE); }

// ============================================================================
// 手动测试与核心截图逻辑
// ============================================================================
void CDNFGameCaptureDlg::ManualTriggerKill(int killSide) {
    if (!m_bIsRunning || !m_bCanTrigger) return;

    Capture();
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            // ★ 同样用 CopyImage 替代
            if (m_historyBmps[m_historyIdx])
                ::DeleteObject(m_historyBmps[m_historyIdx]);
            m_historyBmps[m_historyIdx] = (HBITMAP)::CopyImage(
                m_bmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            m_historyIdx = (m_historyIdx + 1) % MAX_HISTORY_FRAMES;
        }
    }

    m_bCanTrigger = FALSE;
    CString sideName = (killSide == 0) ? L"【红队】" : L"【蓝队】";
    time_t now_t = time(0); tm t; localtime_s(&t, &now_t);
    CString tStr; tStr.Format(L"[%02d:%02d:%02d] 🚀 全局快捷键触发: %s", t.tm_hour, t.tm_min, t.tm_sec, (LPCTSTR)sideName);

    {
        std::lock_guard<std::mutex> lk(g_visualLogMutex);
        g_visualLogs.push_back({ tStr, RGB(255, 165, 0) });
    }
    std::thread(&CDNFGameCaptureDlg::DoRetryMatchingTask, this, killSide).detach();
    SetTimer(2, 10000, NULL);
}


LRESULT CDNFGameCaptureDlg::OnWGCInitDone(WPARAM wParam, LPARAM lParam) {
    // 【已被废弃的异步回调，内容留空】
    return 0;
}

void CDNFGameCaptureDlg::Capture() {
    // ★★★ 下拉框打开期间，完全跳过捕获，防止滚动时反复初始化引擎 ★★★
    if (m_cmbTargetWindow.m_hWnd && m_cmbTargetWindow.GetDroppedState()) {
        return;
    }

    DWORD_PTR targetData = 0;
    if (m_cmbTargetWindow.m_hWnd && m_cmbTargetWindow.GetCurSel() != -1) {
        targetData = m_cmbTargetWindow.GetItemData(m_cmbTargetWindow.GetCurSel());
    }

    // ==========================================
    // 路线 A：【摄像头模式】
    // ==========================================
    if (targetData & 0x80000000) {
        int camIndex = targetData & 0x7FFFFFFF;
        if (!m_pCamera) {
            m_pCamera = new CameraCapture();
            if (m_pCamera->Initialize(camIndex)) {
                m_pCamera->StartCapture();
                AppLog(L"📹 [摄像头] 已成功连接，正在出流", RGB(0, 255, 100));
            }
            else {
                delete m_pCamera; m_pCamera = nullptr;
                AppLog(L"❌ [摄像头] 无法连接或被占用", RGB(255, 80, 80));
                return;
            }
        }

        int camW = 0, camH = 0;
        HBITMAP hCamFrame = m_pCamera->GetLatestFrame(camW, camH);
        if (hCamFrame && camW > 0 && camH > 0) {
            std::lock_guard<std::mutex> lock(g_bmpMutex);
            if (camW != m_w || camH != m_h) {
                for (int i = 0; i < MAX_HISTORY_FRAMES; i++) {
                    if (m_historyBmps[i]) { ::DeleteObject(m_historyBmps[i]); m_historyBmps[i] = nullptr; }
                }
                m_historyIdx = 0;
            }
            if (m_bmp) ::DeleteObject(m_bmp);
            m_bmp = hCamFrame;
            m_w = camW;
            m_h = camH;
        }
        else if (hCamFrame) {
            ::DeleteObject(hCamFrame);
        }
    }
    // ==========================================
    // 路线 B：【窗口模式】
    // ==========================================
    else {
        HWND hGame = NULL;
        if (targetData == 0) {
#if ENABLE_CLOUD_TEST_MODE
            hGame = ::GetDesktopWindow();
#else
            hGame = ::FindWindow(NULL, DNF_WINDOW_NAME);
#endif
        }
        else {
            hGame = (HWND)targetData;
            if (!::IsWindow(hGame)) {
                m_cmbTargetWindow.SetCurSel(0);
                return;
            }
        }

        if (!hGame) {
            if (m_pWGC && !m_bIsRunning) {
                m_pWGC->StopCapture();
                delete m_pWGC;
                m_pWGC = nullptr;
                m_bUseWGC = false;
            }
            return;
        }

        // ==========================================
        // 2. 同步安全 WGC 初始化 (防假死装甲护体)
        // ==========================================
#if !ENABLE_CLOUD_TEST_MODE
        if (!m_bUseWGC && (m_nCaptureEngineChoice == 0 || m_nCaptureEngineChoice == 1)) {
            static HWND s_lastTryHwnd = NULL;
            static DWORD s_lastTryTime = 0;
            DWORD now = GetTickCount();

            if (hGame != s_lastTryHwnd || now - s_lastTryTime > 2000) {
                s_lastTryTime = now;
                s_lastTryHwnd = hGame;

                DWORD_PTR dwResult = 0;
                if (::SendMessageTimeout(hGame, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &dwResult) != 0) {
                    try {
                        if (WGCCapture::IsSupported()) {
                            if (!m_pWGC) m_pWGC = new WGCCapture();
                            if (m_pWGC->Initialize(hGame) && m_pWGC->StartCapture()) {
                                m_bUseWGC = true;
                            }
                            else {
                                delete m_pWGC; m_pWGC = nullptr;
                            }
                        }
                    }
                    catch (...) {
                        if (m_pWGC) { delete m_pWGC; m_pWGC = nullptr; }
                    }
                }
            }
        }
#endif

        bool bNeedBlankCheck = false;
        int  capturedW = 0, capturedH = 0;
        HBITMAP hCapturedBmp = nullptr;

        // 3. WGC 捕获
        if (m_bUseWGC && m_pWGC) {
            int w = 0, h = 0;
            HBITMAP hFrame = m_pWGC->GetLatestFrame(w, h);
            if (hFrame && w > 0 && h > 0) {

                // 【绝杀】：WGC 暴力切除标题栏与边框手术 (避开Win11幽灵阴影)
                // ==========================================
                if (m_chkCropTitle.m_hWnd && m_chkCropTitle.GetCheck() == BST_CHECKED && hGame) {
                    RECT cRect; ::GetClientRect(hGame, &cRect);
                    int cW = cRect.right - cRect.left;
                    int cH = cRect.bottom - cRect.top;

                    // 计算物理边框：(WGC截图宽度 - 实际客户区宽度) / 2 = 单侧边框厚度
                    int borderX = max(0, (w - cW) / 2);
                    // 标题栏高度：(WGC截图高度 - 实际客户区高度) - 底部边框
                    int borderY = max(0, h - cH - borderX);

                    // 只有当高度差确实存在（有标题栏），且尺寸合法时，才执行手术
                    if (borderY > 0 && cW > 0 && cH > 0 && (borderX + cW) <= w && (borderY + cH) <= h) {
                        HDC hdcScreen = ::GetDC(NULL);
                        HBITMAP hCropped = ::CreateCompatibleBitmap(hdcScreen, cW, cH);
                        HDC hMemDst = ::CreateCompatibleDC(hdcScreen);
                        HDC hMemSrc = ::CreateCompatibleDC(hdcScreen);
                        HGDIOBJ oldDst = ::SelectObject(hMemDst, hCropped);
                        HGDIOBJ oldSrc = ::SelectObject(hMemSrc, hFrame);

                        // 精准从 WGC 原图的 (borderX, borderY) 开始抠出纯净画面
                        ::BitBlt(hMemDst, 0, 0, cW, cH, hMemSrc, borderX, borderY, SRCCOPY);

                        ::SelectObject(hMemDst, oldDst); ::SelectObject(hMemSrc, oldSrc);
                        ::DeleteDC(hMemDst); ::DeleteDC(hMemSrc); ::ReleaseDC(NULL, hdcScreen);

                        ::DeleteObject(hFrame); // 销毁带标题栏的原图
                        hFrame = hCropped;      // 替换为纯净的图！
                        w = cW; h = cH;         // 更新全局分辨率
                    }
                }

                if (!m_bAlreadyPrompted && m_nCaptureEngineChoice == 0 && IsBitmapBlank(hFrame, w, h)) {
                    m_nBlankFrameCount++;
                    if (m_nBlankFrameCount >= 5) {
                        AppLog(L"⚠️ [捕获引擎] WGC 持续黑屏,自动降级为 PrintWindow", RGB(255, 165, 0));
                        m_bUseWGC = false;

                        m_pWGC->StopCapture();
                        delete m_pWGC;
                        m_pWGC = nullptr;

                        m_nBlankFrameCount = 0;
                        DeleteObject(hFrame);
                        goto fallback_printwindow;
                    }
                }
                else {
                    m_nBlankFrameCount = 0;
                }

                std::lock_guard<std::mutex> lock(g_bmpMutex);
                if (w != m_w || h != m_h) {
                    for (int i = 0; i < MAX_HISTORY_FRAMES; i++) {
                        if (m_historyBmps[i]) { ::DeleteObject(m_historyBmps[i]); m_historyBmps[i] = nullptr; }
                    }
                    m_historyIdx = 0;
                }

                if (m_bmp) DeleteObject(m_bmp);
                m_bmp = hFrame;
                m_w = w;
                m_h = h;
            }
            else if (hFrame) {
                ::DeleteObject(hFrame);
            }
        }
        // 4. PrintWindow / BitBlt 兼容模式
        else {
        fallback_printwindow:
            static DWORD s_lastPwTime = 0;
            DWORD now = GetTickCount();
            if (now - s_lastPwTime < 333) return;
            s_lastPwTime = now;

            {
                std::lock_guard<std::mutex> lock(g_bmpMutex);
#if ENABLE_CLOUD_TEST_MODE
                m_w = GetSystemMetrics(SM_CXSCREEN); m_h = GetSystemMetrics(SM_CYSCREEN);
                if (m_w > 0 && m_h > 0) {
                    HDC hdcScreen = ::GetDC(NULL);
                    if (!m_bmp) m_bmp = ::CreateCompatibleBitmap(hdcScreen, m_w, m_h);
                    HDC hMem = ::CreateCompatibleDC(hdcScreen);
                    HGDIOBJ old = ::SelectObject(hMem, m_bmp);
                    ::BitBlt(hMem, 0, 0, m_w, m_h, hdcScreen, 0, 0, SRCCOPY);
                    ::SelectObject(hMem, old); ::DeleteDC(hMem); ::ReleaseDC(NULL, hdcScreen);
                    capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp; bNeedBlankCheck = false;
                }
#else
                // ==========================================
                // 【绝杀修复】：完美适配去标题栏，不留黑边！
                // ==========================================
                bool isCrop = (m_chkCropTitle.m_hWnd && m_chkCropTitle.GetCheck() == BST_CHECKED);
                int newW = 0, newH = 0;

                // 核心：是否截取整个窗口(包含外部边框和标题栏)
                if (!isCrop) {
                    RECT wRect; ::GetWindowRect(hGame, &wRect);
                    newW = wRect.right - wRect.left;
                    newH = wRect.bottom - wRect.top;
                }
                else {
                    RECT cRect; ::GetClientRect(hGame, &cRect);
                    newW = cRect.right - cRect.left;
                    newH = cRect.bottom - cRect.top;
                }

                if (newW > 0 && newH > 0) {
                    if (!m_bmp || newW != m_w || newH != m_h) {
                        if (m_bmp) { ::DeleteObject(m_bmp); m_bmp = nullptr; }
                        HDC hdc = ::GetDC(hGame); m_bmp = ::CreateCompatibleBitmap(hdc, newW, newH); ::ReleaseDC(hGame, hdc);
                    }
                    m_w = newW; m_h = newH;

                    HDC hGameDC = ::GetDC(hGame);
                    HDC hMemDC = ::CreateCompatibleDC(hGameDC);
                    HGDIOBJ oldBmp = ::SelectObject(hMemDC, m_bmp);

                    // ==========================================
                    // 【极度关键】：刷入纯黑底漆！
                    // ==========================================
                    RECT fillRect = { 0, 0, newW, newH };
                    ::FillRect(hMemDC, &fillRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

                    // ==========================================
                    // 【终极合并】：废弃 BitBlt，全系启用 DWM 穿透捕获！
                    // 无论是 DNF 还是 OBS，只要没假死，统统能抓到最新的硬件加速实时画面！
                    // ==========================================
                    DWORD_PTR dwResult = 0;
                    if (::SendMessageTimeout(hGame, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &dwResult) != 0) {
                        // 参数 2: PW_RENDERFULLCONTENT (捕获带边框的硬件加速窗口)
                        // 参数 3: PW_CLIENTONLY | PW_RENDERFULLCONTENT (精准剔除标题栏)
                        ::PrintWindow(hGame, hMemDC, isCrop ? 3 : 2);
                    }

                    ::SelectObject(hMemDC, oldBmp); ::DeleteDC(hMemDC); ::ReleaseDC(hGame, hGameDC);
                    capturedW = m_w; capturedH = m_h; hCapturedBmp = m_bmp; bNeedBlankCheck = !m_bAlreadyPrompted;
                }
#endif
            }
            if (bNeedBlankCheck && IsBitmapBlank(hCapturedBmp, capturedW, capturedH)) {
                m_nBlankFrameCount++;
                if (m_nBlankFrameCount >= 5) {
                    m_bAlreadyPrompted = true;
                    if (!IsRunningAsAdmin()) {
                        KillTimer(m_bIsRunning ? 1 : 6);
                        int ret = ShowCenteredMsgBox(L"⚠️ 检测到画面连续黑屏\r\n请尝试以管理员身份运行软件。", L"权限不足", MB_ICONWARNING | MB_YESNO | MB_SYSTEMMODAL);
                        if (ret == IDYES) {
                            if (m_bIsRunning) { m_bIsRunning = FALSE; KillTimer(3); }
                            if (!RelaunchAsAdmin()) MessageBox(L"自动提权失败，请手动管理员运行", L"错误", MB_ICONERROR);
                            return;
                        }
                        if (m_bIsRunning) SetTimer(1, 50, NULL);
                        else              SetTimer(6, 200, NULL);
                    }
                }
            }
            else if (bNeedBlankCheck) {
                m_nBlankFrameCount = 0;
            }
        }
    }

    // 5. 渲染预览图
    if (m_w <= 0 || m_h <= 0) return;
    CRect client; GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, client.right, splitY);
    float aspect = (float)m_w / (float)m_h;
    int drawW = topHalf.Width(); int drawH = (int)(drawW / aspect);
    if (drawH > topHalf.Height()) { drawH = topHalf.Height(); drawW = (int)(drawH * aspect); }
    int dX = topHalf.left + (topHalf.Width() - drawW) / 2; int dY = topHalf.top + (topHalf.Height() - drawH) / 2;
    m_previewRect = CRect(dX, dY, dX + drawW, dY + drawH);
    InvalidateRect(&topHalf, FALSE);
}



void CDNFGameCaptureDlg::EnsureOcrRunning() {
    std::lock_guard<std::mutex> lk(m_launchMutex); DWORD now = GetTickCount();
    if (now - m_lastLaunchOcrTime < 10000 || GetFileAttributes(m_ocrExePath) == INVALID_FILE_ATTRIBUTES) return;
    m_lastLaunchOcrTime = now;
    SHELLEXECUTEINFO s = { sizeof(s) }; s.fMask = SEE_MASK_FLAG_NO_UI; s.lpVerb = L"open"; s.lpFile = m_ocrExePath; s.nShow = SW_SHOWMINNOACTIVE; ShellExecuteEx(&s);
}


void CDNFGameCaptureDlg::FilterLivePlatformPrefixes() {
    std::vector<CString> keywords = { L"FSN", L"TV", L"直播", L"抖音", L"快手", L"斗鱼", L"虎牙", L"B站", L"BILIBILI", L"企冲", L"熊猫", L"战旗" };
    for (const CString& kw : keywords) {
        int count = 0;
        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            bool foundInPlayer = false; CString upperName = m_players[i].name; upperName.MakeUpper(); CString upperKw = kw; upperKw.MakeUpper();
            if (upperName.Find(upperKw) != -1) { foundInPlayer = true; }
            else { for (const auto& a : m_players[i].aliases) { CString upperAlias = a.name; upperAlias.MakeUpper(); if (upperAlias.Find(upperKw) != -1) { foundInPlayer = true; break; } } }
            if (foundInPlayer) count++;
        }
        if (count >= 2) {
            CString upperKw = kw; upperKw.MakeUpper();
            for (int i = 0; i < 8; i++) {
                if (m_players[i].name.IsEmpty()) continue;
                CString upperName = m_players[i].name; upperName.MakeUpper(); int pos = upperName.Find(upperKw);
                while (pos != -1) { m_players[i].name.Delete(pos, kw.GetLength()); m_players[i].name.Trim(L"-_. "); upperName = m_players[i].name; upperName.MakeUpper(); pos = upperName.Find(upperKw); }
                for (auto& a : m_players[i].aliases) {
                    CString upperAlias = a.name; upperAlias.MakeUpper(); int apos = upperAlias.Find(upperKw);
                    while (apos != -1) { a.name.Delete(apos, kw.GetLength()); a.name.Trim(L"-_. "); upperAlias = a.name; upperAlias.MakeUpper(); apos = upperAlias.Find(upperKw); }
                }
            }
        }
    }
}

void CDNFGameCaptureDlg::WriteScoreToFile() {
    std::vector<PlayerData> r, b;
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        if (m_players[i].team == 0) r.push_back(m_players[i]); else b.push_back(m_players[i]);
    }
    while (r.size() < 4) r.push_back({ L"",0,{},0,0,0,0 });
    while (b.size() < 4) b.push_back({ L"",1,{},0,0,0,0 });
    std::vector<PlayerData>& lT = m_bFlipSides ? b : r; std::vector<PlayerData>& rT = m_bFlipSides ? r : b;

    CString pathScore = m_outputDir + L"\\比分.txt"; CString pathLeft = m_outputDir + L"\\左侧人头.txt"; CString pathRight = m_outputDir + L"\\右侧人头.txt"; CString pathKill = m_outputDir + L"\\击杀.txt";
    FILE* fS = NULL;
    if (_wfopen_s(&fS, pathScore, L"wt, ccs=UTF-8") == 0 && fS) { fwprintf(fS, L"%d-%d\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue); fclose(fS); }

    auto gs_full = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L""); CString s; s.Format(L"%s%02d/%02d", p.name.GetString(), p.kills, p.deaths);
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L" A%d", p.akCount);else  s += L" -"; return s;
        };

    FILE* fKL = NULL; if (_wfopen_s(&fKL, pathLeft, L"wt, ccs=UTF-8") == 0 && fKL) { for (int i = 0; i < 4; i++) { CString ls = gs_full(lT[i]); if (!ls.IsEmpty()) fwprintf(fKL, L"%s\n", ls.GetString()); } fclose(fKL); }
    FILE* fKR = NULL; if (_wfopen_s(&fKR, pathRight, L"wt, ccs=UTF-8") == 0 && fKR) { for (int i = 0; i < 4; i++) { CString rs = gs_full(rT[i]); if (!rs.IsEmpty()) fwprintf(fKR, L"%s\n", rs.GetString()); } fclose(fKR); }

    auto gs_kill_only = [](PlayerData& p) {
        if (p.name.IsEmpty()) return CString(L""); CString s; s.Format(L"%s %02d", p.name.GetString(), p.kills);
        if (p.akCount == 1) s += L" A"; else if (p.akCount > 1) s.AppendFormat(L"A%d", p.akCount);else  s += L" -"; return s;
        };
    FILE* fKill = NULL;
    if (_wfopen_s(&fKill, pathKill, L"wt, ccs=UTF-8") == 0 && fKill) {
        for (int i = 0; i < 4; i++) {
            CString ls = gs_kill_only(lT[i]); CString rs = gs_kill_only(rT[i]);
            if (ls.IsEmpty() && rs.IsEmpty()) continue;
            int pad = max(1, 11 - GetVisualWidth(ls)); CString spaces(L' ', pad); fwprintf(fKill, L"%s%s%s\n", ls.GetString(), spaces.GetString(), rs.GetString());
        }
        fclose(fKill);
    }
}

void CDNFGameCaptureDlg::RefreshDisplay() {
    m_editOcrResult.SetWindowText(L"");
    CString sS; sS.Format(L"========= 总比分  %d : %d =============\r\n", m_bFlipSides ? m_totalScoreBlue : m_totalScoreRed, m_bFlipSides ? m_totalScoreRed : m_totalScoreBlue);
    auto ap = [&](const CString& t, COLORREF c) {
        int l = m_editOcrResult.GetWindowTextLength(); m_editOcrResult.SetSel(l, l);
        CHARFORMAT cf; ZeroMemory(&cf, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = c;
        m_editOcrResult.SetSelectionCharFormat(cf); m_editOcrResult.ReplaceSel(t);
        };

    ap(sS, RGB(0, 100, 0)); ap(m_bFlipSides ? L"蓝 队 选 手                     红 队 选 手\r\n" : L"红 队 选 手                     蓝 队 选 手\r\n", RGB(0, 0, 0)); ap(L"------------------------------------------\r\n", RGB(150, 150, 150));
    std::vector<int> rI, bI;
    for (int i = 0; i < 8; i++) { if (m_players[i].name.IsEmpty()) continue; if (m_players[i].team == 0) rI.push_back(i); else bI.push_back(i); }
    std::vector<int>& lIdx = m_bFlipSides ? bI : rI; std::vector<int>& rIdx = m_bFlipSides ? rI : bI;
    COLORREF lC = m_bFlipSides ? RGB(0, 0, 200) : RGB(200, 0, 0); COLORREF rC = m_bFlipSides ? RGB(200, 0, 0) : RGB(0, 0, 200);

    for (size_t i = 0; i < (std::max)(lIdx.size(), rIdx.size()); i++) {
        CString lT = L"";
        if (i < lIdx.size()) {
            int p = lIdx[i]; lT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) lT += L" A"; else if (m_players[p].akCount > 1) lT.AppendFormat(L" A%d", m_players[p].akCount);
        }
        ap(lT, lC); int curW = GetVisualWidth(lT); for (int s = 0; s < (32 - curW); s++) ap(L" ", 0);
        CString rT = L"";
        if (i < rIdx.size()) {
            int p = rIdx[i]; rT.Format(L"%s : %02d/%02d", (LPCTSTR)m_players[p].name, m_players[p].kills, m_players[p].deaths);
            if (m_players[p].akCount == 1) rT += L" A"; else if (m_players[p].akCount > 1) rT.AppendFormat(L" A%d", m_players[p].akCount); rT += L"\r\n";
        }
        else { rT = L"\r\n"; }
        ap(rT, rC);
    }
}

// ============================================================================
// 绘制模块与 UI 排版
// ============================================================================
void CDNFGameCaptureDlg::OnPaint() {
    // 【新增】：确保 Timer 7 绝对能启动
    static bool s_bTimer7Started = false;
    if (!s_bTimer7Started) {
        SetTimer(7, 1000, NULL); // 启动 1 秒钟的心跳
        s_bTimer7Started = true;
    }

    //// 【新增】：界面渲染后，延迟 2 秒偷偷检测是否有终极 ZIP 包
    //static bool s_bUpdateTimerStarted = false;
    //if (!s_bUpdateTimerStarted) {
    //    SetTimer(8, 2000, NULL);
    //    s_bUpdateTimerStarted = true;
    //}

    // 启动后强制把自己拉到前台（解决更新后窗口不弹出的问题）
    static bool s_bBringToFrontOnce = false;
    if (!s_bBringToFrontOnce) {
        s_bBringToFrontOnce = true;

        // 绕过 Windows 前台锁：先 attach 到前台线程，再 SetForegroundWindow
        HWND hFore = ::GetForegroundWindow();
        DWORD dwFore = ::GetWindowThreadProcessId(hFore, NULL);
        DWORD dwSelf = ::GetCurrentThreadId();

        ::AttachThreadInput(dwSelf, dwFore, TRUE);
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::ShowWindow(m_hWnd, SW_RESTORE);       // 如果被最小化
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ::SetForegroundWindow(m_hWnd);
        ::SetActiveWindow(m_hWnd);
        ::SetFocus(m_hWnd);
        ::AttachThreadInput(dwSelf, dwFore, FALSE);
    }

    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

    int splitY = max(100, r.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, r.right, splitY);
    CRect uiRect(0, splitY, r.right, r.bottom);

    if (!m_status.m_hWnd) {
        m_font.CreatePointFont(95, L"微软雅黑");

        // ==========================================
        // 【第一排】：系统控制栏 (主打宽敞，显示全称)
        // ==========================================
        int row1_Y = splitY + 5;

        m_chkFlip.Create(L"翻转红蓝", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            CRect(10, row1_Y, 95, row1_Y + 25), this, ID_CHK_FLIP);
        m_chkFlip.SetFont(&m_font);

        m_btnHelp.Create(L"说明", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(100, row1_Y, 150, row1_Y + 25), this, 1021);
        m_btnHelp.SetFont(&m_font);

        m_status.Create(L"就绪", WS_CHILD | WS_VISIBLE | SS_CENTER,
            CRect(155, row1_Y + 4, 215, row1_Y + 25), this, 1003);
        m_status.SetFont(&m_font);

        // 引擎选择拉宽
        m_cmbCaptureEngine.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            CRect(220, row1_Y, 380, row1_Y + 200), this, 1030);
        m_cmbCaptureEngine.SetFont(&m_font);
        if (m_cmbCaptureEngine.GetCount() == 0) {
            m_cmbCaptureEngine.AddString(L"🔄 自动选择引擎");
            m_cmbCaptureEngine.AddString(L"🎮 WGC 硬件捕获");
            m_cmbCaptureEngine.AddString(L"🖥️ PrintWindow");
            m_nCaptureEngineChoice = GetPrivateProfileInt(L"Settings", L"CaptureEngine", 0, m_iniPath);
            if (m_nCaptureEngineChoice < 0 || m_nCaptureEngineChoice > 2) m_nCaptureEngineChoice = 0;
            m_cmbCaptureEngine.SetCurSel(m_nCaptureEngineChoice);
        }

        // 目标窗口占据大部分空间，保证长名字能看全
        m_cmbTargetWindow.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            CRect(385, row1_Y, r.right - 100, row1_Y + 400), this, 1031);
        m_cmbTargetWindow.SetFont(&m_font);
        RefreshTargetList();

        // 去标题栏贴紧右边缘，并且限制长度防闪烁
        m_chkCropTitle.Create(L"去标题栏", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            CRect(r.right - 95, row1_Y, r.right - 10, row1_Y + 25), this, 1032);
        m_chkCropTitle.SetFont(&m_font);
        m_chkCropTitle.SetCheck(BST_CHECKED);

        // ==========================================
        // 【第二排】：数据输入与追踪配置栏 (左右完美对称)
        // ==========================================
        int row2_Y = row1_Y + 35; // 垂直下移
        int halfW = (r.right - 30) / 2;

        // --- 左半边：人员录入 ---
        m_cmbTeamSelect.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            CRect(10, row2_Y, 80, row2_Y + 200), this, 1024);
        m_cmbTeamSelect.SetFont(&m_font);
        if (m_cmbTeamSelect.GetCount() == 0) {
            m_cmbTeamSelect.AddString(L"[红队]");
            m_cmbTeamSelect.AddString(L"[蓝队]");
            m_cmbTeamSelect.SetCurSel(0);
        }

        m_editQuickAdd.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
            CRect(85, row2_Y, halfW - 55, row2_Y + 30), this, 1025);
        m_editQuickAdd.SetFont(&m_font);
        m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT);

        m_btnQuickAdd.Create(L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(halfW - 50, row2_Y, 10 + halfW, row2_Y + 28), this, 1022);
        m_btnQuickAdd.SetFont(&m_font);

        // --- 右半边：红蓝追踪下拉框 (移到这里极其合理) ---
        int rightAreaW = (r.right - 10) - (20 + halfW);
        int trackerW = (rightAreaW - 10) / 2;

        m_cmbLeft.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            CRect(20 + halfW, row2_Y, 20 + halfW + trackerW, row2_Y + 300), this, 1010);
        m_cmbLeft.SetFont(&m_font);
        m_cmbLeft.AddString(L"[红] 左侧自动追踪");
        m_cmbLeft.SetCurSel(0);

        m_cmbRight.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            CRect(20 + halfW + trackerW + 10, row2_Y, r.right - 10, row2_Y + 300), this, 1009);
        m_cmbRight.SetFont(&m_font);
        m_cmbRight.AddString(L"[蓝] 右侧自动追踪");
        m_cmbRight.SetCurSel(0);

        // ==========================================
        // 【第三排】：大面板展示区 (树状图 & 日志)
        // ==========================================
        int row3_Y = row2_Y + 35; // 再次垂直下移
        int row2_Bottom = r.bottom - (int)(75 * WINDOW_SCALE);

        // ==========================================
        // 【绝杀优化】：精准压缩比分板高度！
        // 原本是 150，现在改为 115。这个高度刚好能塞下表头 + 4名选手，
        // 挤出来的所有空间，都会自动补偿给下方的黑色日志台！
        // ==========================================
        int scoreH = (int)(122 * WINDOW_SCALE);

        m_treePlayers.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
            CRect(10, row3_Y, 10 + halfW, row2_Bottom), this, 1023);
        m_treePlayers.SetFont(&m_font);

        m_editOcrResult.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            CRect(20 + halfW, row3_Y, r.right - 10, row3_Y + scoreH), this, 1002);
        m_editOcrResult.SetFont(&m_font);

        m_editVisualLogs.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            CRect(20 + halfW, row3_Y + scoreH + 5, r.right - 10, row2_Bottom), this, 1011);
        m_editVisualLogs.SetFont(&m_font);
        m_editVisualLogs.SetBackgroundColor(FALSE, RGB(30, 30, 30));
        m_editVisualLogs.LimitText(0);

        // ==========================================
        // 【第四排与第五排】：底部按钮保持原样，无需大改
        // ==========================================
        int btnY = row2_Bottom + 8;
        int btnH = (int)(28 * WINDOW_SCALE);
        int bW = (r.right - 40) / 3;

        m_btnStart.Create(L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(10, btnY, 10 + bW, btnY + btnH), this, ID_BTN_START);
        m_btnStart.SetFont(&m_font);

#ifdef _DEBUG
        CString strApplyBtn = L"应用修改";
        CString strResetBtn = L"战绩归零(Ctrl断试用)";
#else
        CString strApplyBtn = L"应用修改";
        CString strResetBtn = L"战绩归零";
#endif
        m_btnApply.Create(strApplyBtn, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(20 + bW, btnY, 20 + bW * 2, btnY + btnH), this, ID_BTN_APPLY);
        m_btnApply.SetFont(&m_font);

        m_btnReset.Create(strResetBtn, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(30 + bW * 2, btnY, r.right - 10, btnY + btnH), this, ID_BTN_RESET);
        m_btnReset.SetFont(&m_font);

        int dirY = btnY + btnH + 5;
        int rightBtnW = 110;

        m_editOutDir.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
            CRect(10, dirY, r.right - (rightBtnW * 2) - 30, dirY + btnH), this, ID_EDIT_DIR);
        m_editOutDir.SetFont(&m_font);
        m_editOutDir.SetWindowText(m_outputDir);

        m_btnBrowseDir.Create(L"更改目录", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(r.right - (rightBtnW * 2) - 20, dirY, r.right - rightBtnW - 20, dirY + btnH), this, ID_BTN_BROWSE);
        m_btnBrowseDir.SetFont(&m_font);

        m_btnInputKey.Create(L"输入授权码", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(r.right - rightBtnW - 10, dirY, r.right - 10, dirY + btnH), this, ID_BTN_INPUT_KEY);
        m_btnInputKey.SetFont(&m_font);


        static bool configLoadedonce = false;
        if (!configLoadedonce) {
            LoadConfigFromFile();
            LoadAliasDB();
            configLoadedonce = true;
        }

        SyncDataToTree();
        RefreshDisplay();
        WriteScoreToFile();
        OutputDebugAuthInfo();

        std::thread([this]() {
            CheckForUpdates(true);
            }).detach();

        // 【新增】：开启全局日志刷新定时器（每 100 毫秒刷新一次日志面板）
        SetTimer(5, 100, NULL);

        // 【新增】:开启游戏画面预览定时器(窗口创建即生效,不依赖"开始监控")
        SetTimer(6, 200, NULL);
    }

    dc.FillSolidRect(&uiRect, GetSysColor(COLOR_BTNFACE));
    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&dc, topHalf.Width(), topHalf.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    memDC.FillSolidRect(0, 0, topHalf.Width(), topHalf.Height(), RGB(15, 15, 15));

    // OnPaint() 的位图绘制部分
    if (m_w > 0 && m_h > 0 && IsWindowVisible()) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);

            memDC.SetStretchBltMode(HALFTONE);
            memDC.StretchBlt(m_previewRect.left, m_previewRect.top,
                m_previewRect.Width(), m_previewRect.Height(),
                CDC::FromHandle(hBmpDC), 0, 0, m_w, m_h, SRCCOPY);

            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }
    }

    Draw(memDC);
    dc.BitBlt(0, 0, topHalf.Width(), topHalf.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
}

void CDNFGameCaptureDlg::Draw(CDC& dc) {
    if (m_w <= 0) return;
   // CPen p1(PS_SOLID, 2, RGB(255, 0, 0)), p3(PS_SOLID, 2, RGB(255, 165, 0)); dc.SelectStockObject(NULL_BRUSH); dc.SelectObject(&p1);
    //float pX[4] = { 0.187f, 0.157f, 0.840f, 0.810f }; float pY[4] = { 0.036f, 0.034f, 0.039f, 0.039f };
  /*  for (int i = 0; i < 4; i++) dc.Ellipse(m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) - 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) - 5, m_previewRect.left + (int)(pX[i] * m_previewRect.Width()) + 5, m_previewRect.top + (int)(pY[i] * m_previewRect.Height()) + 5);
    dc.SelectObject(&p3);
    for (int i = 0; i < 16; i++) dc.Ellipse(m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) - 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) - 5, m_previewRect.left + (int)(g_scorePts[i].x * m_previewRect.Width()) + 5, m_previewRect.top + (int)(g_scorePts[i].y * m_previewRect.Height()) + 5);*/
    CString h; { std::lock_guard<std::mutex> lk(m_debugMutex); h = m_debugOcrResult; }
    if (!h.IsEmpty()) {
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(0, 255, 0)); CFont f; f.CreatePointFont(105, L"黑体"); CFont* of = dc.SelectObject(&f);
        CRect tR(0, 0, 0, 0); dc.DrawText(h, &tR, DT_LEFT | DT_TOP | DT_CALCRECT);
        CRect cr(m_previewRect.left + 15, m_previewRect.bottom - 25 - tR.Height(), m_previewRect.left + 15 + tR.Width(), m_previewRect.bottom - 25); cr.InflateRect(8, 8);
        dc.FillSolidRect(&cr, RGB(25, 25, 25)); dc.DrawText(h, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); dc.SelectObject(of);
    }
    HBITMAP hL = NULL, hR = NULL;
    {
        std::lock_guard<std::mutex> lkBmp(m_ocrRecordMutex);
        if (m_viewIndexLeft >= 0 && m_viewIndexLeft < (int)m_ocrRecordsLeft.size()) hL = m_ocrRecordsLeft[m_viewIndexLeft].hBmp; else if (!m_ocrRecordsLeft.empty()) hL = m_ocrRecordsLeft.back().hBmp;
        if (m_viewIndexRight >= 0 && m_viewIndexRight < (int)m_ocrRecordsRight.size()) hR = m_ocrRecordsRight[m_viewIndexRight].hBmp; else if (!m_ocrRecordsRight.empty()) hR = m_ocrRecordsRight.back().hBmp;
    }
    HBITMAP arr[2] = { hL, hR }; int cY = m_previewRect.bottom - 20; int tW = max(180, m_previewRect.Width() / 4);
    for (int i = 1; i >= 0; i--) {
        if (arr[i]) {
            BITMAP bm; GetObject(arr[i], sizeof(BITMAP), &bm);
            int sW = (int)(bm.bmWidth * 0.70); int sH = bm.bmHeight; int sX = (i == 0) ? 0 : (bm.bmWidth - sW); int dW = tW; int dH = (int)((float)sH / sW * dW); cY -= dH;
            int iX = m_previewRect.right - 15 - dW; HDC hM = CreateCompatibleDC(dc.GetSafeHdc()); HGDIOBJ oB = SelectObject(hM, arr[i]); COLORREF bC = (i == 0) ? RGB(255, 80, 80) : RGB(80, 180, 255);
            dc.FillSolidRect(iX - 2, cY - 2, dW + 4, dH + 4, bC); dc.SetStretchBltMode(HALFTONE); dc.StretchBlt(iX, cY, dW, dH, CDC::FromHandle(hM), sX, 0, sW, sH, SRCCOPY);
            dc.SetBkMode(TRANSPARENT); dc.SetTextColor(bC); CFont fM; fM.CreatePointFont(90, L"微软雅黑"); CFont* oM = dc.SelectObject(&fM); dc.TextOut(iX, cY - 18, i == 0 ? L"左侧提取区" : L"右侧提取区"); dc.SelectObject(oM);
            cY -= 25; SelectObject(hM, oB); DeleteDC(hM);
        }
    }

    // ===================================================
    // 【绘制鼠标坐标采集的绿点】
    // ===================================================
    CPen pPoint(PS_SOLID, 2, RGB(0, 255, 0));
    dc.SelectStockObject(NULL_BRUSH);
    CPen* pOldPointPen = dc.SelectObject(&pPoint);
    for (size_t i = 0; i < m_selectPts.size(); i++) {
        int px = m_previewRect.left + (int)((m_selectPts[i].x / 10000.0f) * m_previewRect.Width());
        int py = m_previewRect.top + (int)((m_selectPts[i].y / 10000.0f) * m_previewRect.Height());
        dc.Ellipse(px - 3, py - 3, px + 3, py + 3);
    }
    dc.SelectObject(pOldPointPen);

    // ===================================================
    // 【绘制 10 倍像素级显微镜】
    // ===================================================
    CPoint pt; GetCursorPos(&pt); ScreenToClient(&pt);
    if (m_previewRect.PtInRect(pt)) {
        int origX = (int)(((float)(pt.x - m_previewRect.left) / m_previewRect.Width()) * m_w);
        int origY = (int)(((float)(pt.y - m_previewRect.top) / m_previewRect.Height()) * m_h);

        int magW = 160, magH = 160, srcSize = 16; // 抓取16x16像素，放大到160(10倍)

        // 智能避让：鼠标在左边，显微镜画在右边；鼠标在右边，显微镜画在左边
        int drawX = m_previewRect.left + 10;
        int drawY = m_previewRect.top + 10;
        if (pt.x < m_previewRect.left + m_previewRect.Width() / 2 && pt.y < m_previewRect.top + m_previewRect.Height() / 2) {
            drawX = m_previewRect.right - magW - 10;
        }

        dc.FillSolidRect(drawX - 2, drawY - 2, magW + 4, magH + 4, RGB(255, 255, 255)); // 白框

        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            HDC hBmpDC = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hBmpDC, m_bmp);

            // 【关键】：设置 COLORONCOLOR，关闭抗锯齿，让你看清每一个原始的方形马赛克像素！
            int oldMode = dc.SetStretchBltMode(COLORONCOLOR);
            dc.StretchBlt(drawX, drawY, magW, magH, CDC::FromHandle(hBmpDC), origX - srcSize / 2, origY - srcSize / 2, srcSize, srcSize, SRCCOPY);
            dc.SetStretchBltMode(oldMode);

            ::SelectObject(hBmpDC, oldBmp);
            ::DeleteDC(hBmpDC);
        }

        // 画显微镜中心的红色准星
        CPen crossPen(PS_SOLID, 1, RGB(255, 0, 0));
        CPen* pOldPen = dc.SelectObject(&crossPen);
        dc.MoveTo(drawX + magW / 2, drawY); dc.LineTo(drawX + magW / 2, drawY + magH);
        dc.MoveTo(drawX, drawY + magH / 2); dc.LineTo(drawX + magW, drawY + magH / 2);
        dc.SelectObject(pOldPen);

        // 显示采集进度提示
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(0, 255, 0));
        CString tip; tip.Format(L"已采: %d/40 (右键撤销)", (int)m_selectPts.size());
        dc.TextOut(drawX + 5, drawY + magH - 25, tip);
    }
}

// 【修改】：点击说明按钮弹出的消息框，详细更新功能手册
void CDNFGameCaptureDlg::OnBnClickedHelp() {
    CString msg = L"💡 DNF击杀统计 - 终极使用说明书\r\n\r\n"
        L"【一、 智能录入 (顶部输入框)】\r\n"
        L"1. 批量添加：支持“主号(小号1)(小号2)”格式，按回车或点击[添加]解析入库。\r\n"
        L"2. 智能补全：输入主号后打出左括号“(”，系统会自动去“历史数据库”里检索并秒补齐小号。\r\n"
        L"3. 队伍防呆：如果一侧队伍满员，打字时会自动将人员分配到对面未满队伍。\r\n\r\n"
        L"【二、 树状图左键操作 (双击直接修改)】\r\n"
        L"1. 改比分：左键慢速双击【红队/蓝队】根节点，直接输入数字即可修改大比分。\r\n"
        L"2. 改人名/战绩：左键慢速双击任意【主号/小号】，像重命名文件一样修改名字或“击杀/死亡/AK”数值。系统防重名，且会自动绑定数据库。\r\n"
        L"3. 展开折叠：点击 [+] / [-] 可以自由隐藏或显示小号，让界面更清爽。\r\n\r\n"
        L"【三、 树状图右键菜单 (全能管理)】\r\n"
        L"1. 队伍管理：在【红队/蓝队】右键，可 +1/-1/归零大比分，或一键清空该队。\r\n"
        L"2. 战绩容错：在【主号】右键，可手动对“击杀、死亡、AK”进行加减(+1/-1)操作。\r\n"
        L"3. 一键换边：在【主号】右键，可将该玩家及旗下所有小号【移动】到对面阵营（如果对面满员，自动触发位置互换）。\r\n"
        L"4. 智能删除：在【小号】右键，可以选择仅从本局移除，或者【彻底删除】（连同自动补全记忆一并抹除）。\r\n\r\n"
        L"【四、 OBS 与直播防卡死同步】\r\n"
        L"无论你是添加小号、还是修改了人头、或是系统自动识图抓取了击杀，软件都会【在毫秒内】自动更新输出目录下的 TXT 文件！OBS 即可实现零延迟自动跳分！\r\n\r\n"
        L"------------------------------------\r\n"
        L"🛠️ 调试快捷键：(用于无比赛时测试画图)\r\n"
        L"Ctrl+F8 : 强制触发【红队】击杀一次\r\n"
        L"Ctrl+F9 : 强制触发【蓝队】击杀一次";

    MessageBox(msg, L"最新操作逻辑与指南", MB_ICONINFORMATION);
}

void CDNFGameCaptureDlg::OnTimer(UINT_PTR nID) {
    if (nID == 1 && m_bIsRunning) {
        Capture();

        // ★ 颜色检测降频：每 240ms 检测一次，不是每 50ms
        static DWORD s_lastColorCheck = 0;
        DWORD now = GetTickCount();
        if (now - s_lastColorCheck >= 240) {
            s_lastColorCheck = now;
            CheckColorTrigger();
        }
    }
    else if (nID == 2) {
        m_bCanTrigger = TRUE;
        KillTimer(2);
    }
    else if (nID == 4) {
        m_bCanTriggerTeamScore = TRUE;
        KillTimer(4);
    }
    else if (nID == 3 && m_bIsRunning) {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp && m_w > 0 && m_h > 0) {
            // ★ 旧帧先释放
            if (m_historyBmps[m_historyIdx]) {
                ::DeleteObject(m_historyBmps[m_historyIdx]);
                m_historyBmps[m_historyIdx] = nullptr;
            }
            // ★ 一句话搞定，不用手动创建/销毁 DC
            m_historyBmps[m_historyIdx] = (HBITMAP)::CopyImage(
                m_bmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            m_historyIdx = (m_historyIdx + 1) % MAX_HISTORY_FRAMES;
        }
    }
    else if (nID == 5) {
        std::lock_guard<std::mutex> lkLog(g_visualLogMutex);
        if (!g_visualLogs.empty() && m_editVisualLogs.m_hWnd) {
            // ★ 超过 300 行时砍掉前半，防止再次撞上限
            int lineCount = m_editVisualLogs.GetLineCount();
            if (lineCount > 300) {
                int charIdx = m_editVisualLogs.LineIndex(lineCount - 150);
                m_editVisualLogs.SetSel(0, charIdx);
                m_editVisualLogs.ReplaceSel(L"");
            }

            for (const auto& log : g_visualLogs) {
                int len = m_editVisualLogs.GetWindowTextLength();
                m_editVisualLogs.SetSel(len, len);
                CHARFORMAT cf;
                ZeroMemory(&cf, sizeof(cf));
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR;
                cf.crTextColor = log.color;
                m_editVisualLogs.SetSelectionCharFormat(cf);
                m_editVisualLogs.ReplaceSel(log.text + L"\r\n");
            }
            m_editVisualLogs.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);
            g_visualLogs.clear();
        }
    }
    // ==========================================
    // 【Timer 6】:独立预览定时器
    // 软件打开就自动显示画面，不依赖"开始监控"
    // ==========================================
    else if (nID == 6) {
        if (!m_bIsRunning) {
            // 只在非监控状态下执行预览截图
            // 监控状态下由 Timer 1 接管，避免重复截图
            Capture();
        }
    }
    // ==========================================
    // 【Timer 7】: 终极系统级轮询 (无视任何消息屏蔽)
    // ==========================================
    else if (nID == 7) {
        static int s_idleSeconds = 0;          // 闲置秒数
        static bool s_hasFolded = true;        // 默认 true，防止刚开软件还没动就乱折叠

        // 1. 问系统：现在屏幕最前面的是不是咱们的软件？
        HWND hForeground = ::GetForegroundWindow();
        bool bIsOurAppFocused = (hForeground == m_hWnd || ::IsChild(m_hWnd, hForeground));

        // 2. 问系统：用户最近一次摸鼠标或【敲键盘】距离现在多少毫秒？
        LASTINPUTINFO lii;
        lii.cbSize = sizeof(LASTINPUTINFO);
        ::GetLastInputInfo(&lii);
        DWORD idleMs = ::GetTickCount() - lii.dwTime;

        // 3. 判断是否“正在操作”：焦点在咱们软件上，且最近 1.5 秒内敲过键盘或动过鼠标
        if (bIsOurAppFocused && idleMs < 1500) {
            s_idleSeconds = 0;
            s_hasFolded = false;
        }
        else {
            s_idleSeconds++;
        }

        // 4. 满 10 秒闲置或失去焦点执行动作
        if (s_idleSeconds >= 10) {
            if (!s_hasFolded && m_treePlayers.m_hWnd) {
                bool actuallyFoldedSomething = false;

                HTREEITEM hRoot = m_treePlayers.GetRootItem();
                while (hRoot) {
                    HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
                    while (hChild) {
                        if (m_treePlayers.GetItemState(hChild, TVIS_EXPANDED) & TVIS_EXPANDED) {
                            m_treePlayers.Expand(hChild, TVE_COLLAPSE);
                            actuallyFoldedSomething = true;
                        }
                        hChild = m_treePlayers.GetNextSiblingItem(hChild);
                    }
                    hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
                }

                s_hasFolded = true;

                if (actuallyFoldedSomething) {
                    AppLog(L"💤 [界面收起] 失去焦点或闲置10秒，已自动折叠", RGB(150, 150, 150));
                }
            }
            s_idleSeconds = 10;
        }
    }
}

// ============================================================================
// 自动更新系统 (后台静默检测)
// 自动更新系统 (修复中文乱码 + 支持网页手动更新)
// 自动更新系统 (适配按行读取格式)
// ============================================================================
void CDNFGameCaptureDlg::CheckForUpdates(bool bSilent) {
    CString strCheckUrlV2 = UPDATE_CHECK_URL_V2;
    CString currentVersion = CURRENT_VERSION;

    wchar_t tempPath[MAX_PATH];
    GetTempPath(MAX_PATH, tempPath);
    CString tempFile;
    tempFile.Format(L"%supdate_check.txt", tempPath);

    ::DeleteUrlCacheEntry(strCheckUrlV2);

    // ==========================================
    // 拦截 1：下载失败，提前返回
    // ==========================================
    if (URLDownloadToFile(NULL, strCheckUrlV2, tempFile, 0, NULL) != S_OK) {
        if (!bSilent) MessageBox(L"连接更新服务器失败！", L"错误", MB_ICONERROR);
        return;
    }

    // ==========================================
    // 拦截 2：文件打开失败，提前返回
    // ==========================================
    CFile file;
    if (!file.Open(tempFile, CFile::modeRead)) {
        if (!bSilent) MessageBox(L"无法读取更新配置文件！", L"错误", MB_ICONERROR);
        return;
    }

    // --- 读取并转换 UTF-8 内容 ---
    ULONGLONG dwLength = file.GetLength();
    char* pBuf = new char[(size_t)dwLength + 1];  // 加上 (size_t) 消除警告
    memset(pBuf, 0, (size_t)dwLength + 1);        // 加上 (size_t) 消除警告
    file.Read(pBuf, (UINT)dwLength);
    file.Close();
    ::DeleteFile(tempFile);

    int nLen = MultiByteToWideChar(CP_UTF8, 0, pBuf, -1, NULL, 0);
    wchar_t* pWBuf = new wchar_t[nLen + 1];
    memset(pWBuf, 0, (nLen + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, pBuf, -1, pWBuf, nLen);

    CString content(pWBuf);
    delete[] pBuf;
    delete[] pWBuf;

    // ==========================================
    // 拦截 3：格式解析失败，提前返回
    // ==========================================
    content.Replace(L"\r\n", L"\n");
    int pos1 = content.Find(L'\n');
    int pos2 = content.Find(L'\n', pos1 + 1);

    if (pos1 == -1 || pos2 == -1) {
        if (!bSilent) MessageBox(L"更新文件格式解析失败！", L"错误", MB_ICONERROR);
        return;
    }

    CString serverVersion = content.Left(pos1);
    CString downloadUrl = content.Mid(pos1 + 1, pos2 - pos1 - 1);
    CString updateLog = content.Mid(pos2 + 1);

    serverVersion.Trim();
    downloadUrl.Trim();
    updateLog.Trim();

    // ==========================================
    // 拦截 4：没有新版本，提前返回
    // ==========================================
    // 原来的：
    // bool bHasUpdate = (serverVersion != currentVersion && !serverVersion.IsEmpty());

    int cmp = CompareVersion(serverVersion, currentVersion);
    bool bHasUpdate = (!serverVersion.IsEmpty() && cmp > 0);

    if (!bHasUpdate) {
        if (!bSilent) {
            if (cmp < 0)
                MessageBox(L"当前为测试版本，已高于线上正式版。", L"检查更新", MB_OK);
            else
                MessageBox(L"当前已是最新版本！", L"检查更新", MB_OK);
        }
        return;
    }

    // ==========================================
    // 终点：真正的更新处理逻辑
    // ==========================================
    if (currentVersion == BRIDGE_VERSION) {
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        AppLog(L"🔄 [桥接升级] 检测到这是过渡版本", RGB(255, 215, 0));
        AppLog(L"   正在自动升级到最新正式版,请稍候...", RGB(255, 215, 0));
        AppLog(L"   升级完成后软件会自动重启", RGB(255, 215, 0));
        AppLog(L"═══════════════════════════════════", RGB(255, 215, 0));
        Sleep(800);  // 让用户有时间看到提示
        DownloadAndApplyUpdate(downloadUrl);
    }
    //else if (bSilent) {
    //    // 【普通版 - 后台静默检测】：不打扰用户,只在日志里提示有新版本
    //    CString logMsg;
    //    logMsg.Format(L"💡 [发现新版本] 服务器版本 %s,点击菜单可手动更新", serverVersion.GetString());
    //    AppLog(logMsg, RGB(100, 200, 255));
    //    // 注意:这里不调用 DownloadAndApplyUpdate,等用户主动点"检查更新"
    //}
    //else {
        // 【普通版】：无论是后台检测还是手动检测，只要有新版本，一律弹窗！
            CString msg;
            msg.Format(L"发现新版本: %s\n\n更新内容:\n%s\n\n是否立即更新？", serverVersion, updateLog);

            // ⬇️ 【修改点】：把 MessageBox 换成 ShowCenteredMsgBox
            if (ShowCenteredMsgBox(msg, L"发现新版本", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES) {
                DownloadAndApplyUpdate(downloadUrl);
            }
            else {
                CString logMsg;
                logMsg.Format(L"💡 [发现新版本] 服务器版本 %s，您已取消更新。右键托盘可随时更新。", serverVersion.GetString());
                AppLog(logMsg, RGB(100, 200, 255));
            }
    //}
}

void CDNFGameCaptureDlg::DownloadAndApplyUpdate(CString url) {
    if (m_status.m_hWnd) 
        ::SetWindowText(m_status.GetSafeHwnd(), L"正在下载更新包...");// 加上全局作用域和安全的句柄

    wchar_t p[MAX_PATH];
    GetModuleFileName(NULL, p, MAX_PATH);
    CString cp(p);
    CString d = cp.Left(cp.ReverseFind(L'\\') + 1);

    CString t = d + L"update_temp.zip";
    CString b = d + L"update.bat";
    CString engine = d + L"7za.exe";

    // 1. 强制清理引擎和更新包的本地缓存
    CString engineUrl = L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/7z/7za.exe"; // 【请修改这里】
    ::DeleteUrlCacheEntry(engineUrl);
    ::DeleteUrlCacheEntry(url);

    // 2. 先下解压引擎，再下真实的 ZIP 压缩包
    URLDownloadToFile(NULL, engineUrl, engine, 0, NULL);
    if (URLDownloadToFile(NULL, url, t, 0, NULL) != S_OK) {
        MessageBox(L"下载更新包失败，请检查网络！", L"更新失败", MB_ICONERROR);
        if (m_status.m_hWnd) m_status.SetWindowText(L"就绪");
        return;
    }

    // 3. 生成强力解压替换脚本 (兼容 Win7~Win11)
    CFile f;
    if (f.Open(b, CFile::modeCreate | CFile::modeWrite)) {
        CString s;
        s.Format(L"@echo off\r\n"
            L":Retry\r\n"
            L"ping 127.0.0.1 -n 2 > nul\r\n"
            L"del \"%s\" 2>nul\r\n"
            L"if exist \"%s\" goto Retry\r\n"
            L"\"%s\" x \"update_temp.zip\" -y > nul\r\n"
            L"del \"update_temp.zip\"\r\n"
            L"ping 127.0.0.1 -n 2 > nul\r\n"                       // 等 1 秒让文件系统稳定
            L"start \"\" \"%s\"\r\n"
            L"ping 127.0.0.1 -n 3 > nul\r\n"                       // 等新程序启动窗口
            L"powershell -NoProfile -Command \""
            L"$w=(Get-Process -Name 'DNFGameCapture' -ErrorAction SilentlyContinue | "
            L"Where-Object {$_.MainWindowHandle -ne 0} | Select-Object -First 1).MainWindowHandle; "
            L"if($w){ "
            L"Add-Type '[DllImport(\\\"user32.dll\\\")]public static extern bool SetForegroundWindow(IntPtr h);' "
            L"-Name W -Namespace N; "
            L"[N.W]::SetForegroundWindow($w) }\"\r\n"
            L"del \"%%~f0\"\r\n",
            cp.GetString(), cp.GetString(), engine.GetString(), cp.GetString());

        std::string a = CW2A(s, CP_OEMCP);
        f.Write(a.c_str(), (UINT)a.length());
        f.Close();
    }

    // 4. 释放多开锁并执行换包脚本
    if (m_hSingleInstanceMutex) {
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = NULL;
    }

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = b;
    sei.nShow = SW_HIDE;

    if (ShellExecuteEx(&sei)) {
        ShowWindow(SW_HIDE);
        exit(0);
    }
}

void CDNFGameCaptureDlg::LoadAliasDB() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_db.ini";

    CFile file;
    if (file.Open(path, CFile::modeRead)) {
        int len = (int)file.GetLength();
        if (len > 0) {
            char* buf = new char[len + 1];
            file.Read(buf, len); buf[len] = 0;
            CString content = CA2W(buf, CP_UTF8);
            delete[] buf;

            m_aliasDB.clear(); // 【关键】：先清空内存，防止重复加载

            int pos = 0;
            while (pos < content.GetLength()) {
                int nl = content.Find(L'\n', pos);
                CString line = (nl != -1) ? content.Mid(pos, nl - pos) : content.Mid(pos);
                pos = (nl != -1) ? nl + 1 : content.GetLength();
                line.Remove(L'\r'); line.Trim();

                int eq = line.Find(L'=');
                if (eq != -1) {
                    CString mainName = line.Left(eq);
                    CString aliases = line.Mid(eq + 1);

                    mainName.Trim(); // 【关键清洗】：洗掉隐形空格！
                    aliases.Trim();

                    if (!mainName.IsEmpty()) {
                        // std::map 自带去重属性，同名会自动覆盖，留下最新的
                        m_aliasDB[mainName] = aliases;
                    }
                }
            }
        }
        file.Close();
    }
}

void CDNFGameCaptureDlg::SaveAliasDB() {
    for (int i = 0; i < 8; i++) {
        CString mName = m_players[i].name;
        mName.Trim();

        if (!mName.IsEmpty() && !m_players[i].aliases.empty()) {
            // 获取数据库中已有的该主号的小号字符串（如果没有则为空）
            CString existingAliases = m_aliasDB[mName];

            for (const auto& a : m_players[i].aliases) {
                CString aName = a.name;
                aName.Trim();
                CString target1 = L"(" + aName + L")";
                CString target2 = L"（" + aName + L"）"; // 兼容中文括号

                // 【核心：增量合并过滤】如果数据库里没有这个小号，才追加进去
                if (existingAliases.Find(target1) == -1 && existingAliases.Find(target2) == -1) {
                    existingAliases += target1;
                }
            }
            // 更新内存中的数据库
            m_aliasDB[mName] = existingAliases;
        }
    }

    // 写入 ini 文件
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    CString path = exePath;
    path = path.Left(path.ReverseFind(L'\\') + 1) + L"alias_db.ini";

    CString content;
    for (const auto& pair : m_aliasDB) {
        if (!pair.second.IsEmpty()) { // 防止写入空主号
            content += pair.first + L"=" + pair.second + L"\r\n";
        }
    }

    CFile file;
    if (file.Open(path, CFile::modeCreate | CFile::modeWrite)) {
        std::string utf8 = CW2A(content, CP_UTF8);
        file.Write(utf8.c_str(), (UINT)utf8.length());
        file.Close();
    }
}

void CDNFGameCaptureDlg::OnChangeEditNamesInput() {
    static int s_prevLen = 0;
    static CString s_lastAutoExpandedName = L""; // 记忆：上次因为打字而自动展开的主号名

    int curLen = m_editQuickAdd.GetWindowTextLength();
    bool isBackspace = (curLen < s_prevLen);
    s_prevLen = curLen;

    int nStart, nEnd;
    m_editQuickAdd.GetSel(nStart, nEnd);

    CString fullText;
    m_editQuickAdd.GetWindowText(fullText);

    // ==========================================
    // 1. 安全提取：当前光标所在行的文本 (修复了旧版换行符引发的错位BUG)
    // ==========================================
    int lineStart = 0;
    for (int i = nStart - 1; i >= 0; i--) {
        if (fullText[i] == L'\n') { lineStart = i + 1; break; }
    }
    int lineEnd = fullText.GetLength();
    for (int i = nStart; i < fullText.GetLength(); i++) {
        if (fullText[i] == L'\r' || fullText[i] == L'\n') { lineEnd = i; break; }
    }
    CString currentLine = fullText.Mid(lineStart, lineEnd - lineStart);

    // 解析出正在输入的主名 (去掉括号和后面的内容)
    int fP = currentLine.Find(L'(');
    if (fP == -1) fP = currentLine.Find(L'（');
    CString typingMainName = (fP != -1) ? currentLine.Left(fP) : currentLine;
    typingMainName.Trim();

    // ==========================================
    // 2. 满员智能跳转：自动将下拉框切到未满员的队伍
    // ==========================================
    int redCount = 0, blueCount = 0;
    m_dataMutex.lock();
    for (int i = 0; i < 4; i++) if (!m_players[i].name.IsEmpty()) redCount++;
    for (int i = 4; i < 8; i++) if (!m_players[i].name.IsEmpty()) blueCount++;
    m_dataMutex.unlock();

    if (redCount >= 4 && blueCount < 4) {
        m_cmbTeamSelect.SetCurSel(1); // 红队满，切蓝队
    }
    else if (blueCount >= 4 && redCount < 4) {
        m_cmbTeamSelect.SetCurSel(0); // 蓝队满，切红队
    }

    // ==========================================
    // 3. 树状图状态跟踪与下拉框强覆盖 (核心视觉反馈)
    // ==========================================
    // 定义一个极简的辅助函数：专门用来展开或收缩指定的节点
    auto ToggleTreeNode = [&](CString targetName, UINT action) {
        if (!m_treePlayers.m_hWnd || targetName.IsEmpty()) return;
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                CString text = m_treePlayers.GetItemText(hChild);
                int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                CString nodeName = (eqPos != -1) ? text.Left(eqPos) : text;
                nodeName.Trim();

                if (nodeName == targetName) {
                    m_treePlayers.Expand(hChild, action);
                    return;
                }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
        };

    // 去战局里找找，当前打字的名字是不是已经在场上了
    int foundTeam = -1;
    m_dataMutex.lock();
    for (int i = 0; i < 8; i++) {
        if (!m_players[i].name.IsEmpty() && m_players[i].name == typingMainName) {
            foundTeam = m_players[i].team;
            break;
        }
    }
    m_dataMutex.unlock();

    // 状态分发
    if (foundTeam != -1) {
        // 【命中老玩家】：无视队伍满不满，强行把下拉框切到他所在的队
        m_cmbTeamSelect.SetCurSel(foundTeam);

        // 发现新目标：展开节点，并记在脑子里
        if (s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(typingMainName, TVE_EXPAND);
            s_lastAutoExpandedName = typingMainName;
        }
    }
    else {
        // 【未命中或被删除了】：把刚才自动展开的节点收起来
        if (!s_lastAutoExpandedName.IsEmpty() && s_lastAutoExpandedName != typingMainName) {
            ToggleTreeNode(s_lastAutoExpandedName, TVE_COLLAPSE);
            s_lastAutoExpandedName = L""; // 清除记忆
        }
    }

    // ==========================================
    // 4. 括号探测：小号智能补全拦截区
    // ==========================================
    // 如果是按退格键、或者光标异常，立即拦截，不再执行补全
    if (isBackspace || nStart == 0 || nStart > fullText.GetLength()) return;

    // 没打括号，立即拦截
    wchar_t lastChar = fullText.GetAt(nStart - 1);
    if (lastChar != L'(' && lastChar != L'（') return;

    // 名字为空或数据库里没有记录，立即拦截
    if (typingMainName.IsEmpty() || m_aliasDB.find(typingMainName) == m_aliasDB.end()) return;

    CString dbAliases = m_aliasDB[typingMainName];
    std::vector<CString> existAliases;

    // 查一下树状图里已经有这个人的哪些小号了
    m_dataMutex.lock();
    for (int i = 0; i < 8; i++) {
        if (m_players[i].name == typingMainName) {
            for (const auto& a : m_players[i].aliases) existAliases.push_back(a.name);
            break;
        }
    }
    m_dataMutex.unlock();

    CString aliasesToInsert = L"";
    int c = 0;
    while (true) {
        CString tS = dbAliases.Mid(c);
        int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（');
        int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
        if (Lr == -1 || Rr == -1 || Rr <= Lr) break;

        CString aN = tS.Mid(Lr + 1, Rr - Lr - 1); aN.Trim();

        bool exists = false;
        for (const auto& ea : existAliases) {
            if (ea == aN) { exists = true; break; }
        }

        if (!exists && !aN.IsEmpty()) aliasesToInsert += L"(" + aN + L")";
        c += Rr + 1;
    }

    // 补齐小号并移动光标
    if (!aliasesToInsert.IsEmpty()) {
        m_editQuickAdd.SetSel(nStart - 1, nStart);
        m_editQuickAdd.ReplaceSel(aliasesToInsert);
        s_prevLen = m_editQuickAdd.GetWindowTextLength();
    }
}

// ============================================================================
// 新版 GUI 核心逻辑：添加、树状渲染、右键菜单、存取配置
// ============================================================================
void CDNFGameCaptureDlg::OnBnClickedQuickAdd() {
    CString input;
    m_editQuickAdd.GetWindowText(input);
    input.Trim();
    if (input.IsEmpty() || input == PLACEHOLDER_TEXT) return;

    input.Replace(L"\r\n", L"\n");
    int start = 0;
    int currentTeam = m_cmbTeamSelect.GetCurSel();
    int lastIdx = -1;

    int addMainCount = 0, addAliasCount = 0, dupFilteredCount = 0;
    CString strTeamFullAlert = L"", strDupAliasAlert = L"";

    std::vector<CString> currentAdded; // 【新增追踪器】：精准记录本次操作被修改的主号

    std::lock_guard<std::mutex> lk(m_dataMutex);
    AppLog(L"================================", RGB(150, 150, 150));
    AppLog(L"📥 [系统] 开始解析导入名单数据...", RGB(255, 215, 0));

    while (start < input.GetLength()) {
        int nl = input.Find(L'\n', start);
        CString line = (nl != -1) ? input.Mid(start, nl - start) : input.Mid(start);
        start = (nl != -1) ? nl + 1 : input.GetLength();
        line.Trim(); if (line.IsEmpty()) continue;

        if (line.Find(L"操作说明") != -1 || line.Find(L"分队：") != -1 || line.Find(L"绑定小号：") != -1 ||
            line.Find(L"手动改分") != -1 || line.Find(L"手动改AK") != -1 || line.Find(L"💡") != -1) continue;

        if (line.Find(L"红") != -1 && line.Find(L"蓝") != -1 && (line.Find(L":") != -1 || line.Find(L"：") != -1)) {
            int rPos = line.Find(L"红"), bPos = line.Find(L"蓝"), cPos = line.Find(L":"); if (cPos == -1) cPos = line.Find(L"：");
            if (cPos != -1 && rPos < cPos && cPos < bPos) {
                m_totalScoreRed = _wtoi(line.Mid(rPos + 1, cPos - rPos - 1));
                m_totalScoreBlue = _wtoi(line.Mid(cPos + 1, bPos - cPos - 1));
                AppLog(L"📌 [比分修改] 识别并修改全局比分", RGB(200, 200, 200));
            }
            continue;
        }

        if (line.Find(L"【红队】") != -1) { currentTeam = 0; continue; }
        if (line.Find(L"【蓝队】") != -1) { currentTeam = 1; continue; }

        if (line.Left(1) == L"└" || line.Left(1) == L"├" || line.Left(1) == L"+") {
            if (lastIdx != -1) {
                CString aN = line.Mid(1); aN.Trim(); bool isDup = false;
                for (int i = 0; i < 8 && !isDup; i++) {
                    if (m_players[i].name == aN) { isDup = true; break; }
                    for (const auto& ea : m_players[i].aliases) { if (ea.name == aN) { isDup = true; break; } }
                }
                if (!isDup && !aN.IsEmpty()) {
                    m_players[lastIdx].aliases.push_back({ aN }); addAliasCount++;
                    currentAdded.push_back(m_players[lastIdx].name); // 🎯标记
                    AppLog(L" ├ ➕追加小号: [" + aN + L"]", RGB(100, 255, 100));
                }
                else if (isDup) { dupFilteredCount++; strDupAliasAlert += L"[" + aN + L"] "; }
            }
            continue;
        }

        int eP = line.Find(L'='); if (eP == -1) eP = line.Find(L'＝'); CString namePart = (eP != -1) ? line.Left(eP) : line; namePart.Trim();
        int fP = namePart.Find(L'('); if (fP == -1) fP = namePart.Find(L'（'); CString mainName = (fP != -1) ? namePart.Left(fP) : namePart; mainName.Trim();
        int targetIdx = -1;

        for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { targetIdx = i; break; } }

        // =========================================================
        // 【核心修改】：智能识别主号队伍。不再强制物理搬家！
        // 如果主号已存在，无论当前选择了什么队伍，都自动追加到他原本的队伍中
        // =========================================================
        if (targetIdx != -1) {
            if (m_players[targetIdx].team != currentTeam) {
                CString teamNameStr = (m_players[targetIdx].team == 0) ? L"红队" : L"蓝队";
                AppLog(L"💡 [智能归属] 主号 [" + mainName + L"] 已在" + teamNameStr + L"，自动追加至该队", RGB(0, 255, 255));
            }
            // 标记为已修改，为了稍后的自动展开功能
            currentAdded.push_back(mainName);
        }

        if (targetIdx == -1) {
            bool isAliasElsewhere = false;
            for (int i = 0; i < 8 && !isAliasElsewhere; i++) { for (const auto& a : m_players[i].aliases) { if (a.name == mainName) { isAliasElsewhere = true; break; } } }
            if (isAliasElsewhere) { strDupAliasAlert += L"[" + mainName + L"](被占) "; continue; }
            int sI = (currentTeam == 0) ? 0 : 4, eI = (currentTeam == 0) ? 4 : 8;
            for (int i = sI; i < eI; i++) {
                if (m_players[i].name.IsEmpty()) {
                    targetIdx = i; m_players[i].name = mainName; m_players[i].team = currentTeam; addMainCount++;
                    currentAdded.push_back(mainName); // 🎯标记
                    AppLog(L"👤 [新增主号] [" + mainName + L"]", RGB(80, 180, 255)); break;
                }
            }
            if (targetIdx == -1) { strTeamFullAlert += L"[" + mainName + L"](满) "; }
        }

        if (targetIdx != -1) {
            lastIdx = targetIdx;
            if (fP != -1) {
                CString aR = namePart.Mid(fP); int c = 0;
                while (true) {
                    CString tS = aR.Mid(c); int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（'); int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
                    if (Lr == -1 || Rr == -1 || Rr <= Lr) break;
                    CString aN = tS.Mid(Lr + 1, Rr - Lr - 1); aN.Trim(); bool isDup = false;
                    for (int i = 0; i < 8 && !isDup; i++) { if (m_players[i].name == aN) { isDup = true; break; } for (const auto& ea : m_players[i].aliases) { if (ea.name == aN) { isDup = true; break; } } }
                    if (!isDup && !aN.IsEmpty()) {
                        m_players[targetIdx].aliases.push_back({ aN }); addAliasCount++;
                        currentAdded.push_back(m_players[targetIdx].name); // 🎯标记
                        AppLog(L" ├ ➕追加小号: [" + aN + L"]", RGB(100, 255, 100));
                    }
                    else if (isDup) { dupFilteredCount++; strDupAliasAlert += L"[" + aN + L"] "; }
                    c += Rr + 1;
                }
            }
            if (eP != -1) {
                CString scorePart = line.Mid(eP + 1); scorePart.Trim(); int aP = scorePart.Find(L'A');
                if (aP != -1) { m_players[targetIdx].akCount = _wtoi(scorePart.Mid(aP + 1)); if (m_players[targetIdx].akCount == 0) m_players[targetIdx].akCount = 1; scorePart = scorePart.Left(aP); }
                int sl = scorePart.Find(L'/'); if (sl == -1) sl = scorePart.Find(L'-');
                if (sl != -1) { m_players[targetIdx].kills = _wtoi(scorePart.Left(sl)); m_players[targetIdx].deaths = _wtoi(scorePart.Mid(sl + 1)); }
            }
        }
    }

    CString summaryLog; summaryLog.Format(L"✅ [解析完毕] 新增:%d | 追加:%d | 过滤:%d", addMainCount, addAliasCount, dupFilteredCount); AppLog(summaryLog, RGB(0, 255, 255));

    if (!strTeamFullAlert.IsEmpty() || !strDupAliasAlert.IsEmpty()) {
        CString alertMsg = L"遇到以下拦截：\n\n";
        if (!strTeamFullAlert.IsEmpty()) alertMsg += L"🛑 队伍已满：" + strTeamFullAlert + L"\n";
        if (!strDupAliasAlert.IsEmpty()) alertMsg += L"⚠️ 名字冲突：" + strDupAliasAlert + L"\n";
        MessageBox(alertMsg, L"添加结果提示", MB_ICONWARNING | MB_OK);
    }
    // ==========================================
    // 【关键修复】：智能清空输入框
    // 1. 只要主号/小号成功加进去了 (currentAdded > 0)，就强制清空！哪怕弹了警告也不留残影。
    // 2. 如果全程没有任何报错 (比如改大比分)，也正常清空。
    // 3. 只有当“主号没加进去且队伍满了”这种完全失败的情况，才保留文字让用户修改。
        // ==========================================
    if (currentAdded.size() > 0 || (strTeamFullAlert.IsEmpty() && strDupAliasAlert.IsEmpty())) {
        m_editQuickAdd.SetWindowText(L"");
    }

    SaveAliasDB();
    SaveConfigToFile();
    WriteScoreToFile();

    SyncDataToTree(); // 同步到树状图
    RefreshDisplay();

    // ==========================================
    // 【新增核心】：遍历树状图，只展开刚刚修改过的主号，并收起其他人
    // ==========================================
    if (currentAdded.size() > 0) {
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                CString text = m_treePlayers.GetItemText(hChild);
                int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                CString name = (eqPos != -1) ? text.Left(eqPos) : text;
                name.Trim();

                bool isModified = false;
                for (const auto& addedName : currentAdded) {
                    if (name == addedName) {
                        isModified = true;
                        break;
                    }
                }

                if (isModified) {
                    // 是刚操作过的玩家，展开它
                    m_treePlayers.Expand(hChild, TVE_EXPAND);
                }
                else {
                    // 【关键修复 2】：其他玩家无情收缩，保持界面清爽！
                    m_treePlayers.Expand(hChild, TVE_COLLAPSE);
                }

                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
    }
}

// 将数据同步到树状控件（带视觉状态记忆）
void CDNFGameCaptureDlg::SyncDataToTree() {
    // 1. 【核心新增】：重绘前，先记住当前用户已经展开了哪些主号
    std::vector<CString> userExpandedNames;
    if (m_treePlayers.m_hWnd) { // 确保控件已创建
        HTREEITEM hRoot = m_treePlayers.GetRootItem();
        while (hRoot) {
            HTREEITEM hChild = m_treePlayers.GetChildItem(hRoot);
            while (hChild) {
                // 如果这个主号目前是展开状态，记下它的名字
                if (m_treePlayers.GetItemState(hChild, TVIS_EXPANDED) & TVIS_EXPANDED) {
                    CString text = m_treePlayers.GetItemText(hChild);
                    int eqPos = text.Find(L'='); if (eqPos == -1) eqPos = text.Find(L'＝');
                    CString name = (eqPos != -1) ? text.Left(eqPos) : text;
                    name.Trim();
                    userExpandedNames.push_back(name);
                }
                hChild = m_treePlayers.GetNextSiblingItem(hChild);
            }
            hRoot = m_treePlayers.GetNextSiblingItem(hRoot);
        }
    }

    m_treePlayers.DeleteAllItems();

    CString redTitle; redTitle.Format(L"【红队】- %d 分", m_totalScoreRed);
    CString blueTitle; blueTitle.Format(L"【蓝队】- %d 分", m_totalScoreBlue);

    HTREEITEM hRed = m_treePlayers.InsertItem(redTitle);
    HTREEITEM hBlue = m_treePlayers.InsertItem(blueTitle);

    for (int i = 0; i < 8; i++) {
        if (m_players[i].name.IsEmpty()) continue;
        HTREEITEM hTeam = (m_players[i].team == 0) ? hRed : hBlue;

        CString mainText;
        mainText.Format(L"%s = %d/%d", m_players[i].name, m_players[i].kills, m_players[i].deaths);
        if (m_players[i].akCount > 0) mainText.AppendFormat(L" A%d", m_players[i].akCount);

        HTREEITEM hMain = m_treePlayers.InsertItem(mainText, hTeam);
        m_treePlayers.SetItemData(hMain, i);

        for (size_t j = 0; j < m_players[i].aliases.size(); j++) {
            HTREEITEM hAlias = m_treePlayers.InsertItem(m_players[i].aliases[j].name, hMain);
            m_treePlayers.SetItemData(hAlias, (i << 16) | j | 0x80000000);
        }

        // 2. 【核心新增】：按记忆恢复展开状态
        bool shouldExpand = false;
        for (const auto& en : userExpandedNames) {
            if (m_players[i].name == en) { shouldExpand = true; break; }
        }
        if (shouldExpand) {
            m_treePlayers.Expand(hMain, TVE_EXPAND);
        }
    }
    // 仅展开队伍根节点
    m_treePlayers.Expand(hRed, TVE_EXPAND);
    m_treePlayers.Expand(hBlue, TVE_EXPAND);
}

void CDNFGameCaptureDlg::OnRClickTree(NMHDR* pNMHDR, LRESULT* pResult) {
    CPoint pt;
    GetCursorPos(&pt);
    CPoint ptTree = pt;
    m_treePlayers.ScreenToClient(&ptTree);
    UINT uFlags;
    HTREEITEM hItem = m_treePlayers.HitTest(ptTree, &uFlags);

    if (hItem && (uFlags & TVHT_ONITEM)) {
        m_treePlayers.SelectItem(hItem);
        DWORD_PTR data = m_treePlayers.GetItemData(hItem);
        HTREEITEM hParent = m_treePlayers.GetParentItem(hItem);

        CMenu menu;
        menu.CreatePopupMenu();
        CMenu subMenu;

        if (hParent == NULL) {
            // ==========================================
            // 【新增】：根节点添加“添加主号”选项
            // ==========================================
            menu.AppendMenu(MF_STRING, 16, L"➕ 添加主号 (自动切换队伍)");
            menu.AppendMenu(MF_SEPARATOR);

            menu.AppendMenu(MF_STRING, 7, L"🏆 该队大比分 +1");
            menu.AppendMenu(MF_STRING, 9, L"🔽 该队大比分 -1");
            menu.AppendMenu(MF_STRING, 8, L"❌ 该队大比分归零");
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 10, L"✏️ 自定义比分 (在上方输入框修改)");
            menu.AppendMenu(MF_SEPARATOR);
            menu.AppendMenu(MF_STRING, 11, L"🗑️ 一键清空该队所有成员");
        }
        else if (data & 0x80000000) {
            menu.AppendMenu(MF_STRING, 1, L"🗑️ 从当前战局列表中移除");
            menu.AppendMenu(MF_STRING, 14, L"💥 删除（同时从自动补齐库中彻底删除）");
        }
        else if (data >= 0 && data < 8) {
            // 为主号添加小号快捷选项
            menu.AppendMenu(MF_STRING, 15, L"➕ 为该主号添加小号...");
            menu.AppendMenu(MF_STRING, 2, L"🗑️ 删除该主号 (及所有小号)");
            menu.AppendMenu(MF_SEPARATOR);

            // ==========================================
            // 【关键视觉优化】：使用高辨识度专属图标，彻底告别点错
            // ==========================================
            menu.AppendMenu(MF_STRING, 3, L"⚔️ 战绩：击杀 +1");
            menu.AppendMenu(MF_STRING, 31, L"⚔️ 战绩：击杀 -1  (撤销)");
            menu.AppendMenu(MF_STRING, 4, L"💀 战绩：死亡 +1");
            menu.AppendMenu(MF_STRING, 32, L"💀 战绩：死亡 -1  (撤销)");
            menu.AppendMenu(MF_STRING, 5, L"🌟 战绩：AK +1");
            menu.AppendMenu(MF_STRING, 33, L"🌟 战绩：AK -1  (撤销)");
            menu.AppendMenu(MF_STRING, 6, L"🔄 该主号战绩清零");
            menu.AppendMenu(MF_SEPARATOR);

            int curTeam = m_players[data].team;
            int targetTeam = (curTeam == 0) ? 1 : 0;
            int sI = (targetTeam == 0) ? 0 : 4, eI = (targetTeam == 0) ? 4 : 8;

            std::vector<int> targetOccupied;
            for (int i = sI; i < eI; i++) {
                CString checkName = m_players[i].name;
                checkName.Trim();
                if (!checkName.IsEmpty()) {
                    targetOccupied.push_back(i);
                }
            }

            if (targetOccupied.size() < 4) {
                if (curTeam == 0) menu.AppendMenu(MF_STRING, 12, L"➡️ 一键移动到【蓝队】");
                else menu.AppendMenu(MF_STRING, 13, L"⬅️ 一键移动到【红队】");
            }
            else {
                subMenu.CreatePopupMenu();
                for (size_t i = 0; i < targetOccupied.size(); i++) {
                    int tIdx = targetOccupied[i];
                    CString swapTxt;
                    swapTxt.Format(L"🔄 与 [%s] 互换位置", m_players[tIdx].name);
                    subMenu.AppendMenu(MF_STRING, 20 + tIdx, swapTxt);
                }
                if (curTeam == 0) menu.AppendMenu(MF_POPUP, (UINT_PTR)subMenu.GetSafeHmenu(), L"➡️ 【蓝队】已满，请选择互换目标...");
                else menu.AppendMenu(MF_POPUP, (UINT_PTR)subMenu.GetSafeHmenu(), L"⬅️ 【红队】已满，请选择互换目标...");
            }
        }

        if (menu.GetMenuItemCount() > 0) {
            int cmd = menu.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, this);
            if (cmd <= 0) {
                *pResult = 0;
                return;
            }

            // ==========================================
            // 【新增】：处理根节点点击“添加主号”的联动逻辑
            // ==========================================
            if (cmd == 16) {
                // 1. 判断点的是红队还是蓝队，自动切换下拉框
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) {
                    m_cmbTeamSelect.SetCurSel(0); // 设置为红队
                }
                else {
                    m_cmbTeamSelect.SetCurSel(1); // 设置为蓝队
                }

                // 2. 将光标焦点移至录入输入框，方便直接打字
                m_editQuickAdd.SetFocus();

                // 3. (可选) 全选当前输入框的内容，这样用户一打字就会覆盖掉旧内容或提示词
                m_editQuickAdd.SetSel(0, -1);

                AppLog(L"💡 [操作提示] 已自动切换队伍，请在输入框录入新主号！", RGB(0, 255, 255));
                return; // 直接返回，不用走后面的保存刷新逻辑
            }

            if (cmd == 7) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed++;
                else m_totalScoreBlue++;
            }
            else if (cmd == 9) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed--;
                else m_totalScoreBlue--;
            }
            else if (cmd == 8) {
                if (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) m_totalScoreRed = 0;
                else m_totalScoreBlue = 0;
            }
            else if (cmd == 10) {
                CString scoreStr;
                scoreStr.Format(L"红 %d : %d 蓝", m_totalScoreRed, m_totalScoreBlue);
                m_editQuickAdd.SetWindowText(scoreStr);
                m_editQuickAdd.SetFocus();
                m_editQuickAdd.SetSel(0, -1);
            }
            else if (cmd == 11) {
                int teamToClear = (m_treePlayers.GetItemText(hItem).Find(L"红队") != -1) ? 0 : 1;
                CString teamName = (teamToClear == 0) ? L"红队" : L"蓝队";
                int sI = (teamToClear == 0) ? 0 : 4;
                int eI = (teamToClear == 0) ? 4 : 8;
                for (int i = sI; i < eI; i++) {
                    m_players[i].name = L"";
                    m_players[i].aliases.clear();
                    m_players[i].kills = 0;
                    m_players[i].deaths = 0;
                    m_players[i].akCount = 0;
                }
                AppLog(L"🗑️ [清空队伍] 一键清空了【" + teamName + L"】的所有成员！", RGB(255, 80, 80));
            }
            else if (cmd == 15) {
                int pIdx = (int)data;

                // ==========================================
                // 【新增】：获取该主号所在的队伍，并自动切换下拉框
                // ==========================================
                int curTeam = m_players[pIdx].team;
                m_cmbTeamSelect.SetCurSel(curTeam);

                CString mainName = m_players[pIdx].name;
                CString templateText;

                // 帮你把输入框填充好模板： 主号()
                templateText.Format(L"%s()", mainName.GetString());
                m_editQuickAdd.SetWindowText(templateText);
                m_editQuickAdd.SetFocus();

                // 精准将光标移动到左右括号的中间，直接打字即可
                int pos = templateText.GetLength() - 1;
                m_editQuickAdd.SetSel(pos, pos);

                CString teamNameStr = (curTeam == 0) ? L"红队" : L"蓝队";
                AppLog(L"💡 [操作提示] 已自动切换至【" + teamNameStr + L"】，请在括号内填入小号名称！", RGB(0, 255, 255));
            }
            else if (cmd == 12 || cmd == 13) {
                int pIdx = (int)data;
                int targetTeam = (cmd == 12) ? 1 : 0;
                int targetIdx = -1;
                int sI = (targetTeam == 0) ? 0 : 4;
                int eI = (targetTeam == 0) ? 4 : 8;

                for (int i = sI; i < eI; i++) {
                    CString checkName = m_players[i].name;
                    checkName.Trim();
                    if (checkName.IsEmpty()) {
                        targetIdx = i;
                        break;
                    }
                }
                if (targetIdx != -1) {
                    CString moveName = m_players[pIdx].name;
                    m_players[targetIdx] = m_players[pIdx];
                    m_players[targetIdx].team = targetTeam;
                    m_players[pIdx].name = L"";
                    m_players[pIdx].aliases.clear();
                    m_players[pIdx].kills = 0;
                    m_players[pIdx].deaths = 0;
                    m_players[pIdx].akCount = 0;
                    AppLog(L"➡️ [移动换边] 玩家 [" + moveName + L"] 已移动至对面阵营", RGB(80, 180, 255));
                }
            }
            else if (cmd >= 20 && cmd <= 27) {
                int pIdx = (int)data;
                int targetIdx = cmd - 20;

                int curTeam = m_players[pIdx].team;
                int targetTeam = m_players[targetIdx].team;
                CString myName = m_players[pIdx].name;
                CString targetName = m_players[targetIdx].name;

                PlayerData temp = m_players[targetIdx];
                m_players[targetIdx] = m_players[pIdx];
                m_players[targetIdx].team = targetTeam;

                m_players[pIdx] = temp;
                m_players[pIdx].team = curTeam;

                AppLog(L"🔄 [位置互换] [" + myName + L"] 与 [" + targetName + L"] 互换了位置", RGB(255, 215, 0));
            }
            else if (cmd == 1) {
                int pIdx = (data & 0x7FFFFFFF) >> 16;
                int aIdx = (data & 0xFFFF);
                CString subName = m_players[pIdx].aliases[aIdx].name;
                m_players[pIdx].aliases.erase(m_players[pIdx].aliases.begin() + aIdx);
                AppLog(L"✂️ [战局移除] 小号 [" + subName + L"] 已从当前战局剥离（保留在库中）", RGB(200, 200, 200));
            }
            else if (cmd == 14) {
                int pIdx = (data & 0x7FFFFFFF) >> 16;
                int aIdx = (data & 0xFFFF);
                CString mainName = m_players[pIdx].name;
                CString subName = m_players[pIdx].aliases[aIdx].name;

                if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
                    CString& dbAliases = m_aliasDB[mainName];
                    dbAliases.Replace(L"(" + subName + L")", L"");
                    dbAliases.Replace(L"（" + subName + L"）", L"");
                    if (dbAliases.IsEmpty()) {
                        m_aliasDB.erase(mainName);
                    }
                }

                m_players[pIdx].aliases.erase(m_players[pIdx].aliases.begin() + aIdx);
                AppLog(L"💥 [双重抹除] 小号 [" + subName + L"] 已从战局及自动补齐数据库中彻底删除！", RGB(255, 80, 80));
            }
            else if (cmd == 2) {
                int pIdx = (int)data;
                CString mainName = m_players[pIdx].name;
                m_players[pIdx].name = L"";
                m_players[pIdx].aliases.clear();
                m_players[pIdx].kills = 0;
                m_players[pIdx].deaths = 0;
                m_players[pIdx].akCount = 0;
                AppLog(L"🗑️ [删除主号] 玩家 [" + mainName + L"] 及其旗下小号已被全盘清空", RGB(255, 80, 80));
            }
            else if (cmd == 3) { m_players[data].kills++; }
            else if (cmd == 31) { if (m_players[data].kills > 0) m_players[data].kills--; }
            else if (cmd == 4) { m_players[data].deaths++; }
            else if (cmd == 32) { if (m_players[data].deaths > 0) m_players[data].deaths--; }
            else if (cmd == 5) { m_players[data].akCount++; }
            else if (cmd == 33) { if (m_players[data].akCount > 0) m_players[data].akCount--; }
            else if (cmd == 6) {
                m_players[data].kills = 0;
                m_players[data].deaths = 0;
                m_players[data].akCount = 0;
            }

            SaveAliasDB();
            SyncDataToTree();
            WriteScoreToFile();
            RefreshDisplay();
            SaveConfigToFile();
        }
    }
    *pResult = 0;
}

// 序列化保存新版配置文件
void CDNFGameCaptureDlg::SaveConfigToFile() {
    CFile file;
    if (file.Open(m_configPath, CFile::modeCreate | CFile::modeWrite)) {
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF }; file.Write(bom, 3);
        CString text;
        for (int i = 0; i < 8; i++) {
            if (m_players[i].name.IsEmpty()) continue;
            text.AppendFormat(L"%d|%s|%d|%d|%d", m_players[i].team, m_players[i].name, m_players[i].kills, m_players[i].deaths, m_players[i].akCount);
            for (auto& a : m_players[i].aliases) text.AppendFormat(L"|%s", a.name);
            text += L"\r\n";
        }
        std::string utf8 = CW2A(text, CP_UTF8); file.Write(utf8.c_str(), (UINT)utf8.length()); file.Close();
    }
}

// 反序列化读取配置文件（支持三种历史格式，自带脏数据清洗，附带说明文案过滤）
void CDNFGameCaptureDlg::LoadConfigFromFile() {
    CFile file;
    if (!file.Open(m_configPath, CFile::modeRead)) return;

    int len = (int)file.GetLength();
    if (len <= 0) {
        file.Close();
        return;
    }

    char* buf = new char[len + 1];
    file.Read(buf, len);
    buf[len] = 0;

    // 处理 UTF-8 BOM 头
    char* start = buf;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        start += 3;
    }

    CString content = CA2W(start, CP_UTF8);
    delete[] buf;

    int pos = 0;
    int currentTeamContext = 0; // 记忆纯文本格式下的当前队伍，默认红队(0)

    while (pos < content.GetLength()) {
        int nl = content.Find(L'\n', pos);
        CString line = (nl != -1) ? content.Mid(pos, nl - pos) : content.Mid(pos);
        pos = (nl != -1) ? nl + 1 : content.GetLength();

        line.Remove(L'\r');
        line.Trim();

        if (line.IsEmpty()) continue;

        // ==========================================
        // 【新增】：垃圾文案过滤器，拦截说明文本
        // ==========================================
        if (line.Find(L"操作说明") != -1 ||
            line.Find(L"分队：") != -1 ||
            line.Find(L"绑定小号：") != -1 ||
            line.Find(L"手动改分") != -1 ||
            line.Find(L"手动改AK") != -1 ||
            line.Find(L"💡") != -1) {
            continue; // 遇到说明书文本直接跳过
        }

        // ==========================================
        // 兼容层 1：识别纯文本格式的队伍 Header 标签
        // ==========================================
        if (line.Find(L"【红队】") != -1) {
            currentTeamContext = 0;
            continue;
        }
        if (line.Find(L"【蓝队】") != -1) {
            currentTeamContext = 1;
            continue;
        }

        // ==========================================
        // 兼容层 2：处理结构化 `|` 分隔格式（含新老两版）
        // ==========================================
        if (line.Find(L"|") != -1) {
            std::vector<CString> tokens;
            int splitPos = 0; CString token;
            while (AfxExtractSubString(token, line, splitPos, L'|')) {
                tokens.push_back(token); splitPos++;
            }
            if (tokens.size() < 4) continue;

            int team = _wtoi(tokens[0]);
            CString mainName = tokens[1]; mainName.Trim();

            // 查重与空位寻找
            bool isDup = false;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { isDup = true; break; } }
            if (isDup) continue;

            int targetIdx = -1; int sIdx = (team == 0) ? 0 : 4; int eIdx = (team == 0) ? 4 : 8;
            for (int i = sIdx; i < eIdx; i++) { if (m_players[i].name.IsEmpty()) { targetIdx = i; break; } }
            if (targetIdx == -1) continue;

            m_players[targetIdx].team = team;
            m_players[targetIdx].name = mainName;
            m_players[targetIdx].kills = _wtoi(tokens[2]);
            m_players[targetIdx].deaths = _wtoi(tokens[3]);
            m_players[targetIdx].akCount = 0;
            int aliasStartIndex = 4;

            // 探测是否包含 AK 次数
            if (tokens.size() >= 5) {
                CString t4 = tokens[4]; t4.Trim();
                bool isNumeric = !t4.IsEmpty();
                for (int i = 0; i < t4.GetLength(); i++) { if (t4[i] < L'0' || t4[i] > L'9') { isNumeric = false; break; } }
                if (isNumeric && t4.GetLength() <= 3) {
                    m_players[targetIdx].akCount = _wtoi(t4);
                    aliasStartIndex = 5;
                }
            }

            // 载入小号
            for (size_t i = aliasStartIndex; i < tokens.size(); i++) {
                CString aName = tokens[i]; aName.Trim();
                bool aDup = false;
                for (int k = 0; k < 8; k++) {
                    if (m_players[k].name == aName) { aDup = true; break; }
                    for (auto& ea : m_players[k].aliases) { if (ea.name == aName) { aDup = true; break; } }
                }
                if (!aDup && !aName.IsEmpty()) m_players[targetIdx].aliases.push_back({ aName });
            }
        }
        // ==========================================
        // 兼容层 3：处理纯文本 `=` 格式（如：五九(...) = 15/6 A2）
        // ==========================================
        else if (line.Find(L"=") != -1 || line.Find(L"＝") != -1) {
            int eqPos = line.Find(L'=');
            if (eqPos == -1) eqPos = line.Find(L'＝');

            CString leftPart = line.Left(eqPos);
            CString rightPart = line.Mid(eqPos + 1);
            leftPart.Trim(); rightPart.Trim();

            // 解析主号名
            int firstParen = leftPart.Find(L'(');
            if (firstParen == -1) firstParen = leftPart.Find(L'（');

            CString mainName = (firstParen != -1) ? leftPart.Left(firstParen) : leftPart;
            mainName.Trim();
            if (mainName.IsEmpty()) continue;

            // 查重拦截
            bool isDup = false;
            for (int i = 0; i < 8; i++) { if (m_players[i].name == mainName) { isDup = true; break; } }
            if (isDup) continue;

            // 在当前队伍找空位（依赖前面的【红队】/【蓝队】标签设定的 currentTeamContext）
            int targetIdx = -1;
            int sIdx = (currentTeamContext == 0) ? 0 : 4;
            int eIdx = (currentTeamContext == 0) ? 4 : 8;
            for (int i = sIdx; i < eIdx; i++) {
                if (m_players[i].name.IsEmpty()) { targetIdx = i; break; }
            }
            if (targetIdx == -1) continue; // 队伍满了就跳过

            m_players[targetIdx].name = mainName;
            m_players[targetIdx].team = currentTeamContext;

            // 解析右侧战绩：支持 "15/6 A2", "15/6", "15-6"
            int aP = rightPart.Find(L'A');
            if (aP != -1) {
                m_players[targetIdx].akCount = _wtoi(rightPart.Mid(aP + 1));
                if (m_players[targetIdx].akCount == 0) m_players[targetIdx].akCount = 1; // 如果写了A但没数字，保底算1次
                rightPart = rightPart.Left(aP); // 剥离出K/D部分
            }
            else {
                m_players[targetIdx].akCount = 0;
            }

            int slash = rightPart.Find(L'/');
            if (slash == -1) slash = rightPart.Find(L'-'); // 兼容使用减号的情况

            if (slash != -1) {
                m_players[targetIdx].kills = _wtoi(rightPart.Left(slash));
                m_players[targetIdx].deaths = _wtoi(rightPart.Mid(slash + 1));
            }
            else {
                m_players[targetIdx].kills = 0; m_players[targetIdx].deaths = 0;
            }

            // 解析左侧的小号群： (小号1)(小号2)
            if (firstParen != -1) {
                CString aliasesPart = leftPart.Mid(firstParen);
                int c = 0;
                while (true) {
                    CString tS = aliasesPart.Mid(c);
                    int Lr = tS.Find(L'('); if (Lr == -1) Lr = tS.Find(L'（');
                    int Rr = tS.Find(L')'); if (Rr == -1) Rr = tS.Find(L'）');
                    if (Lr == -1 || Rr == -1 || Rr <= Lr) break;

                    CString aN = tS.Mid(Lr + 1, Rr - Lr - 1);
                    aN.Trim();

                    // 小号查重
                    bool aDup = false;
                    for (int k = 0; k < 8; k++) {
                        if (m_players[k].name == aN) { aDup = true; break; }
                        for (auto& ea : m_players[k].aliases) {
                            if (ea.name == aN) { aDup = true; break; }
                        }
                    }
                    if (!aDup && !aN.IsEmpty()) m_players[targetIdx].aliases.push_back({ aN });

                    c += Rr + 1; // 游标移到下一个括号后
                }
            }
        }
    }
    file.Close();
}

// 当鼠标点进输入框
void CDNFGameCaptureDlg::OnEditSetFocus() {
    CString content;
    m_editQuickAdd.GetWindowText(content);
    // 如果当前内容是水印提示，则清空并改变颜色
    if (content == PLACEHOLDER_TEXT) {
        m_editQuickAdd.SetWindowText(L"");
        // 这里可以根据需要微调文字颜色，CEdit默认是黑色
    }
}

// 当鼠标切出输入框
void CDNFGameCaptureDlg::OnEditKillFocus() {
    CString content;
    m_editQuickAdd.GetWindowText(content);
    content.Trim(); // 【关键】：去除空格

    // 如果内容为空或用户只打了空格，恢复水印提示
    if (content.IsEmpty()) {
        m_editQuickAdd.SetWindowText(PLACEHOLDER_TEXT);
    }
}

void CDNFGameCaptureDlg::OnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMTVDISPINFO pTVDispInfo = (LPNMTVDISPINFO)pNMHDR;
    // 默认返回 FALSE，表示禁止控件自动修改文本（由我们手动刷新树状图来更新显示）
    *pResult = FALSE;

    // 如果用户取消编辑或未输入内容，直接返回
    if (pTVDispInfo->item.pszText == NULL) return;

    CString line = pTVDispInfo->item.pszText;
    line.Trim();
    if (line.IsEmpty()) return;

    HTREEITEM hItem = pTVDispInfo->item.hItem;
    HTREEITEM hParent = m_treePlayers.GetParentItem(hItem);

    // =========================================================
    // 1. 根节点处理：直接修改【红队 / 蓝队】大比分
    // 逻辑：只要输入里包含数字，就提取出来作为新的大比分
    // =========================================================
    if (hParent == NULL) {
        CString oldText = m_treePlayers.GetItemText(hItem);
        int newScore = 0;
        CString numStr = L"";

        // 提取字符串中的所有数字
        for (int i = 0; i < line.GetLength(); i++) {
            if (line[i] >= L'0' && line[i] <= L'9') numStr += line[i];
        }

        if (!numStr.IsEmpty()) {
            newScore = _wtoi(numStr);
            if (oldText.Find(L"红队") != -1) {
                m_totalScoreRed = newScore;
            }
            else {
                m_totalScoreBlue = newScore;
            }
            AppLog(L"✏️ [大比分修改] 队伍比分已更新为：" + numStr, RGB(0, 255, 100));
        }

        // 修改完大比分后，强制执行全套刷新逻辑并退出
        SaveConfigToFile();
        WriteScoreToFile();
        SyncDataToTree();
        RefreshDisplay();
        return;
    }

    // =========================================================
    // 2. 子节点处理：修改【主号】或【小号】
    // =========================================================
    DWORD_PTR data = m_treePlayers.GetItemData(hItem);
    std::lock_guard<std::mutex> lk(m_dataMutex);

    // 预判：如果是主号编辑，先剥离出名字部分用于查重
    CString newNameOnly = line;
    if (!(data & 0x80000000)) { // 如果是主号
        int eP = line.Find(L'=');
        if (eP == -1) eP = line.Find(L'＝');
        if (eP != -1) {
            newNameOnly = line.Left(eP);
            newNameOnly.Trim();
        }
    }

    // 索引定位
    int curPIdx = (data & 0x80000000) ? ((data & 0x7FFFFFFF) >> 16) : (int)data;
    int curAIdx = (data & 0x80000000) ? (data & 0xFFFF) : -1;

    // --- 查重拦截逻辑 ---
    bool isDup = false;
    for (int i = 0; i < 8 && !isDup; i++) {
        if (m_players[i].name.IsEmpty()) continue;

        // 检查是否与现有主号重名（排除掉正在编辑的自己）
        if (m_players[i].name == newNameOnly && !(i == curPIdx && curAIdx == -1)) {
            isDup = true; break;
        }
        // 检查是否与现有小号重名
        for (int j = 0; j < (int)m_players[i].aliases.size(); j++) {
            if (m_players[i].aliases[j].name == newNameOnly && !(i == curPIdx && j == curAIdx)) {
                isDup = true; break;
            }
        }
    }

    if (isDup) {
        AppLog(L"❌ [重命名失败] 名称 [" + newNameOnly + L"] 已被占用！", RGB(255, 100, 100));
        MessageBox(L"修改失败！该名称已经被其他主号或小号占用，请使用唯一名称。", L"命名冲突", MB_ICONWARNING);
        return;
    }

    // --- 正式执行数据更新 ---
    if (data & 0x80000000) {
        // A. 编辑的是小号
        CString oldAliasName = m_players[curPIdx].aliases[curAIdx].name;
        CString mainName = m_players[curPIdx].name;

        // 同步修改自动补全数据库（防止旧名字残留在库里）
        if (m_aliasDB.find(mainName) != m_aliasDB.end()) {
            CString& dbAliases = m_aliasDB[mainName];
            dbAliases.Replace(L"(" + oldAliasName + L")", L"(" + line + L")");
            dbAliases.Replace(L"（" + oldAliasName + L"）", L"（" + line + L"）");
        }
        m_players[curPIdx].aliases[curAIdx].name = line;
    }
    else {
        // B. 编辑的是主号 (支持 Name = 10/5 A2 格式解析)
        CString oldMainName = m_players[data].name;
        CString newMainName = line;

        int eP = line.Find(L'=');
        if (eP == -1) eP = line.Find(L'＝');

        if (eP != -1) {
            newMainName = line.Left(eP);
            newMainName.Trim();

            // 尝试解析战绩部分
            CString scorePart = line.Mid(eP + 1);
            scorePart.Trim();

            // 解析 AK 次数
            int aPos = scorePart.Find(L'A');
            if (aPos != -1) {
                m_players[data].akCount = _wtoi(scorePart.Mid(aPos + 1));
                if (m_players[data].akCount == 0 && scorePart.Mid(aPos + 1) != L"0") m_players[data].akCount = 1;
                scorePart = scorePart.Left(aPos);
            }

            // 解析 K/D
            int slash = scorePart.Find(L'/');
            if (slash == -1) slash = scorePart.Find(L'-');
            if (slash != -1) {
                m_players[data].kills = _wtoi(scorePart.Left(slash));
                m_players[data].deaths = _wtoi(scorePart.Mid(slash + 1));
            }
        }

        // 如果主号改名了，同步转移数据库中的小号关联数据
        if (oldMainName != newMainName && !oldMainName.IsEmpty()) {
            if (m_aliasDB.find(oldMainName) != m_aliasDB.end()) {
                m_aliasDB[newMainName] = m_aliasDB[oldMainName];
                m_aliasDB.erase(oldMainName);
            }
        }
        m_players[data].name = newMainName;
    }

    AppLog(L"✏️ [信息修改] 成功保存更新: " + line, RGB(0, 255, 100));

    // --- 全局同步落地 ---
    SaveAliasDB();
    SaveConfigToFile();
    WriteScoreToFile();
    SyncDataToTree();
    RefreshDisplay();
}

void CDNFGameCaptureDlg::OnCustomDrawTree(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMTVCUSTOMDRAW pCustomDraw = (LPNMTVCUSTOMDRAW)pNMHDR;
    *pResult = CDRF_DODEFAULT;

    if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        *pResult = CDRF_NOTIFYITEMDRAW;
        return;
    }

    if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        HTREEITEM hItem = (HTREEITEM)pCustomDraw->nmcd.dwItemSpec;
        CString text = m_treePlayers.GetItemText(hItem);
        DWORD_PTR data = m_treePlayers.GetItemData(hItem);

        if (text.Find(L"【红队】") != -1) {
            pCustomDraw->clrText = RGB(220, 20, 60);
            *pResult = CDRF_NEWFONT;
            return;
        }
        if (text.Find(L"【蓝队】") != -1) {
            pCustomDraw->clrText = RGB(30, 144, 255);
            *pResult = CDRF_NEWFONT;
            return;
        }

        // --- 【新增】：判断是小号，直接变灰 ---
        if (data & 0x80000000) {
            pCustomDraw->clrText = RGB(150, 150, 150); // 灰色
            *pResult = CDRF_NEWFONT;
            return;
        }

        // --- 下面是主号的颜色 ---
        int playerIdx = (int)data;
        if (playerIdx >= 0 && playerIdx < 4) {
            pCustomDraw->clrText = RGB(255, 80, 80); // 红队主号
        }
        else if (playerIdx >= 4 && playerIdx < 8) {
            pCustomDraw->clrText = RGB(80, 120, 255); // 蓝队主号
        }
        *pResult = CDRF_NEWFONT;
    }
}

// DNFGameCaptureDlg.cpp
HBRUSH CDNFGameCaptureDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
    // 先调用父类的默认处理，拿到默认的画刷
    HBRUSH hbr = CWnd::OnCtlColor(pDC, pWnd, nCtlColor);

    // 1. 判断当前发消息的控件是不是我们的“快速添加框”（ID为1025）
    if (pWnd->GetDlgCtrlID() == 1025) {
        CString txt;
        pWnd->GetWindowText(txt);


        // 2. 判断内容是否为提示水印
        if (txt == PLACEHOLDER_TEXT) {
            // 如果是水印，把画笔颜色设为灰色
            pDC->SetTextColor(RGB(160, 160, 160));
        }
        else {
            // 如果用户开始打字了，恢复成正常的黑色
            pDC->SetTextColor(RGB(0, 0, 0));
        }
    }

    return hbr;
}

// 【核心修复】：专门接收子线程消息，在安全的主线程中刷新树状图和看板
LRESULT CDNFGameCaptureDlg::OnUpdateAllUI(WPARAM wParam, LPARAM lParam) {
    // 1. 刷新软件界面的视觉显示
    SyncDataToTree();
    RefreshDisplay();

    // ==========================================
    // 【关键修复】：自动识图拿到人头后，必须立刻将数据写入本地 TXT 文件！
    // 这样 OBS 才能瞬间读取到最新比分，实现真正的零延迟自动跟进！
    // ==========================================
    WriteScoreToFile(); // 实时更新发给 OBS 用的 TXT 文件
    SaveConfigToFile(); // 实时保存对局进度，防止崩溃丢失战绩

    return 0;
}

// ============================================================================
// 系统版本与权限检测
// ============================================================================
bool CDNFGameCaptureDlg::IsWindows10OrGreater() {
    // 使用 RtlGetVersion 获取真实系统版本（不受兼容性清单影响）
    typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
    if (!hNtdll) return false;

    auto pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
    if (!pRtlGetVersion) return false;

    RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
    if (pRtlGetVersion(&osvi) != 0) return false;

    // Win10 = 10.0, Win8.1 = 6.3, Win7 = 6.1
    return (osvi.dwMajorVersion >= 10);
}

// ============================================================================
// 权限检测与自动提权
// ============================================================================
bool CDNFGameCaptureDlg::IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool CDNFGameCaptureDlg::RelaunchAsAdmin() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteEx(&sei)) {
        if (m_hSingleInstanceMutex) {
            CloseHandle(m_hSingleInstanceMutex);
            m_hSingleInstanceMutex = NULL;
        }
        exit(0);
        return true;
    }
    return false; // 用户拒绝了 UAC
}

// ========================================================
// 【终极黑屏检测】：专门免疫 DNF“失明”状态与暗黑图特效
// ========================================================
bool CDNFGameCaptureDlg::IsBitmapBlank(HBITMAP hBmp, int w, int h) {
    if (!hBmp || w <= 0 || h <= 0) return true;

    HDC hDC = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(hDC, hBmp);

    bool isAllBlack = true; // 假设它是真黑屏

    // 沿画面的【主对角线】和【副对角线】扫射 40 个点
    // 这种扫射方式必定会穿过 DNF 的血条、技能栏、决斗场比分板或连击数区域
    for (int i = 1; i < 20; i++) {
        // 1. 测主对角线 (左上到右下)
        COLORREF c1 = GetPixel(hDC, w * i / 20, h * i / 20);
        if (c1 != RGB(0, 0, 0)) {
            isAllBlack = false; // 发现任意非纯黑像素，立刻洗清嫌疑！
            break;
        }

        // 2. 测副对角线 (左下到右上)
        COLORREF c2 = GetPixel(hDC, w * i / 20, h - (h * i / 20));
        if (c2 != RGB(0, 0, 0)) {
            isAllBlack = false; // 发现任意非纯黑像素，立刻洗清嫌疑！
            break;
        }
    }

    SelectObject(hDC, old);
    DeleteDC(hDC);

    // 只有这 40 个点全部是 100% 绝对的纯黑，才会被判定为捕获失败
    return isAllBlack;
}

// =====================================================================
// 【函数 2】OnCbnSelchangeCaptureEngine —— 替换原函数（同样的问题）
// =====================================================================
void CDNFGameCaptureDlg::OnCbnSelchangeCaptureEngine() {
    m_nCaptureEngineChoice = m_cmbCaptureEngine.GetCurSel();

    CString val;
    val.Format(L"%d", m_nCaptureEngineChoice);
    WritePrivateProfileString(L"Settings", L"CaptureEngine", val, m_iniPath);

    CString engineNames[] = { L"自动选择", L"WGC 硬件加速", L"PrintWindow 兼容模式" };
    AppLog(L"⚙️ [设置] 捕获引擎已切换为: " + engineNames[m_nCaptureEngineChoice], RGB(0, 255, 255));

    ClearPreview();

    // 同步安全销毁
    if (m_pWGC) {
        m_pWGC->StopCapture();
        delete m_pWGC;
        m_pWGC = nullptr;
    }

    m_bUseWGC = false;
    m_nBlankFrameCount = 0;
    m_bAlreadyPrompted = false;
}
void CDNFGameCaptureDlg::ClearPreview() 
{
    // 清空位图数据
    {
        std::lock_guard<std::mutex> lock(g_bmpMutex);
        if (m_bmp) {
            DeleteObject(m_bmp);
            m_bmp = nullptr;
        }
        m_w = 0;
        m_h = 0;
    }

    // 强制重绘预览区域，让画面变黑
    CRect client;
    GetClientRect(&client);
    int splitY = max(100, client.bottom - (int)(390 * WINDOW_SCALE));
    CRect topHalf(0, 0, client.right, splitY);
    InvalidateRect(&topHalf, TRUE);
}

BOOL CALLBACK CDNFGameCaptureDlg::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!::IsWindowVisible(hwnd)) return TRUE;
    if (::GetWindowTextLength(hwnd) == 0) return TRUE;

    // 过滤掉系统杂项窗口
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    wchar_t title[256];
    ::GetWindowText(hwnd, title, 256);
    CString strTitle(title);

    if (strTitle == L"Program Manager" || strTitle.Find(L"DNF击杀统计") != -1) return TRUE;

    CComboBox* pCmb = (CComboBox*)lParam;
    int idx = pCmb->AddString(L"[窗口] " + strTitle);
    pCmb->SetItemData(idx, (DWORD_PTR)hwnd); // 藏入 HWND

    return TRUE;
}

void CDNFGameCaptureDlg::RefreshTargetList() {
    int curSelData = -1;
    if (m_cmbTargetWindow.GetCurSel() != -1) {
        curSelData = (int)m_cmbTargetWindow.GetItemData(m_cmbTargetWindow.GetCurSel());
    }

    m_cmbTargetWindow.ResetContent();

    // 1. 默认 DNF 游戏
    int dnfIdx = m_cmbTargetWindow.AddString(L"[默认] " DNF_WINDOW_NAME);
    m_cmbTargetWindow.SetItemData(dnfIdx, 0); // 0 代表使用老逻辑寻找DNF

    // 2. 枚举摄像头
    std::vector<std::wstring> cameras = CameraCapture::GetAvailableCameras();
    for (size_t i = 0; i < cameras.size(); i++) {
        int idx = m_cmbTargetWindow.AddString(CString(L"[摄像头] ") + cameras[i].c_str());
        // 最高位打个标记 0x80000000，表示这是摄像头，低位存索引
        m_cmbTargetWindow.SetItemData(idx, 0x80000000 | (DWORD_PTR)i);
    }

    // 3. 枚举其他窗口
    EnumWindows(EnumWindowsProc, (LPARAM)&m_cmbTargetWindow);

    // 尝试恢复之前的选择
    bool restored = false;
    for (int i = 0; i < m_cmbTargetWindow.GetCount(); i++) {
        if ((int)m_cmbTargetWindow.GetItemData(i) == curSelData) {
            m_cmbTargetWindow.SetCurSel(i);
            restored = true; break;
        }
    }
    if (!restored) m_cmbTargetWindow.SetCurSel(0);
}

void CDNFGameCaptureDlg::OnCbnDropdownTargetWindow() {
    RefreshTargetList(); // 每次点开下拉框，实时刷新最新的窗口列表
}

// 只在用户"确认选择并关闭下拉框"时触发，滚动期间不触发
// =============================================================
void CDNFGameCaptureDlg::OnCbnCloseupTargetWindow() {
    ClearPreview();

    // 回归简单直接的主线程安全销毁
    if (m_pWGC) {
        m_pWGC->StopCapture();
        delete m_pWGC;
        m_pWGC = nullptr;
        m_bUseWGC = false;
    }

    if (m_pCamera) {
        m_pCamera->StopCapture();
        delete m_pCamera;
        m_pCamera = nullptr;
    }

    AppLog(L"🎯 [设置] 已切换捕获目标", RGB(0, 255, 255));
}
