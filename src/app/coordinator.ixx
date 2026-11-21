export module motrix.app:coordinator;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :command;
import :snapshot;

namespace motrix::app {

export struct CompletedTaskNotice {
    std::string name;
    std::string dir;
    bool btSeeding = false;
};

export struct TaskCompletionTracker {
    bool initialSyncSeen = false;
    std::unordered_map<std::string, motrix::domain::TaskStatus> lastStatuses;
};

export [[nodiscard]] std::vector<CompletedTaskNotice>
UpdateTaskCompletionTracker(TaskCompletionTracker& tracker, const AppStateSnapshot& state);

export struct CoordinatorOptions {
    motrix::domain::ThemePreference initialTheme = motrix::domain::ThemePreference::Auto;
    motrix::core::Locale initialLocale = motrix::core::Locale::EnUS;
    motrix::domain::TaskCategory initialCategory = motrix::domain::TaskCategory::Downloading;
    int initialDetailTab = 0;
    std::string initialUri;
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
    void SetSessionEnding(bool ending);
    void BeginShutdown();

private:
    struct DispatchOutcome;

    void InitStateFromOptions(const CoordinatorOptions& options);
    void StartLiveEngineIfEnabled(const CoordinatorOptions& options);
    void CreateAndStartEngineRuntime(const AppStateSnapshot& state,
                                     const std::string& binPathOverride = {});
    void EnqueueGlobalOptionsWork(std::vector<std::pair<std::string, std::string>> options);
    void PublishMutated(const std::function<void(AppStateSnapshot&)>& mutator);
    void RecomputeStats(AppStateSnapshot& state);
    [[nodiscard]] bool TryHandleImmediateCommand(const Command& cmd);
    void ApplySelectionCommand(AppStateSnapshot& state, const Command& cmd);
    static void PruneSelectionGids(AppStateSnapshot& state);
    void ApplyStateCommand(AppStateSnapshot& state, const Command& cmd, DispatchOutcome& outcome);
    [[nodiscard]] bool
    ApplySystemAndFileCommand(AppStateSnapshot& state, const Command& cmd, DispatchOutcome& outcome);
    [[nodiscard]] bool ApplyDesktopActionCommand(AppStateSnapshot& state, const Command& cmd);
    [[nodiscard]] bool
    ApplyLiveSubmissionCommand(AppStateSnapshot& state, const Command& cmd, DispatchOutcome& outcome);
    void HandleTaskAction(AppStateSnapshot& state, const Command& cmd);
    void HandleBulkAndSubmit(AppStateSnapshot& state, const Command& cmd);
    void HandleEngineSyncUpdate(const EngineSyncUpdate& update);
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
    void StartAria2NextUpdateAsync(bool forceReinstall);
    void ExecuteAria2NextUpdate(std::stop_token st, motrix::engine::Aria2NextUpdateSpec spec);
    void FinishAria2NextInstall(const std::stop_token& st,
                                const motrix::engine::Aria2NextUpdateSpec& spec,
                                const motrix::engine::Aria2NextStageOutcome& stage);
    void HandleTrayAction(motrix::platform::TrayMenuAction action);
    void HandleStartupPayload(const HandleStartupPayloadCmd& cmd);
    static void SeedPreviewTasks(AppStateSnapshot& state);
    static void RefreshMockDetail(AppStateSnapshot& state);
    static void RecordHistoryDirectory(motrix::domain::UserPreferences& prefs, std::string_view dir);
    [[nodiscard]] static std::string GenerateHexSecret();

    motrix::core::Logger& logger_;
    std::atomic<motrix::platform::IWindowHost*> windowHost_{nullptr};
    std::filesystem::path appDataDir_;
    bool persistConfig_ = false;
    bool enableOsIntegration_ = false;
    bool previewMockMode_ = false;
    bool enableEngine_ = false;
    std::atomic<bool> sleepBlockerActive_{false};
    std::atomic<bool> hasLiveRuntime_{false};
    std::atomic<bool> stopping_{false};
    TaskCompletionTracker completionTracker_;
    std::mutex writeMutex_;
    std::mutex platformStatusMu_;
    mutable std::mutex engineLifecycleMu_;
    AppStateSnapshot mutableState_;
    std::atomic<std::shared_ptr<const AppStateSnapshot>> published_;
    std::uint64_t nextLocalGid_ = 1000;
    std::string enginePathOverride_;
    std::unique_ptr<EngineRuntime> engineRuntime_;
    std::jthread trackerSyncWorker_;
    std::jthread engineUpdateWorker_;
};

}  // namespace motrix::app
