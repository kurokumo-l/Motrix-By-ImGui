module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
// clang-format on

module motrix.engine;

import std;
import motrix.core;
import :win32_util;

namespace motrix::engine::detail {

ScopedWinHandle::~ScopedWinHandle() {
    Reset();
}

ScopedWinHandle::ScopedWinHandle(ScopedWinHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

ScopedWinHandle& ScopedWinHandle::operator=(ScopedWinHandle&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool ScopedWinHandle::IsValid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

void ScopedWinHandle::Reset(void* newHandle) noexcept {
    if (IsValid()) {
        ::CloseHandle(handle_);
    }
    handle_ = newHandle;
}

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
    wchar_t buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buf).parent_path();
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

}  // namespace motrix::engine::detail
