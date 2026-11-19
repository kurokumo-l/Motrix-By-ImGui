module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

[[nodiscard]] const std::vector<motrix::domain::DownloadTaskView>&
ActiveTaskListRef(const AppStateSnapshot& state) noexcept {
    switch (state.activeCategory) {
        case motrix::domain::TaskCategory::Downloading: return state.downloadingTasks;
        case motrix::domain::TaskCategory::Waiting: return state.waitingTasks;
        case motrix::domain::TaskCategory::Stopped: return state.stoppedTasks;
    }
    return state.downloadingTasks;
}

[[nodiscard]] bool ContainsTaskGid(const std::vector<motrix::domain::DownloadTaskView>& list,
                                   std::string_view gid) noexcept {
    return std::any_of(list.begin(), list.end(), [&](const auto& t) { return t.gid == gid; });
}

[[nodiscard]] std::optional<std::size_t>
FindGidIndex(const std::vector<motrix::domain::DownloadTaskView>& list, std::string_view gid) noexcept {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].gid == gid) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string ExtractOutName(const motrix::domain::DownloadTaskView& t) {
    if (t.taskType == motrix::domain::TaskType::BitTorrent || t.files.empty()
        || t.files[0].path.empty()) {
        return {};
    }
    const std::string_view p = t.files[0].path;
    const std::size_t slash = p.find_last_of("/\\");
    return std::string((slash == std::string_view::npos) ? p : p.substr(slash + 1));
}

[[nodiscard]] std::vector<std::string> CollectFiles(const motrix::domain::DownloadTaskView& t) {
    std::vector<std::string> out;
    out.reserve(t.files.size());
    for (const auto& f : t.files) {
        if (!f.path.empty()) {
            out.push_back(f.path);
        }
    }
    return out;
}

[[nodiscard]] EngineUpdateStatus MapPhaseToStatus(motrix::engine::Aria2NextUpdatePhase phase) noexcept {
    switch (phase) {
        case motrix::engine::Aria2NextUpdatePhase::Checking: return EngineUpdateStatus::Checking;
        case motrix::engine::Aria2NextUpdatePhase::Downloading: return EngineUpdateStatus::Downloading;
        case motrix::engine::Aria2NextUpdatePhase::Verifying: return EngineUpdateStatus::Verifying;
    }
    return EngineUpdateStatus::Checking;
}

[[nodiscard]] bool IsUpdateInProgress(EngineUpdateStatus st) noexcept {
    return st == EngineUpdateStatus::Checking || st == EngineUpdateStatus::Downloading
           || st == EngineUpdateStatus::Verifying || st == EngineUpdateStatus::Restarting;
}

[[nodiscard]] const motrix::domain::DownloadTaskView* FindTaskInSnapshot(const AppStateSnapshot& state,
                                                                         std::string_view gid) noexcept {
    for (const auto* list : {&state.downloadingTasks, &state.waitingTasks, &state.stoppedTasks}) {
        for (const auto& t : *list) {
            if (t.gid == gid) {
                return &t;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] EngineWorkCommand BuildRemoveCommand(const AppStateSnapshot& state,
                                                   const motrix::domain::DownloadTaskView* task,
                                                   std::string_view gid,
                                                   bool deleteFiles) {
    const bool allowDeleteFiles =
        deleteFiles && (!task || task->status != motrix::domain::TaskStatus::Complete);
    std::vector<std::string> toDelete;
    std::string taskDir;
    if (task) {
        taskDir = task->dir;
        if (allowDeleteFiles) {
            toDelete = CollectFiles(*task);
        }
    }
    return EngineWorkCommand{
        .kind = EngineWorkCommand::Kind::Remove,
        .gid = std::string(gid),
        .dir = std::move(taskDir),
        .taskInStopped = ContainsTaskGid(state.stoppedTasks, gid),
        .deleteFiles = allowDeleteFiles,
        .filesToDelete = std::move(toDelete),
    };
}

}  // namespace

void AppCoordinator::PruneSelectionGids(AppStateSnapshot& state) {
    std::erase_if(state.selectedTaskGids, [&](const std::string& g) {
        return !ContainsTaskGid(state.downloadingTasks, g) && !ContainsTaskGid(state.waitingTasks, g)
               && !ContainsTaskGid(state.stoppedTasks, g);
    });
    if (!state.selectedTaskGid.empty()
        && std::ranges::find(state.selectedTaskGids, state.selectedTaskGid)
               == state.selectedTaskGids.end()) {
        state.selectedTaskGid =
            state.selectedTaskGids.empty() ? std::string{} : state.selectedTaskGids.front();
    }
}

void AppCoordinator::ApplySelectionCommand(AppStateSnapshot& state, const Command& cmd) {
    if (std::holds_alternative<SelectAllTasksCmd>(cmd)) {
        const auto& list = ActiveTaskListRef(state);
        state.selectedTaskGids.clear();
        state.selectedTaskGids.reserve(list.size());
        for (const auto& t : list) {
            state.selectedTaskGids.push_back(t.gid);
        }
        if (!state.selectedTaskGids.empty()
            && std::ranges::find(state.selectedTaskGids, state.selectedTaskGid)
                   == state.selectedTaskGids.end()) {
            state.selectedTaskGid = state.selectedTaskGids.front();
        }
        return;
    }

    const auto* c = std::get_if<SelectTaskCmd>(&cmd);
    if (!c) {
        return;
    }
    if (c->gid.empty()) {
        state.selectedTaskGid.clear();
        state.selectedTaskGids.clear();
        return;
    }
    if (c->range && !state.selectedTaskGid.empty()) {
        const auto& list = ActiveTaskListRef(state);
        const auto a = FindGidIndex(list, state.selectedTaskGid);
        const auto b = FindGidIndex(list, c->gid);
        if (a && b) {
            const std::size_t lo = std::min(*a, *b);
            const std::size_t hi = std::max(*a, *b);
            state.selectedTaskGids.clear();
            for (std::size_t i = lo; i <= hi; ++i) {
                state.selectedTaskGids.push_back(list[i].gid);
            }
            return;
        }
    }
    if (c->toggle) {
        auto it = std::ranges::find(state.selectedTaskGids, c->gid);
        if (it != state.selectedTaskGids.end()) {
            state.selectedTaskGids.erase(it);
            state.selectedTaskGid =
                state.selectedTaskGids.empty() ? std::string{} : state.selectedTaskGids.back();
        } else {
            state.selectedTaskGids.push_back(c->gid);
            state.selectedTaskGid = c->gid;
        }
        return;
    }
    state.selectedTaskGid = c->gid;
    state.selectedTaskGids = {c->gid};
}

std::vector<EngineWorkCommand>
BuildLiveTaskWorks(AppStateSnapshot& state, const Command& cmd, bool& clearDetail) {
    std::vector<EngineWorkCommand> works;
    auto enqueueRemoveOne = [&](std::string_view gid, bool deleteFiles) {
        works.push_back(BuildRemoveCommand(state, FindTaskInSnapshot(state, gid), gid, deleteFiles));
        std::erase(state.selectedTaskGids, gid);
        if (state.detailedTaskGid == gid) {
            state.detailedTaskGid.clear();
            state.detailedTask.reset();
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
            clearDetail = true;
        }
    };

    if (const auto* pauseCmd = std::get_if<PauseTaskCmd>(&cmd)) {
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::Pause, .gid = pauseCmd->gid});
    } else if (const auto* resumeCmd = std::get_if<ResumeTaskCmd>(&cmd)) {
        works.push_back(
            EngineWorkCommand{.kind = EngineWorkCommand::Kind::Resume, .gid = resumeCmd->gid});
    } else if (const auto* removeCmd = std::get_if<RemoveTaskCmd>(&cmd)) {
        enqueueRemoveOne(removeCmd->gid, removeCmd->deleteFiles);
    } else if (const auto* batchRemoveCmd = std::get_if<RemoveTasksCmd>(&cmd)) {
        for (const auto& g : batchRemoveCmd->gids) {
            enqueueRemoveOne(g, batchRemoveCmd->deleteFiles);
        }
    } else if (const auto* restartCmd = std::get_if<RestartTaskCmd>(&cmd)) {
        if (const auto* t = FindTaskInSnapshot(state, restartCmd->gid); t && !t->uri.empty()) {
            const bool isStopped = (t->status == motrix::domain::TaskStatus::Complete
                                    || t->status == motrix::domain::TaskStatus::Error
                                    || t->status == motrix::domain::TaskStatus::Removed);
            works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::Restart,
                                              .gid = restartCmd->gid,
                                              .uris = {t->uri},
                                              .outName = ExtractOutName(*t),
                                              .dir = t->dir,
                                              .split = state.sysConfig.split,
                                              .taskInStopped = isStopped});
        }
    } else if (std::holds_alternative<RefreshTasksCmd>(cmd)) {
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::Sync});
    } else if (std::holds_alternative<PauseAllCmd>(cmd)) {
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::PauseAll});
    } else if (std::holds_alternative<ResumeAllCmd>(cmd)) {
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::ResumeAll});
    } else if (std::holds_alternative<PurgeStoppedCmd>(cmd)) {
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::PurgeStopped});
    } else if (std::holds_alternative<ResetSessionCmd>(cmd)) {
        state.selectedTaskGid.clear();
        state.selectedTaskGids.clear();
        state.detailedTaskGid.clear();
        state.detailedTask.reset();
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
        clearDetail = true;
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::ResetSession});
    }
    return works;
}

void AppCoordinator::StartAria2NextUpdateAsync(bool forceReinstall) {
    const auto initialSnap = Snapshot();
    if (IsUpdateInProgress(initialSnap->engineUpdateStatus)) {
        return;
    }
    if (engineUpdateWorker_.joinable()) {
        engineUpdateWorker_.request_stop();
        engineUpdateWorker_.join();
    }

    const std::filesystem::path dataDir =
        appDataDir_.empty() ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
                            : appDataDir_;
    const std::string srcUrl = initialSnap->prefs.aria2NextUpdateUrl.empty()
                                   ? std::string(motrix::domain::kDefaultAria2NextUpdateUrl)
                                   : initialSnap->prefs.aria2NextUpdateUrl;

    motrix::engine::Aria2NextUpdateSpec spec{
        .updateUrl = srcUrl,
        .mirrorPrefix = initialSnap->prefs.aria2NextMirrorPrefix,
        .proxyUrl = initialSnap->prefs.useProxy ? initialSnap->sysConfig.allProxy : std::string{},
        .targetExePath = motrix::engine::ManagedAria2NextExecutablePath(dataDir),
        .currentVersion = initialSnap->engineInfo.version,
        .currentKind = initialSnap->engineInfo.kind,
        .timeout = std::chrono::milliseconds(30000),
        .forceReinstall = forceReinstall,
    };

    PublishMutated([](AppStateSnapshot& s) {
        s.engineUpdateStatus = EngineUpdateStatus::Checking;
        s.engineUpdateMessage.clear();
        s.engineUpdateVersion.clear();
    });

    engineUpdateWorker_ = std::jthread([this, s = std::move(spec)](std::stop_token st) mutable {
        ExecuteAria2NextUpdate(std::move(st), std::move(s));
    });
}

void AppCoordinator::ExecuteAria2NextUpdate(std::stop_token st,
                                            motrix::engine::Aria2NextUpdateSpec spec) {
    auto notifyUi = [this] {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->RequestFrame();
        }
    };
    auto onPhase = [this, &st, &notifyUi](motrix::engine::Aria2NextUpdatePhase phase) {
        if (st.stop_requested()) {
            return;
        }
        PublishMutated([phase](AppStateSnapshot& s) { s.engineUpdateStatus = MapPhaseToStatus(phase); });
        notifyUi();
    };

    spec.stopToken = st;
    auto stageRes = motrix::engine::StageAria2NextUpdate(spec, onPhase);
    if (st.stop_requested()) {
        return;
    }
    if (!stageRes) {
        logger_.Warn("updater", "aria2-next update failed: {}", stageRes.error().message);
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Failed;
            s.engineUpdateMessage = stageRes.error().message;
        });
        notifyUi();
        return;
    }
    if (stageRes->alreadyUpToDate) {
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Succeeded;
            s.engineUpdateVersion = stageRes->detectedVersion.empty()
                                        ? stageRes->release.normalizedVersion
                                        : stageRes->detectedVersion;
            s.engineUpdateMessage.clear();
        });
        notifyUi();
        return;
    }

    PublishMutated([](AppStateSnapshot& s) { s.engineUpdateStatus = EngineUpdateStatus::Restarting; });
    notifyUi();
    FinishAria2NextInstall(st, spec, *stageRes);
    notifyUi();
}

void AppCoordinator::FinishAria2NextInstall(const std::stop_token& st,
                                            const motrix::engine::Aria2NextUpdateSpec& spec,
                                            const motrix::engine::Aria2NextStageOutcome& stage) {
    std::unique_ptr<EngineRuntime> stoppedRt;
    {
        std::scoped_lock lk(engineLifecycleMu_);
        hasLiveRuntime_.store(false, std::memory_order_release);
        stoppedRt = std::move(engineRuntime_);
    }
    stoppedRt.reset();

    const auto instRes = motrix::engine::InstallStagedAria2Next(stage.stagedExePath, spec.targetExePath);
    if (!instRes) {
        logger_.Warn("updater", "Failed installing aria2-next: {}", instRes.error().message);
        if (!st.stop_requested() && enableEngine_) {
            CreateAndStartEngineRuntime(*Snapshot());
        }
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Failed;
            s.engineUpdateMessage = instRes.error().message;
        });
        return;
    }

    const std::string installedUtf8 = motrix::core::PathToUtf8(spec.targetExePath);
    PublishMutated([&](AppStateSnapshot& s) {
        s.prefs.engineBinPath = installedUtf8;
        s.resolvedEnginePath = installedUtf8;
        s.engineError.clear();
        s.engineInfo.kind = stage.detectedKind;
        s.engineInfo.version = stage.detectedVersion;
        s.engineInfo.product = "aria2-next";
        s.engineUpdateStatus = EngineUpdateStatus::Succeeded;
        s.engineUpdateVersion = stage.detectedVersion;
        s.engineUpdateMessage = std::format("v{}", stage.detectedVersion);
    });
    if (persistConfig_) {
        PersistConfigs(*Snapshot(), true, false, true);
    }
    if (!st.stop_requested() && enableEngine_) {
        {
            std::scoped_lock lk(engineLifecycleMu_);
            enginePathOverride_ = installedUtf8;
        }
        CreateAndStartEngineRuntime(*Snapshot(), installedUtf8);
    }
}

}  // namespace motrix::app
