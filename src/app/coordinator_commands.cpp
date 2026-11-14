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

}  // namespace

struct AppCoordinator::DispatchOutcome {
    bool saveUserCfg = false;
    bool saveSystemCfg = false;
    bool prefsUpdated = false;
    std::optional<EngineWorkCommand> engineWork;
};

bool AppCoordinator::TryHandleImmediateCommand(const Command& cmd) {
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
        PersistConfigs(*Snapshot(), true, true, true);
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

void AppCoordinator::Dispatch(Command cmd) {
    if (TryHandleImmediateCommand(cmd)) {
        return;
    }

    DispatchOutcome outcome;
    PublishMutated([&](AppStateSnapshot& state) { ApplyStateCommand(state, cmd, outcome); });

    if (outcome.saveUserCfg || outcome.saveSystemCfg) {
        PersistConfigs(*Snapshot(), outcome.saveUserCfg, outcome.saveSystemCfg, true);
    }
    if (outcome.prefsUpdated) {
        const auto snap = Snapshot();
        ApplyDesktopIntegration(*snap);
        UpdateLivePlatformStatus(*snap);
    }
    if (outcome.engineWork && engineRuntime_) {
        engineRuntime_->Enqueue(std::move(*outcome.engineWork));
    }
}

void AppCoordinator::ApplyStateCommand(AppStateSnapshot& state,
                                       const Command& cmd,
                                       DispatchOutcome& outcome) {
    if (const auto* c = std::get_if<SelectCategoryCmd>(&cmd)) {
        state.activeSection = 0;
        state.activeCategory = c->category;
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
        return;
    }
    if (std::holds_alternative<ToggleThemeCmd>(cmd)) {
        state.prefs.theme = (state.prefs.theme == motrix::domain::ThemePreference::Dark)
                                ? motrix::domain::ThemePreference::Light
                                : motrix::domain::ThemePreference::Dark;
        outcome.saveUserCfg = persistConfig_;
        return;
    }
    if (const auto* c = std::get_if<SetLocaleCmd>(&cmd)) {
        state.prefs.locale = c->locale;
        outcome.saveUserCfg = persistConfig_;
        return;
    }
    if (ApplySystemAndFileCommand(state, cmd, outcome)) {
        return;
    }
    if (previewMockMode_ || !engineRuntime_) {
        HandleTaskAction(state, cmd);
        return;
    }
    if (ApplyLiveSubmissionCommand(state, cmd, outcome)) {
        return;
    }
    ApplyLiveTaskCommand(state, cmd, outcome);
}

bool AppCoordinator::ApplySystemAndFileCommand(AppStateSnapshot& state,
                                               const Command& cmd,
                                               DispatchOutcome& outcome) {
    if (const auto* c = std::get_if<SelectTaskCmd>(&cmd)) {
        state.selectedTaskGid = c->gid;
        if (!c->openDrawer) {
            return true;
        }
        state.detailedTaskGid = (state.detailedTaskGid == c->gid) ? "" : c->gid;
        if (state.detailedTaskGid.empty()) {
            state.detailedTask.reset();
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
            if (engineRuntime_) {
                engineRuntime_->SetDetailGid({});
            }
        } else if (previewMockMode_ || !engineRuntime_) {
            RefreshMockDetail(state);
        } else {
            if (const auto* existing = FindTaskByGid(state, state.detailedTaskGid)) {
                state.detailedTask = *existing;
            }
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
            engineRuntime_->SetDetailGid(state.detailedTaskGid);
            outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::FetchDetail,
                                                   .gid = state.detailedTaskGid};
        }
        return true;
    }
    if (const auto* dropCmd = std::get_if<QueueDroppedFilesCmd>(&cmd)) {
        for (const auto& path : dropCmd->paths) {
            if (motrix::core::AsciiLower(path).ends_with(".torrent")) {
                state.pendingDroppedTorrentPath = path;
                ++state.dropSeq;
                state.activeModal = ActiveModal::AddTask;
                break;
            }
        }
        return true;
    }
    if (const auto* savePrefsCmd = std::get_if<SavePreferencesCmd>(&cmd)) {
        state.prefs = savePrefsCmd->prefs;
        state.sysConfig = savePrefsCmd->sysConfig;
        state.activeModal = ActiveModal::None;
        outcome.saveUserCfg = persistConfig_;
        outcome.saveSystemCfg = persistConfig_;
        outcome.prefsUpdated = true;
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->SetDarkFrame(state.prefs.theme == motrix::domain::ThemePreference::Dark);
        }
        if (!previewMockMode_ && engineRuntime_) {
            engineRuntime_->UpdateUpnpPreference(
                state.prefs.enableUpnp, state.sysConfig.listenPort, state.sysConfig.dhtListenPort);
            outcome.engineWork = EngineWorkCommand{
                .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
                .options = motrix::domain::SystemConfigToOptionPairs(state.sysConfig),
            };
        }
        return true;
    }
    if (const auto* c = std::get_if<CopyTaskLinkCmd>(&cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            if (const auto* task = FindTaskByGid(state, c->gid); task) {
                host->CopyTextToClipboard(task->uri);
            }
        }
        return true;
    }
    if (const auto* c = std::get_if<ShowInFolderCmd>(&cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            if (const auto* task = FindTaskByGid(state, c->gid); task) {
                host->OpenPathInExplorer(task->dir);
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
        auto work = EngineWorkCommand{.kind = EngineWorkCommand::Kind::AddUri, .uris = addCmd->uris};
        RecordHistoryDirectory(state.prefs, addCmd->dir);
        outcome.saveUserCfg = persistConfig_;
        ApplyCommonAddFields(work, *addCmd, state);
        outcome.engineWork = std::move(work);
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
        outcome.engineWork = std::move(work);
        return true;
    }
    if (const auto* fileSelCmd = std::get_if<ApplyTaskFileSelectionCmd>(&cmd)) {
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::ChangeTaskOption,
                                               .gid = fileSelCmd->gid,
                                               .selectedIndices = fileSelCmd->selectedIndices};
        return true;
    }
    if (std::holds_alternative<FactoryResetCmd>(cmd)) {
        const std::string savedDir = state.sysConfig.dir;
        state.prefs = motrix::domain::DefaultUserPreferences();
        state.sysConfig = motrix::domain::DefaultSystemConfig();
        if (!savedDir.empty()) {
            state.sysConfig.dir = savedDir;
        }
        state.sysConfig.rpcSecret = GenerateHexSecret();
        outcome.saveUserCfg = persistConfig_;
        outcome.saveSystemCfg = persistConfig_;
        outcome.prefsUpdated = true;
        outcome.engineWork = EngineWorkCommand{
            .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
            .options = motrix::domain::SystemConfigToOptionPairs(state.sysConfig),
        };
        return true;
    }
    return false;
}

void AppCoordinator::ApplyLiveTaskCommand(AppStateSnapshot& state,
                                          const Command& cmd,
                                          DispatchOutcome& outcome) {
    if (const auto* pauseCmd = std::get_if<PauseTaskCmd>(&cmd)) {
        outcome.engineWork =
            EngineWorkCommand{.kind = EngineWorkCommand::Kind::Pause, .gid = pauseCmd->gid};
    } else if (const auto* resumeCmd = std::get_if<ResumeTaskCmd>(&cmd)) {
        outcome.engineWork =
            EngineWorkCommand{.kind = EngineWorkCommand::Kind::Resume, .gid = resumeCmd->gid};
    } else if (const auto* removeCmd = std::get_if<RemoveTaskCmd>(&cmd)) {
        const bool isStopped = std::any_of(state.stoppedTasks.begin(),
                                           state.stoppedTasks.end(),
                                           [&](const auto& t) { return t.gid == removeCmd->gid; });
        outcome.engineWork = EngineWorkCommand{
            .kind = EngineWorkCommand::Kind::Remove, .gid = removeCmd->gid, .taskInStopped = isStopped};
        if (state.detailedTaskGid == removeCmd->gid) {
            state.detailedTaskGid.clear();
        }
    } else if (const auto* restartCmd = std::get_if<RestartTaskCmd>(&cmd)) {
        if (const auto* t = FindTaskByGid(state, restartCmd->gid); t && !t->uri.empty()) {
            outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Restart,
                                                   .gid = restartCmd->gid,
                                                   .uris = {t->uri},
                                                   .dir = t->dir,
                                                   .split = state.sysConfig.split};
        }
    } else if (std::holds_alternative<RefreshTasksCmd>(cmd)) {
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Sync};
    } else if (std::holds_alternative<PauseAllCmd>(cmd)) {
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::PauseAll};
    } else if (std::holds_alternative<ResumeAllCmd>(cmd)) {
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::ResumeAll};
    } else if (std::holds_alternative<PurgeStoppedCmd>(cmd)) {
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::PurgeStopped};
    } else if (std::holds_alternative<ResetSessionCmd>(cmd)) {
        if (!state.sessionFilePath.empty()) {
            std::error_code ec;
            std::filesystem::remove(motrix::core::PathFromUtf8(state.sessionFilePath), ec);
        }
        outcome.engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::PurgeStopped};
    }
}

}  // namespace motrix::app