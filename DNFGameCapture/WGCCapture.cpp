#include "pch.h"
#include "WGCCapture.h"

WGCCapture::WGCCapture() {
    // 不在这里调用 winrt::init_apartment
    // 已移到 IsSupported() 里按需调用
}

WGCCapture::~WGCCapture() {
    StopCapture();
    if (m_latestBmp) DeleteObject(m_latestBmp);
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
    if (m_d3dDevice) { m_d3dDevice->Release(); m_d3dDevice = nullptr; }
}

// =============================================================
// 【函数 1】WGCCapture::IsSupported()  —— 替换 WGCCapture.cpp 中的同名函数
// =============================================================
bool WGCCapture::IsSupported() {
    // 第一关：系统版本检测
    typedef LONG(WINAPI* RtlGetVersionFunc)(OSVERSIONINFOEXW*);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    auto pRtlGetVersion = (RtlGetVersionFunc)GetProcAddress(hNtdll, "RtlGetVersion");
    if (!pRtlGetVersion) return false;

    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    if (pRtlGetVersion(&osvi) != 0) return false;

    if (osvi.dwMajorVersion < 10 ||
        (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber < 18362)) {
        return false;
    }

    // ★★★ 核心修复：static 改为 thread_local ★★★
    // COM apartment 是 per-thread 的，每个线程必须独立初始化
    static thread_local bool s_initialized = false;
    if (!s_initialized) {
        try {
            // 🚨 全部替换为下面这句（强制使用单线程模式，匹配 MFC）：
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        }
        catch (...) {}
        s_initialized = true;
    }

    // 第三关：检测 WGC 支持
    try {
        return GraphicsCaptureSession::IsSupported();
    }
    catch (...) {
        return false;
    }
}


bool WGCCapture::CreateD3DDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, nullptr, 0, D3D11_SDK_VERSION,
        &m_d3dDevice, &fl, &m_d3dContext);

    if (FAILED(hr)) {
        // 回退到 WARP 软件渲染
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            flags, nullptr, 0, D3D11_SDK_VERSION,
            &m_d3dDevice, &fl, &m_d3dContext);
    }
    return SUCCEEDED(hr);
}

IDirect3DDevice WGCCapture::CreateWinRTDevice() {
    IDXGIDevice* dxgiDevice = nullptr;
    m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    winrt::com_ptr<::IInspectable> inspectable;
    CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, inspectable.put());
    dxgiDevice->Release();

    return inspectable.as<IDirect3DDevice>();
}

bool WGCCapture::Initialize(HWND hTargetWnd) {
    if (!IsSupported()) return false;
    if (!hTargetWnd || !IsWindow(hTargetWnd)) return false;

    m_hTargetWnd = hTargetWnd;

    if (!CreateD3DDevice()) {
        m_d3dDevice = nullptr;
        m_d3dContext = nullptr;
        return false;
    }

    // 验证设备和上下文都有效
    if (!m_d3dDevice || !m_d3dContext) return false;

    try {
        m_winrtDevice = CreateWinRTDevice();
    }
    catch (...) {
        if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
        if (m_d3dDevice) { m_d3dDevice->Release(); m_d3dDevice = nullptr; }
        return false;
    }

    try {
        auto interop = winrt::get_activation_factory<
            GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

        winrt::com_ptr<IGraphicsCaptureItem> rawItem;
        HRESULT hr = interop->CreateForWindow(
            m_hTargetWnd,
            winrt::guid_of<IGraphicsCaptureItem>(),
            rawItem.put_void());

        if (FAILED(hr)) return false;

        m_captureItem = rawItem.as<GraphicsCaptureItem>();
    }
    catch (...) {
        return false;
    }

    return true;
}

bool WGCCapture::StartCapture() {
    if (!m_captureItem || m_isCapturing) return false;
    if (!m_d3dDevice || !m_d3dContext || !m_winrtDevice) return false;

    try {
        auto size = m_captureItem.Size();
        m_captureWidth = size.Width;
        m_captureHeight = size.Height;

        m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_winrtDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2, size);

        m_frameArrivedToken = m_framePool.FrameArrived(
            { this, &WGCCapture::OnFrameArrived });

        m_session = m_framePool.CreateCaptureSession(m_captureItem);

        try { m_session.IsBorderRequired(false); }
        catch (...) {}
        try { m_session.IsCursorCaptureEnabled(false); }
        catch (...) {}

        m_session.StartCapture();
        m_isCapturing = true;
        return true;
    }
    catch (...) {
        if (m_session) { m_session.Close();   m_session = nullptr; }
        if (m_framePool) { m_framePool.Close(); m_framePool = nullptr; }
        return false;
    }
}

void WGCCapture::StopCapture() {
    if (!m_isCapturing) return;
    m_isCapturing = false;

    if (m_session) {
        m_session.Close();
        m_session = nullptr;
    }
    if (m_framePool) {
        m_framePool.Close();
        m_framePool = nullptr;
    }
}

void WGCCapture::OnFrameArrived(
    Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const&)
{
    if (!m_isCapturing || !m_d3dDevice || !m_d3dContext) return;

    auto frame = sender.TryGetNextFrame();
    if (!frame) return;

    try {
        // ★ 检测窗口尺寸是否变化，若变化则重建帧池
        auto newSize = frame.ContentSize();
        if (newSize.Width != m_captureWidth || newSize.Height != m_captureHeight) {
            m_captureWidth = newSize.Width;
            m_captureHeight = newSize.Height;
            m_framePool.Recreate(
                m_winrtDevice,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                { m_captureWidth, m_captureHeight });
            frame.Close();
            return; // 本帧丢弃，下一帧就是新尺寸了
        }

        auto surface = frame.Surface();
        // ... 后面的纹理拷贝逻辑不变 ...

        auto access = surface.as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();

        winrt::com_ptr<ID3D11Texture2D> frameTex;
        access->GetInterface(IID_PPV_ARGS(frameTex.put()));

        if (frameTex && m_d3dDevice && m_d3dContext) {
            D3D11_TEXTURE2D_DESC desc;
            frameTex->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;

            ID3D11Texture2D* stagingTex = nullptr;
            if (SUCCEEDED(m_d3dDevice->CreateTexture2D(
                &desc, nullptr, &stagingTex)))
            {
                m_d3dContext->CopyResource(stagingTex, frameTex.get());

                HBITMAP hBmp = TextureToHBitmap(stagingTex);
                if (hBmp) {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    if (m_latestBmp) DeleteObject(m_latestBmp);
                    m_latestBmp = hBmp;
                    m_width = desc.Width;
                    m_height = desc.Height;
                }
                stagingTex->Release();
            }
        }
    }
    catch (...) {
        // 安全忽略
    }

    frame.Close();
}

HBITMAP WGCCapture::TextureToHBitmap(ID3D11Texture2D* pTexture) {
    // ★★★ 核心修复：空指针保护 ★★★
    if (!pTexture || !m_d3dContext) return nullptr;

    D3D11_TEXTURE2D_DESC desc;
    pTexture->GetDesc(&desc);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_d3dContext->Map(pTexture, 0,
        D3D11_MAP_READ, 0, &mapped)))
        return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = desc.Width;
    bmi.bmiHeader.biHeight = -(int)desc.Height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi,
        DIB_RGB_COLORS, &pBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (hBmp && pBits) {
        BYTE* src = (BYTE*)mapped.pData;
        BYTE* dst = (BYTE*)pBits;
        int rowBytes = desc.Width * 4;

        for (UINT y = 0; y < desc.Height; y++) {
            memcpy(dst + y * rowBytes,
                src + y * mapped.RowPitch, rowBytes);
        }
    }

    // 🚨【终极防崩溃】：在解除映射时再次确认指针依然存活
    if (m_d3dContext) {
        m_d3dContext->Unmap(pTexture, 0);
    }
    return hBmp;
}

HBITMAP WGCCapture::GetLatestFrame(int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    outWidth = m_width;
    outHeight = m_height;

    if (!m_latestBmp) return nullptr;

    return (HBITMAP)CopyImage(m_latestBmp,
        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
}