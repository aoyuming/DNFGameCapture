#include "pch.h"
#include "DNFGameCaptureDlg.h"

namespace {
HANDLE g_singleInstanceMutex = nullptr;
}

void DnfReleaseSingleInstanceMutex() noexcept
{
    if (g_singleInstanceMutex) {
        ::CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
}

class CApp : public CWinApp
{
public:
    BOOL InitInstance()
    {
        g_singleInstanceMutex = ::CreateMutexW(
            nullptr, TRUE, L"Global\\DNFGameCapture_SingleInstance");
        if (!g_singleInstanceMutex) return FALSE;
        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            DnfReleaseSingleInstanceMutex();
            return FALSE;
        }

        CDNFGameCaptureDlg wnd;
        m_pMainWnd = &wnd;
        wnd.ShowWindow(SW_HIDE);
        wnd.UpdateWindow();

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            if (!PreTranslateMessage(&msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        return FALSE;
    }

    int ExitInstance() override
    {
        DnfReleaseSingleInstanceMutex();
        return CWinApp::ExitInstance();
    }
} theApp;
