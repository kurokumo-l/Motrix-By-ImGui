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

    if (options.populatePreviewTasks) {
        SeedPreviewTasks(mutableState_);
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
        }
    });

    if (saveUserCfg) {
        const auto snap = Snapshot();
        (void)motrix::core::WriteTextAtomic(appDataDir_ / "user.json",
                                            motrix::domain::SerializeUserPreferencesJson(snap->prefs));
    }
    if (engineWork && engineRuntime_) {
        engineRuntime_->Enqueue(std::move(*engineWork));
    }
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
        }
    });
}

void AppCoordinator::PersistConfigs(const AppStateSnapshot& state) {
    std::error_code ec;
    std::filesystem::create_directories(appDataDir_, ec);
    if (!std::filesystem::exists(appDataDir_ / "user.json", ec)) {
        (void)motrix::core::WriteTextAtomic(appDataDir_ / "user.json",
                                            motrix::domain::SerializeUserPreferencesJson(state.prefs));
    }
    if (!std::filesystem::exists(appDataDir_ / "system.json", ec)) {
        (void)motrix::core::WriteTextAtomic(appDataDir_ / "system.json",
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
