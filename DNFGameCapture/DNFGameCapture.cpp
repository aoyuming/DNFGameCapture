#include "pch.h"
#include "DNFGameCaptureDlg.h"

class CApp : public CWinApp
{
public:
    BOOL InitInstance()
    {
        CDNFGameCaptureDlg wnd;
        m_pMainWnd = &wnd;
        wnd.ShowWindow(SW_SHOW);
        wnd.UpdateWindow();

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return FALSE;
    }
} theApp;