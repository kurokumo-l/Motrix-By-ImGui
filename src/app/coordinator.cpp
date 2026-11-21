module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

void ApplyNavigationAndPrefOverrides(const CoordinatorOptions& options,
                                     AppStateSnapshot& state,
                                     std::string_view fallbackSecret) {
    if (options.rpcPortOverride != 0) {
        state.sysConfig.rpcListenPort = options.rpcPortOverride;
    }
    if (state.sysConfig.dir.empty() && !options.defaultDownloadDir.empty()) {
        state.sysConfig.dir = options.defaultDownloadDir;
    }
    if (state.sysConfig.rpcSecret.empty()) {
        state.sysConfig.rpcSecret = std::string(fallbackSecret);
    }
    state.activeDetailTab = std::clamp(options.initialDetailTab, 0, 4);
    if (!options.initialUri.empty()) {
        state.pendingPrefillUri = options.initialUri;
        ++state.prefillUriSeq;
        state.activeModal = ActiveModal::AddTask;
    } else if (options.openAddModal) {
        state.activeModal = ActiveModal::AddTask;
    } else if (options.openAboutModal) {
        state.activeModal = ActiveModal::About;
    }
    if (options.openPrefsSection) {
        state.activeSection = 1;
    }
}

}  // namespace

std::string AppCoordinator::GenerateHexSecret() {
    std::random_device rd;
    std::array<std::uint32_t, 4> words{rd(), rd(), rd(), rd()};
    return std::format("{:08x}{:08x}{:08x}{:08x}", words[0], words[1], words[2], words[3]);
}

AppCoordinator::AppCoordinator(CoordinatorOptions options,
                               motrix::core::Logger& logger,
                               motrix::platform::IWindowHost* windowHost)
    : logger_(logger), windowHost_(windowHost), appDataDir_(std::move(options.appDataDir)),
      persistConfig_(options.loadPersistedConfig && !appDataDir_.empty()),
      enableOsIntegration_(options.enableOsIntegration),
      previewMockMode_(options.populatePreviewTasks || !options.enableEngine),
      enableEngine_(options.enableEngine && !options.populatePreviewTasks),
      enginePathOverride_(options.enginePathOverride) {
    InitStateFromOptions(options);
    if (persistConfig_) {
        PersistConfigs(mutableState_);
    }
    StartLiveEngineIfEnabled(options);
    if (enableOsIntegration_) {
        ApplyDesktopIntegration(mutableState_);
    }
}

AppCoordinator::~AppCoordinator() {
    BeginShutdown();
}

void AppCoordinator::BeginShutdown() {
    stopping_.store(true, std::memory_order_release);
    if (trackerSyncWorker_.joinable()) {
        trackerSyncWorker_.request_stop();
    }
    if (engineUpdateWorker_.joinable()) {
        engineUpdateWorker_.request_stop();
    }
    std::unique_ptr<EngineRuntime> oldRt;
    {
        std::scoped_lock lock(engineLifecycleMu_);
        hasLiveRuntime_.store(false, std::memory_order_release);
        oldRt = std::move(engineRuntime_);
    }
    oldRt.reset();
    if (trackerSyncWorker_.joinable()) {
        trackerSyncWorker_.join();
    }
    if (engineUpdateWorker_.joinable()) {
        engineUpdateWorker_.join();
    }
}

void AppCoordinator::SetSessionEnding(bool ending) {
    std::scoped_lock lock(engineLifecycleMu_);
    if (engineRuntime_) {
        engineRuntime_->SetShutdownSuspended(ending);
    }
}

void AppCoordinator::InitStateFromOptions(const CoordinatorOptions& options) {
    mutableState_.revision = 1;
    mutableState_.activeCategory = options.initialCategory;

    bool loadedUserPrefs = false;
    if (persistConfig_) {
        if (auto u = motrix::core::ReadFileUtf8(appDataDir_ / "user.json"); u) {
            if (auto parsed = motrix::domain::ParseUserPreferencesJson(*u); parsed) {
                mutableState_.prefs = *parsed;
                loadedUserPrefs = true;
            }
        }
        if (auto s = motrix::core::ReadFileUtf8(appDataDir_ / "system.json"); s) {
            if (auto parsed = motrix::domain::ParseSystemConfigJson(*s); parsed) {
                mutableState_.sysConfig = *parsed;
            }
        }
    }

    if (options.themeOverride || !loadedUserPrefs || !persistConfig_) {
        mutableState_.prefs.theme = options.initialTheme;
    }
    if (options.localeOverride || !loadedUserPrefs || !persistConfig_) {
        mutableState_.prefs.locale = options.initialLocale;
    }
    ApplyNavigationAndPrefOverrides(options, mutableState_, GenerateHexSecret());

    if (options.populatePreviewTasks) {
        SeedPreviewTasks(mutableState_);
        if (options.openDetailDemo) {
            mutableState_.selectedTaskGid = "0001000000000002";
            mutableState_.selectedTaskGids = {"0001000000000002"};
            mutableState_.detailedTaskGid = "0001000000000002";
            mutableState_.activeCategory = motrix::domain::TaskCategory::Waiting;
            RefreshMockDetail(mutableState_);
        }
    } else if (!options.enableEngine) {
        mutableState_.engineState = EngineConnectionState::Offline;
    }

    RecomputeStats(mutableState_);
    const std::filesystem::path effectiveDataDir =
        appDataDir_.empty() ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
                            : appDataDir_;
    mutableState_.engineConfPath = motrix::core::PathToUtf8(effectiveDataDir / "aria2.conf");
    mutableState_.sessionFilePath = motrix::core::PathToUtf8(effectiveDataDir / "aria2.session");
    mutableState_.appLogPath = motrix::core::PathToUtf8(effectiveDataDir / "logs" / "motrix_native.log");
    logger_.SetMinLevel(motrix::core::ParseLogLevel(mutableState_.prefs.logLevel));
    published_.store(std::make_shared<const AppStateSnapshot>(mutableState_), std::memory_order_release);
}

void AppCoordinator::CreateAndStartEngineRuntime(const AppStateSnapshot& state,
                                                 const std::string& binPathOverride) {
    std::string overrideCopy;
    {
        std::scoped_lock lock(engineLifecycleMu_);
        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }
        overrideCopy = enginePathOverride_;
    }
    const std::string binOverride =
        !binPathOverride.empty() ? binPathOverride
                                 : (!overrideCopy.empty() ? overrideCopy : state.prefs.engineBinPath);
    const std::filesystem::path engineDataDir =
        (!persistConfig_ || appDataDir_.empty())
            ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
            : appDataDir_;
    EngineRuntimeConfig rtCfg{
        .appDataDir = engineDataDir,
        .enginePathOverride = binOverride,
        .sysConfig = motrix::domain::BuildEffectiveSystemConfig(
            state.sysConfig, state.prefs, state.speedLimitPaused),
        .logLevel = state.prefs.logLevel,
        .enableUpnp = state.prefs.enableUpnp,
        .resumeAllWhenLaunched = state.prefs.resumeAllWhenAppLaunched,
    };
    auto runtime = std::make_unique<EngineRuntime>(
        logger_, std::move(rtCfg), [this](const EngineSyncUpdate& update) {
            HandleEngineSyncUpdate(update);
        });
    runtime->SetDetailCallback([this](std::optional<motrix::engine::TaskDetailResult> detail,
                                      std::vector<motrix::domain::TaskPeerView> peers) {
        HandleEngineDetailUpdate(std::move(detail), std::move(peers));
    });
    if (!state.detailedTaskGid.empty()) {
        runtime->SetDetailGid(state.detailedTaskGid);
    }
    runtime->Start();
    std::unique_ptr<EngineRuntime> discarded;
    {
        std::scoped_lock lock(engineLifecycleMu_);
        if (stopping_.load(std::memory_order_acquire)) {
            discarded = std::move(runtime);
        } else {
            discarded = std::move(engineRuntime_);
            hasLiveRuntime_.store(true, std::memory_order_release);
            engineRuntime_ = std::move(runtime);
        }
    }
    discarded.reset();
}

void AppCoordinator::EnqueueGlobalOptionsWork(std::vector<std::pair<std::string, std::string>> options) {
    std::scoped_lock lock(engineLifecycleMu_);
    if (engineRuntime_) {
        engineRuntime_->Enqueue(EngineWorkCommand{
            .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
            .options = std::move(options),
        });
    }
}

void AppCoordinator::StartLiveEngineIfEnabled(const CoordinatorOptions& options) {
    if (!enableEngine_) {
        return;
    }
    CreateAndStartEngineRuntime(mutableState_, options.enginePathOverride);
    const auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    if (motrix::domain::ShouldAutoSyncTrackers(mutableState_.prefs, nowSec)) {
        const std::string proxy =
            mutableState_.prefs.useProxy ? mutableState_.sysConfig.allProxy : std::string{};
        StartTrackerSyncAsync(mutableState_.prefs.trackerSource, proxy);
    }
}

void AppCoordinator::AttachWindowHost(motrix::platform::IWindowHost* windowHost) noexcept {
    windowHost_.store(windowHost, std::memory_order_release);
}

std::shared_ptr<const AppStateSnapshot> AppCoordinator::Snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}

void AppCoordinator::PublishMutated(const std::function<void(AppStateSnapshot&)>& mutator) {
    {
        std::scoped_lock lock(writeMutex_);
        mutator(mutableState_);
        ++mutableState_.revision;
        if (previewMockMode_ || !hasLiveRuntime_.load(std::memory_order_acquire)) {
            RecomputeStats(mutableState_);
        }
        published_.store(std::make_shared<const AppStateSnapshot>(mutableState_),
                         std::memory_order_release);
    }
    if (auto* host = windowHost_.load(std::memory_order_acquire)) {
        host->RequestFrame();
    }
}

std::vector<CompletedTaskNotice> UpdateTaskCompletionTracker(TaskCompletionTracker& tracker,
                                                             const AppStateSnapshot& state) {
    std::vector<CompletedTaskNotice> notices;
    std::unordered_map<std::string, motrix::domain::TaskStatus> nextStatuses;
    nextStatuses.reserve(state.downloadingTasks.size() + state.waitingTasks.size()
                         + state.stoppedTasks.size());

    auto collectList = [&](const std::vector<motrix::domain::DownloadTaskView>& list, bool activeGroup) {
        for (const auto& t : list) {
            const bool completedOrSeeding =
                (t.status == motrix::domain::TaskStatus::Complete)
                || (activeGroup && t.taskType == motrix::domain::TaskType::BitTorrent && t.isSeeding);
            const motrix::domain::TaskStatus effectiveStatus =
                completedOrSeeding ? motrix::domain::TaskStatus::Complete : t.status;

            const auto it = tracker.lastStatuses.find(t.gid);
            nextStatuses.insert_or_assign(t.gid, effectiveStatus);
            if (!tracker.initialSyncSeen || !completedOrSeeding) {
                continue;
            }
            if (it == tracker.lastStatuses.end() || it->second != motrix::domain::TaskStatus::Complete) {
                const bool btSeeding =
                    activeGroup && t.taskType == motrix::domain::TaskType::BitTorrent && t.isSeeding;
                notices.push_back(CompletedTaskNotice{
                    .name = t.name,
                    .dir = t.dir,
                    .btSeeding = btSeeding,
                });
            }
        }
    };

    collectList(state.downloadingTasks, true);
    collectList(state.waitingTasks, false);
    collectList(state.stoppedTasks, false);
    tracker.lastStatuses = std::move(nextStatuses);
    tracker.initialSyncSeen = true;
    return notices;
}

const motrix::domain::DownloadTaskView* FindTaskByGid(const AppStateSnapshot& state,
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

void AppCoordinator::PruneSelectionGids(AppStateSnapshot& state) {
    std::erase_if(state.selectedTaskGids,
                  [&](const std::string& g) { return FindTaskByGid(state, g) == nullptr; });
    if (!state.selectedTaskGid.empty()
        && std::ranges::find(state.selectedTaskGids, state.selectedTaskGid)
               == state.selectedTaskGids.end()) {
        state.selectedTaskGid =
            state.selectedTaskGids.empty() ? std::string{} : state.selectedTaskGids.front();
    }
}

void AppCoordinator::RecordHistoryDirectory(motrix::domain::UserPreferences& prefs,
                                            std::string_view dir) {
    if (dir.empty()) {
        return;
    }
    auto& hist = prefs.historyDirectories;
    hist.erase(std::remove(hist.begin(), hist.end(), dir), hist.end());
    hist.insert(hist.begin(), std::string(dir));
    if (hist.size() > 5) {
        hist.resize(5);
    }
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
    const auto confPath = appDataDir_ / "aria2.conf";
    if (saveSys && (overwrite || !std::filesystem::exists(confPath, ec))) {
        const auto eff = motrix::domain::BuildEffectiveSystemConfig(state.sysConfig, state.prefs);
        const auto rawPairs = motrix::domain::SystemConfigToOptionPairs(eff);
        std::vector<motrix::engine::EngineOptionPair> engPairs;
        engPairs.reserve(rawPairs.size() + 1);
        for (const auto& [k, v] : rawPairs) {
            engPairs.push_back({k, v});
        }
        engPairs.push_back({"bt-port-mapping", state.prefs.enableUpnp ? "true" : "false"});
        const auto kind = (state.engineInfo.kind == motrix::engine::EngineKind::Unknown)
                              ? motrix::engine::EngineKind::Aria2Next
                              : state.engineInfo.kind;
        auto dialect = motrix::engine::CreateDialect(kind);
        const auto filtered = dialect->FilterGlobalOptions(engPairs);
        (void)motrix::core::WriteTextAtomic(confPath, motrix::engine::FormatAria2Conf(filtered));
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

}  // namespace motrix::app