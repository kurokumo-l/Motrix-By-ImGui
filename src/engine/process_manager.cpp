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

using detail::BuildCommandLineW;
using detail::IsRegularFileSafe;
using detail::ScopedWinHandle;

[[nodiscard]] motrix::core::Result<ScopedWinHandle> CreateKillOnCloseJob() {
    ScopedWinHandle job(::CreateJobObjectW(nullptr, nullptr));
    if (!job.IsValid()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create Win32 JobObject"));
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to configure JobObject limits"));
    }
    return job;
}

[[nodiscard]] motrix::core::Result<std::pair<ScopedWinHandle, ScopedWinHandle>> CreateLogPipe() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (!::CreatePipe(&rawRead, &rawWrite, &sa, 0)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create engine log pipe"));
    }
    ScopedWinHandle pipeRead(rawRead);
    ScopedWinHandle pipeWrite(rawWrite);
    ::SetHandleInformation(pipeRead.Get(), HANDLE_FLAG_INHERIT, 0);
    return std::make_pair(std::move(pipeRead), std::move(pipeWrite));
}

class Win32EngineProcessManager final : public IEngineProcessManager {
public:
    explicit Win32EngineProcessManager(motrix::core::Logger& logger) : logger_(logger) {}

    ~Win32EngineProcessManager() override { (void)Stop(std::chrono::milliseconds(1500)); }

    motrix::core::Result<void> Start(const EngineSpawnSpec& spec) override {
        (void)Stop(std::chrono::milliseconds(1000));
        if (IsOsSessionShuttingDown()) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::Cancelled,
                                                           "OS session shutdown in progress"));
        }
        if (!IsRegularFileSafe(spec.executablePath)) {
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::NotFound,
                                        std::format("Engine executable not found: {}",
                                                    motrix::core::PathToUtf8(spec.executablePath))));
        }

        auto job = CreateKillOnCloseJob();
        if (!job) {
            return std::unexpected(job.error());
        }
        auto pipes = CreateLogPipe();
        if (!pipes) {
            return std::unexpected(pipes.error());
        }
        auto& [pipeRead, pipeWrite] = *pipes;

        const EngineKind kind =
            (spec.hintKind == EngineKind::Unknown) ? EngineKind::ClassicAria2 : spec.hintKind;
        auto dialect = CreateDialect(kind);
        auto piRes = LaunchSuspendedChild(spec, *dialect, pipeWrite.Get());
        pipeWrite.Reset();
        if (!piRes) {
            return std::unexpected(piRes.error());
        }

        ScopedWinHandle childProc(piRes->process);
        ScopedWinHandle childThread(piRes->thread);
        if (!::AssignProcessToJobObject(job->Get(), childProc.Get())) {
            ::TerminateProcess(childProc.Get(), 1);
            state_.store(EngineProcessState::Stopped, std::memory_order_release);
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::IoFailure,
                                        "Failed to assign engine child process to JobObject"));
        }
        ::ResumeThread(childThread.Get());
        childThread.Reset();

        jobHandle_ = std::move(*job);
        procHandle_ = std::move(childProc);
        pid_.store(piRes->processId, std::memory_order_release);
        activeKind_.store(kind, std::memory_order_release);
        state_.store(EngineProcessState::Running, std::memory_order_release);

        logger_.Info("engine",
                     "Started {} (pid={}, kind={})",
                     motrix::core::PathToUtf8(spec.executablePath),
                     piRes->processId,
                     dialect->Name());
        logThread_ = std::jthread([this, pipe = std::move(pipeRead)](std::stop_token st) mutable {
            DrainPipeLoop(std::move(pipe), st);
        });
        watchThread_ = std::jthread([this](std::stop_token st) { WatchProcessExit(st); });
        return {};
    }

    motrix::core::Result<void> Stop(std::chrono::milliseconds gracefulTimeout) override {
        const auto prev = state_.load(std::memory_order_acquire);
        if (prev == EngineProcessState::Stopped && !procHandle_.IsValid() && !jobHandle_.IsValid()) {
            return {};
        }

        state_.store(EngineProcessState::Stopping, std::memory_order_release);
        if (watchThread_.joinable()) {
            watchThread_.request_stop();
        }
        if (logThread_.joinable()) {
            logThread_.request_stop();
        }

        if (procHandle_.IsValid()) {
            const DWORD waitMs = static_cast<DWORD>(std::max<std::int64_t>(0, gracefulTimeout.count()));
            const DWORD wr = ::WaitForSingleObject(procHandle_.Get(), waitMs);
            if (wr == WAIT_TIMEOUT) {
                logger_.Warn("engine", "Graceful stop timed out; terminating engine child");
                ::TerminateProcess(procHandle_.Get(), 1);
                ::WaitForSingleObject(procHandle_.Get(), 1000);
            }
        }

        if (watchThread_.joinable()) {
            watchThread_.join();
        }
        if (logThread_.joinable()) {
            logThread_.join();
        }

        procHandle_.Reset();
        jobHandle_.Reset();
        pid_.store(0, std::memory_order_release);
        state_.store(EngineProcessState::Stopped, std::memory_order_release);
        return {};
    }

    EngineProcessState State() const noexcept override { return state_.load(std::memory_order_acquire); }

    std::uint32_t ProcessId() const noexcept override { return pid_.load(std::memory_order_acquire); }

    EngineKind ActiveKind() const noexcept override {
        return activeKind_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] motrix::core::Result<detail::ChildProcessHandles>
    LaunchSuspendedChild(const EngineSpawnSpec& spec, const IEngineDialect& dialect, HANDLE stdWrite) {
        auto launchOpts = spec.launchOptions;
        if (launchOpts.hostPid == 0) {
            launchOpts.hostPid = ::GetCurrentProcessId();
        }
        const std::vector<std::string> args = dialect.BuildLaunchArgs(launchOpts, spec.initialOptions);
        state_.store(EngineProcessState::Starting, std::memory_order_release);
        detail::ChildProcessLaunchSpec launchSpec{
            .executablePath = spec.executablePath,
            .commandLine = BuildCommandLineW(spec.executablePath, args),
            .workingDir = spec.executablePath.parent_path(),
            .stdOutErrHandle = stdWrite,
            .creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED,
            .suppressErrorDialogs = true,
        };
        auto launched = detail::LaunchChildProcessWithHandleList(launchSpec);
        if (!launched) {
            state_.store(EngineProcessState::Stopped, std::memory_order_release);
            return std::unexpected(launched.error());
        }
        return *launched;
    }

    void DrainPipeLoop(ScopedWinHandle pipe, std::stop_token st) {
        std::string lineBuf;
        std::array<char, 1024> chunk{};
        while (!st.stop_requested() && pipe.IsValid()) {
            DWORD avail = 0;
            if (!::PeekNamedPipe(pipe.Get(), nullptr, 0, nullptr, &avail, nullptr)) {
                break;
            }
            if (avail == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }
            DWORD bytesRead = 0;
            const DWORD toRead = std::min<DWORD>(avail, static_cast<DWORD>(chunk.size()));
            const BOOL ok = ::ReadFile(pipe.Get(), chunk.data(), toRead, &bytesRead, nullptr);
            if (!ok || bytesRead == 0) {
                break;
            }
            lineBuf.append(chunk.data(), bytesRead);
            std::size_t nl = std::string::npos;
            while ((nl = lineBuf.find('\n')) != std::string::npos) {
                const std::string_view line =
                    motrix::core::TrimAscii(std::string_view(lineBuf.data(), nl));
                if (!line.empty()) {
                    logger_.Debug("aria2", "{}", line);
                }
                lineBuf.erase(0, nl + 1);
            }
        }
        const std::string_view tail = motrix::core::TrimAscii(lineBuf);
        if (!tail.empty()) {
            logger_.Debug("aria2", "{}", tail);
        }
    }

    void HandleWatchedProcessExit(std::stop_token st) {
        DWORD exitCode = 0;
        ::GetExitCodeProcess(procHandle_.Get(), &exitCode);
        if (IsSessionTerminationExitCode(exitCode)) {
            for (int i = 0; i < 6 && !st.stop_requested() && !IsOsSessionShuttingDown(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (st.stop_requested() || IsOsSessionShuttingDown()) {
                EngineProcessState expected = EngineProcessState::Running;
                if (state_.compare_exchange_strong(
                        expected, EngineProcessState::Stopped, std::memory_order_acq_rel)) {
                    logger_.Info(
                        "engine", "Engine exited during OS session shutdown (code={})", exitCode);
                }
                return;
            }
        }
        EngineProcessState expected = EngineProcessState::Running;
        if (state_.compare_exchange_strong(
                expected, EngineProcessState::Crashed, std::memory_order_acq_rel)) {
            logger_.Warn("engine", "Engine process exited unexpectedly (code={})", exitCode);
        }
    }

    void WatchProcessExit(std::stop_token st) {
        while (!st.stop_requested() && procHandle_.IsValid()) {
            const DWORD wr = ::WaitForSingleObject(procHandle_.Get(), 200);
            if (wr == WAIT_OBJECT_0) {
                HandleWatchedProcessExit(st);
                break;
            }
        }
    }

    motrix::core::Logger& logger_;
    ScopedWinHandle jobHandle_;
    ScopedWinHandle procHandle_;
    std::atomic<std::uint32_t> pid_{0};
    std::atomic<EngineKind> activeKind_{EngineKind::Unknown};
    std::atomic<EngineProcessState> state_{EngineProcessState::Stopped};
    std::jthread watchThread_;
    std::jthread logThread_;
};

}  // namespace

bool IsOsSessionShuttingDown() noexcept {
    return ::GetSystemMetrics(SM_SHUTTINGDOWN) != 0;
}

bool IsSessionTerminationExitCode(std::uint32_t exitCode) noexcept {
    return exitCode == 0x40010004u || exitCode == 0xC000013Au;
}

std::unique_ptr<IEngineProcessManager> CreateEngineProcessManager(motrix::core::Logger& logger) {
    return std::make_unique<Win32EngineProcessManager>(logger);
}

}  // namespace motrix::engine
