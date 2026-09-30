#pragma once

#include <vector>

// Creates a modeless MFC dialog whose top-level window uses its own window class
// instead of the shared "#32770". Streaming tools (直播伴侣 / OBS window capture)
// re-find a saved window by exe + window class after a restart; with every window of
// this exe sharing "#32770" they all resolve to the same window. A unique class per
// display window keeps each capture source pointing at the right window.
inline BOOL DnfCreateDialogWithClass(CDialog* dialog, LPCWSTR className, CWnd* parent,
    short cxDlu = 509, short cyDlu = 319)
{
    if (!dialog || !className || !*className) return FALSE;
    HINSTANCE instance = AfxGetInstanceHandle();

    WNDCLASSEXW wc = { sizeof(wc) };
    if (!::GetClassInfoExW(instance, className, &wc)) {
        WNDCLASSEXW dlg = { sizeof(dlg) };
        if (!::GetClassInfoExW(nullptr, MAKEINTRESOURCEW(0x8002), &dlg)) return FALSE; // "#32770"
        dlg.cbSize = sizeof(dlg);
        dlg.hInstance = instance;
        dlg.lpszClassName = className;
        if (!::RegisterClassExW(&dlg) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }

    // DLGTEMPLATE, then: menu (0), class name, title (empty); no controls, no font.
    std::vector<WORD> buffer(sizeof(DLGTEMPLATE) / sizeof(WORD), 0);
    auto* tpl = reinterpret_cast<DLGTEMPLATE*>(buffer.data());
    tpl->style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    tpl->dwExtendedStyle = 0;
    tpl->cdit = 0;
    tpl->x = 0;
    tpl->y = 0;
    tpl->cx = cxDlu;
    tpl->cy = cyDlu;
    buffer.push_back(0); // no menu
    for (const wchar_t* p = className; *p; ++p) buffer.push_back(static_cast<WORD>(*p));
    buffer.push_back(0); // end of class name
    buffer.push_back(0); // empty title
    buffer.push_back(0); // padding

    return dialog->CreateIndirect(reinterpret_cast<LPCDLGTEMPLATE>(buffer.data()), parent);
}

constexpr wchar_t kDnfKillDisplayWindowClass[] = L"DNFGameCapture.KillDisplay";
constexpr wchar_t kDnfKillFxWindowClass[] = L"DNFGameCapture.KillFxFullscreen";
