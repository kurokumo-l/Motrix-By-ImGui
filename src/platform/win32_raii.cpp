module;

// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;

namespace motrix::platform {

namespace {

std::filesystem::path g_dumpDirectory;

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE hProcess,
                                          DWORD ProcessId,
                                          HANDLE hFile,
                                          MINIDUMP_TYPE DumpType,
                                          PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                          PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                          PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exceptionInfo) {
    if (g_dumpDirectory.empty()) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    std::error_code ec;
    std::filesystem::create_directories(g_dumpDirectory, ec);

    const HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const auto writeDump =
        reinterpret_cast<MiniDumpWriteDumpFn>(::GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (writeDump) {
        const auto pid = ::GetCurrentProcessId();
        const std::filesystem::path dumpPath = g_dumpDirectory / std::format("motrix_crash_{}.dmp", pid);
        UniqueHandle file(::CreateFileW(
            dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.IsValid()) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = ::GetCurrentThreadId();
            mei.ExceptionPointers = exceptionInfo;
            mei.ClientPointers = FALSE;
            writeDump(::GetCurrentProcess(),
                      pid,
                      file.Get(),
                      MiniDumpNormal,
                      exceptionInfo ? &mei : nullptr,
                      nullptr,
                      nullptr);
        }
    }

    ::FreeLibrary(dbghelp);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

UniqueHandle::~UniqueHandle() {
    Reset();
}

UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool UniqueHandle::IsValid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

void UniqueHandle::Reset(void* newHandle) noexcept {
    if (IsValid()) {
        ::CloseHandle(handle_);
    }
    handle_ = newHandle;
}

void* UniqueHandle::Release() noexcept {
    void* prev = handle_;
    handle_ = nullptr;
    return prev;
}

UniqueFindHandle::~UniqueFindHandle() {
    Reset();
}

UniqueFindHandle::UniqueFindHandle(UniqueFindHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

UniqueFindHandle& UniqueFindHandle::operator=(UniqueFindHandle&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool UniqueFindHandle::IsValid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

void UniqueFindHandle::Reset(void* newHandle) noexcept {
    if (IsValid()) {
        ::FindClose(handle_);
    }
    handle_ = newHandle;
}

void InstallCrashMinidumpHandler(const std::filesystem::path& dumpDir) {
    g_dumpDirectory = dumpDir;
    ::SetUnhandledExceptionFilter(&CrashFilter);
}

}  // namespace motrix::platform
