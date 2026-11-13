module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

[[nodiscard]] std::string GenerateHexSecret() {
    std::random_device rd;
    std::array<std::uint32_t, 4> words{rd(), rd(), rd(), rd()};
    return std::format("{:08x}{:08x}{:08x}{:08x}", words[0], words[1], words[2], words[3]);
}

}  // namespace

AppCoordinator::AppCoordinator(CoordinatorOptions options,
                               motrix::core::Logger& logger,
                               motrix::platform::IWindowHost* windowHost)
    : logger_(logger), windowHost_(windowHost), appDataDir_(std::move(options.appDataDir)),
      persistConfig_(options.loadPersistedConfig && !appDataDir_.empty()),
      enableOsIntegration_(options.enableOsIntegration),
      previewMockMode_(options.populatePreviewTasks || !options.enableEngine) {
    mutableState_.revision = 1;
    mutableState_.activeCategory = options.initialCategory;

    if (persistConfig_) {
        if (auto u = motrix::core::ReadFileUtf8(appDataDir_ / "user.json"); u) {
            if (auto parsed = motrix::domain::ParseUserPreferencesJson(*u); parsed) {
                mutableState_.prefs = *parsed;
            }
        }
        if (auto s = motrix::core::ReadFileUtf8(appDataDir_ / "system.json"); s) {
            if (auto parsed = motrix::domain::ParseSystemConfigJson(*s); parsed) {
                mutableState_.sysConfig = *parsed;
            }
        }
    }

    if (options.themeOverride || !persistConfig_) {
        mutableState_.prefs.theme = options.initialTheme;
    }
    if (options.localeOverride || !persistConfig_) {
        mutableState_.prefs.locale = options.initialLocale;
    }
    if (options.rpcPortOverride != 0) {
        mutableState_.sysConfig.rpcListenPort = options.rpcPortOverride;
    }
    if (mutableState_.sysConfig.dir.empty() && !options.defaultDownloadDir.empty()) {
        mutableState_.sysConfig.dir = options.defaultDownloadDir;
    }
    if (mutableState_.sysConfig.rpcSecret.empty()) {
        mutableState_.sysConfig.rpcSecret = GenerateHexSecret();
    }

    if (options.openAddModal) {
        mutableState_.activeModal = ActiveModal::AddTask;
    } else if (options.openAboutModal) {
        mutableState_.activeModal = ActiveModal::About;
    }
    if (options.openPrefsSection) {
        mutableState_.activeSection = 1;
    }

    if (options.populatePreviewTasks) {
        SeedPreviewTasks(mutableState_);
        if (options.openDetailDemo) {
            mutableState_.selectedTaskGid = "0001000000000002";
            mutableState_.detailedTaskGid = "0001000000000002";
            mutableState_.activeCategory = motrix::domain::TaskCategory::Waiting;
            RefreshMockDetail(mutableState_);
        }
    } else if (!options.enableEngine) {
        mutableState_.engineState = EngineConnectionState::Offline;
    }

    RecomputeStats(mutableState_);
    published_.store(std::make_shared<const AppStateSnapshot>(mutableState_), std::memory_order_release);

    if (persistConfig_) {
        PersistConfigs(mutableState_);
    }

    if (options.enableEngine && !options.populatePreviewTasks) {
        const std::string binOverride = !options.enginePathOverride.empty()
                                            ? options.enginePathOverride
                                            : mutableState_.prefs.engineBinPath;
        const std::filesystem::path engineDataDir =
            (!persistConfig_ || appDataDir_.empty())
                ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
                : appDataDir_;
        EngineRuntimeConfig rtCfg{
            .appDataDir = engineDataDir,
            .enginePathOverride = binOverride,
            .sysConfig = mutableState_.sysConfig,
            .enableUpnp = mutableState_.prefs.enableUpnp,
            .resumeAllWhenLaunched = mutableState_.prefs.resumeAllWhenAppLaunched,
        };
        engineRuntime_ = std::make_unique<EngineRuntime>(
            logger_,
            std::move(rtCfg),
            [this](EngineConnectionState conn,
                   const motrix::engine::EngineVersionInfo* ver,
                   const motrix::engine::EngineSyncSnapshot* sync,
                   std::string_view binPath,
                   std::string_view err) { HandleEngineSyncUpdate(conn, ver, sync, binPath, err); });
        engineRuntime_->SetDetailCallback([this](std::optional<motrix::engine::TaskDetailResult> detail,
                                                 std::vector<motrix::domain::TaskPeerView> peers) {
            HandleEngineDetailUpdate(std::move(detail), std::move(peers));
        });
        engineRuntime_->Start();
        const auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        if (motrix::domain::ShouldAutoSyncTrackers(mutableState_.prefs, nowSec)) {
            const std::string proxy =
                mutableState_.prefs.useProxy ? mutableState_.sysConfig.allProxy : std::string{};
            StartTrackerSyncAsync(mutableState_.prefs.trackerSource, proxy);
        }
    }
    if (enableOsIntegration_) {
        ApplyDesktopIntegration(mutableState_);
    }
}

AppCoordinator::~AppCoordinator() {
    if (trackerSyncWorker_.joinable()) {
        trackerSyncWorker_.request_stop();
        trackerSyncWorker_.join();
    }
    engineRuntime_.reset();
}

void AppCoordinator::AttachWindowHost(motrix::platform::IWindowHost* windowHost) noexcept {
    windowHost_.store(windowHost, std::memory_order_release);
}

std::shared_ptr<const AppStateSnapshot> AppCoordinator::Snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}

void AppCoordinator::Dispatch(Command cmd) {
    if (const auto* c = std::get_if<SyncTrackersCmd>(&cmd)) {
        const auto snap = Snapshot();
        const auto sources = c->sources.empty() ? snap->prefs.trackerSource : c->sources;
        const std::string proxy = snap->prefs.useProxy ? snap->sysConfig.allProxy : std::string{};
        StartTrackerSyncAsync(sources, proxy);
        return;
    }
    if (const auto* c = std::get_if<HandleStartupPayloadCmd>(&cmd)) {
        HandleStartupPayload(*c);
        return;
    }
    if (const auto* c = std::get_if<HandleTrayActionCmd>(&cmd)) {
        HandleTrayAction(c->action);
        return;
    }
    if (std::holds_alternative<HideToTrayCmd>(cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->HideToTray();
        }
        return;
    }
    if (std::holds_alternative<ShowFromTrayCmd>(cmd)) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->ShowFromTray();
        }
        return;
    }
    if (std::holds_alternative<QuitAppCmd>(cmd)) {
        PersistConfigs(*Snapshot(), true, true, true);
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->RequestQuit();
        }
        return;
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
        return;
    }

    bool saveUserCfg = false;
    bool saveSystemCfg = false;
    bool prefsUpdated = false;
    std::optional<EngineWorkCommand> engineWork;

    PublishMutated([&](AppStateSnapshot& state) {
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
        if (const auto* c = std::get_if<SelectTaskCmd>(&cmd)) {
            state.selectedTaskGid = c->gid;
            if (c->openDrawer) {
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
                    engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::FetchDetail,
                                                   .gid = state.detailedTaskGid};
                }
            }
            return;
        }
        if (const auto* c = std::get_if<SetDetailTabCmd>(&cmd)) {
            state.activeDetailTab = c->tabIndex;
            return;
        }
        if (const auto* c = std::get_if<SetThemeCmd>(&cmd)) {
            state.prefs.theme = c->theme;
            saveUserCfg = persistConfig_;
            return;
        }
        if (std::holds_alternative<ToggleThemeCmd>(cmd)) {
            state.prefs.theme = (state.prefs.theme == motrix::domain::ThemePreference::Dark)
                                    ? motrix::domain::ThemePreference::Light
                                    : motrix::domain::ThemePreference::Dark;
            saveUserCfg = persistConfig_;
            return;
        }
        if (const auto* c = std::get_if<SetLocaleCmd>(&cmd)) {
            state.prefs.locale = c->locale;
            saveUserCfg = persistConfig_;
            return;
        }
        if (const auto* dropCmd = std::get_if<QueueDroppedFilesCmd>(&cmd)) {
            for (const auto& path : dropCmd->paths) {
                const std::string lower = motrix::core::AsciiLower(path);
                if (lower.ends_with(".torrent")) {
                    state.pendingDroppedTorrentPath = path;
                    ++state.dropSeq;
                    state.activeModal = ActiveModal::AddTask;
                    break;
                }
            }
            return;
        }
        if (const auto* savePrefsCmd = std::get_if<SavePreferencesCmd>(&cmd)) {
            state.prefs = savePrefsCmd->prefs;
            state.sysConfig = savePrefsCmd->sysConfig;
            state.activeModal = ActiveModal::None;
            saveUserCfg = persistConfig_;
            saveSystemCfg = persistConfig_;
            prefsUpdated = true;
            if (auto* host = windowHost_.load(std::memory_order_acquire)) {
                host->SetDarkFrame(state.prefs.theme == motrix::domain::ThemePreference::Dark);
            }
            if (!previewMockMode_ && engineRuntime_) {
                engineRuntime_->UpdateUpnpPreference(
                    state.prefs.enableUpnp, state.sysConfig.listenPort, state.sysConfig.dhtListenPort);
                engineWork = EngineWorkCommand{
                    .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
                    .options = motrix::domain::SystemConfigToOptionPairs(state.sysConfig),
                };
            }
            return;
        }
        if (const auto* c = std::get_if<CopyTaskLinkCmd>(&cmd)) {
            if (auto* host = windowHost_.load(std::memory_order_acquire)) {
                if (const auto* task = FindTaskByGid(state, c->gid); task) {
                    host->CopyTextToClipboard(task->uri);
                }
            }
            return;
        }
        if (const auto* c = std::get_if<ShowInFolderCmd>(&cmd)) {
            if (auto* host = windowHost_.load(std::memory_order_acquire)) {
                if (const auto* task = FindTaskByGid(state, c->gid); task) {
                    host->OpenPathInExplorer(task->dir);
                }
            }
            return;
        }

        if (previewMockMode_ || !engineRuntime_) {
            HandleTaskAction(state, cmd);
            return;
        }

        if (const auto* pauseCmd = std::get_if<PauseTaskCmd>(&cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Pause, .gid = pauseCmd->gid};
        } else if (const auto* resumeCmd = std::get_if<ResumeTaskCmd>(&cmd)) {
            engineWork =
                EngineWorkCommand{.kind = EngineWorkCommand::Kind::Resume, .gid = resumeCmd->gid};
        } else if (const auto* removeCmd = std::get_if<RemoveTaskCmd>(&cmd)) {
            const bool isStopped = std::any_of(state.stoppedTasks.begin(),
                                               state.stoppedTasks.end(),
                                               [&](const auto& t) { return t.gid == removeCmd->gid; });
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Remove,
                                           .gid = removeCmd->gid,
                                           .taskInStopped = isStopped};
            if (state.detailedTaskGid == removeCmd->gid) {
                state.detailedTaskGid.clear();
            }
        } else if (const auto* restartCmd = std::get_if<RestartTaskCmd>(&cmd)) {
            if (const auto* t = FindTaskByGid(state, restartCmd->gid); t && !t->uri.empty()) {
                engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Restart,
                                               .gid = restartCmd->gid,
                                               .uris = {t->uri},
                                               .dir = t->dir,
                                               .split = state.sysConfig.split};
            }
        } else if (std::holds_alternative<RefreshTasksCmd>(cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::Sync};
        } else if (std::holds_alternative<PauseAllCmd>(cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::PauseAll};
        } else if (std::holds_alternative<ResumeAllCmd>(cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::ResumeAll};
        } else if (std::holds_alternative<PurgeStoppedCmd>(cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::PurgeStopped};
        } else if (const auto* addCmd = std::get_if<SubmitAddUriCmd>(&cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::AddUri,
                                           .uris = addCmd->uris,
                                           .outName = addCmd->outName,
                                           .dir = addCmd->dir,
                                           .split = addCmd->split};
            state.activeModal = ActiveModal::None;
            if (state.prefs.newTaskShowDownloading) {
                state.activeCategory = motrix::domain::TaskCategory::Downloading;
                state.activeSection = 0;
            }
        } else if (const auto* addTorCmd = std::get_if<SubmitAddTorrentCmd>(&cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::AddTorrent,
                                           .torrentBase64 = addTorCmd->torrentBase64,
                                           .selectedIndices = addTorCmd->selectedIndices,
                                           .totalFiles = addTorCmd->totalFiles,
                                           .outName = addTorCmd->outName,
                                           .dir = addTorCmd->dir,
                                           .split = addTorCmd->split};
            state.activeModal = ActiveModal::None;
            if (state.prefs.newTaskShowDownloading) {
                state.activeCategory = motrix::domain::TaskCategory::Downloading;
                state.activeSection = 0;
            }
        } else if (const auto* fileSelCmd = std::get_if<ApplyTaskFileSelectionCmd>(&cmd)) {
            engineWork = EngineWorkCommand{.kind = EngineWorkCommand::Kind::ChangeTaskOption,
                                           .gid = fileSelCmd->gid,
                                           .selectedIndices = fileSelCmd->selectedIndices};
        }
    });

    if (saveUserCfg || saveSystemCfg) {
        PersistConfigs(*Snapshot(), saveUserCfg, saveSystemCfg, true);
    }
    if (prefsUpdated) {
        const auto snap = Snapshot();
        ApplyDesktopIntegration(*snap);
        UpdateLivePlatformStatus(*snap);
    }
    if (engineWork && engineRuntime_) {
        engineRuntime_->Enqueue(std::move(*engineWork));
    }
}

void AppCoordinator::PublishMutated(const std::function<void(AppStateSnapshot&)>& mutator) {
    {
        std::scoped_lock lock(writeMutex_);
        mutator(mutableState_);
        ++mutableState_.revision;
        if (previewMockMode_ || !engineRuntime_) {
            RecomputeStats(mutableState_);
        }
        published_.store(std::make_shared<const AppStateSnapshot>(mutableState_),
                         std::memory_order_release);
    }
    if (auto* host = windowHost_.load(std::memory_order_acquire)) {
        host->RequestFrame();
    }
}

}  // namespace motrix::app
