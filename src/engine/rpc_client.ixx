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

export struct EngineProbeResult {
    std::string fileName;
    std::uint64_t totalLength = 0;
    std::string contentType;
};

export class IEngineRpcClient {
public:
    virtual ~IEngineRpcClient() = default;

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

    [[nodiscard]] virtual motrix::core::Result<std::string> AddUri(const AddUriRequest& request) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Pause(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Unpause(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Remove(std::string_view gid) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> SaveSession() = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Shutdown(bool force = false) = 0;
};

}  // namespace motrix::engine
