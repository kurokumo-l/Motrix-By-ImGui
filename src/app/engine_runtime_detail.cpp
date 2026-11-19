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
    if (!cmd.outName.empty() && cmd.uris.size() <= 1) {
        out.push_back({"out", cmd.outName});
    }
    if (cmd.split > 0) {
        const int clampedSplit = std::clamp(cmd.split, 1, 128);
        out.push_back({"split", std::to_string(clampedSplit)});
        out.push_back({"max-connection-per-server", std::to_string(clampedSplit)});
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

void RemoveFileWithRetry(const std::filesystem::path& target) {
    std::error_code ec;
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (std::filesystem::remove(target, ec) || !std::filesystem::exists(target, ec)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

[[nodiscard]] bool IsSameDirectoryPath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec) && !ec) {
        return true;
    }
    const auto stripTrail = [](const std::filesystem::path& p) {
        std::string utf8 = motrix::core::AsciiLower(motrix::core::PathToUtf8(p.lexically_normal()));
        while (utf8.size() > 3u && (utf8.back() == '\\' || utf8.back() == '/')) {
            utf8.pop_back();
        }
        return utf8;
    };
    return stripTrail(a) == stripTrail(b);
}

[[nodiscard]] bool IsStrictDescendantDir(std::filesystem::path candidate,
                                         const std::filesystem::path& root) {
    if (candidate.empty() || root.empty() || IsSameDirectoryPath(candidate, root)) {
        return false;
    }
    for (int depth = 0; depth < 32; ++depth) {
        const std::filesystem::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            return false;
        }
        if (IsSameDirectoryPath(parent, root)) {
            return true;
        }
        candidate = parent;
    }
    return false;
}

void PruneEmptyTaskSubdirs(std::filesystem::path dir, const std::filesystem::path& stopRoot) {
    if (dir.empty() || stopRoot.empty() || !IsStrictDescendantDir(dir, stopRoot)) {
        return;
    }
    std::error_code ec;
    for (int depth = 0; depth < 16 && !dir.empty(); ++depth) {
        if (IsSameDirectoryPath(dir, stopRoot) || !std::filesystem::is_directory(dir, ec)
            || !std::filesystem::is_empty(dir, ec)) {
            break;
        }
        if (!std::filesystem::remove(dir, ec) || ec) {
            break;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) {
            break;
        }
        dir = parent;
    }
}

void DeleteRemovedTaskFiles(const std::vector<std::string>& paths, std::string_view taskDirUtf8) {
    const auto baseDir =
        taskDirUtf8.empty() ? std::filesystem::path{} : motrix::core::PathFromUtf8(taskDirUtf8);
    for (const auto& utf8Path : paths) {
        if (utf8Path.empty()) {
            continue;
        }
        std::filesystem::path p = motrix::core::PathFromUtf8(utf8Path);
        if (p.is_relative() && !baseDir.empty()) {
            p = baseDir / p;
        }
        RemoveFileWithRetry(p);
        RemoveFileWithRetry(motrix::core::PathFromUtf8(motrix::core::PathToUtf8(p) + ".aria2"));
        PruneEmptyTaskSubdirs(p.parent_path(), baseDir);
    }
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

void RemoveAndClearEngineTask(motrix::engine::IEngineRpcClient& rpc,
                              std::string_view gid,
                              bool alreadyStopped);

void ExecuteRestartTaskCommand(motrix::engine::IEngineRpcClient& rpc,
                               const motrix::engine::IEngineDialect& dialect,
                               const EngineWorkCommand& cmd,
                               std::string& lastErr) {
    if (cmd.uris.empty()) {
        return;
    }
    const motrix::engine::AddUriRequest req{
        .uris = cmd.uris,
        .options = dialect.FilterTaskOptions(BuildAddUriOptions(cmd)),
        .position = std::nullopt,
    };
    if (auto res = rpc.AddUri(req); res) {
        RemoveAndClearEngineTask(rpc, cmd.gid, cmd.taskInStopped);
    } else {
        lastErr = res.error().message;
    }
}

struct RemovalTargets {
    std::vector<std::string> files;
    std::string dir;
    std::string btName;
    std::string infoHash;
};

[[nodiscard]] RemovalTargets CollectRemovalTargets(motrix::engine::IEngineRpcClient& rpc,
                                                   const EngineWorkCommand& cmd) {
    RemovalTargets out{.files = cmd.filesToDelete, .dir = cmd.dir};
    if (!cmd.deleteFiles) {
        return out;
    }
    auto detail = rpc.TellStatus(cmd.gid);
    if (!detail) {
        return out;
    }
    std::vector<std::string> fullFiles;
    fullFiles.reserve(detail->task.files.size());
    for (const auto& f : detail->task.files) {
        if (!f.path.empty()) {
            fullFiles.push_back(f.path);
        }
    }
    if (!fullFiles.empty()) {
        out.files = std::move(fullFiles);
    }
    if (out.dir.empty()) {
        out.dir = detail->task.dir;
    }
    if (detail->task.btInfo) {
        out.btName = detail->task.btInfo->name;
        out.infoHash = detail->task.btInfo->infoHash;
    }
    return out;
}

void DeleteBtSidecarFiles(const std::filesystem::path& appDataDir, const RemovalTargets& targets) {
    const auto baseDir =
        targets.dir.empty() ? std::filesystem::path{} : motrix::core::PathFromUtf8(targets.dir);
    if (!targets.btName.empty() && !baseDir.empty()) {
        RemoveFileWithRetry(baseDir / motrix::core::PathFromUtf8(targets.btName + ".aria2"));
    }
    if (targets.infoHash.empty()) {
        return;
    }
    const auto torrentRel = motrix::core::PathFromUtf8(targets.infoHash + ".torrent");
    if (!baseDir.empty()) {
        RemoveFileWithRetry(baseDir / torrentRel);
    }
    if (!appDataDir.empty()) {
        RemoveFileWithRetry(appDataDir / torrentRel);
    }
}

void RemoveAndClearEngineTask(motrix::engine::IEngineRpcClient& rpc,
                              std::string_view gid,
                              bool alreadyStopped) {
    if (alreadyStopped) {
        (void)rpc.RemoveDownloadResult(gid);
        return;
    }
    (void)rpc.ForceRemove(gid);
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (rpc.RemoveDownloadResult(gid)) {
            return;
        }
        if (const auto st = rpc.TellStatus(gid); !st) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

}  // namespace

void EngineRuntime::HandleApplyGlobalOptionsCmd(const EngineWorkCommand& cmd, std::string& lastErr) {
    std::vector<motrix::engine::EngineOptionPair> allOpts;
    allOpts.reserve(cmd.options.size() + 1);
    std::vector<motrix::engine::EngineOptionPair> runtimeOpts;
    runtimeOpts.reserve(cmd.options.size() + 1);
    for (const auto& [k, v] : cmd.options) {
        allOpts.push_back({k, v});
        if (k != "rpc-listen-port" && k != "rpc-secret" && k != "listen-port" && k != "dht-listen-port"
            && k != "continue" && k != "follow-torrent" && k != "disable-ipv6" && k != "log-level") {
            runtimeOpts.push_back({k, v});
        }
    }
    allOpts.push_back({"bt-port-mapping", config_.enableUpnp ? "true" : "false"});
    runtimeOpts.push_back({"bt-port-mapping", config_.enableUpnp ? "true" : "false"});
    constexpr std::array<std::string_view, 4> kClearableRuntimeKeys{
        "bt-tracker", "all-proxy", "no-proxy", "user-agent"};
    for (const auto key : kClearableRuntimeKeys) {
        const bool present =
            std::ranges::any_of(runtimeOpts, [&](const auto& opt) { return opt.key == key; });
        if (!present) {
            runtimeOpts.push_back({std::string(key), ""});
        }
    }
    const auto filtered = dialect_->FilterGlobalOptions(runtimeOpts);
    if (!filtered.empty()) {
        if (auto res = rpc_->ChangeGlobalOption(filtered); !res) {
            lastErr = res.error().message;
            logger_.Warn("rpc", "changeGlobalOption failed: {}", lastErr);
        }
    }
    if (!config_.appDataDir.empty() && !cmd.options.empty()) {
        const auto confOpts = dialect_->FilterGlobalOptions(allOpts);
        (void)motrix::core::WriteTextAtomic(config_.appDataDir / "aria2.conf",
                                            motrix::engine::FormatAria2Conf(confOpts));
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
            ExecuteRemoveCommand(cmd);
            needSync = true;
            break;
        case K::Restart:
            ExecuteRestartTaskCommand(*rpc_, *dialect_, cmd, lastErr);
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
        case K::UpdateUpnp: {
            config_.enableUpnp = cmd.enableUpnp;
            config_.sysConfig.listenPort = cmd.btPort;
            config_.sysConfig.dhtListenPort = cmd.dhtPort;
            const bool nativeUpnp = dialect_ && dialect_->SupportsNativeUpnp();
            StartOrRefreshUpnpAsync(nativeUpnp ? false : cmd.enableUpnp, cmd.btPort, cmd.dhtPort);
            break;
        }
        case K::ResetSession: ExecuteResetSessionCommand(); break;
        default: break;
    }
}

void EngineRuntime::ExecuteRemoveCommand(const EngineWorkCommand& cmd) {
    const RemovalTargets targets = CollectRemovalTargets(*rpc_, cmd);
    RemoveAndClearEngineTask(*rpc_, cmd.gid, cmd.taskInStopped);
    if (!cmd.deleteFiles) {
        return;
    }
    DeleteRemovedTaskFiles(targets.files, targets.dir);
    DeleteBtSidecarFiles(config_.appDataDir, targets);
}

void EngineRuntime::ExecuteResetSessionCommand() {
    if (rpc_->IsConnected()) {
        (void)rpc_->Shutdown(true);
        rpc_->Disconnect();
    }
    (void)proc_->Stop(std::chrono::milliseconds(3000));
    if (!config_.appDataDir.empty()) {
        std::error_code ec;
        std::filesystem::remove(config_.appDataDir / "aria2.session", ec);
        std::filesystem::remove_all(config_.appDataDir / "engine_state", ec);
    }
    resetSessionRequested_ = true;
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
    Enqueue(EngineWorkCommand{
        .kind = EngineWorkCommand::Kind::UpdateUpnp,
        .enableUpnp = enabled,
        .btPort = btPort,
        .dhtPort = dhtPort,
    });
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
