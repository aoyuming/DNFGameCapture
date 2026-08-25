#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <Windows.h>

class CKeyMappingHook
{
public:
    CKeyMappingHook();
    ~CKeyMappingHook();

    bool Install(DWORD& errorCode);
    void Uninstall();
    bool IsInstalled() const;
    bool IsKeyDown(UINT vk);

private:
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static CKeyMappingHook* s_activeHook;

    void ThreadMain();
    void ClearKeyStates();
    void SetKeyState(UINT vk, bool down);
    bool ReadAndCorrectKeyState(UINT vk);

    std::array<std::atomic<bool>, 256> m_keyStates;
    std::thread m_thread;
    std::mutex m_startMutex;
    std::condition_variable m_startCondition;
    bool m_startCompleted = false;
    bool m_startSucceeded = false;
    DWORD m_startError = ERROR_SUCCESS;
    std::atomic<bool> m_installed{ false };
    std::atomic<DWORD> m_threadId{ 0 };
};
