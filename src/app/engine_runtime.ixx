export module motrix.app:engine_runtime;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import :snapshot;

namespace motrix::app {

export struct EngineWorkCommand {
    enum class Kind {
        Sync,
        AddUri,
        Pause,
        Resume,
        Remove,
        Restart,
        PauseAll,
        ResumeAll,
        PurgeStopped,
    };
    Kind kind = Kind::Sync;
    std::string gid;
    std::vector<std::string> uris;
    std::string outName;
    std::string dir;
    int split = 64;
    bool taskInStopped = false;
};

export struct EngineRuntimeConfig {
    std::filesystem::path appDataDir;
    std::string enginePathOverride;
    motrix::domain::SystemConfig sysConfig;
};

export class EngineRuntime {
public:
    using SyncCallback = std::function<void(EngineConnectionState connState,
                                            const motrix::engine::EngineVersionInfo* version,
                                            const motrix::engine::EngineSyncSnapshot* sync,
                                            std::string_view enginePath,
                                            std::string_view errorMsg)>;

    EngineRuntime(motrix::core::Logger& logger, EngineRuntimeConfig cfg, SyncCallback onSync);
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    void Start();
    void Stop();
    void Enqueue(EngineWorkCommand cmd);

private:
    void RunLoop(std::stop_token st);
    [[nodiscard]] bool TryStartAndConnect(std::stop_token st);
    void ProcessCommand(const EngineWorkCommand& cmd, bool& needSync, std::string& lastErr);
    void ApplySyncSnapshot(const motrix::engine::EngineSyncSnapshot& snap, std::string_view err);

    motrix::core::Logger& logger_;
    EngineRuntimeConfig config_;
    SyncCallback onSync_;

    std::unique_ptr<motrix::engine::IEngineProcessManager> proc_;
    std::unique_ptr<motrix::engine::IEngineRpcClient> rpc_;
    std::unique_ptr<motrix::engine::IEngineDialect> dialect_;
    motrix::engine::EngineVersionInfo versionInfo_;
    std::string resolvedPath_;

    std::mutex queueMutex_;
    std::condition_variable_any queueCv_;
    std::deque<EngineWorkCommand> queue_;
    std::atomic<bool> wakeSync_{false};
    std::atomic<bool> disconnectedFlag_{false};
    bool hasActiveTasks_ = false;
    std::jthread worker_;
};

}  // namespace motrix::app
