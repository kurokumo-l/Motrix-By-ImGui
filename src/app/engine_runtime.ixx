export module motrix.app:engine_runtime;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import :command;
import :snapshot;

namespace motrix::app {

export struct EngineWorkCommand {
    enum class Kind {
        Sync,
        AddUri,
        AddTorrent,
        ChangeTaskOption,
        ApplyGlobalOptions,
        FetchDetail,
        Pause,
        Resume,
        Remove,
        Restart,
        PauseAll,
        ResumeAll,
        PurgeStopped,
        UpdateUpnp,
        ResetSession,
    };
    Kind kind = Kind::Sync;
    std::string gid;
    std::vector<std::string> uris;
    std::string torrentBase64;
    std::vector<int> selectedIndices;
    std::size_t totalFiles = 0;
    std::string outName;
    std::string dir;
    int split = 16;
    std::string userAgent;
    std::string authorization;
    std::string referer;
    std::string cookie;
    std::string allProxy;
    bool taskInStopped = false;
    bool deleteFiles = false;
    bool enableUpnp = true;
    int btPort = 0;
    int dhtPort = 0;
    std::vector<std::string> filesToDelete;
    std::vector<std::pair<std::string, std::string>> options;
};

export struct EngineRuntimeConfig {
    std::filesystem::path appDataDir;
    std::string enginePathOverride;
    motrix::domain::SystemConfig sysConfig;
    std::string logLevel = "warn";
    bool enableUpnp = true;
    bool resumeAllWhenLaunched = false;
};

export class EngineRuntime {
public:
    using SyncCallback = std::function<void(const EngineSyncUpdate&)>;
    using DetailCallback = std::function<void(std::optional<motrix::engine::TaskDetailResult> detail,
                                              std::vector<motrix::domain::TaskPeerView> peers)>;

    EngineRuntime(motrix::core::Logger& logger, EngineRuntimeConfig cfg, SyncCallback onSync);
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    void Start();
    void Stop();
    void Enqueue(EngineWorkCommand cmd);
    void SetDetailCallback(DetailCallback cb);
    void SetDetailGid(std::string gid);
    void UpdateUpnpPreference(bool enabled, int btPort, int dhtPort);
    void Reconfigure(EngineRuntimeConfig cfg, bool restartEngine);
    void SetShutdownSuspended(bool suspended) noexcept;

private:
    struct PreparedLaunch {
        motrix::engine::EngineSpawnSpec spec;
        std::vector<motrix::engine::EngineOptionPair> globalOpts;
    };

    void RunLoop(std::stop_token st);
    void RunConnectedSession(std::stop_token st);
    void ShutdownEngineGracefully();
    [[nodiscard]] bool IsShutdownBlocked() const noexcept;
    [[nodiscard]] bool WaitWhileShutdownSuspended(std::stop_token st);
    void WaitReconnectBackoff(std::stop_token st, std::chrono::milliseconds delay);
    [[nodiscard]] bool TryStartAndConnect(std::stop_token st);
    [[nodiscard]] PreparedLaunch PrepareLaunchSpec(const std::filesystem::path& binPath,
                                                   motrix::engine::EngineKind kind) const;
    void FinalizeConnectedSession(const motrix::core::Result<motrix::engine::EngineVersionInfo>& probe,
                                  const std::vector<motrix::engine::EngineOptionPair>& globalOpts);
    void ProcessCommand(const EngineWorkCommand& cmd, bool& needSync, std::string& lastErr);
    void HandleTaskControlCommand(const EngineWorkCommand& cmd, bool& needSync, std::string& lastErr);
    void HandleApplyGlobalOptionsCmd(const EngineWorkCommand& cmd, std::string& lastErr);
    void ExecuteRemoveCommand(const EngineWorkCommand& cmd);
    void ExecuteResetSessionCommand();
    void ApplySyncSnapshot(const motrix::engine::EngineSyncSnapshot& snap, std::string_view err);
    void FetchAndPublishDetail(std::string_view gid);
    void StartOrRefreshUpnpAsync(bool enabled, int btPort, int dhtPort);

    motrix::core::Logger& logger_;
    EngineRuntimeConfig config_;
    SyncCallback onSync_;
    std::mutex detailMutex_;
    std::string detailGid_;
    DetailCallback onDetail_;

    std::unique_ptr<motrix::engine::IEngineProcessManager> proc_;
    std::unique_ptr<motrix::engine::IEngineRpcClient> rpc_;
    std::unique_ptr<motrix::engine::IEngineDialect> dialect_;
    std::unique_ptr<motrix::engine::IUpnpPortMapper> upnp_;
    motrix::engine::EngineVersionInfo versionInfo_;
    std::string resolvedPath_;

    std::mutex queueMutex_;
    std::condition_variable_any queueCv_;
    std::deque<EngineWorkCommand> queue_;
    std::optional<EngineRuntimeConfig> pendingConfig_;
    std::atomic<bool> wakeSync_{false};
    std::atomic<bool> disconnectedFlag_{false};
    std::atomic<bool> restartRequested_{false};
    std::atomic<bool> shutdownSuspended_{false};
    bool resetSessionRequested_ = false;
    bool hasActiveTasks_ = false;
    bool didLaunchResume_ = false;
    std::jthread upnpWorker_;
    std::jthread worker_;
};

}  // namespace motrix::app
