module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

template <typename SrcT>
void ApplyCommonAddFields(EngineWorkCommand& work, const SrcT& src, AppStateSnapshot& state) {
    work.outName = src.outName;
    work.dir = src.dir;
    work.split = src.split;
    work.userAgent = src.userAgent;
    work.authorization = src.authorization;
    work.referer = src.referer;
    work.cookie = src.cookie;
    work.allProxy = src.allProxy;
    state.activeModal = ActiveModal::None;
    if (src.newTaskShowDownloading.value_or(state.prefs.newTaskShowDownloading)) {
        state.activeCategory = motrix::domain::TaskCategory::Downloading;
        state.activeSection = 0;
    }
}

[[nodiscard]] std::vector<std::string> NormalizeSubmittedUris(std::span<const std::string> rawUris) {
    std::vector<std::string> out;
    out.reserve(rawUris.size());
    for (const auto& u : rawUris) {
        auto decoded = motrix::domain::DecodeThunderUri(u);
        if (decoded && !decoded->empty()) {
            out.push_back(std::move(*decoded));
        } else if (!u.empty()) {
            out.push_back(u);
        }
    }
    return out;
}

void ClearTaskDetailFields(AppStateSnapshot& state, bool clearTask) {
    if (clearTask) {
        state.detailedTask.reset();
    }
    state.detailedTrackers.clear();
    state.detailedPeers.clear();
}

[[nodiscard]] std::optional<EngineWorkCommand>
ApplySavePrefsEngine(EngineRuntime& runtime,
                     const AppStateSnapshot& state,
                     const std::filesystem::path& engineDataDir,
                     bool restartEngine) {
    const auto effSys =
        motrix::domain::BuildEffectiveSystemConfig(state.sysConfig, state.prefs, state.speedLimitPaused);
    runtime.UpdateUpnpPreference(state.prefs.enableUpnp, effSys.listenPort, effSys.dhtListenPort);
    runtime.Reconfigure(
        EngineRuntimeConfig{
            .appDataDir = engineDataDir,
            .enginePathOverride = state.prefs.engineBinPath,
            .sysConfig = effSys,
            .logLevel = state.prefs.logLevel,
            .enableUpnp = state.prefs.enableUpnp,
            .resumeAllWhenLaunched = state.prefs.resumeAllWhenAppLaunched,
        },
        restartEngine);
    if (restartEngine) {
        return std::nullopt;
    }
    return EngineWorkCommand{
        .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
        .options = motrix::domain::SystemConfigToOptionPairs(effSys),
    };
}

[[nodiscard]] std::filesystem::path ResolveEngineDataDir(bool persistConfig,
                                                         const std::filesystem::path& appDataDir) {
    return (!persistConfig || appDataDir.empty())
               ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
               : appDataDir;
}

void ApplyFactoryResetDefaults(AppStateSnapshot& state, std::string secret) {
    const std::string savedDir = state.sysConfig.dir;
    state.prefs = motrix::domain::DefaultUserPreferences();
    state.sysConfig = motrix::domain::DefaultSystemConfig();
    state.speedLimitPaused = false;
    if (!savedDir.empty()) {
        state.sysConfig.dir = savedDir;
    }
    state.sysConfig.rpcSecret = std::move(secret);
}

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

[[nodiscard]] std::vector<EngineWorkCommand>
BuildLiveTaskWorks(AppStateSnapshot& state, const Command& cmd, bool& clearDetail) {
    std::vector<EngineWorkCommand> works;
    auto enqueueRemoveOne = [&](std::string_view gid, bool deleteFiles) {
        works.push_back(BuildRemoveCommand(state, FindTaskByGid(state, gid), gid, deleteFiles));
        std::erase(state.selectedTaskGids, gid);
        if (state.detailedTaskGid == gid) {
            state.detailedTaskGid.clear();
            ClearTaskDetailFields(state, true);
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
        if (const auto* t = FindTaskByGid(state, restartCmd->gid); t && !t->uri.empty()) {
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
        ClearTaskDetailFields(state, true);
        clearDetail = true;
        works.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::ResetSession});
    }
    return works;
}

}  // namespace

struct AppCoordinator::DispatchOutcome {
    bool saveUserCfg = false;
    bool saveSystemCfg = false;
    bool prefsUpdated = false;
    bool visualsUpdated = false;
    bool clearDetailGid = false;
    std::vector<EngineWorkCommand> engineWorks;
};

void AppCoordinator::Dispatch(Command cmd) {
    if (TryHandleImmediateCommand(cmd)) {
        return;
    }

    DispatchOutcome outcome;
    PublishMutated([&](AppStateSnapshot& state) { ApplyStateCommand(state, cmd, outcome); });

    if (outcome.saveUserCfg || outcome.saveSystemCfg) {
        PersistConfigs(*Snapshot(), outcome.saveUserCfg, outcome.saveSystemCfg, true);
    }
    if (outcome.prefsUpdated || outcome.visualsUpdated) {
        const auto snap = Snapshot();
        if (outcome.prefsUpdated) {
            ApplyDesktopIntegration(*snap);
        } else if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            const bool isDark = snap->prefs.theme == motrix::domain::ThemePreference::Dark
                                || (snap->prefs.theme == motrix::domain::ThemePreference::Auto
                                    && host->IsSystemDarkMode());
            host->SetDarkFrame(isDark);
        }
        UpdateLivePlatformStatus(*snap);
    }
    if (outcome.clearDetailGid || !outcome.engineWorks.empty()) {
        std::scoped_lock lock(engineLifecycleMu_);
        if (engineRuntime_) {
            if (outcome.clearDetailGid) {
                engineRuntime_->SetDetailGid({});
            }
            for (auto& work : outcome.engineWorks) {
                engineRuntime_->Enqueue(std::move(work));
            }
        }
    }
}

namespace {

void ApplyToggleSpeedLimit(AppStateSnapshot& state,
                           std::vector<EngineWorkCommand>& works,
                           bool previewMockMode) {
    if (state.sysConfig.maxOverallDownloadLimit == 0 && state.sysConfig.maxOverallUploadLimit == 0) {
        return;
    }
    state.speedLimitPaused = !state.speedLimitPaused;
    const auto effSys =
        motrix::domain::BuildEffectiveSystemConfig(state.sysConfig, state.prefs, state.speedLimitPaused);
    if (!previewMockMode) {
        works.push_back(EngineWorkCommand{
            .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
            .options = motrix::domain::SystemConfigToOptionPairs(effSys),
        });
    }
}

}  // namespace

void AppCoordinator::ApplyStateCommand(AppStateSnapshot& state,
                                       const Command& cmd,
                                       DispatchOutcome& outcome) {
    if (const auto* c = std::get_if<SelectCategoryCmd>(&cmd)) {
        state.activeSection = 0;
        state.activeCategory = c->category;
        state.selectedTaskGid.clear();
        state.selectedTaskGids.clear();
        return;
    }
    if (std::holds_alternative<SelectAllTasksCmd>(cmd)) {
        ApplySelectionCommand(state, cmd);
        return;
    }
    if (std::holds_alternative<ToggleSpeedLimitCmd>(cmd)) {
        ApplyToggleSpeedLimit(state, outcome.engineWorks, previewMockMode_);
        return;
    }
    if (const auto* c = std::get_if<SelectSectionCmd>(&cmd)) {
        state.activeSection = c->sectionIndex;
        return;
    }
    if (const auto* c = std::get_if<OpenModalCmd>(&cmd)) {
        state.activeModal = c->modal;
        return;
    }
    if (std::holds_alternative<CloseModalCmd>(cmd)) {
        state.activeModal = ActiveModal::None;
        return;
    }
    if (const auto* c = std::get_if<SetDetailTabCmd>(&cmd)) {
        state.activeDetailTab = c->tabIndex;
        return;
    }
    if (const auto* c = std::get_if<SetThemeCmd>(&cmd)) {
        state.prefs.theme = c->theme;
        outcome.saveUserCfg = persistConfig_;
        outcome.visualsUpdated = true;
        return;
    }
    if (std::holds_alternative<ToggleThemeCmd>(cmd)) {
        state.prefs.theme = (state.prefs.theme == motrix::domain::ThemePreference::Dark)
                                ? motrix::domain::ThemePreference::Light
                                : motrix::domain::ThemePreference::Dark;
        outcome.saveUserCfg = persistConfig_;
        outcome.visualsUpdated = true;
        return;
    }
    if (const auto* c = std::get_if<SetLocaleCmd>(&cmd)) {
        state.prefs.locale = c->locale;
        outcome.saveUserCfg = persistConfig_;
        outcome.visualsUpdated = true;
        return;
    }
    if (ApplySystemAndFileCommand(state, cmd, outcome)) {
        return;
    }
    bool hasRuntime = false;
    {
        std::scoped_lock lk(engineLifecycleMu_);
        hasRuntime = (engineRuntime_ != nullptr);
    }
    if (previewMockMode_ || !hasRuntime) {
        HandleTaskAction(state, cmd);
        return;
    }
    if (ApplyLiveSubmissionCommand(state, cmd, outcome)) {
        return;
    }
    auto taskWorks = BuildLiveTaskWorks(state, cmd, outcome.clearDetailGid);
    if (std::holds_alternative<RemoveTaskCmd>(cmd) || std::holds_alternative<RemoveTasksCmd>(cmd)) {
        PruneSelectionGids(state);
    }
    for (auto& w : taskWorks) {
        outcome.engineWorks.push_back(std::move(w));
    }
}

bool AppCoordinator::ApplySystemAndFileCommand(AppStateSnapshot& state,
                                               const Command& cmd,
                                               DispatchOutcome& outcome) {
    if (const auto* c = std::get_if<SelectTaskCmd>(&cmd)) {
        ApplySelectionCommand(state, cmd);
        if (!c->openDrawer) {
            return true;
        }
        state.detailedTaskGid = (state.detailedTaskGid == c->gid) ? "" : c->gid;
        std::scoped_lock lk(engineLifecycleMu_);
        if (state.detailedTaskGid.empty()) {
            ClearTaskDetailFields(state, true);
            if (engineRuntime_) {
                engineRuntime_->SetDetailGid({});
            }
        } else if (previewMockMode_ || !engineRuntime_) {
            RefreshMockDetail(state);
        } else {
            if (const auto* existing = FindTaskByGid(state, state.detailedTaskGid)) {
                state.detailedTask = *existing;
            }
            ClearTaskDetailFields(state, false);
            engineRuntime_->SetDetailGid(state.detailedTaskGid);
            outcome.engineWorks.push_back(EngineWorkCommand{.kind = EngineWorkCommand::Kind::FetchDetail,
                                                            .gid = state.detailedTaskGid});
        }
        return true;
    }
    if (ApplyDesktopActionCommand(state, cmd)) {
        return true;
    }
    bool restartEngine = false;
    bool prefsCommitted = false;
    if (const auto* savePrefsCmd = std::get_if<SavePreferencesCmd>(&cmd)) {
        state.prefs = savePrefsCmd->prefs;
        state.sysConfig = savePrefsCmd->sysConfig;
        state.speedLimitPaused = false;
        state.activeModal = ActiveModal::None;
        restartEngine = savePrefsCmd->restartEngineIfNeeded;
        prefsCommitted = true;
    } else if (std::holds_alternative<FactoryResetCmd>(cmd)) {
        ApplyFactoryResetDefaults(state, GenerateHexSecret());
        restartEngine = true;
        prefsCommitted = true;
    }
    if (prefsCommitted) {
        logger_.SetMinLevel(motrix::core::ParseLogLevel(state.prefs.logLevel));
        outcome.saveUserCfg = persistConfig_;
        outcome.saveSystemCfg = persistConfig_;
        outcome.prefsUpdated = true;
        std::scoped_lock lk(engineLifecycleMu_);
        if (!previewMockMode_ && engineRuntime_) {
            const auto dir = ResolveEngineDataDir(persistConfig_, appDataDir_);
            if (auto w = ApplySavePrefsEngine(*engineRuntime_, state, dir, restartEngine)) {
                outcome.engineWorks.push_back(std::move(*w));
            }
        }
        return true;
    }
    return false;
}

bool AppCoordinator::ApplyLiveSubmissionCommand(AppStateSnapshot& state,
                                                const Command& cmd,
                                                DispatchOutcome& outcome) {
    if (const auto* addCmd = std::get_if<SubmitAddUriCmd>(&cmd)) {
        auto work = EngineWorkCommand{
            .kind = EngineWorkCommand::Kind::AddUri,
            .uris = NormalizeSubmittedUris(addCmd->uris),
        };
        RecordHistoryDirectory(state.prefs, addCmd->dir);
        outcome.saveUserCfg = persistConfig_;
        ApplyCommonAddFields(work, *addCmd, state);
        outcome.engineWorks.push_back(std::move(work));
        return true;
    }
    if (const auto* addTorCmd = std::get_if<SubmitAddTorrentCmd>(&cmd)) {
        auto work = EngineWorkCommand{.kind = EngineWorkCommand::Kind::AddTorrent,
                                      .torrentBase64 = addTorCmd->torrentBase64,
                                      .selectedIndices = addTorCmd->selectedIndices,
                                      .totalFiles = addTorCmd->totalFiles};
        RecordHistoryDirectory(state.prefs, addTorCmd->dir);
        outcome.saveUserCfg = persistConfig_;
        ApplyCommonAddFields(work, *addTorCmd, state);
        outcome.engineWorks.push_back(std::move(work));
        return true;
    }
    if (const auto* fileSelCmd = std::get_if<ApplyTaskFileSelectionCmd>(&cmd)) {
        outcome.engineWorks.push_back(
            EngineWorkCommand{.kind = EngineWorkCommand::Kind::ChangeTaskOption,
                              .gid = fileSelCmd->gid,
                              .selectedIndices = fileSelCmd->selectedIndices});
        return true;
    }
    return false;
}

bool AppCoordinator::TryHandleImmediateCommand(const Command& cmd) {
    if (const auto* c = std::get_if<StartAria2NextUpdateCmd>(&cmd)) {
        StartAria2NextUpdateAsync(c->force);
        return true;
    }
    if (const auto* c = std::get_if<SyncTrackersCmd>(&cmd)) {
        const auto snap = Snapshot();
        const auto sources = c->sources.empty() ? snap->prefs.trackerSource : c->sources;
        const std::string proxy = snap->prefs.useProxy ? snap->sysConfig.allProxy : std::string{};
        StartTrackerSyncAsync(sources, proxy);
        return true;
    }
    if (const auto* c = std::get_if<HandleStartupPayloadCmd>(&cmd)) {
        HandleStartupPayload(*c);
        return true;
    }
    if (const auto* c = std::get_if<HandleTrayActionCmd>(&cmd)) {
        HandleTrayAction(c->action);
        return true;
    }
    if (std::holds_alternative<HideToTrayCmd>(cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->HideToTray();
        }
        return true;
    }
    if (std::holds_alternative<ShowFromTrayCmd>(cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->ShowFromTray();
        }
        return true;
    }
    if (std::holds_alternative<QuitAppCmd>(cmd)) {
        if (persistConfig_) {
            PersistConfigs(*Snapshot(), true, true, true);
        }
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->RequestQuit();
        }
        return true;
    }
    if (const auto* c = std::get_if<UpdateWindowBoundsCmd>(&cmd)) {
        PublishMutated([&](AppStateSnapshot& s) {
            s.prefs.windowWidth = c->bounds.width;
            s.prefs.windowHeight = c->bounds.height;
            s.prefs.windowMaximized = c->bounds.maximized;
            if (c->bounds.hasPosition) {
                s.prefs.windowX = c->bounds.x;
                s.prefs.windowY = c->bounds.y;
            }
        });
        if (persistConfig_) {
            PersistConfigs(*Snapshot(), true, false, true);
        }
        return true;
    }
    return false;
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

bool AppCoordinator::ApplyDesktopActionCommand(AppStateSnapshot& state, const Command& cmd) {
    if (const auto* dropCmd = std::get_if<QueueDroppedFilesCmd>(&cmd)) {
        for (const auto& path : dropCmd->paths) {
            if (motrix::core::EndsWithAsciiInsensitive(path, ".torrent")) {
                state.pendingDroppedTorrentPath = path;
                ++state.dropSeq;
                state.activeModal = ActiveModal::AddTask;
                break;
            }
        }
        return true;
    }
    if (const auto* c = std::get_if<CopyTaskLinkCmd>(&cmd)) {
        if (auto* h = windowHost_.load(std::memory_order_acquire)) {
            if (const auto* t = FindTaskByGid(state, c->gid)) {
                h->CopyTextToClipboard(t->uri);
            }
        }
        return true;
    }
    if (const auto* c = std::get_if<ShowInFolderCmd>(&cmd)) {
        if (auto* h = windowHost_.load(std::memory_order_acquire)) {
            if (const auto* t = FindTaskByGid(state, c->gid)) {
                h->OpenPathInExplorer(motrix::domain::ResolveTaskFullPath(*t));
            }
        }
        return true;
    }
    return false;
}

}  // namespace motrix::app