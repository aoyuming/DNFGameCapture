#pragma once
#include <afxwin.h>
#include <functional>
#include <utility>
#include "HpBarCalibration.h"

// Modeless native panel; all callbacks and controls run on the owner's UI thread.
class CHpBarCalibrationPanel final : public CWnd {
public:
    struct State {
        int width = 0, height = 0;
        float rect[2][4] = {};
        int percent[2] = { -1, -1 };
        int selectedSide = -1, selectedPart = 2;
    };
    using Read = std::function<State()>;
    using Apply = std::function<bool(int, const dnf::hp::PixelRect&, int, int)>;
    using Select = std::function<void(int, int)>;
    CHpBarCalibrationPanel(Read read, Apply apply, Select select,
        std::function<void()> save, std::function<void()> cancel,
        std::function<void(int)> defaults)
        : m_read(std::move(read)), m_apply(std::move(apply)), m_select(std::move(select)),
          m_save(std::move(save)), m_cancel(std::move(cancel)), m_defaults(std::move(defaults)) {}
    ~CHpBarCalibrationPanel() override { if (::IsWindow(GetSafeHwnd())) DestroyWindow(); }

    bool Open(CWnd* owner) {
        if (!::IsWindow(GetSafeHwnd())) {
            CClientDC dc(owner);
            m_scale = dc.GetDeviceCaps(LOGPIXELSX) / 96.0;
            CRect bounds(0, 0, Px(446), Px(415));
            const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
            const DWORD ex = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
            ::AdjustWindowRectEx(&bounds, style, FALSE, ex);
            CRect parent; owner->GetWindowRect(&parent);
            bounds.OffsetRect(parent.left + Px(60), parent.top + Px(105));
            if (!CreateEx(ex, AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW),
                ::GetSysColorBrush(COLOR_3DFACE)), L"血条百分比检测 · 坐标微调", style,
                bounds, owner, 0)) return false;
            m_font.CreatePointFont(95, L"微软雅黑");
            if (!CreateControls()) { DestroyWindow(); return false; }
            CenterWindow(owner);
        }
        m_dirty = false;
        Refresh(true);
        SetTimer(1, 150, nullptr);
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
        return true;
    }
    void Hide() {
        if (::IsWindow(GetSafeHwnd())) { KillTimer(1); ShowWindow(SW_HIDE); }
        m_dirty = false;
    }
    bool ContainsWindow(HWND hwnd) const {
        return GetSafeHwnd() && (hwnd == GetSafeHwnd() || ::IsChild(GetSafeHwnd(), hwnd));
    }
    BOOL PreTranslateMessage(MSG* msg) override {
        if (msg && msg->message == WM_KEYDOWN) {
            if (msg->wParam == VK_ESCAPE) { m_cancel(); return TRUE; }
            if (msg->wParam == VK_RETURN) { ApplyFields(); return TRUE; }
            const int id = ::GetDlgCtrlID(msg->hwnd);
            if (id < EditLeft || id > EditBottom) {
                int dx = 0, dy = 0;
                if (msg->wParam == VK_LEFT) dx = -1;
                if (msg->wParam == VK_RIGHT) dx = 1;
                if (msg->wParam == VK_UP) dy = -1;
                if (msg->wParam == VK_DOWN) dy = 1;
                // Leave combo-box navigation alone.
                if ((dx || dy) && id != Side && id != Part && id != Step) {
                    Nudge(dx, dy); return TRUE;
                }
            }
        }
        if (msg && ::IsDialogMessage(GetSafeHwnd(), msg)) return TRUE;
        return CWnd::PreTranslateMessage(msg);
    }
protected:
    LRESULT WindowProc(UINT message, WPARAM wp, LPARAM lp) override {
        if (message == WM_CLOSE) { m_cancel(); return 0; }
        if (message == WM_TIMER && wp == 1) { Refresh(false); return 0; }
        if (message == WM_COMMAND) {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (code == EN_CHANGE && id >= EditLeft && id <= EditBottom && !m_refreshing) {
                m_dirty = true; Note(L"坐标尚未应用；按 Enter 或点击“应用坐标”。"); return 0;
            }
            if (code == CBN_SELCHANGE && (id == Side || id == Part)) {
                const int nextSide = static_cast<int>(::SendDlgItemMessage(GetSafeHwnd(), Side, CB_GETCURSEL, 0, 0));
                const int nextPart = static_cast<int>(::SendDlgItemMessage(GetSafeHwnd(), Part, CB_GETCURSEL, 0, 0));
                if (!ApplyFields()) {
                    ::SendDlgItemMessage(GetSafeHwnd(), Side, CB_SETCURSEL, m_side, 0);
                    ::SendDlgItemMessage(GetSafeHwnd(), Part, CB_SETCURSEL, m_part, 0);
                    return 0;
                }
                m_side = nextSide;
                m_part = nextPart;
                m_select(m_side, m_part == 0 ? 2 : m_part - 1);
                Refresh(true); return 0;
            }
            if (code == BN_CLICKED) {
                switch (id) {
                case ApplyButton: ApplyFields(); return 0;
                case Left: Nudge(-1, 0); return 0;
                case Right: Nudge(1, 0); return 0;
                case Up: Nudge(0, -1); return 0;
                case Down: Nudge(0, 1); return 0;
                case Save: if (ApplyFields()) m_save(); return 0;
                case Cancel: m_cancel(); return 0;
                case Defaults: m_dirty = false; m_defaults(m_side); Refresh(true);
                    Note(L"已恢复当前侧默认区域；点击保存后才写入配置。"); return 0;
                }
            }
        }
        return CWnd::WindowProc(message, wp, lp);
    }
private:
    enum { Side = 2100, Part, Step, EditLeft, EditTop, EditRight, EditBottom,
        ApplyButton, Left, Right, Up, Down, Save, Cancel, Defaults, Dimensions, Percent, Status };
    Read m_read; Apply m_apply; Select m_select;
    std::function<void()> m_save, m_cancel;
    std::function<void(int)> m_defaults;
    CFont m_font;
    double m_scale = 1.0;
    int m_side = 0, m_part = 0, m_width = 0, m_height = 0;
    bool m_dirty = false, m_refreshing = false;
    int Px(int n) const { return static_cast<int>(std::lround(n * m_scale)); }
    bool Control(LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
        HWND child = ::CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
            Px(x), Px(y), Px(w), Px(h), GetSafeHwnd(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            AfxGetInstanceHandle(), nullptr);
        if (child) ::SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font.GetSafeHandle()), TRUE);
        return child != nullptr;
    }
    bool CreateControls() {
        bool ok = true;
        auto label = [&](LPCWSTR text, int x, int y, int w, int h, int id = -1) {
            ok = Control(L"STATIC", text, SS_LEFT, x, y, w, h, id) && ok;
        };
        label(L"坐标原点：捕获画面左上角（不含预览留白）", 16, 12, 414, 22);
        label(L"等待捕获画面…", 16, 38, 414, 23, Dimensions);
        ok = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 16, 70, 120, 150, Side) && ok;
        ok = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 146, 70, 166, 150, Part) && ok;
        ok = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 322, 70, 108, 150, Step) && ok;
        for (auto value : { L"左侧血条", L"右侧血条" }) ::SendDlgItemMessage(GetSafeHwnd(), Side, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (auto value : { L"整体移动", L"左上角（调边界）", L"右下角（调边界）" }) ::SendDlgItemMessage(GetSafeHwnd(), Part, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (auto value : { L"1 像素", L"5 像素", L"10 像素" }) ::SendDlgItemMessage(GetSafeHwnd(), Step, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        for (int id : { Side, Part, Step }) ::SendDlgItemMessage(GetSafeHwnd(), id, CB_SETCURSEL, 0, 0);
        const wchar_t* names[] = { L"左 X", L"上 Y", L"右 X", L"下 Y" };
        for (int k = 0; k < 4; ++k) {
            const int x = 16 + (k % 2) * 210, y = 112 + (k / 2) * 38;
            label(names[k], x, y + 3, 52, 24);
            ok = Control(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, x + 56, y, 138, 27, EditLeft + k, WS_EX_CLIENTEDGE) && ok;
            ::SendDlgItemMessage(GetSafeHwnd(), EditLeft + k, EM_SETLIMITTEXT, 8, 0);
        }
        label(L"右 / 下坐标为区域外边界；输入范围随捕获分辨率更新。", 16, 187, 414, 24);
        auto button = [&](LPCWSTR text, int x, int y, int w, int id) {
            ok = Control(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x, y, w, 29, id) && ok;
        };
        button(L"←", 16, 219, 48, Left); button(L"↑", 70, 219, 48, Up);
        button(L"↓", 124, 219, 48, Down); button(L"→", 178, 219, 48, Right);
        button(L"应用坐标", 246, 219, 184, ApplyButton);
        label(L"", 16, 258, 414, 25, Percent);
        label(L"方向键微调；Shift + 方向键为 10 像素。", 16, 287, 414, 22);
        label(L"", 16, 312, 414, 40, Status);
        button(L"保存并退出", 16, 366, 124, Save);
        button(L"取消并退出", 150, 366, 124, Cancel);
        button(L"恢复当前侧默认", 284, 366, 146, Defaults);
        return ok;
    }
    void Note(LPCWSTR text) { SetDlgItemText(Status, text); }
    void Refresh(bool force) {
        if (!::IsWindow(GetSafeHwnd())) return;
        const State state = m_read();
        const bool changedSize = state.width != m_width || state.height != m_height;
        if (changedSize) {
            if (m_dirty) Note(L"捕获分辨率变化，已丢弃未应用输入并重新读取坐标。");
            m_dirty = false;
        }
        m_width = state.width; m_height = state.height;
        const bool valid = dnf::hp::HasFrame(m_width, m_height);
        CString text;
        if (valid) text.Format(L"原始捕获分辨率：%d × %d  |  单位：原图像素", m_width, m_height);
        else text = L"尚无有效捕获画面，请先选择窗口或摄像头。";
        SetDlgItemText(Dimensions, text);
        for (int id : { EditLeft, EditTop, EditRight, EditBottom, ApplyButton, Left, Right, Up, Down, Save })
            ::EnableWindow(::GetDlgItem(GetSafeHwnd(), id), valid);
        if (!m_dirty && state.selectedSide >= 0 && state.selectedSide < 2) {
            m_side = state.selectedSide;
            m_part = state.selectedPart == 2 ? 0 : state.selectedPart + 1;
            ::SendDlgItemMessage(GetSafeHwnd(), Side, CB_SETCURSEL, m_side, 0);
            ::SendDlgItemMessage(GetSafeHwnd(), Part, CB_SETCURSEL, m_part, 0);
        }
        const auto r = dnf::hp::ToPixels(state.rect[m_side], m_width, m_height);
        if ((force || !m_dirty) && valid) {
            m_refreshing = true;
            const int values[] = { r.left, r.top, r.right, r.bottom };
            for (int k = 0; k < 4; ++k) {
                CString current, desired; GetDlgItemText(EditLeft + k, current); desired.Format(L"%d", values[k]);
                if (current != desired) SetDlgItemText(EditLeft + k, desired);
            }
            m_refreshing = false;
        }
        const int pct = state.percent[m_side];
        CString health; if (pct < 0 || !valid) health = L"--（未识别）"; else health.Format(L"%d%%", pct);
        text.Format(L"当前区域：%d × %d  |  实时血量：%s", valid ? r.right - r.left : 0,
            valid ? r.bottom - r.top : 0, health.GetString());
        SetDlgItemText(Percent, text);
    }
    bool Parse(int id, int& result) {
        CString s; GetDlgItemText(id, s); s.Trim();
        if (s.IsEmpty()) return false;
        int value = 0;
        for (int k = 0; k < s.GetLength(); ++k) {
            if (s[k] < L'0' || s[k] > L'9' || value > 1000000) return false;
            value = value * 10 + s[k] - L'0';
        }
        result = value; return true;
    }
    bool ApplyFields() {
        if (!m_dirty) return true;
        const State state = m_read();
        if (!dnf::hp::HasFrame(state.width, state.height)) { Note(L"没有有效捕获画面，无法应用像素坐标。"); return false; }
        if (state.width != m_width || state.height != m_height) { Refresh(true); Note(L"分辨率已变化，请按新分辨率重新输入。"); return false; }
        dnf::hp::PixelRect r;
        if (!Parse(EditLeft, r.left) || !Parse(EditTop, r.top) || !Parse(EditRight, r.right) || !Parse(EditBottom, r.bottom) ||
            !dnf::hp::IsValid(r, state.width, state.height)) {
            CString message; message.Format(L"坐标须为范围内整数，右 > 左、下 > 上；最小区域 %d × %d。",
                dnf::hp::MinWidth(state.width), dnf::hp::MinHeight(state.height)); Note(message); return false;
        }
        if (!m_apply(m_side, r, state.width, state.height)) { Note(L"捕获画面已变化，坐标未应用，请重试。"); return false; }
        m_dirty = false; Refresh(true); Note(L"已应用到实时检测；尚未保存到 config.ini。"); return true;
    }
    void Nudge(int dx, int dy) {
        if (!ApplyFields()) return;
        const State state = m_read();
        if (!dnf::hp::HasFrame(state.width, state.height)) { Note(L"请先取得有效捕获画面。"); return; }
        auto rect = dnf::hp::ToPixels(state.rect[m_side], state.width, state.height);
        const int sel = static_cast<int>(::SendDlgItemMessage(GetSafeHwnd(), Step, CB_GETCURSEL, 0, 0));
        const int step = (::GetKeyState(VK_SHIFT) & 0x8000) ? 10 : (sel == 2 ? 10 : sel == 1 ? 5 : 1);
        const auto handle = m_part == 0 ? dnf::hp::Handle::Whole : m_part == 1 ? dnf::hp::Handle::TopLeft : dnf::hp::Handle::BottomRight;
        if (!dnf::hp::Nudge(rect, state.width, state.height, handle, dx * step, dy * step)) {
            Note(L"当前区域过小，请先输入合法边界或恢复默认。"); return;
        }
        if (!m_apply(m_side, rect, state.width, state.height)) { Refresh(true); Note(L"捕获画面已变化，请重试。"); return; }
        Refresh(true); Note(L"已按原图像素微调（边界处自动限制）；尚未保存。");
    }
};
