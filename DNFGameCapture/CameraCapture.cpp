#include "pch.h"
#include "CameraCapture.h"

CameraCapture::CameraCapture() {
    MFStartup(MF_VERSION);
}

CameraCapture::~CameraCapture() {
    StopCapture();
    if (m_latestBmp) DeleteObject(m_latestBmp);
    MFShutdown();
}

std::vector<std::wstring> CameraCapture::GetAvailableCameras() {
    std::vector<std::wstring> cameras;
    winrt::com_ptr<IMFAttributes> pConfig;
    MFCreateAttributes(pConfig.put(), 1);
    pConfig->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(pConfig.get(), &ppDevices, &count))) {
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* szFriendlyName = nullptr;
            UINT32 nameLength = 0;
            if (SUCCEEDED(ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szFriendlyName, &nameLength))) {
                cameras.push_back(szFriendlyName);
                CoTaskMemFree(szFriendlyName);
            }
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }
    return cameras;
}

bool CameraCapture::Initialize(int deviceIndex) {
    winrt::com_ptr<IMFAttributes> pConfig;
    MFCreateAttributes(pConfig.put(), 1);
    pConfig->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    bool bSuccess = false;

    // 先检查枚举是否成功，只要成功，就必须负责释放 ppDevices！
    if (SUCCEEDED(MFEnumDeviceSources(pConfig.get(), &ppDevices, &count))) {
        // 再单独判断索引是否有效
        if (count > (UINT32)deviceIndex) {
            if (SUCCEEDED(ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(m_pMediaSource.put())))) {
                winrt::com_ptr<IMFAttributes> pReaderConfig;
                MFCreateAttributes(pReaderConfig.put(), 1);
                pReaderConfig->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

                if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(m_pMediaSource.get(), pReaderConfig.get(), m_pReader.put()))) {
                    winrt::com_ptr<IMFMediaType> pType;
                    MFCreateMediaType(pType.put());
                    pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

                    if (SUCCEEDED(m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType.get()))) {
                        bSuccess = true;
                    }
                }
            }
        }
        // 【关键修复】：无论上面是否激活成功，最终都必须释放内存池！
        for (UINT32 i = 0; i < count; i++) {
            if (ppDevices[i]) ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }
    return bSuccess;
}

bool CameraCapture::StartCapture() {
    if (!m_pReader || m_isCapturing) return false;
    m_isCapturing = true;
    m_captureThread = std::thread(&CameraCapture::CaptureThreadFunc, this);
    return true;
}

void CameraCapture::StopCapture() {
    if (!m_isCapturing) return;
    m_isCapturing = false;
    if (m_captureThread.joinable()) m_captureThread.join();
}

void CameraCapture::CaptureThreadFunc() {
    while (m_isCapturing) {
        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;
        winrt::com_ptr<IMFSample> pSample;

        HRESULT hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, pSample.put());
        if (FAILED(hr) || !pSample) {
            Sleep(10); continue;
        }

        winrt::com_ptr<IMFMediaBuffer> pBuffer;
        pSample->ConvertToContiguousBuffer(pBuffer.put());

        BYTE* pData = nullptr;
        DWORD currentLength = 0;
        pBuffer->Lock(&pData, NULL, &currentLength);

        // 获取视频分辨率
        winrt::com_ptr<IMFMediaType> pCurrentType;
        m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, pCurrentType.put());
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(pCurrentType.get(), MF_MT_FRAME_SIZE, &w, &h);
        LONG stride = 0;
        pCurrentType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride);
        if (stride == 0) stride = w * 4; // RGB32 默认步长

        // 生成位图并加锁更新
        HBITMAP hNewBmp = BufferToHBITMAP(pData, w, h, stride);
        pBuffer->Unlock();

        if (hNewBmp) {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            if (m_latestBmp) DeleteObject(m_latestBmp);
            m_latestBmp = hNewBmp;
            m_width = w;
            m_height = h;
        }
    }
}

HBITMAP CameraCapture::BufferToHBITMAP(BYTE* pBuffer, int width, int height, int stride) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 负数代表从上到下绘制
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (hBmp && pBits) {
        if (stride == width * 4) {
            memcpy(pBits, pBuffer, width * height * 4);
        }
        else {
            for (int y = 0; y < height; y++) {
                memcpy((BYTE*)pBits + y * width * 4, pBuffer + y * stride, width * 4);
            }
        }
    }
    return hBmp;
}

HBITMAP CameraCapture::GetLatestFrame(int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    outWidth = m_width;
    outHeight = m_height;
    if (!m_latestBmp) return nullptr;
    return (HBITMAP)CopyImage(m_latestBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
}