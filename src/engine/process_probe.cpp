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

namespace {

constexpr std::size_t kMaxProbeOutputBytes = 1024u * 1024u;

[[nodiscard]] std::wstring_view TrimWideToken(std::wstring_view sv) noexcept {
    while (!sv.empty() && (sv.front() == L' ' || sv.front() == L'\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == L' ' || sv.back() == L'\t')) {
        sv.remove_suffix(1);
    }
    if (sv.size() >= 2 && sv.front() == L'"' && sv.back() == L'"') {
        sv.remove_prefix(1);
        sv.remove_suffix(1);
    }
    while (!sv.empty() && (sv.front() == L' ' || sv.front() == L'\t')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == L' ' || sv.back() == L'\t')) {
        sv.remove_suffix(1);
    }
    return sv;
}

[[nodiscard]] std::optional<std::filesystem::path>
SearchEnvPathForExecutable(std::wstring_view exeName) {
    std::wstring pathEnv;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD cap = ::GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (cap == 0) {
            return std::nullopt;
        }
        pathEnv.assign(cap, L'\0');
        const DWORD got = ::GetEnvironmentVariableW(L"PATH", pathEnv.data(), cap);
        if (got > 0 && got < cap) {
            pathEnv.resize(got);
            break;
        }
        pathEnv.clear();
    }
    if (pathEnv.empty()) {
        return std::nullopt;
    }

    std::size_t pos = 0;
    while (pos <= pathEnv.size()) {
        const std::size_t sep = pathEnv.find(L';', pos);
        const std::wstring_view raw = (sep == std::wstring::npos)
                                          ? std::wstring_view(pathEnv).substr(pos)
                                          : std::wstring_view(pathEnv).substr(pos, sep - pos);
        const std::wstring_view entry = TrimWideToken(raw);
        if (!entry.empty()) {
            const std::filesystem::path dir(entry);
            if (dir.is_absolute()) {
                const std::filesystem::path candidate = dir / exeName;
                if (detail::IsRegularFileSafe(candidate)) {
                    return candidate;
                }
            }
        }
        if (sep == std::wstring::npos) {
            break;
        }
        pos = sep + 1;
    }
    return std::nullopt;
}

[[nodiscard]] detail::ScopedWinHandle CreateProbeKillJob() {
    detail::ScopedWinHandle job(::CreateJobObjectW(nullptr, nullptr));
    if (!job.IsValid()) {
        return {};
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        return {};
    }
    return job;
}

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
            if (output.size() < kMaxProbeOutputBytes) {
                const std::size_t take =
                    (std::min)(static_cast<std::size_t>(got), kMaxProbeOutputBytes - output.size());
                output.append(buf.data(), take);
            }
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
        if (auto found = SearchEnvPathForExecutable(name)) {
            return found;
        }
    }
    return std::nullopt;
}

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

    detail::ScopedWinHandle job = CreateProbeKillJob();
    if (!job.IsValid()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create probe JobObject"));
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
        .creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED,
        .suppressErrorDialogs = true,
    };
    const auto pi = detail::LaunchChildProcessWithHandleList(launchSpec);
    pipeWrite.Reset();
    if (!pi) {
        return std::unexpected(pi.error());
    }

    detail::ScopedWinHandle proc(pi->process);
    detail::ScopedWinHandle thrd(pi->thread);
    if (!::AssignProcessToJobObject(job.Get(), proc.Get())) {
        ::TerminateProcess(proc.Get(), 1);
        ::WaitForSingleObject(proc.Get(), 3000);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to assign probe process to JobObject"));
    }
    ::ResumeThread(thrd.Get());
    thrd.Reset();

    std::string output;
    if (!DrainStdoutAndWaitExit(proc.Get(), pipeRead.Get(), timeout, output)) {
        ::TerminateProcess(proc.Get(), 1);
        ::WaitForSingleObject(proc.Get(), 3000);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::Timeout,
                                                       "Version probe timed out"));
    }
    return ParseCliVersionBanner(output);
}

}  // namespace motrix::engine
