#pragma once
#include "pch.h"          // 必须第一行
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <mutex>
#include <atomic>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

class WGCCapture {
public:
    WGCCapture();
    ~WGCCapture();

    // 初始化：传入目标窗口句柄
    bool Initialize(HWND hTargetWnd);

    // 开始/停止捕获
    bool StartCapture();
    void StopCapture();

    // 获取最新一帧的 HBITMAP（线程安全）
    HBITMAP GetLatestFrame(int& outWidth, int& outHeight);

    // 检查当前系统是否支持 WGC
    static bool IsSupported();

private:
    void OnFrameArrived(
        Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

    // 将 D3D11 纹理转为 HBITMAP
    HBITMAP TextureToHBitmap(ID3D11Texture2D* pTexture);

    // 创建 Direct3D 设备
    bool CreateD3DDevice();

    // 创建 WinRT 互操作设备
    IDirect3DDevice CreateWinRTDevice();

    HWND m_hTargetWnd = nullptr;

    // D3D11 资源
    ID3D11Device* m_d3dDevice = nullptr;
    ID3D11DeviceContext* m_d3dContext = nullptr;

    // WGC 核心对象
    GraphicsCaptureItem m_captureItem{ nullptr };
    Direct3D11CaptureFramePool m_framePool{ nullptr };
    GraphicsCaptureSession m_session{ nullptr };
    IDirect3DDevice m_winrtDevice{ nullptr };

    // 帧到达事件令牌
    winrt::event_token m_frameArrivedToken;

    // 最新帧缓存（线程安全）
    std::mutex m_frameMutex;
    HBITMAP m_latestBmp = nullptr;
    int m_width = 0;
    int m_height = 0;

    std::atomic<bool> m_isCapturing{ false };
};