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
    if (options.openAddModal) {
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
      previewMockMode_(options.populatePreviewTasks || !options.enableEngine) {
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
    if (trackerSyncWorker_.joinable()) {
        trackerSyncWorker_.request_stop();
        trackerSyncWorker_.join();
    }
    engineRuntime_.reset();
}

void AppCoordinator::InitStateFromOptions(const CoordinatorOptions& options) {
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
    ApplyNavigationAndPrefOverrides(options, mutableState_, GenerateHexSecret());

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
    const std::filesystem::path effectiveDataDir =
        appDataDir_.empty() ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
                            : appDataDir_;
    mutableState_.engineConfPath = motrix::core::PathToUtf8(effectiveDataDir / "system.json");
    mutableState_.sessionFilePath = motrix::core::PathToUtf8(effectiveDataDir / "aria2.session");
    mutableState_.appLogPath = motrix::core::PathToUtf8(effectiveDataDir / "motrix.log");
    published_.store(std::make_shared<const AppStateSnapshot>(mutableState_), std::memory_order_release);
}

void AppCoordinator::StartLiveEngineIfEnabled(const CoordinatorOptions& options) {
    if (!options.enableEngine || options.populatePreviewTasks) {
        return;
    }
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
        logger_, std::move(rtCfg), [this](const EngineSyncUpdate& update) {
            HandleEngineSyncUpdate(update);
        });
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