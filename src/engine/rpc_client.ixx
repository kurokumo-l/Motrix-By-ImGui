export module motrix.engine:rpc_client;

import std;
import motrix.core;
import motrix.domain;
import :dialect;

namespace motrix::engine {

export struct RpcConnectionOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 16800;
    std::string secret;
};

export struct EngineGlobalStat {
    std::uint64_t downloadSpeed = 0;
    std::uint64_t uploadSpeed = 0;
    int numActive = 0;
    int numWaiting = 0;
    int numStopped = 0;
};

export struct AddUriRequest {
    std::vector<std::string> uris;
    std::vector<EngineOptionPair> options;
    std::optional<int> position;
};

export struct AddTorrentRequest {
    std::string torrentBase64;
    std::vector<std::string> uris;
    std::vector<EngineOptionPair> options;
    std::optional<int> position;
};

export struct TaskDetailResult {
    motrix::domain::DownloadTaskView task;
    std::vector<std::string> trackers;
};

export struct EngineProbeResult {
    std::string fileName;
    std::uint64_t totalLength = 0;
    std::string contentType;
};

export enum class EngineEventKind {
    Connected = 0,
    Disconnected,
    DownloadStart,
    DownloadPause,
    DownloadStop,
    DownloadComplete,
    DownloadError,
    BtDownloadComplete,
};

export struct EngineNotification {
    EngineEventKind kind = EngineEventKind::Connected;
    std::string gid;
};

export struct EngineSyncSnapshot {
    EngineGlobalStat globalStat;
    std::vector<motrix::domain::DownloadTaskView> active;
    std::vector<motrix::domain::DownloadTaskView> waiting;
    std::vector<motrix::domain::DownloadTaskView> stopped;
};

export class IEngineRpcClient {
public:
    virtual ~IEngineRpcClient() = default;

    using NotificationCallback = std::function<void(const EngineNotification&)>;
    virtual void SetNotificationCallback(NotificationCallback cb) = 0;

    [[nodiscard]] virtual motrix::core::Result<void> Connect(const RpcConnectionOptions& options) = 0;
    virtual void Disconnect() = 0;
    [[nodiscard]] virtual bool IsConnected() const noexcept = 0;

    [[nodiscard]] virtual motrix::core::Result<EngineVersionInfo> GetVersion() = 0;
    [[nodiscard]] virtual motrix::core::Result<EngineGlobalStat> GetGlobalStat() = 0;
    [[nodiscard]] virtual motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>>
    TellActive() = 0;
    [[nodiscard]] virtual motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>>
    TellWaiting(int offset, int num) = 0;
    [[nodiscard]] virtual motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>>
    TellStopped(int offset, int num) = 0;
    [[nodiscard]] virtual motrix::core::Result<EngineSyncSnapshot> SyncAll(int waitingLimit = 100,
                                                                           int stoppedLimit = 100) = 0;

    [[nodiscard]] virtual motrix::core::Result<TaskDetailResult> TellStatus(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<std::vector<motrix::domain::TaskFileView>>
    GetFiles(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<std::vector<motrix::domain::TaskPeerView>>
    GetPeers(std::string_view gid) = 0;

    [[nodiscard]] virtual motrix::core::Result<std::string> AddUri(const AddUriRequest& request) = 0;
    [[nodiscard]] virtual motrix::core::Result<std::string>
    AddTorrent(const AddTorrentRequest& request) = 0;
    [[nodiscard]] virtual motrix::core::Result<void>
    ChangeTaskOption(std::string_view gid, std::span<const EngineOptionPair> options) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Pause(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Unpause(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> ForceRemove(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Remove(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> RemoveDownloadResult(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> PauseAll() = 0;
    [[nodiscard]] virtual motrix::core::Result<void> UnpauseAll() = 0;
    [[nodiscard]] virtual motrix::core::Result<void> PurgeDownloadResult() = 0;
    [[nodiscard]] virtual motrix::core::Result<void>
    ChangeGlobalOption(std::span<const EngineOptionPair> options) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> SaveSession() = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Shutdown(bool force = false) = 0;
};

export [[nodiscard]] std::unique_ptr<IEngineRpcClient> CreateWsRpcClient(motrix::core::Logger& logger);

}  // namespace motrix::engine
