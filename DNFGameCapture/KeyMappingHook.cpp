#include "pch.h"
#include "KeyMappingHook.h"

CKeyMappingHook* CKeyMappingHook::s_activeHook = nullptr;

CKeyMappingHook::CKeyMappingHook()
{
    ClearKeyStates();
}

CKeyMappingHook::~CKeyMappingHook()
{
    Uninstall();
}

bool CKeyMappingHook::Install(DWORD& errorCode)
{
    errorCode = ERROR_SUCCESS;
    if (m_installed.load(std::memory_order_acquire)) return true;

    Uninstall();

    ClearKeyStates();
    {
        std::lock_guard<std::mutex> lock(m_startMutex);
        m_startCompleted = false;
        m_startSucceeded = false;
        m_startError = ERROR_SUCCESS;
    }

    try {
        m_thread = std::thread(&CKeyMappingHook::ThreadMain, this);
    }
    catch (...) {
        errorCode = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }

    std::unique_lock<std::mutex> lock(m_startMutex);
    m_startCondition.wait(lock, [this]() { return m_startCompleted; });
    const bool succeeded = m_startSucceeded;
    errorCode = m_startError;
    lock.unlock();

    if (!succeeded && m_thread.joinable()) {
        m_thread.join();
    }
    return succeeded;
}

void CKeyMappingHook::Uninstall()
{
    const DWORD threadId = m_threadId.load(std::memory_order_acquire);
    if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_installed.store(false, std::memory_order_release);
    m_threadId.store(0, std::memory_order_release);
    ClearKeyStates();
}

bool CKeyMappingHook::IsInstalled() const
{
    return m_installed.load(std::memory_order_acquire);
}

void CKeyMappingHook::ThreadMain()
{
    MSG message = {};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    m_threadId.store(GetCurrentThreadId(), std::memory_order_release);

    DWORD errorCode = ERROR_SUCCESS;
    HHOOK hook = nullptr;
    if (s_activeHook && s_activeHook != this) {
        errorCode = ERROR_ALREADY_EXISTS;
    }
    else {
        s_activeHook = this;
        hook = SetWindowsHookExW(
            WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
        if (!hook) {
            errorCode = GetLastError();
            s_activeHook = nullptr;
        }
    }

    m_installed.store(hook != nullptr, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_startMutex);
        m_startSucceeded = hook != nullptr;
        m_startError = errorCode;
        m_startCompleted = true;
    }
    m_startCondition.notify_one();

    if (!hook) {
        m_threadId.store(0, std::memory_order_release);
        return;
    }

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    m_installed.store(false, std::memory_order_release);
    UnhookWindowsHookEx(hook);
    if (s_activeHook == this) s_activeHook = nullptr;
    m_threadId.store(0, std::memory_order_release);
    ClearKeyStates();
}

void CKeyMappingHook::ClearKeyStates()
{
    for (auto& state : m_keyStates) {
        state.store(false, std::memory_order_relaxed);
    }
}

void CKeyMappingHook::SetKeyState(UINT vk, bool down)
{
    if (vk < m_keyStates.size()) {
        m_keyStates[vk].store(down, std::memory_order_relaxed);
    }
}

bool CKeyMappingHook::ReadAndCorrectKeyState(UINT vk)
{
    if (vk >= m_keyStates.size()) return false;
    if (!m_keyStates[vk].load(std::memory_order_relaxed)) return false;
    if ((GetAsyncKeyState((int)vk) & 0x8000) != 0) return true;
    m_keyStates[vk].store(false, std::memory_order_relaxed);
    return false;
}

bool CKeyMappingHook::IsKeyDown(UINT vk)
{
    if (vk >= m_keyStates.size()) return false;
    if (vk == VK_CONTROL) {
        return ReadAndCorrectKeyState(VK_CONTROL) ||
            ReadAndCorrectKeyState(VK_LCONTROL) ||
            ReadAndCorrectKeyState(VK_RCONTROL);
    }
    if (vk == VK_SHIFT) {
        return ReadAndCorrectKeyState(VK_SHIFT) ||
            ReadAndCorrectKeyState(VK_LSHIFT) ||
            ReadAndCorrectKeyState(VK_RSHIFT);
    }
    if (vk == VK_MENU) {
        return ReadAndCorrectKeyState(VK_MENU) ||
            ReadAndCorrectKeyState(VK_LMENU) ||
            ReadAndCorrectKeyState(VK_RMENU);
    }
    return ReadAndCorrectKeyState(vk);
}

LRESULT CALLBACK CKeyMappingHook::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_activeHook && lParam) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if ((info->flags & LLKHF_INJECTED) == 0) {
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
            if (down || up) {
                s_activeHook->SetKeyState(info->vkCode, down);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
