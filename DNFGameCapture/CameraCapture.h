#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <winrt/base.h> // 复用你项目中已有的 winrt::com_ptr 来管理 COM 智能指针

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

class CameraCapture {
public:
    CameraCapture();
    ~CameraCapture();

    // 传入摄像头设备索引（通常 0 是默认摄像头）
    bool Initialize(int deviceIndex = 0);

    // 开始与停止捕获
    bool StartCapture();
    void StopCapture();

    // 线程安全地获取最新一帧
    HBITMAP GetLatestFrame(int& outWidth, int& outHeight);

    // 获取系统中可用的摄像头列表
    static std::vector<std::wstring> GetAvailableCameras();

private:
    void CaptureThreadFunc();
    HBITMAP BufferToHBITMAP(BYTE* pBuffer, int width, int height, int stride);

private:
    winrt::com_ptr<IMFMediaSource> m_pMediaSource;
    winrt::com_ptr<IMFSourceReader> m_pReader;

    std::thread m_captureThread;
    std::atomic<bool> m_isCapturing{ false };
    std::atomic<bool> m_stopRequested{ false };

    std::mutex m_frameMutex;
    HBITMAP m_latestBmp = nullptr;
    int m_width = 0;
    int m_height = 0;
};
