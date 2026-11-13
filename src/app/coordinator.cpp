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
    }
}

AppCoordinator::~AppCoordinator() {
    engineRuntime_.reset();
}

void AppCoordinator::AttachWindowHost(motrix::platform::IWindowHost* windowHost) noexcept {
    windowHost_.store(windowHost, std::memory_order_release);
}

std::shared_ptr<const AppStateSnapshot> AppCoordinator::Snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}

void AppCoordinator::Dispatch(Command cmd) {
    bool saveUserCfg = false;
    bool saveSystemCfg = false;
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
            if (auto* host = windowHost_.load(std::memory_order_acquire)) {
                host->SetDarkFrame(state.prefs.theme == motrix::domain::ThemePreference::Dark);
            }
            if (!previewMockMode_ && engineRuntime_) {
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
    if (engineWork && engineRuntime_) {
        engineRuntime_->Enqueue(std::move(*engineWork));
    }
}

void AppCoordinator::HandleEngineDetailUpdate(std::optional<motrix::engine::TaskDetailResult> detail,
                                              std::vector<motrix::domain::TaskPeerView> peers) {
    PublishMutated([&](AppStateSnapshot& state) {
        if (state.detailedTaskGid.empty()) {
            state.detailedTask.reset();
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
            return;
        }
        if (detail && detail->task.gid == state.detailedTaskGid) {
            state.detailedTask = std::move(detail->task);
            state.detailedTrackers = std::move(detail->trackers);
            state.detailedPeers = std::move(peers);
        }
    });
}

void AppCoordinator::HandleEngineSyncUpdate(EngineConnectionState connState,
                                            const motrix::engine::EngineVersionInfo* version,
                                            const motrix::engine::EngineSyncSnapshot* sync,
                                            std::string_view enginePath,
                                            std::string_view errorMsg) {
    PublishMutated([&](AppStateSnapshot& state) {
        state.engineState = connState;
        state.engineError = std::string(errorMsg);
        if (!enginePath.empty()) {
            state.resolvedEnginePath = std::string(enginePath);
        }
        if (version) {
            state.engineInfo = *version;
        }
        if (sync) {
            state.downloadingTasks = sync->active;
            state.waitingTasks = sync->waiting;
            state.stoppedTasks = sync->stopped;
            state.globalStat = GlobalStatSnapshot{
                .downloadSpeed = sync->globalStat.downloadSpeed,
                .uploadSpeed = sync->globalStat.uploadSpeed,
                .numActive = sync->globalStat.numActive,
                .numWaiting = sync->globalStat.numWaiting,
                .numStopped = sync->globalStat.numStopped,
            };
            if (!state.detailedTaskGid.empty() && !FindTaskByGid(state, state.detailedTaskGid)) {
                state.detailedTaskGid.clear();
                state.detailedTask.reset();
                state.detailedTrackers.clear();
                state.detailedPeers.clear();
                if (engineRuntime_) {
                    engineRuntime_->SetDetailGid({});
                }
            }
        }
    });
}

void AppCoordinator::PersistConfigs(const AppStateSnapshot& state,
                                    bool saveUser,
                                    bool saveSys,
                                    bool overwrite) {
    if (appDataDir_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(appDataDir_, ec);
    const auto userPath = appDataDir_ / "user.json";
    const auto sysPath = appDataDir_ / "system.json";
    if (saveUser && (overwrite || !std::filesystem::exists(userPath, ec))) {
        (void)motrix::core::WriteTextAtomic(userPath,
                                            motrix::domain::SerializeUserPreferencesJson(state.prefs));
    }
    if (saveSys && (overwrite || !std::filesystem::exists(sysPath, ec))) {
        (void)motrix::core::WriteTextAtomic(sysPath,
                                            motrix::domain::SerializeSystemConfigJson(state.sysConfig));
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

void AppCoordinator::RecomputeStats(AppStateSnapshot& state) {
    std::uint64_t down = 0;
    std::uint64_t up = 0;
    for (const auto& t : state.downloadingTasks) {
        down += t.downloadSpeed;
        up += t.uploadSpeed;
    }
    state.globalStat = GlobalStatSnapshot{
        .downloadSpeed = down,
        .uploadSpeed = up,
        .numActive = static_cast<int>(state.downloadingTasks.size()),
        .numWaiting = static_cast<int>(state.waitingTasks.size()),
        .numStopped = static_cast<int>(state.stoppedTasks.size()),
    };
}

const motrix::domain::DownloadTaskView*
AppCoordinator::FindTaskByGid(const AppStateSnapshot& state, std::string_view gid) const noexcept {
    const auto findIn = [&](const std::vector<motrix::domain::DownloadTaskView>& list)
        -> const motrix::domain::DownloadTaskView* {
        for (const auto& t : list) {
            if (t.gid == gid) {
                return &t;
            }
        }
        return nullptr;
    };
    if (const auto* p = findIn(state.downloadingTasks))
        return p;
    if (const auto* p = findIn(state.waitingTasks))
        return p;
    return findIn(state.stoppedTasks);
}

}  // namespace motrix::app
