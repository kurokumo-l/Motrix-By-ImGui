module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import :engine_runtime;

namespace motrix::app {

namespace {

[[nodiscard]] std::vector<motrix::engine::EngineOptionPair>
ToEnginePairs(const std::vector<std::pair<std::string, std::string>>& raw) {
    std::vector<motrix::engine::EngineOptionPair> out;
    out.reserve(raw.size());
    for (const auto& [k, v] : raw) {
        out.push_back({k, v});
    }
    return out;
}

}  // namespace

EngineRuntime::EngineRuntime(motrix::core::Logger& logger, EngineRuntimeConfig cfg, SyncCallback onSync)
    : logger_(logger), config_(std::move(cfg)), onSync_(std::move(onSync)) {
    proc_ = motrix::engine::CreateEngineProcessManager(logger_);
    rpc_ = motrix::engine::CreateWsRpcClient(logger_);
    upnp_ = motrix::engine::CreateUpnpPortMapper(logger_);
    rpc_->SetNotificationCallback([this](const motrix::engine::EngineNotification& ev) {
        if (ev.kind == motrix::engine::EngineEventKind::Disconnected) {
            disconnectedFlag_.store(true, std::memory_order_release);
        } else if (ev.kind == motrix::engine::EngineEventKind::DownloadComplete
                   || ev.kind == motrix::engine::EngineEventKind::BtDownloadComplete) {
            wakeSync_.store(true, std::memory_order_release);
        } else {
            wakeSync_.store(true, std::memory_order_release);
        }
        queueCv_.notify_all();
    });
}

EngineRuntime::~EngineRuntime() {
    Stop();
}

void EngineRuntime::Start() {
    if (worker_.joinable()) {
        return;
    }
    worker_ = std::jthread([this](std::stop_token st) { RunLoop(st); });
}

void EngineRuntime::Stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        queueCv_.notify_all();
        worker_.join();
    }
    if (upnpWorker_.joinable()) {
        upnpWorker_.request_stop();
        upnpWorker_.join();
    }
    if (upnp_) {
        upnp_->UnmapAll();
    }
}

void EngineRuntime::Enqueue(EngineWorkCommand cmd) {
    {
        std::scoped_lock lock(queueMutex_);
        queue_.push_back(std::move(cmd));
    }
    queueCv_.notify_all();
}

void EngineRuntime::Reconfigure(EngineRuntimeConfig cfg, bool restartEngine) {
    {
        std::scoped_lock lock(queueMutex_);
        pendingConfig_ = std::move(cfg);
        if (restartEngine) {
            restartRequested_.store(true, std::memory_order_release);
        }
    }
    queueCv_.notify_all();
}

void EngineRuntime::SetShutdownSuspended(bool suspended) noexcept {
    shutdownSuspended_.store(suspended, std::memory_order_release);
    queueCv_.notify_all();
}

bool EngineRuntime::IsShutdownBlocked() const noexcept {
    return shutdownSuspended_.load(std::memory_order_acquire)
           || motrix::engine::IsOsSessionShuttingDown();
}

bool EngineRuntime::WaitWhileShutdownSuspended(std::stop_token st) {
    std::unique_lock lock(queueMutex_);
    queueCv_.wait(lock, st, [&] {
        return st.stop_requested() || !shutdownSuspended_.load(std::memory_order_acquire);
    });
    return !st.stop_requested() && !motrix::engine::IsOsSessionShuttingDown();
}

void EngineRuntime::WaitReconnectBackoff(std::stop_token st, std::chrono::milliseconds delay) {
    std::unique_lock lock(queueMutex_);
    queueCv_.wait_for(lock, st, delay, [&] { return st.stop_requested() || IsShutdownBlocked(); });
}

void EngineRuntime::RunLoop(std::stop_token st) {
    onSync_(EngineSyncUpdate{.connState = EngineConnectionState::Starting});
    while (!st.stop_requested()) {
        if (!WaitWhileShutdownSuspended(st)) {
            break;
        }
        {
            std::scoped_lock lock(queueMutex_);
            if (pendingConfig_) {
                config_ = std::move(*pendingConfig_);
                pendingConfig_.reset();
            }
            restartRequested_.store(false, std::memory_order_release);
            resetSessionRequested_ = false;
        }
        if (IsShutdownBlocked()) {
            continue;
        }
        if (!TryStartAndConnect(st)) {
            if (st.stop_requested() || motrix::engine::IsOsSessionShuttingDown()) {
                break;
            }
            if (!shutdownSuspended_.load(std::memory_order_acquire)) {
                WaitReconnectBackoff(st, std::chrono::seconds(3));
            }
            continue;
        }
        RunConnectedSession(st);
        if (st.stop_requested() || motrix::engine::IsOsSessionShuttingDown()) {
            break;
        }
        if (resetSessionRequested_) {
            onSync_(EngineSyncUpdate{.connState = EngineConnectionState::Starting});
        } else if (restartRequested_.load(std::memory_order_acquire)) {
            ShutdownEngineGracefully();
            onSync_(EngineSyncUpdate{.connState = EngineConnectionState::Starting});
        } else if (!shutdownSuspended_.load(std::memory_order_acquire)) {
            WaitReconnectBackoff(st, std::chrono::milliseconds(400));
        }
    }
    ShutdownEngineGracefully();
}

void EngineRuntime::RunConnectedSession(std::stop_token st) {
    disconnectedFlag_.store(false, std::memory_order_release);
    wakeSync_.store(true, std::memory_order_release);

    while (!st.stop_requested() && !IsShutdownBlocked()) {
        if (restartRequested_.load(std::memory_order_acquire)) {
            return;
        }
        const bool lost = disconnectedFlag_.load(std::memory_order_acquire)
                          || proc_->State() == motrix::engine::EngineProcessState::Crashed
                          || !rpc_->IsConnected();
        if (lost) {
            if (!IsShutdownBlocked()) {
                onSync_(EngineSyncUpdate{
                    .connState = EngineConnectionState::Reconnecting,
                    .version = &versionInfo_,
                    .enginePath = resolvedPath_,
                    .errorMsg = "Engine connection lost, reconnecting...",
                });
            }
            rpc_->Disconnect();
            (void)proc_->Stop(std::chrono::milliseconds(1000));
            return;
        }

        std::deque<EngineWorkCommand> pending;
        {
            std::unique_lock lock(queueMutex_);
            if (pendingConfig_ && !restartRequested_.load(std::memory_order_acquire)) {
                config_ = std::move(*pendingConfig_);
                pendingConfig_.reset();
            }
            const auto waitDur =
                hasActiveTasks_ ? std::chrono::milliseconds(500) : std::chrono::seconds(5);
            if (queue_.empty() && !wakeSync_.load(std::memory_order_acquire)) {
                queueCv_.wait_for(lock, st, waitDur, [&] {
                    return st.stop_requested() || IsShutdownBlocked() || !queue_.empty()
                           || wakeSync_.load(std::memory_order_acquire)
                           || disconnectedFlag_.load(std::memory_order_acquire)
                           || restartRequested_.load(std::memory_order_acquire);
                });
            }
            if (st.stop_requested() || IsShutdownBlocked()
                || restartRequested_.load(std::memory_order_acquire)
                || disconnectedFlag_.load(std::memory_order_acquire)) {
                return;
            }
            pending.swap(queue_);
        }

        bool needSync = wakeSync_.exchange(false, std::memory_order_acq_rel);
        std::string cmdErr;
        while (!pending.empty()) {
            ProcessCommand(pending.front(), needSync, cmdErr);
            pending.pop_front();
            if (resetSessionRequested_ || st.stop_requested() || IsShutdownBlocked()
                || restartRequested_.load(std::memory_order_acquire)
                || disconnectedFlag_.load(std::memory_order_acquire)) {
                break;
            }
        }
        if (!pending.empty() && !st.stop_requested()) {
            std::scoped_lock lock(queueMutex_);
            while (!pending.empty()) {
                queue_.push_front(std::move(pending.back()));
                pending.pop_back();
            }
        }
        if (resetSessionRequested_) {
            return;
        }
        if (!needSync && !hasActiveTasks_) {
            continue;
        }
        if (auto snap = rpc_->SyncAll(100, 100); snap) {
            ApplySyncSnapshot(*snap, cmdErr);
        } else if (!st.stop_requested()) {
            logger_.Warn("app", "SyncAll failed: {}", snap.error().message);
            disconnectedFlag_.store(true, std::memory_order_release);
        }
    }
}

void EngineRuntime::ShutdownEngineGracefully() {
    if (rpc_->IsConnected()) {
        (void)rpc_->SaveSession();
        (void)rpc_->Shutdown(false);
        rpc_->Disconnect();
    }
    (void)proc_->Stop(std::chrono::milliseconds(3000));
}

EngineRuntime::PreparedLaunch EngineRuntime::PrepareLaunchSpec(const std::filesystem::path& binPath,
                                                               motrix::engine::EngineKind kind) const {
    std::error_code ec;
    std::filesystem::create_directories(config_.appDataDir, ec);

    motrix::engine::EngineLaunchOptions launch{};
    launch.rpcPort = config_.sysConfig.rpcListenPort;
    launch.rpcSecret = config_.sysConfig.rpcSecret;
    launch.hostPid = motrix::engine::CurrentHostProcessId();
    launch.saveIntervalSeconds = 10;
    launch.logLevel = config_.logLevel.empty() ? "warn" : config_.logLevel;
    launch.sessionFilePath = config_.appDataDir / "aria2.session";
    launch.sessionFileExists = std::filesystem::is_regular_file(launch.sessionFilePath, ec);
    if (dialect_->UsesStateDirInsteadOfAria2Files()) {
        launch.stateOrSessionPath = config_.appDataDir / "engine_state";
        std::filesystem::create_directories(launch.stateOrSessionPath, ec);
    } else {
        launch.stateOrSessionPath = launch.sessionFilePath;
    }

    auto globalOpts = ToEnginePairs(motrix::domain::SystemConfigToOptionPairs(config_.sysConfig));
    globalOpts.push_back({"bt-port-mapping", config_.enableUpnp ? "true" : "false"});
    const auto filteredOpts = dialect_->FilterGlobalOptions(globalOpts);
    const std::filesystem::path confPath = config_.appDataDir / "aria2.conf";
    bool wroteConf = false;
    {
        std::unique_lock<std::mutex> persistLock;
        if (config_.configPersistMutex) {
            persistLock = std::unique_lock<std::mutex>(*config_.configPersistMutex);
        }
        wroteConf =
            motrix::core::WriteTextAtomic(confPath, motrix::engine::FormatAria2Conf(filteredOpts))
                .has_value();
    }
    if (wroteConf) {
        launch.confPath = confPath;
    }

    PreparedLaunch out{};
    out.spec.executablePath = binPath;
    out.spec.launchOptions = launch;
    if (launch.confPath.empty()) {
        out.spec.initialOptions = filteredOpts;
    }
    out.spec.hintKind = kind;
    out.globalOpts = std::move(globalOpts);
    return out;
}

void EngineRuntime::FinalizeConnectedSession(
    const motrix::core::Result<motrix::engine::EngineVersionInfo>& probe,
    const std::vector<motrix::engine::EngineOptionPair>& globalOpts) {
    if (auto ver = rpc_->GetVersion(); ver) {
        if (ver->product.empty() && probe) {
            ver->product = probe->product;
            ver->kind = probe->kind;
        }
        if (ver->enabledFeatures.empty() && probe) {
            ver->enabledFeatures = probe->enabledFeatures;
        }
        versionInfo_ = *ver;
        if (ver->kind != dialect_->Kind()) {
            dialect_ = motrix::engine::CreateDialect(ver->kind);
        }
    }
    (void)rpc_->ChangeGlobalOption(dialect_->FilterGlobalOptions(globalOpts));
    if (config_.resumeAllWhenLaunched && !didLaunchResume_) {
        didLaunchResume_ = true;
        (void)rpc_->UnpauseAll();
    }
    if (!dialect_->SupportsNativeUpnp()) {
        StartOrRefreshUpnpAsync(
            config_.enableUpnp, config_.sysConfig.listenPort, config_.sysConfig.dhtListenPort);
    }
    if (auto initialSync = rpc_->SyncAll(100, 100); initialSync) {
        ApplySyncSnapshot(*initialSync, {});
        return;
    }
    onSync_(EngineSyncUpdate{
        .connState = EngineConnectionState::Connected,
        .version = &versionInfo_,
        .enginePath = resolvedPath_,
    });
}

bool EngineRuntime::TryStartAndConnect(std::stop_token st) {
    if (st.stop_requested() || IsShutdownBlocked()) {
        return false;
    }
    const auto binOpt = motrix::engine::DiscoverEngineExecutable(config_.enginePathOverride);
    if (!binOpt) {
        onSync_(EngineSyncUpdate{
            .connState = EngineConnectionState::Offline,
            .errorMsg = "Engine binary (aria2c / aria2-next) not found",
        });
        return false;
    }

    resolvedPath_ = motrix::core::PathToUtf8(*binOpt);
    const auto probe = motrix::engine::ProbeEngineBinaryVersion(*binOpt);
    if (st.stop_requested() || IsShutdownBlocked()) {
        return false;
    }
    const auto kind = probe ? probe->kind : motrix::engine::EngineKind::ClassicAria2;
    if (probe) {
        versionInfo_ = *probe;
    }
    dialect_ = motrix::engine::CreateDialect(kind);
    const PreparedLaunch launch = PrepareLaunchSpec(*binOpt, kind);

    if (auto startRes = proc_->Start(launch.spec); !startRes) {
        if (!IsShutdownBlocked()) {
            onSync_(EngineSyncUpdate{
                .connState = EngineConnectionState::Offline,
                .version = &versionInfo_,
                .enginePath = resolvedPath_,
                .errorMsg = startRes.error().message,
            });
        }
        return false;
    }

    const motrix::engine::RpcConnectionOptions rpcOpts{
        .host = "127.0.0.1",
        .port = config_.sysConfig.rpcListenPort,
        .secret = config_.sysConfig.rpcSecret,
    };
    for (int attempt = 0; attempt < 25 && !st.stop_requested() && !IsShutdownBlocked(); ++attempt) {
        if (proc_->State() == motrix::engine::EngineProcessState::Crashed) {
            if (!IsShutdownBlocked()) {
                onSync_(EngineSyncUpdate{
                    .connState = EngineConnectionState::Offline,
                    .version = &versionInfo_,
                    .enginePath = resolvedPath_,
                    .errorMsg = "Engine exited immediately on startup (check RPC port conflict)",
                });
            }
            return false;
        }
        if (rpc_->Connect(rpcOpts)) {
            FinalizeConnectedSession(probe, launch.globalOpts);
            return true;
        }
        std::unique_lock lock(queueMutex_);
        queueCv_.wait_for(lock, st, std::chrono::milliseconds(150), [&] {
            return st.stop_requested() || IsShutdownBlocked();
        });
    }

    rpc_->Disconnect();
    (void)proc_->Stop(std::chrono::milliseconds(1000));
    if (!st.stop_requested() && !IsShutdownBlocked()) {
        onSync_(EngineSyncUpdate{
            .connState = EngineConnectionState::Offline,
            .version = &versionInfo_,
            .enginePath = resolvedPath_,
            .errorMsg = "Timed out connecting to aria2 WebSocket RPC",
        });
    }
    return false;
}

void EngineRuntime::ApplySyncSnapshot(const motrix::engine::EngineSyncSnapshot& snap,
                                      std::string_view err) {
    hasActiveTasks_ = !snap.active.empty() || snap.globalStat.numActive > 0;
    onSync_(EngineSyncUpdate{
        .connState = EngineConnectionState::Connected,
        .version = &versionInfo_,
        .sync = &snap,
        .enginePath = resolvedPath_,
        .errorMsg = err,
    });
    std::string activeDetailGid;
    {
        std::scoped_lock lock(detailMutex_);
        activeDetailGid = detailGid_;
    }
    if (!activeDetailGid.empty()) {
        FetchAndPublishDetail(activeDetailGid);
    }
}

}  // namespace motrix::app
