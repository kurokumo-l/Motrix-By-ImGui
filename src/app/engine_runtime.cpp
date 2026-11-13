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

[[nodiscard]] std::vector<motrix::engine::EngineOptionPair>
BuildAddUriOptions(const EngineWorkCommand& cmd) {
    std::vector<motrix::engine::EngineOptionPair> out;
    if (!cmd.dir.empty()) {
        out.push_back({"dir", cmd.dir});
    }
    if (!cmd.outName.empty()) {
        out.push_back({"out", cmd.outName});
    }
    if (cmd.split > 0) {
        out.push_back({"split", std::to_string(cmd.split)});
        out.push_back({"max-connection-per-server", std::to_string(std::clamp(cmd.split, 1, 64))});
    }
    return out;
}

}  // namespace

EngineRuntime::EngineRuntime(motrix::core::Logger& logger, EngineRuntimeConfig cfg, SyncCallback onSync)
    : logger_(logger), config_(std::move(cfg)), onSync_(std::move(onSync)) {
    proc_ = motrix::engine::CreateEngineProcessManager(logger_);
    rpc_ = motrix::engine::CreateWsRpcClient(logger_);
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
}

void EngineRuntime::Enqueue(EngineWorkCommand cmd) {
    {
        std::scoped_lock lock(queueMutex_);
        queue_.push_back(std::move(cmd));
    }
    queueCv_.notify_all();
}

void EngineRuntime::RunLoop(std::stop_token st) {
    onSync_(EngineConnectionState::Starting, nullptr, nullptr, {}, {});
    while (!st.stop_requested()) {
        if (!TryStartAndConnect(st)) {
            if (st.stop_requested()) {
                break;
            }
            std::unique_lock lock(queueMutex_);
            queueCv_.wait_for(lock, st, std::chrono::seconds(3), [&] { return st.stop_requested(); });
            continue;
        }

        disconnectedFlag_.store(false, std::memory_order_release);
        wakeSync_.store(true, std::memory_order_release);

        while (!st.stop_requested()) {
            if (disconnectedFlag_.load(std::memory_order_acquire)
                || proc_->State() == motrix::engine::EngineProcessState::Crashed
                || !rpc_->IsConnected()) {
                onSync_(EngineConnectionState::Reconnecting,
                        &versionInfo_,
                        nullptr,
                        resolvedPath_,
                        "Engine connection lost, reconnecting...");
                rpc_->Disconnect();
                (void)proc_->Stop(std::chrono::milliseconds(1000));
                break;
            }

            std::deque<EngineWorkCommand> pending;
            {
                std::unique_lock lock(queueMutex_);
                const auto waitDur =
                    hasActiveTasks_ ? std::chrono::milliseconds(500) : std::chrono::seconds(5);
                if (queue_.empty() && !wakeSync_.load(std::memory_order_acquire)) {
                    queueCv_.wait_for(lock, st, waitDur, [&] {
                        return st.stop_requested() || !queue_.empty()
                               || wakeSync_.load(std::memory_order_acquire)
                               || disconnectedFlag_.load(std::memory_order_acquire);
                    });
                }
                pending.swap(queue_);
            }

            if (st.stop_requested()) {
                break;
            }

            bool needSync = wakeSync_.exchange(false, std::memory_order_acq_rel);
            std::string cmdErr;
            while (!pending.empty()) {
                EngineWorkCommand item = std::move(pending.front());
                pending.pop_front();
                ProcessCommand(item, needSync, cmdErr);
            }

            if (needSync || hasActiveTasks_) {
                auto snap = rpc_->SyncAll(100, 100);
                if (snap) {
                    ApplySyncSnapshot(*snap, cmdErr);
                } else if (!st.stop_requested()) {
                    logger_.Warn("app", "SyncAll failed: {}", snap.error().message);
                    disconnectedFlag_.store(true, std::memory_order_release);
                }
            }
        }
    }

    if (rpc_->IsConnected()) {
        if (dialect_ && !dialect_->UsesStateDirInsteadOfAria2Files()) {
            (void)rpc_->SaveSession();
        }
        (void)rpc_->Shutdown(false);
        rpc_->Disconnect();
    }
    (void)proc_->Stop(std::chrono::milliseconds(3000));
}

bool EngineRuntime::TryStartAndConnect(std::stop_token st) {
    const auto binOpt = motrix::engine::DiscoverEngineExecutable(config_.enginePathOverride);
    if (!binOpt) {
        onSync_(EngineConnectionState::Offline,
                nullptr,
                nullptr,
                {},
                "Engine binary (aria2c / aria2-next) not found");
        return false;
    }

    resolvedPath_ = motrix::core::PathToUtf8(*binOpt);
    auto probe = motrix::engine::ProbeEngineBinaryVersion(*binOpt);
    motrix::engine::EngineKind kind = probe ? probe->kind : motrix::engine::EngineKind::ClassicAria2;
    if (probe) {
        versionInfo_ = *probe;
    }

    dialect_ = motrix::engine::CreateDialect(kind);
    std::error_code ec;
    std::filesystem::create_directories(config_.appDataDir, ec);

    motrix::engine::EngineLaunchOptions launch{};
    launch.rpcPort = config_.sysConfig.rpcListenPort;
    launch.rpcSecret = config_.sysConfig.rpcSecret;
    launch.hostPid = motrix::engine::CurrentHostProcessId();
    launch.saveIntervalSeconds = 10;
    if (dialect_->UsesStateDirInsteadOfAria2Files()) {
        launch.stateOrSessionPath = config_.appDataDir / "engine_state";
        std::filesystem::create_directories(launch.stateOrSessionPath, ec);
    } else {
        launch.stateOrSessionPath = config_.appDataDir / "aria2.session";
        launch.sessionFileExists = std::filesystem::is_regular_file(launch.stateOrSessionPath, ec);
    }

    const auto globalOpts = ToEnginePairs(motrix::domain::SystemConfigToOptionPairs(config_.sysConfig));
    motrix::engine::EngineSpawnSpec spec{};
    spec.executablePath = *binOpt;
    spec.launchOptions = launch;
    spec.initialOptions = dialect_->FilterGlobalOptions(globalOpts);
    spec.hintKind = kind;

    if (auto startRes = proc_->Start(spec); !startRes) {
        onSync_(EngineConnectionState::Offline,
                &versionInfo_,
                nullptr,
                resolvedPath_,
                startRes.error().message);
        return false;
    }

    const motrix::engine::RpcConnectionOptions rpcOpts{
        .host = "127.0.0.1",
        .port = config_.sysConfig.rpcListenPort,
        .secret = config_.sysConfig.rpcSecret,
    };

    for (int attempt = 0; attempt < 25 && !st.stop_requested(); ++attempt) {
        if (proc_->State() == motrix::engine::EngineProcessState::Crashed) {
            onSync_(EngineConnectionState::Offline,
                    &versionInfo_,
                    nullptr,
                    resolvedPath_,
                    "Engine exited immediately on startup (check RPC port conflict)");
            return false;
        }
        if (rpc_->Connect(rpcOpts)) {
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
            if (auto initialSync = rpc_->SyncAll(100, 100); initialSync) {
                ApplySyncSnapshot(*initialSync, {});
            } else {
                onSync_(EngineConnectionState::Connected, &versionInfo_, nullptr, resolvedPath_, {});
            }
            return true;
        }
        std::unique_lock lock(queueMutex_);
        queueCv_.wait_for(lock, st, std::chrono::milliseconds(150), [&] { return st.stop_requested(); });
    }

    rpc_->Disconnect();
    (void)proc_->Stop(std::chrono::milliseconds(1000));
    onSync_(EngineConnectionState::Offline,
            &versionInfo_,
            nullptr,
            resolvedPath_,
            "Timed out connecting to aria2 WebSocket RPC");
    return false;
}

void EngineRuntime::ProcessCommand(const EngineWorkCommand& cmd, bool& needSync, std::string& lastErr) {
    using K = EngineWorkCommand::Kind;
    switch (cmd.kind) {
        case K::Sync: needSync = true; break;
        case K::AddUri: {
            const auto filtered = dialect_->FilterTaskOptions(BuildAddUriOptions(cmd));
            for (const auto& uri : cmd.uris) {
                const motrix::engine::AddUriRequest req{
                    .uris = {uri},
                    .options = filtered,
                    .position = std::nullopt,
                };
                if (auto res = rpc_->AddUri(req); !res) {
                    lastErr = res.error().message;
                    logger_.Warn("rpc", "addUri failed: {}", lastErr);
                }
            }
            needSync = true;
            break;
        }
        case K::Pause: {
            if (auto res = rpc_->Pause(cmd.gid); !res) {
                lastErr = res.error().message;
            }
            needSync = true;
            break;
        }
        case K::Resume: {
            if (auto res = rpc_->Unpause(cmd.gid); !res) {
                lastErr = res.error().message;
            }
            needSync = true;
            break;
        }
        case K::Remove: {
            if (cmd.taskInStopped) {
                (void)rpc_->RemoveDownloadResult(cmd.gid);
            } else {
                (void)rpc_->ForceRemove(cmd.gid);
                (void)rpc_->RemoveDownloadResult(cmd.gid);
            }
            needSync = true;
            break;
        }
        case K::Restart: {
            if (!cmd.uris.empty()) {
                const motrix::engine::AddUriRequest req{
                    .uris = cmd.uris,
                    .options = dialect_->FilterTaskOptions(BuildAddUriOptions(cmd)),
                    .position = std::nullopt,
                };
                if (auto res = rpc_->AddUri(req); res) {
                    (void)rpc_->RemoveDownloadResult(cmd.gid);
                } else {
                    lastErr = res.error().message;
                }
            }
            needSync = true;
            break;
        }
        case K::PauseAll:
            (void)rpc_->PauseAll();
            needSync = true;
            break;
        case K::ResumeAll:
            (void)rpc_->UnpauseAll();
            needSync = true;
            break;
        case K::PurgeStopped:
            (void)rpc_->PurgeDownloadResult();
            needSync = true;
            break;
    }
}

void EngineRuntime::ApplySyncSnapshot(const motrix::engine::EngineSyncSnapshot& snap,
                                      std::string_view err) {
    hasActiveTasks_ = !snap.active.empty() || snap.globalStat.numActive > 0;
    onSync_(EngineConnectionState::Connected, &versionInfo_, &snap, resolvedPath_, err);
}

}  // namespace motrix::app
