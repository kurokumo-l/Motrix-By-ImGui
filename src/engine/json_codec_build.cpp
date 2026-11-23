module;

#include <nlohmann/json.hpp>

module motrix.engine;

import std;

namespace motrix::engine {

namespace {

[[nodiscard]] nlohmann::json MakeTokenArray(std::string_view secret) {
    nlohmann::json params = nlohmann::json::array();
    if (!secret.empty()) {
        params.push_back(std::format("token:{}", secret));
    }
    return params;
}

[[nodiscard]] nlohmann::json BuildOptionsObject(std::span<const EngineOptionPair> options) {
    nlohmann::json opts = nlohmann::json::object();
    for (const auto& kv : options) {
        if (kv.key.empty()) {
            continue;
        }
        if (kv.key == "header") {
            auto& arr = opts["header"];
            if (!arr.is_array()) {
                arr = nlohmann::json::array();
            }
            arr.push_back(kv.value);
        } else {
            opts[kv.key] = kv.value;
        }
    }
    return opts;
}

[[nodiscard]] std::string
DumpRpcCall(std::string_view id, std::string_view method, nlohmann::json params) {
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    return req.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace

std::string
BuildSimpleRpcRequest(std::string_view id, std::string_view method, std::string_view secret) {
    return DumpRpcCall(id, method, MakeTokenArray(secret));
}

std::string BuildGidRpcRequest(std::string_view id,
                               std::string_view method,
                               std::string_view secret,
                               std::string_view gid) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(gid);
    return DumpRpcCall(id, method, std::move(params));
}

std::string
BuildTellSliceRpcRequest(std::string_view id, std::string_view secret, const RpcTellSliceSpec& slice) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(slice.offset);
    params.push_back(slice.num);
    return DumpRpcCall(id, slice.method, std::move(params));
}

std::string
BuildAddUriRpcRequest(std::string_view id, std::string_view secret, const AddUriRequest& request) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(request.uris);
    params.push_back(BuildOptionsObject(request.options));
    if (request.position.has_value()) {
        params.push_back(*request.position);
    }
    return DumpRpcCall(id, "aria2.addUri", std::move(params));
}

std::string BuildAddTorrentRpcRequest(std::string_view id,
                                      std::string_view secret,
                                      const AddTorrentRequest& request) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(request.torrentBase64);
    params.push_back(request.uris);
    params.push_back(BuildOptionsObject(request.options));
    if (request.position.has_value()) {
        params.push_back(*request.position);
    }
    return DumpRpcCall(id, "aria2.addTorrent", std::move(params));
}

std::string BuildChangeGlobalOptionRpcRequest(std::string_view id,
                                              std::string_view secret,
                                              std::span<const EngineOptionPair> options) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(BuildOptionsObject(options));
    return DumpRpcCall(id, "aria2.changeGlobalOption", std::move(params));
}

std::string BuildChangeTaskOptionRpcRequest(std::string_view id,
                                            std::string_view secret,
                                            std::string_view gid,
                                            std::span<const EngineOptionPair> options) {
    nlohmann::json params = MakeTokenArray(secret);
    params.push_back(gid);
    params.push_back(BuildOptionsObject(options));
    return DumpRpcCall(id, "aria2.changeOption", std::move(params));
}

std::string BuildMulticallSyncRequest(std::string_view id,
                                      std::string_view secret,
                                      int waitingLimit,
                                      int stoppedLimit) {
    const nlohmann::json baseTok = MakeTokenArray(secret);
    nlohmann::json waitTok = baseTok;
    waitTok.push_back(0);
    waitTok.push_back(waitingLimit);
    nlohmann::json stopTok = baseTok;
    stopTok.push_back(0);
    stopTok.push_back(stoppedLimit);

    nlohmann::json calls = nlohmann::json::array({
        {{"methodName", "aria2.getGlobalStat"}, {"params", baseTok}},
        {{"methodName", "aria2.tellActive"}, {"params", baseTok}},
        {{"methodName", "aria2.tellWaiting"}, {"params", std::move(waitTok)}},
        {{"methodName", "aria2.tellStopped"}, {"params", std::move(stopTok)}},
    });
    nlohmann::json params = nlohmann::json::array();
    params.push_back(std::move(calls));
    return DumpRpcCall(id, "system.multicall", std::move(params));
}

}  // namespace motrix::engine
