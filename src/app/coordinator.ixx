export module motrix.app:coordinator;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :command;
import :snapshot;

namespace motrix::app {

export struct CoordinatorOptions {
    motrix::domain::ThemePreference initialTheme = motrix::domain::ThemePreference::Light;
    motrix::core::Locale initialLocale = motrix::core::Locale::EnUS;
    motrix::domain::TaskCategory initialCategory = motrix::domain::TaskCategory::Downloading;
    bool populatePreviewTasks = false;
    bool openAddModal = false;
    bool openAboutModal = false;
    bool openPrefsSection = false;
    bool openDetailDemo = false;
    bool enableEngine = true;
    bool loadPersistedConfig = true;
    bool themeOverride = false;
    bool localeOverride = false;
    bool enableOsIntegration = false;
    bool openedAtLogin = false;
    std::uint16_t rpcPortOverride = 0;
    std::filesystem::path appDataDir;
    std::string defaultDownloadDir;
    std::string enginePathOverride;
};

class EngineRuntime;

export class AppCoordinator final : public ICommandSink {
public:
    AppCoordinator(CoordinatorOptions options,
                   motrix::core::Logger& logger,
                   motrix::platform::IWindowHost* windowHost = nullptr);
    ~AppCoordinator() override;

    AppCoordinator(const AppCoordinator&) = delete;
    AppCoordinator& operator=(const AppCoordinator&) = delete;

    void AttachWindowHost(motrix::platform::IWindowHost* windowHost) noexcept;
    [[nodiscard]] std::shared_ptr<const AppStateSnapshot> Snapshot() const noexcept;

    void Dispatch(Command cmd) override;

private:
    void PublishMutated(const std::function<void(AppStateSnapshot&)>& mutator);
    void RecomputeStats(AppStateSnapshot& state);
    void HandleTaskAction(AppStateSnapshot& state, const Command& cmd);
    void HandleBulkAndSubmit(AppStateSnapshot& state, const Command& cmd);
    void HandleEngineSyncUpdate(EngineConnectionState connState,
                                const motrix::engine::EngineVersionInfo* version,
                                const motrix::engine::EngineSyncSnapshot* sync,
                                std::string_view enginePath,
                                std::string_view errorMsg);
    void HandleEngineDetailUpdate(std::optional<motrix::engine::TaskDetailResult> detail,
                                  std::vector<motrix::domain::TaskPeerView> peers);
    void PersistConfigs(const AppStateSnapshot& state,
                        bool saveUser = true,
                        bool saveSys = true,
                        bool overwrite = false);
    void ApplyDesktopIntegration(const AppStateSnapshot& state);
    void UpdateLivePlatformStatus(const AppStateSnapshot& state);
    void DetectAndNotifyCompletions(const AppStateSnapshot& state);
    void StartTrackerSyncAsync(std::vector<std::string> sources, std::string proxyUrl);
    void HandleTrayAction(motrix::platform::TrayMenuAction action);
    void HandleStartupPayload(const HandleStartupPayloadCmd& cmd);
    static void SeedPreviewTasks(AppStateSnapshot& state);
    static void RefreshMockDetail(AppStateSnapshot& state);
    [[nodiscard]] const motrix::domain::DownloadTaskView*
    FindTaskByGid(const AppStateSnapshot& state, std::string_view gid) const noexcept;

    motrix::core::Logger& logger_;
    std::atomic<motrix::platform::IWindowHost*> windowHost_{nullptr};
    std::filesystem::path appDataDir_;
    bool persistConfig_ = false;
    bool enableOsIntegration_ = false;
    bool previewMockMode_ = false;
    bool sleepBlockerActive_ = false;
    bool initialSyncSeen_ = false;
    std::unordered_map<std::string, motrix::domain::TaskStatus> lastTaskStatuses_;
    std::mutex writeMutex_;
    AppStateSnapshot mutableState_;
    std::atomic<std::shared_ptr<const AppStateSnapshot>> published_;
    std::uint64_t nextLocalGid_ = 1000;
    std::unique_ptr<EngineRuntime> engineRuntime_;
    std::jthread trackerSyncWorker_;
};

}  // namespace motrix::app
