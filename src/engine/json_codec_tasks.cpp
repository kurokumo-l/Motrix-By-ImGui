module;

#include <nlohmann/json.hpp>

module motrix.engine;

import std;
import motrix.core;
import motrix.domain;

namespace motrix::engine {
namespace {

constexpr std::size_t kMaxTaskFilesInSnapshot = 64;

[[nodiscard]] std::uint64_t
ParseUInt64Field(const nlohmann::json& obj, const char* key, std::uint64_t fallback = 0) noexcept {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }
    if (it->is_number_integer()) {
        const auto v = it->get<std::int64_t>();
        return v > 0 ? static_cast<std::uint64_t>(v) : 0;
    }
    if (it->is_string()) {
        const auto& s = it->get_ref<const std::string&>();
        std::uint64_t val = fallback;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
        if (ec == std::errc{} && ptr == s.data() + s.size()) {
            return val;
        }
    }
    return fallback;
}

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
        return it->get<std::string>();
    }
    return {};
}

[[nodiscard]] bool ReadBoolStrOrBool(const nlohmann::json& obj, const char* key, bool fallback = false) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return fallback;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_string()) {
        const auto& s = it->get_ref<const std::string&>();
        return s == "true" || s == "1";
    }
    return fallback;
}

[[nodiscard]] motrix::domain::TaskStatus ParseTaskStatus(std::string_view s) noexcept {
    using motrix::domain::TaskStatus;
    if (s == "active") {
        return TaskStatus::Active;
    }
    if (s == "waiting") {
        return TaskStatus::Waiting;
    }
    if (s == "paused") {
        return TaskStatus::Paused;
    }
    if (s == "complete") {
        return TaskStatus::Complete;
    }
    if (s == "error") {
        return TaskStatus::Error;
    }
    if (s == "removed") {
        return TaskStatus::Removed;
    }
    return TaskStatus::Waiting;
}

[[nodiscard]] std::string ExtractUriDisplayName(std::string_view uri) {
    if (motrix::domain::IsMagnetUri(uri)) {
        auto dn = motrix::domain::ExtractMagnetDisplayName(uri);
        if (!dn.empty()) {
            return dn;
        }
    }
    const std::size_t q = uri.find_first_of("?#");
    const std::string_view clean = (q == std::string_view::npos) ? uri : uri.substr(0, q);
    const std::size_t slash = clean.find_last_of('/');
    if (slash != std::string_view::npos && slash + 1 < clean.size()) {
        return std::string(clean.substr(slash + 1));
    }
    return std::string(uri);
}

[[nodiscard]] std::string ExtractBasename(std::string_view path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos != std::string_view::npos && pos + 1 < path.size()) {
        return std::string(path.substr(pos + 1));
    }
    return std::string(path);
}

[[nodiscard]] EngineGlobalStat ParseGlobalStatObject(const nlohmann::json& obj) {
    EngineGlobalStat stat{};
    stat.downloadSpeed = ParseUInt64Field(obj, "downloadSpeed");
    stat.uploadSpeed = ParseUInt64Field(obj, "uploadSpeed");
    stat.numActive = ParseIntField(obj, "numActive");
    stat.numWaiting = ParseIntField(obj, "numWaiting");
    stat.numStopped = ParseIntField(obj, "numStopped");
    return stat;
}

[[nodiscard]] motrix::domain::TaskFileView ParseFileObject(const nlohmann::json& f,
                                                           std::string& firstUri) {
    motrix::domain::TaskFileView fv{};
    if (firstUri.empty()) {
        if (const auto urisIt = f.find("uris");
            urisIt != f.end() && urisIt->is_array() && !urisIt->empty()) {
            const auto& u0 = (*urisIt)[0];
            if (u0.is_object()) {
                firstUri = ReadStr(u0, "uri");
            }
        }
    }
    fv.index = ParseIntField(f, "index");
    fv.path = ReadStr(f, "path");
    fv.length = ParseUInt64Field(f, "length");
    fv.completedLength = ParseUInt64Field(f, "completedLength");
    fv.selected = ReadBoolStrOrBool(f, "selected", true);
    return fv;
}

[[nodiscard]] std::vector<std::string> ParseAnnounceList(const nlohmann::json& btObj) {
    std::vector<std::string> trackers;
    const auto annIt = btObj.find("announceList");
    if (annIt == btObj.end() || !annIt->is_array()) {
        return trackers;
    }
    for (const auto& tier : *annIt) {
        if (tier.is_array()) {
            for (const auto& url : tier) {
                if (!url.is_string()) {
                    continue;
                }
                const auto& u = url.get_ref<const std::string&>();
                if (!u.empty() && std::ranges::find(trackers, u) == trackers.end()) {
                    trackers.push_back(u);
                }
            }
        } else if (tier.is_string()) {
            const auto& u = tier.get_ref<const std::string&>();
            if (!u.empty() && std::ranges::find(trackers, u) == trackers.end()) {
                trackers.push_back(u);
            }
        }
    }
    return trackers;
}

[[nodiscard]] motrix::domain::DownloadTaskView ParseTaskObject(const nlohmann::json& obj,
                                                               bool truncateFiles) {
    using namespace motrix::domain;
    DownloadTaskView task{};
    task.gid = ReadStr(obj, "gid");
    task.status = ParseTaskStatus(ReadStr(obj, "status"));
    task.totalLength = ParseUInt64Field(obj, "totalLength");
    task.completedLength = ParseUInt64Field(obj, "completedLength");
    task.uploadLength = ParseUInt64Field(obj, "uploadLength");
    task.downloadSpeed = ParseUInt64Field(obj, "downloadSpeed");
    task.uploadSpeed = ParseUInt64Field(obj, "uploadSpeed");
    task.connections = ParseIntField(obj, "connections");
    task.numSeeders = ParseIntField(obj, "numSeeders");
    task.isSeeding = ReadBoolStrOrBool(obj, "seeder", false);
    task.dir = ReadStr(obj, "dir");
    task.bitfieldHex = ReadStr(obj, "bitfield");
    task.numPieces = static_cast<std::uint32_t>(ParseUInt64Field(obj, "numPieces"));
    task.pieceLength = ParseUInt64Field(obj, "pieceLength");
    task.errorCode = ParseIntField(obj, "errorCode");
    task.errorMessage = ReadStr(obj, "errorMessage");

    const std::string infoHash = ReadStr(obj, "infoHash");
    if (const auto btIt = obj.find("bittorrent"); btIt != obj.end() && btIt->is_object()) {
        task.taskType = TaskType::BitTorrent;
        TaskBittorrentInfo bt{};
        bt.infoHash = infoHash;
        bt.comment = ReadStr(*btIt, "comment");
        const std::uint64_t createdSec = ParseUInt64Field(*btIt, "creationDate");
        bt.creationDate = FormatUnixTimestampUtc(static_cast<std::int64_t>(createdSec));
        if (const auto infoIt = btIt->find("info"); infoIt != btIt->end() && infoIt->is_object()) {
            bt.name = ReadStr(*infoIt, "name");
        }
        task.btInfo = std::move(bt);
    } else if (!infoHash.empty()) {
        task.taskType = TaskType::BitTorrent;
    }

    if (const auto filesIt = obj.find("files"); filesIt != obj.end() && filesIt->is_array()) {
        task.filesTotal = filesIt->size();
        if (!truncateFiles) {
            task.files.reserve(filesIt->size());
            for (const auto& f : *filesIt) {
                if (f.is_object()) {
                    task.files.push_back(ParseFileObject(f, task.uri));
                }
            }
        } else {
            std::vector<TaskFileView> selected;
            std::vector<TaskFileView> unselected;
            for (const auto& f : *filesIt) {
                if (!f.is_object()) {
                    continue;
                }
                TaskFileView fv = ParseFileObject(f, task.uri);
                if (fv.selected) {
                    if (selected.size() < kMaxTaskFilesInSnapshot) {
                        selected.push_back(std::move(fv));
                    }
                } else if (selected.size() + unselected.size() < kMaxTaskFilesInSnapshot) {
                    unselected.push_back(std::move(fv));
                }
            }
            task.files = std::move(selected);
            for (auto& u : unselected) {
                if (task.files.size() >= kMaxTaskFilesInSnapshot) {
                    break;
                }
                task.files.push_back(std::move(u));
            }
        }
    }

    if (task.uri.empty() && !infoHash.empty()) {
        task.uri = std::format("magnet:?xt=urn:btih:{}", infoHash);
    }
    if (task.btInfo && !task.btInfo->name.empty()) {
        task.name = task.btInfo->name;
    } else if (!task.files.empty() && !task.files[0].path.empty()) {
        task.name = ExtractBasename(task.files[0].path);
    } else if (!task.uri.empty()) {
        task.name = ExtractUriDisplayName(task.uri);
    } else {
        task.name = task.gid;
    }
    return task;
}

[[nodiscard]] std::vector<motrix::domain::DownloadTaskView> ParseTaskArray(const nlohmann::json& arr) {
    std::vector<motrix::domain::DownloadTaskView> out;
    if (!arr.is_array()) {
        return out;
    }
    out.reserve(arr.size());
    for (const auto& item : arr) {
        if (item.is_object()) {
            out.push_back(ParseTaskObject(item, true));
        }
    }
    return out;
}

}  // namespace

motrix::core::Result<EngineGlobalStat> ParseGlobalStatJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid getGlobalStat JSON"));
    }
    return ParseGlobalStatObject(j);
}

motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>>
ParseTaskListJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid task list JSON"));
    }
    return ParseTaskArray(j);
}

motrix::core::Result<EngineSyncSnapshot> ParseMulticallSyncJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_array() || j.size() < 4) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid system.multicall response"));
    }
    EngineSyncSnapshot snap{};
    if (j[0].is_array() && !j[0].empty() && j[0][0].is_object()) {
        snap.globalStat = ParseGlobalStatObject(j[0][0]);
    }
    if (j[1].is_array() && !j[1].empty()) {
        snap.active = ParseTaskArray(j[1][0]);
    }
    if (j[2].is_array() && !j[2].empty()) {
        snap.waiting = ParseTaskArray(j[2][0]);
    }
    if (j[3].is_array() && !j[3].empty()) {
        snap.stopped = ParseTaskArray(j[3][0]);
    }
    return snap;
}

motrix::core::Result<TaskDetailResult> ParseTaskDetailJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid tellStatus JSON"));
    }
    TaskDetailResult detail{};
    detail.task = ParseTaskObject(j, false);
    if (const auto btIt = j.find("bittorrent"); btIt != j.end() && btIt->is_object()) {
        detail.trackers = ParseAnnounceList(*btIt);
    }
    return detail;
}

motrix::core::Result<std::vector<motrix::domain::TaskFileView>>
ParseTaskFilesJson(std::string_view resultJson) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid getFiles JSON"));
    }
    std::vector<motrix::domain::TaskFileView> files;
    files.reserve(j.size());
    std::string dummyUri;
    for (const auto& f : j) {
        if (f.is_object()) {
            files.push_back(ParseFileObject(f, dummyUri));
        }
    }
    return files;
}

motrix::core::Result<std::vector<motrix::domain::TaskPeerView>>
ParsePeersJson(std::string_view resultJson, std::uint32_t numPieces) {
    const auto j = nlohmann::json::parse(resultJson.begin(), resultJson.end(), nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid getPeers JSON"));
    }
    std::vector<motrix::domain::TaskPeerView> peers;
    peers.reserve(j.size());
    for (const auto& p : j) {
        if (!p.is_object()) {
            continue;
        }
        motrix::domain::TaskPeerView peer{};
        peer.peerId = ReadStr(p, "peerId");
        peer.client = motrix::domain::DecodePeerId(peer.peerId);
        peer.ip = ReadStr(p, "ip");
        peer.port = static_cast<std::uint16_t>(std::clamp(ParseIntField(p, "port"), 0, 65535));
        peer.bitfieldHex = ReadStr(p, "bitfield");
        peer.downloadSpeed = ParseUInt64Field(p, "downloadSpeed");
        peer.uploadSpeed = ParseUInt64Field(p, "uploadSpeed");
        peer.amChoking = ReadBoolStrOrBool(p, "amChoking", false);
        peer.peerChoking = ReadBoolStrOrBool(p, "peerChoking", false);
        peer.seeder = ReadBoolStrOrBool(p, "seeder", false);
        peer.progress =
            peer.seeder ? 1.0f : motrix::domain::BitfieldCompletion(peer.bitfieldHex, numPieces);
        peers.push_back(std::move(peer));
    }
    return peers;
}

}  // namespace motrix::engine
