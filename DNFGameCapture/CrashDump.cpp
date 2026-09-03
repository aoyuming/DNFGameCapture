#include "CrashDump.h"

#include <windows.h>
#include <dbghelp.h>

#include <cwchar>
#include <iterator>

#pragma comment(lib, "Dbghelp.lib")

namespace {

LONG WINAPI DnfWriteCrashDump(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    wchar_t executablePath[MAX_PATH] = {};
    const DWORD pathLength = ::GetModuleFileNameW(nullptr, executablePath,
        static_cast<DWORD>(std::size(executablePath)));
    if (pathLength == 0 || pathLength >= std::size(executablePath)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    wchar_t* separator = std::wcsrchr(executablePath, L'\\');
    if (!separator) return EXCEPTION_EXECUTE_HANDLER;
    *separator = L'\0';

    wchar_t dumpDirectory[MAX_PATH] = {};
    if (swprintf_s(dumpDirectory, L"%s\\crash-dumps", executablePath) <= 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (!::CreateDirectoryW(dumpDirectory, nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    SYSTEMTIME localTime{};
    ::GetLocalTime(&localTime);
    const DWORD processId = ::GetCurrentProcessId();
    const DWORD exceptionCode = exceptionPointers && exceptionPointers->ExceptionRecord ?
        exceptionPointers->ExceptionRecord->ExceptionCode : 0;
    wchar_t dumpPath[MAX_PATH] = {};
    if (swprintf_s(dumpPath,
        L"%s\\DNFGameCapture-crash-%04u%02u%02u-%02u%02u%02u-pid%lu-code%08lX.dmp",
        dumpDirectory,
        localTime.wYear, localTime.wMonth, localTime.wDay,
        localTime.wHour, localTime.wMinute, localTime.wSecond,
        processId, exceptionCode) <= 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    HANDLE dumpFile = ::CreateFileW(dumpPath, GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile == INVALID_HANDLE_VALUE) return EXCEPTION_EXECUTE_HANDLER;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    exceptionInfo.ThreadId = ::GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;
    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithThreadInfo);
    const BOOL dumpWritten = ::MiniDumpWriteDump(
        ::GetCurrentProcess(), processId, dumpFile, dumpType,
        exceptionPointers ? &exceptionInfo : nullptr, nullptr, nullptr);
    if (dumpWritten) {
        ::FlushFileBuffers(dumpFile);
    }
    ::CloseHandle(dumpFile);
    if (!dumpWritten) {
        ::DeleteFileW(dumpPath);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void DnfInstallCrashDumpHandler() noexcept
{
    ::SetUnhandledExceptionFilter(DnfWriteCrashDump);
}
