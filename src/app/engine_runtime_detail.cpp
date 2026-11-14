module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import :engine_runtime;

namespace motrix::app {

namespace {

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
    if (!cmd.userAgent.empty()) {
        out.push_back({"user-agent", cmd.userAgent});
    }
    if (!cmd.referer.empty()) {
        out.push_back({"referer", cmd.referer});
    }
    if (!cmd.allProxy.empty()) {
        out.push_back({"all-proxy", cmd.allProxy});
    }
    if (!cmd.authorization.empty()) {
        out.push_back({"header", std::format("Authorization: {}", cmd.authorization)});
    }
    if (!cmd.cookie.empty()) {
        out.push_back({"header", std::format("Cookie: {}", cmd.cookie)});
    }
    return out;
}

void PopulatePeerProgress(const motrix::domain::DownloadTaskView& task,
                          std::vector<motrix::domain::TaskPeerView>& peers) {
    if (task.numPieces <= 0) {
        return;
    }
    for (auto& p : peers) {
        if (!p.seeder) {
            p.progress = motrix::domain::BitfieldCompletion(p.bitfieldHex, task.numPieces);
        }
    }
}

}  // namespace

void EngineRuntime::HandleApplyGlobalOptionsCmd(const EngineWorkCommand& cmd, std::string& lastErr) {
    std::vector<motrix::engine::EngineOptionPair> runtimeOpts;
    for (const auto& [k, v] : cmd.options) {
        if (k != "rpc-listen-port" && k != "rpc-secret" && k != "listen-port" && k != "dht-listen-port"
            && k != "continue" && k != "follow-torrent") {
            runtimeOpts.push_back({k, v});
        }
    }
    const auto filtered = dialect_->FilterGlobalOptions(runtimeOpts);
    if (filtered.empty()) {
        return;
    }
    if (auto res = rpc_->ChangeGlobalOption(filtered); !res) {
        lastErr = res.error().message;
        logger_.Warn("rpc", "changeGlobalOption failed: {}", lastErr);
    }
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
        case K::AddTorrent: {
            auto rawOpts = BuildAddUriOptions(cmd);
            if (!cmd.selectedIndices.empty() && cmd.selectedIndices.size() < cmd.totalFiles) {
                rawOpts.push_back(
                    {"select-file", motrix::domain::FormatSelectFileOption(cmd.selectedIndices)});
            }
            const motrix::engine::AddTorrentRequest req{
                .torrentBase64 = cmd.torrentBase64,
                .uris = {},
                .options = dialect_->FilterTaskOptions(rawOpts),
                .position = std::nullopt,
            };
            if (auto res = rpc_->AddTorrent(req); !res) {
                lastErr = res.error().message;
                logger_.Warn("rpc", "addTorrent failed: {}", lastErr);
            }
            needSync = true;
            break;
        }
        case K::ChangeTaskOption: {
            if (!cmd.gid.empty() && !cmd.selectedIndices.empty()) {
                const std::vector<motrix::engine::EngineOptionPair> opts{
                    {"select-file", motrix::domain::FormatSelectFileOption(cmd.selectedIndices)}};
                if (auto res = rpc_->ChangeTaskOption(cmd.gid, dialect_->FilterTaskOptions(opts));
                    !res) {
                    lastErr = res.error().message;
                    logger_.Warn("rpc", "changeOption failed: {}", lastErr);
                }
            }
            needSync = true;
            break;
        }
        case K::ApplyGlobalOptions:
            HandleApplyGlobalOptionsCmd(cmd, lastErr);
            needSync = true;
            break;
        case K::FetchDetail: {
            std::string activeGid;
            {
                std::scoped_lock lock(detailMutex_);
                activeGid = detailGid_;
            }
            if (!activeGid.empty()) {
                FetchAndPublishDetail(activeGid);
            }
            break;
        }
        default: HandleTaskControlCommand(cmd, needSync, lastErr); break;
    }
}

void EngineRuntime::HandleTaskControlCommand(const EngineWorkCommand& cmd,
                                             bool& needSync,
                                             std::string& lastErr) {
    using K = EngineWorkCommand::Kind;
    switch (cmd.kind) {
        case K::Pause:
            if (auto res = rpc_->Pause(cmd.gid); !res) {
                lastErr = res.error().message;
            }
            needSync = true;
            break;
        case K::Resume:
            if (auto res = rpc_->Unpause(cmd.gid); !res) {
                lastErr = res.error().message;
            }
            needSync = true;
            break;
        case K::Remove:
            if (cmd.taskInStopped) {
                (void)rpc_->RemoveDownloadResult(cmd.gid);
            } else {
                (void)rpc_->ForceRemove(cmd.gid);
                (void)rpc_->RemoveDownloadResult(cmd.gid);
            }
            needSync = true;
            break;
        case K::Restart:
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
        default: break;
    }
}

void EngineRuntime::SetDetailCallback(DetailCallback cb) {
    std::scoped_lock lock(detailMutex_);
    onDetail_ = std::move(cb);
}

void EngineRuntime::SetDetailGid(std::string gid) {
    {
        std::scoped_lock lock(detailMutex_);
        detailGid_ = std::move(gid);
    }
    Enqueue(EngineWorkCommand{.kind = EngineWorkCommand::Kind::FetchDetail});
}

void EngineRuntime::FetchAndPublishDetail(std::string_view gid) {
    if (gid.empty() || !rpc_ || !rpc_->IsConnected()) {
        return;
    }
    auto detail = rpc_->TellStatus(gid);
    if (!detail) {
        return;
    }
    std::vector<motrix::domain::TaskPeerView> peers;
    if (detail->task.taskType == motrix::domain::TaskType::BitTorrent) {
        if (auto peersRes = rpc_->GetPeers(gid); peersRes) {
            peers = std::move(*peersRes);
            PopulatePeerProgress(detail->task, peers);
        }
    }
    DetailCallback cb;
    {
        std::scoped_lock lock(detailMutex_);
        cb = onDetail_;
    }
    if (cb) {
        cb(std::move(*detail), std::move(peers));
    }
}

void EngineRuntime::UpdateUpnpPreference(bool enabled, int btPort, int dhtPort) {
    config_.enableUpnp = enabled;
    config_.sysConfig.listenPort = btPort;
    config_.sysConfig.dhtListenPort = dhtPort;
    const bool nativeUpnp = dialect_ && dialect_->SupportsNativeUpnp();
    if (!nativeUpnp) {
        StartOrRefreshUpnpAsync(enabled, btPort, dhtPort);
    } else if (upnp_) {
        StartOrRefreshUpnpAsync(false, btPort, dhtPort);
    }
}

void EngineRuntime::StartOrRefreshUpnpAsync(bool enabled, int btPort, int dhtPort) {
    if (upnpWorker_.joinable()) {
        upnpWorker_.request_stop();
        upnpWorker_.join();
    }
    if (!upnp_) {
        return;
    }
    upnpWorker_ = std::jthread([this, enabled, btPort, dhtPort](std::stop_token) {
        if (!enabled) {
            upnp_->UnmapAll();
            return;
        }
        auto res = upnp_->EnsureMapped(btPort, dhtPort);
        if (!res) {
            logger_.Info("upnp", "UPnP port mapping skipped/unavailable: {}", res.error().message);
        }
    });
}

}  // namespace motrix::app
