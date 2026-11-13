module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format on

module motrix.engine;

import std;
import motrix.core;
import :win32_util;

namespace motrix::engine {

std::uint32_t CurrentHostProcessId() noexcept {
    return ::GetCurrentProcessId();
}

std::optional<std::filesystem::path> DiscoverEngineExecutable(std::string_view configuredPath) {
    if (!configuredPath.empty()) {
        const auto p = motrix::core::PathFromUtf8(configuredPath);
        if (detail::IsRegularFileSafe(p)) {
            return p;
        }
    }

    const auto exeDir = detail::GetExecutableDir();
    const auto localAppData = detail::GetLocalAppDataDir();
    const std::array<std::filesystem::path, 6> candidates = {
        exeDir / "engine" / "aria2-next.exe",
        exeDir / "engine" / "aria2c.exe",
        exeDir / "assets" / "engine" / "aria2-next.exe",
        exeDir / "assets" / "engine" / "aria2c.exe",
        localAppData / "Programs" / "Motrix" / "resources" / "engine" / "aria2-next.exe",
        localAppData / "Programs" / "Motrix" / "resources" / "engine" / "aria2c.exe",
    };
    for (const auto& c : candidates) {
        if (detail::IsRegularFileSafe(c)) {
            return c;
        }
    }

    for (const wchar_t* name : {L"aria2-next.exe", L"aria2c.exe"}) {
        wchar_t found[MAX_PATH]{};
        const DWORD len = ::SearchPathW(nullptr, name, nullptr, MAX_PATH, found, nullptr);
        if (len > 0 && len < MAX_PATH) {
            std::filesystem::path p(found);
            if (detail::IsRegularFileSafe(p)) {
                return p;
            }
        }
    }
    return std::nullopt;
}

motrix::core::Result<EngineVersionInfo>
ProbeEngineBinaryVersion(const std::filesystem::path& executablePath,
                         std::chrono::milliseconds timeout) {
    if (!detail::IsRegularFileSafe(executablePath)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::NotFound,
                                                       "Engine binary missing for version probe"));
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (!::CreatePipe(&rawRead, &rawWrite, &sa, 0)) {
        return std::unexpected(motrix::core::MakeError(
            motrix::core::ErrorDomain::Engine, motrix::core::ErrorCode::IoFailure, "CreatePipe failed"));
    }
    detail::ScopedWinHandle pipeRead(rawRead);
    detail::ScopedWinHandle pipeWrite(rawWrite);
    ::SetHandleInformation(pipeRead.Get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = pipeWrite.Get();
    si.hStdError = pipeWrite.Get();

    PROCESS_INFORMATION pi{};
    std::wstring cmd = detail::BuildCommandLineW(executablePath, {"--version"});
    if (!::CreateProcessW(executablePath.c_str(),
                          cmd.data(),
                          nullptr,
                          nullptr,
                          TRUE,
                          CREATE_NO_WINDOW,
                          nullptr,
                          nullptr,
                          &si,
                          &pi)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to run --version probe"));
    }
    pipeWrite.Reset();

    detail::ScopedWinHandle proc(pi.hProcess);
    detail::ScopedWinHandle thrd(pi.hThread);
    thrd.Reset();

    const DWORD wr = ::WaitForSingleObject(
        proc.Get(), static_cast<DWORD>(std::max<std::int64_t>(100, timeout.count())));
    if (wr == WAIT_TIMEOUT) {
        ::TerminateProcess(proc.Get(), 1);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::Timeout,
                                                       "Version probe timed out"));
    }

    std::string output;
    std::array<char, 1024> buf{};
    DWORD n = 0;
    while (::ReadFile(pipeRead.Get(), buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr)
           && n > 0) {
        output.append(buf.data(), n);
    }
    return ParseCliVersionBanner(output);
}

}  // namespace motrix::engine
