module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
// clang-format on

module motrix.engine;

import std;
import motrix.core;
import :win32_util;

namespace motrix::engine::detail {

std::wstring QuoteWin32Arg(std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(arg);
    }
    std::wstring out;
    out.push_back(L'"');
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring BuildCommandLineW(const std::filesystem::path& exe, const std::vector<std::string>& args) {
    std::wstring cmd = QuoteWin32Arg(exe.wstring());
    for (const auto& a : args) {
        cmd.push_back(L' ');
        cmd.append(QuoteWin32Arg(motrix::core::Utf8ToWide(a)));
    }
    return cmd;
}

std::filesystem::path GetExecutableDir() {
    return motrix::core::CurrentExecutablePath().parent_path();
}

std::filesystem::path GetLocalAppDataDir() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) {
        if (raw) {
            ::CoTaskMemFree(raw);
        }
        return {};
    }
    struct Deleter {
        void operator()(PWSTR p) const noexcept { ::CoTaskMemFree(p); }
    };
    const std::unique_ptr<wchar_t, Deleter> guard(raw);
    return std::filesystem::path(guard.get());
}

bool IsRegularFileSafe(const std::filesystem::path& p) {
    if (p.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

namespace {

class ProcAttrListGuard {
public:
    ~ProcAttrListGuard() {
        if (ready_ && list_) {
            ::DeleteProcThreadAttributeList(list_);
        }
    }

    [[nodiscard]] bool Init(DWORD count) {
        SIZE_T bytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, count, 0, &bytes);
        if (bytes == 0) {
            return false;
        }
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        ready_ = ::InitializeProcThreadAttributeList(list_, count, 0, &bytes) != FALSE;
        return ready_;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept { return list_; }

private:
    std::vector<std::uint8_t> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
    bool ready_ = false;
};

struct BcryptAlgGuard {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~BcryptAlgGuard() {
        if (h) {
            ::BCryptCloseAlgorithmProvider(h, 0);
        }
    }
};

struct BcryptHashGuard {
    BCRYPT_HASH_HANDLE h = nullptr;
    ~BcryptHashGuard() {
        if (h) {
            ::BCryptDestroyHash(h);
        }
    }
};

}  // namespace

motrix::core::Result<ChildProcessHandles>
LaunchChildProcessWithHandleList(ChildProcessLaunchSpec& spec) {
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;

    ProcAttrListGuard attrList;
    HANDLE inheritHandle = static_cast<HANDLE>(spec.stdOutErrHandle);
    const bool hasInherit = (inheritHandle != nullptr && inheritHandle != INVALID_HANDLE_VALUE);
    if (hasInherit) {
        si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        si.StartupInfo.hStdOutput = inheritHandle;
        si.StartupInfo.hStdError = inheritHandle;
        if (!attrList.Init(1)
            || !::UpdateProcThreadAttribute(attrList.Get(),
                                            0,
                                            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                            &inheritHandle,
                                            sizeof(inheritHandle),
                                            nullptr,
                                            nullptr)) {
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::IoFailure,
                                        "Failed to build child handle list attribute"));
        }
        si.lpAttributeList = attrList.Get();
    }

    DWORD oldMode = 0;
    const BOOL modeSet =
        spec.suppressErrorDialogs
            ? ::SetThreadErrorMode(
                  SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, &oldMode)
            : FALSE;
    PROCESS_INFORMATION pi{};
    const DWORD flags = spec.creationFlags | (hasInherit ? EXTENDED_STARTUPINFO_PRESENT : 0u);
    const std::wstring workDirW = spec.workingDir.wstring();
    const BOOL ok = ::CreateProcessW(spec.executablePath.c_str(),
                                     spec.commandLine.data(),
                                     nullptr,
                                     nullptr,
                                     hasInherit ? TRUE : FALSE,
                                     flags,
                                     nullptr,
                                     workDirW.empty() ? nullptr : workDirW.c_str(),
                                     &si.StartupInfo,
                                     &pi);
    const DWORD err = ok ? ERROR_SUCCESS : ::GetLastError();
    if (modeSet) {
        ::SetThreadErrorMode(oldMode, nullptr);
    }
    if (!ok) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::IoFailure,
                                    std::format("CreateProcessW failed with Win32 error {}", err)));
    }
    return ChildProcessHandles{pi.hProcess, pi.hThread, pi.dwProcessId};
}

motrix::core::Result<std::string> ComputeFileSha256Hex(const std::filesystem::path& filePath) {
    BcryptAlgGuard alg;
    if (::BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "BCrypt open failed"));
    }
    BcryptHashGuard hash;
    if (::BCryptCreateHash(alg.h, &hash.h, nullptr, 0, nullptr, 0, 0) != 0) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "BCrypt create failed"));
    }
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::NotFound,
                                                       "Cannot read staged file"));
    }
    std::array<std::uint8_t, 65536> buf{};
    while (in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))
           || in.gcount() > 0) {
        const ULONG n = static_cast<ULONG>(in.gcount());
        if (::BCryptHashData(hash.h, buf.data(), n, 0) != 0) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::IoFailure,
                                                           "BCrypt hash update failed"));
        }
    }
    std::array<std::uint8_t, 32> digest{};
    if (::BCryptFinishHash(hash.h, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "BCrypt FinishHash failed"));
    }
    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (std::uint8_t b : digest) {
        hex.push_back(kHex[(b >> 4u) & 0xFu]);
        hex.push_back(kHex[b & 0xFu]);
    }
    return hex;
}

}  // namespace motrix::engine::detail
