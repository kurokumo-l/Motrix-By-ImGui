module;

// clang-format off
#include <windows.h>
#include <dbghelp.h>
#include <objbase.h>
#include <psapi.h>
#include <shlobj.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :win32_internal;

namespace motrix::platform {

namespace {

inline constexpr std::size_t kMaxDumpDirLen = MAX_PATH;
inline constexpr std::size_t kMaxDumpPathLen = MAX_PATH + 96;
wchar_t g_dumpDirectoryBuf[kMaxDumpDirLen]{};
std::atomic<bool> g_dumpDirectoryReady{false};

struct CrashPathSpec {
    const wchar_t* dirBuf = nullptr;
    DWORD pid = 0;
    std::uint32_t yyyymmdd = 0;
    std::uint32_t hhmmssms = 0;
    unsigned int attempt = 0;
};

struct WideCursor {
    wchar_t* cur = nullptr;
    wchar_t* end = nullptr;
    bool ok = true;

    void AppendLiteral(const wchar_t* lit) noexcept {
        if (!lit) {
            return;
        }
        while (*lit != L'\0') {
            if (cur >= end) {
                ok = false;
                return;
            }
            *cur++ = *lit++;
        }
    }

    void AppendUInt(std::uint64_t val, int minDigits) noexcept {
        wchar_t rev[24]{};
        int len = 0;
        do {
            rev[len++] = static_cast<wchar_t>(L'0' + (val % 10u));
            val /= 10u;
        } while (val > 0 && len < 24);
        while (len < minDigits && len < 24) {
            rev[len++] = L'0';
        }
        for (int i = len - 1; i >= 0; --i) {
            if (cur >= end) {
                ok = false;
                return;
            }
            *cur++ = rev[i];
        }
    }
};

void FormatCrashFilenameInto(WideCursor& out, const CrashPathSpec& spec) noexcept {
    out.AppendLiteral(L"motrix_crash_");
    out.AppendUInt(spec.pid, 1);
    out.AppendLiteral(L"_");
    out.AppendUInt(spec.yyyymmdd, 8);
    out.AppendLiteral(L"_");
    out.AppendUInt(spec.hhmmssms, 9);
    if (spec.attempt > 0) {
        out.AppendLiteral(L"_");
        out.AppendUInt(spec.attempt, 1);
    }
    out.AppendLiteral(L".dmp");
    if (out.ok && out.cur < out.end) {
        *out.cur = L'\0';
    } else {
        out.ok = false;
    }
}

[[nodiscard]] bool
BuildCrashDumpPath(wchar_t* outBuf, std::size_t outCap, const CrashPathSpec& spec) noexcept {
    if (!outBuf || outCap == 0 || !spec.dirBuf || spec.dirBuf[0] == L'\0') {
        return false;
    }
    WideCursor cursor{outBuf, outBuf + outCap - 1, true};
    cursor.AppendLiteral(spec.dirBuf);
    if (cursor.ok && cursor.cur > outBuf && cursor.cur[-1] != L'\\' && cursor.cur[-1] != L'/') {
        cursor.AppendLiteral(L"\\");
    }
    FormatCrashFilenameInto(cursor, spec);
    return cursor.ok;
}

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE hProcess,
                                          DWORD ProcessId,
                                          HANDLE hFile,
                                          MINIDUMP_TYPE DumpType,
                                          PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                          PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                          PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

[[nodiscard]] HANDLE
OpenUniqueDumpFile(const wchar_t* dirBuf, DWORD pid, const SYSTEMTIME& st) noexcept {
    const std::uint32_t ymd = static_cast<std::uint32_t>(st.wYear) * 10000u
                              + static_cast<std::uint32_t>(st.wMonth) * 100u
                              + static_cast<std::uint32_t>(st.wDay);
    const std::uint32_t hmsm = static_cast<std::uint32_t>(st.wHour) * 10000000u
                               + static_cast<std::uint32_t>(st.wMinute) * 100000u
                               + static_cast<std::uint32_t>(st.wSecond) * 1000u
                               + static_cast<std::uint32_t>(st.wMilliseconds);
    wchar_t dumpPath[kMaxDumpPathLen]{};
    for (unsigned int attempt = 0; attempt < 16; ++attempt) {
        const CrashPathSpec spec{dirBuf, pid, ymd, hmsm, attempt};
        if (!BuildCrashDumpPath(dumpPath, kMaxDumpPathLen, spec)) {
            return INVALID_HANDLE_VALUE;
        }
        HANDLE raw = ::CreateFileW(
            dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw != INVALID_HANDLE_VALUE) {
            return raw;
        }
        if (::GetLastError() != ERROR_FILE_EXISTS) {
            return INVALID_HANDLE_VALUE;
        }
    }
    return INVALID_HANDLE_VALUE;
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exceptionInfo) {
    if (!g_dumpDirectoryReady.load(std::memory_order_acquire) || g_dumpDirectoryBuf[0] == L'\0') {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    (void)::CreateDirectoryW(g_dumpDirectoryBuf, nullptr);

    const HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const auto writeDump =
        reinterpret_cast<MiniDumpWriteDumpFn>(::GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (writeDump) {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        const DWORD pid = ::GetCurrentProcessId();
        UniqueHandle file(OpenUniqueDumpFile(g_dumpDirectoryBuf, pid, st));
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

void FreeWin32CoTaskMem(void* ptr) noexcept {
    if (ptr) {
        ::CoTaskMemFree(ptr);
    }
}

void InstallCrashMinidumpHandler(const std::filesystem::path& dumpDir) {
    g_dumpDirectoryReady.store(false, std::memory_order_release);
    const std::wstring wideDir = dumpDir.wstring();
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    if (!wideDir.empty() && wideDir.size() < kMaxDumpDirLen) {
        std::memcpy(g_dumpDirectoryBuf, wideDir.c_str(), (wideDir.size() + 1) * sizeof(wchar_t));
        g_dumpDirectoryReady.store(true, std::memory_order_release);
    } else {
        g_dumpDirectoryBuf[0] = L'\0';
    }
    ::SetUnhandledExceptionFilter(&CrashFilter);
}

std::string FormatCrashDumpFilename(std::uint32_t pid,
                                    std::uint32_t yyyymmdd,
                                    std::uint32_t hhmmssms,
                                    unsigned int attempt) {
    wchar_t buf[96]{};
    WideCursor cursor{buf, buf + std::size(buf) - 1, true};
    FormatCrashFilenameInto(cursor, CrashPathSpec{nullptr, pid, yyyymmdd, hhmmssms, attempt});
    return cursor.ok ? motrix::core::WideToUtf8(buf) : std::string{};
}

namespace {

[[nodiscard]] std::filesystem::path QueryKnownFolder(const KNOWNFOLDERID& folderId) {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) {
        FreeWin32CoTaskMem(raw);
        return {};
    }
    const UniqueCoTaskPtr<wchar_t> guard(raw);
    return std::filesystem::path(guard.get());
}

}  // namespace

std::filesystem::path ResolveDefaultDownloadsDir() {
    auto p = QueryKnownFolder(FOLDERID_Downloads);
    if (!p.empty()) {
        return p;
    }
    auto profile = QueryKnownFolder(FOLDERID_Profile);
    if (!profile.empty()) {
        return profile / "Downloads";
    }
    return std::filesystem::temp_directory_path();
}

ScopedComInit::ScopedComInit() noexcept {
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    shouldUninit_ = (hr == S_OK || hr == S_FALSE);
    usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

ScopedComInit::~ScopedComInit() {
    if (shouldUninit_) {
        ::CoUninitialize();
    }
}

std::filesystem::path ResolveAppDataDir() {
    auto local = QueryKnownFolder(FOLDERID_LocalAppData);
    if (!local.empty()) {
        return local / "MotrixNative";
    }
    return std::filesystem::temp_directory_path() / "MotrixNative";
}

void Win32TrimWorkingSet() noexcept {
    ::K32EmptyWorkingSet(::GetCurrentProcess());
}

}  // namespace motrix::platform
