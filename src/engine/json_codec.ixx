export module motrix.engine:json_codec;

import std;
import motrix.core;
import motrix.domain;
import :dialect;
import :rpc_client;

namespace motrix::engine {

export struct RpcParsedFrame {
    bool isNotification = false;
    std::string id;
    bool hasError = false;
    int errorCode = 0;
    std::string errorMessage;
    std::string resultJson;
    EngineNotification notification;
};

export [[nodiscard]] std::string
BuildSimpleRpcRequest(std::string_view id, std::string_view method, std::string_view secret);

export [[nodiscard]] std::string BuildGidRpcRequest(std::string_view id,
                                                    std::string_view method,
                                                    std::string_view secret,
                                                    std::string_view gid);

export struct RpcTellSliceSpec {
    std::string_view method;
    int offset = 0;
    int num = 0;
};

export [[nodiscard]] std::string
BuildTellSliceRpcRequest(std::string_view id, std::string_view secret, const RpcTellSliceSpec& slice);

export [[nodiscard]] std::string
BuildAddUriRpcRequest(std::string_view id, std::string_view secret, const AddUriRequest& request);

export [[nodiscard]] std::string BuildAddTorrentRpcRequest(std::string_view id,
                                                           std::string_view secret,
                                                           const AddTorrentRequest& request);

export [[nodiscard]] std::string BuildChangeGlobalOptionRpcRequest(
    std::string_view id, std::string_view secret, std::span<const EngineOptionPair> options);

export [[nodiscard]] std::string
BuildChangeTaskOptionRpcRequest(std::string_view id,
                                std::string_view secret,
                                std::string_view gid,
                                std::span<const EngineOptionPair> options);

export [[nodiscard]] std::string BuildMulticallSyncRequest(std::string_view id,
                                                           std::string_view secret,
                                                           int waitingLimit,
                                                           int stoppedLimit);

export [[nodiscard]] motrix::core::Result<RpcParsedFrame> ParseRpcFrame(std::string_view raw);

export [[nodiscard]] motrix::core::Result<EngineVersionInfo>
ParseEngineVersionJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<EngineGlobalStat>
ParseGlobalStatJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>>
ParseTaskListJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<EngineSyncSnapshot>
ParseMulticallSyncJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<TaskDetailResult>
ParseTaskDetailJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<std::vector<motrix::domain::TaskFileView>>
ParseTaskFilesJson(std::string_view resultJson);

export [[nodiscard]] motrix::core::Result<std::vector<motrix::domain::TaskPeerView>>
ParsePeersJson(std::string_view resultJson, std::uint32_t numPieces = 0);

export [[nodiscard]] motrix::core::Result<std::string>
ParseStringResultJson(std::string_view resultJson);

}  // namespace motrix::engine
