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
    const std::array<std::filesystem::path, 8> candidates = {
        exeDir / "engine" / "aria2-next.exe",
        exeDir / "engine" / "aria2c.exe",
        exeDir / "assets" / "engine" / "aria2-next.exe",
        exeDir / "assets" / "engine" / "aria2c.exe",
        localAppData / "MotrixNative" / "engine" / "aria2-next.exe",
        localAppData / "MotrixNative" / "engine" / "aria2c.exe",
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

namespace {

[[nodiscard]] bool DrainStdoutAndWaitExit(HANDLE proc,
                                          HANDLE pipeRead,
                                          std::chrono::milliseconds timeout,
                                          std::string& output) {
    const auto clampedMs = std::max<std::int64_t>(100, timeout.count());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(clampedMs);
    std::array<char, 1024> buf{};

    auto drainAvailable = [&]() {
        DWORD avail = 0;
        while (::PeekNamedPipe(pipeRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            const DWORD want = (std::min)(avail, static_cast<DWORD>(buf.size()));
            DWORD got = 0;
            if (!::ReadFile(pipeRead, buf.data(), want, &got, nullptr) || got == 0) {
                break;
            }
            output.append(buf.data(), got);
        }
    };

    for (;;) {
        drainAvailable();
        if (::WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            drainAvailable();
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        ::Sleep(10);
    }
}

}  // namespace

motrix::core::Result<EngineVersionInfo>
ProbeEngineBinaryVersion(const std::filesystem::path& executablePath,
                         std::chrono::milliseconds timeout) {
    if (IsOsSessionShuttingDown()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::Cancelled,
                                                       "OS session shutdown in progress"));
    }
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

    detail::ChildProcessLaunchSpec launchSpec{
        .executablePath = executablePath,
        .commandLine = detail::BuildCommandLineW(executablePath, {"--version"}),
        .workingDir = executablePath.parent_path(),
        .stdOutErrHandle = pipeWrite.Get(),
        .creationFlags = CREATE_NO_WINDOW,
        .suppressErrorDialogs = true,
    };
    const auto pi = detail::LaunchChildProcessWithHandleList(launchSpec);
    pipeWrite.Reset();
    if (!pi) {
        return std::unexpected(pi.error());
    }

    detail::ScopedWinHandle proc(pi->process);
    detail::ScopedWinHandle thrd(pi->thread);
    thrd.Reset();

    std::string output;
    if (!DrainStdoutAndWaitExit(proc.Get(), pipeRead.Get(), timeout, output)) {
        ::TerminateProcess(proc.Get(), 1);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::Timeout,
                                                       "Version probe timed out"));
    }
    return ParseCliVersionBanner(output);
}

}  // namespace motrix::engine
