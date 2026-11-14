module;

#include <nlohmann/json.hpp>

module motrix.engine;

import std;
import motrix.core;
import motrix.domain;

namespace motrix::engine {
namespace {

[[nodiscard]] int ParseIntField(const nlohmann::json& obj, const char* key, int fallback = 0) noexcept {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (it->is_number_integer()) {
        return it->get<int>();
    }
    if (it->is_string()) {
        const auto& s = it->get_ref<const std::string&>();
        int val = fallback;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
        if (ec == std::errc{} && ptr == s.data() + s.size()) {
            return val;
        }
    }
    return fallback;
}

[[nodiscard]] std::string ReadStr(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get_ref<const std::string&>();
    }
    return {};
}

[[nodiscard]] EngineVersionInfo ParseVersionObject(const nlohmann::json& obj) {
    EngineVersionInfo info{};
    info.product = ReadStr(obj, "product");
    info.version = ReadStr(obj, "version");

    const auto readArr = [&](const char* key, std::vector<std::string>& dest) {
        const auto it = obj.find(key);
        if (it == obj.end() || !it->is_array()) {
            return;
        }
        for (const auto& item : *it) {
            if (item.is_string()) {
                dest.push_back(item.get_ref<const std::string&>());
            }
        }
    };
    readArr("enabledFeatures", info.enabledFeatures);
    readArr("mediaFeatures", info.mediaFeatures);
    readArr("downloadFeatures", info.downloadFeatures);

    if (info.product.empty() && (!info.mediaFeatures.empty() || !info.downloadFeatures.empty())) {
        info.product = "aria2-next";
    }
    info.kind = DetectEngineKind(info.product, info.version);
    if (info.product.empty() && info.kind == EngineKind::ClassicAria2) {
        info.product = "aria2";
    }
    return info;
}

[[nodiscard]] EngineEventKind MapNotificationEvent(std::string_view method) noexcept {
    if (method == "aria2.onDownloadStart") {
        return EngineEventKind::DownloadStart;
    }
    if (method == "aria2.onDownloadPause") {
        return EngineEventKind::DownloadPause;
    }
    if (method == "aria2.onDownloadStop") {
        return EngineEventKind::DownloadStop;
    }
    if (method == "aria2.onDownloadComplete") {
        return EngineEventKind::DownloadComplete;
    }
    if (method == "aria2.onDownloadError") {
        return EngineEventKind::DownloadError;
    }
    if (method == "aria2.onBtDownloadComplete") {
        return EngineEventKind::BtDownloadComplete;
    }
    return EngineEventKind::Connected;
}

}  // namespace

motrix::core::Result<RpcParsedFrame> ParseRpcFrame(std::string_view raw) {
    const auto j = nlohmann::json::parse(raw.begin(), raw.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid JSON-RPC frame"));
    }

    RpcParsedFrame frame{};
    if (const auto idIt = j.find("id"); idIt != j.end() && !idIt->is_null()) {
        frame.id = idIt->is_string() ? idIt->get_ref<const std::string&>() : idIt->dump();
        if (const auto errIt = j.find("error"); errIt != j.end() && errIt->is_object()) {
            frame.hasError = true;
            frame.errorCode = ParseIntField(*errIt, "code", -1);
            frame.errorMessage = ReadStr(*errIt, "message");
            return frame;
        }
        if (const auto resIt = j.find("result"); resIt != j.end()) {
            frame.resultJson = resIt->dump();
        } else {
            frame.resultJson = "null";
        }
        return frame;
    }

    const std::string method = ReadStr(j, "method");
    if (!method.empty()) {
        frame.isNotification = true;
        frame.notification.kind = MapNotificationEvent(method);
        if (const auto pIt = j.find("params"); pIt != j.end() && pIt->is_array() && !pIt->empty()) {
            const auto& first = (*pIt)[0];
            if (first.is_object()) {
                frame.notification.gid = ReadStr(first, "gid");
            }
        }
        return frame;
    }
    return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                   motrix::core::ErrorCode::ParseFailure,
                                                   "JSON-RPC frame missing id and method"));
}

motrix::core::Result<EngineVersionInfo> ParseEngineVersionJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid getVersion JSON"));
    }
    return ParseVersionObject(j);
}

motrix::core::Result<std::string> ParseStringResultJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_string()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Expected string JSON-RPC result"));
    }
    return j.get_ref<const std::string&>();
}

}  // namespace motrix::engine
