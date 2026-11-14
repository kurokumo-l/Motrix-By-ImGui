module;

#include <nlohmann/json.hpp>

module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

void ReadBool(const nlohmann::json& j, const char* key, bool& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_boolean()) {
        out = it->get<bool>();
    }
}

void ReadInt(const nlohmann::json& j, const char* key, int& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (it->is_number_unsigned()) {
        const auto val = it->get<std::uint64_t>();
        if (val <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            out = static_cast<int>(val);
        }
    } else if (it->is_number_integer()) {
        const auto val = it->get<std::int64_t>();
        if (val >= (std::numeric_limits<int>::min)() && val <= (std::numeric_limits<int>::max)()) {
            out = static_cast<int>(val);
        }
    }
}

void ReadOptionalInt(const nlohmann::json& j, const char* key, std::optional<int>& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (it->is_null()) {
        out.reset();
        return;
    }
    int parsedVal = 0;
    if (it->is_number_unsigned()) {
        const auto val = it->get<std::uint64_t>();
        if (val <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            parsedVal = static_cast<int>(val);
            out = parsedVal;
        }
    } else if (it->is_number_integer()) {
        const auto val = it->get<std::int64_t>();
        if (val >= (std::numeric_limits<int>::min)() && val <= (std::numeric_limits<int>::max)()) {
            parsedVal = static_cast<int>(val);
            out = parsedVal;
        }
    }
}

void ReadInt64(const nlohmann::json& j, const char* key, std::int64_t& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (it->is_number_unsigned()) {
        const auto val = it->get<std::uint64_t>();
        constexpr auto kMaxI64 = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
        if (val <= kMaxI64) {
            out = static_cast<std::int64_t>(val);
        }
    } else if (it->is_number_integer()) {
        out = it->get<std::int64_t>();
    }
}

void ReadUInt16(const nlohmann::json& j, const char* key, std::uint16_t& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        const auto val = it->get<std::uint64_t>();
        if (val <= 65535u) {
            out = static_cast<std::uint16_t>(val);
        }
    }
}

void ReadUInt64(const nlohmann::json& j, const char* key, std::uint64_t& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
        out = it->get<std::uint64_t>();
    }
}

void ReadDouble(const nlohmann::json& j, const char* key, double& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_number()) {
        out = it->get<double>();
    }
}

void ReadString(const nlohmann::json& j, const char* key, std::string& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_string()) {
        out = it->get<std::string>();
    }
}

void ReadStringArray(const nlohmann::json& j, const char* key, std::vector<std::string>& out) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array()) {
        return;
    }
    out.clear();
    for (const auto& item : *it) {
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
}

}  // namespace

UserPreferences DefaultUserPreferences() {
    UserPreferences prefs{};
    prefs.trackerSource = DefaultTrackerSources();
    return prefs;
}

SystemConfig DefaultSystemConfig() {
    return SystemConfig{};
}

std::vector<std::pair<std::string, std::string>> SystemConfigToOptionPairs(const SystemConfig& cfg) {
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back("max-concurrent-downloads", std::to_string(cfg.maxConcurrentDownloads));
    out.emplace_back("max-connection-per-server", std::to_string(cfg.maxConnectionPerServer));
    out.emplace_back("split", std::to_string(cfg.split));
    out.emplace_back("listen-port", std::to_string(cfg.listenPort));
    out.emplace_back("dht-listen-port", std::to_string(cfg.dhtListenPort));
    out.emplace_back("seed-ratio", std::format("{:.2f}", cfg.seedRatio));
    out.emplace_back("seed-time", std::to_string(cfg.seedTime));
    out.emplace_back("max-overall-download-limit", std::to_string(cfg.maxOverallDownloadLimit));
    out.emplace_back("max-overall-upload-limit", std::to_string(cfg.maxOverallUploadLimit));
    out.emplace_back("continue", cfg.continueDownloads ? "true" : "false");
    out.emplace_back("follow-torrent", cfg.followTorrent ? "true" : "false");
    out.emplace_back("bt-save-metadata", cfg.btSaveMetadata ? "true" : "false");
    out.emplace_back("pause-metadata", cfg.pauseMetadata ? "true" : "false");
    if (cfg.btForceEncryption) {
        out.emplace_back("bt-require-crypto", "true");
        out.emplace_back("bt-min-crypto-level", "arc4");
    } else {
        out.emplace_back("bt-require-crypto", "false");
    }
    if (!cfg.dir.empty()) {
        out.emplace_back("dir", cfg.dir);
    }
    if (!cfg.btTracker.empty()) {
        out.emplace_back("bt-tracker", cfg.btTracker);
    }
    if (!cfg.allProxy.empty()) {
        out.emplace_back("all-proxy", cfg.allProxy);
    }
    if (!cfg.noProxy.empty()) {
        out.emplace_back("no-proxy", cfg.noProxy);
    }
    if (!cfg.userAgent.empty()) {
        out.emplace_back("user-agent", cfg.userAgent);
    }
    return out;
}

std::string SerializeUserPreferencesJson(const UserPreferences& prefs) {
    nlohmann::json windowState = {
        {"width", prefs.windowWidth},
        {"height", prefs.windowHeight},
        {"maximized", prefs.windowMaximized},
    };
    if (prefs.windowX.has_value()) {
        windowState["x"] = *prefs.windowX;
    }
    if (prefs.windowY.has_value()) {
        windowState["y"] = *prefs.windowY;
    }

    nlohmann::json j = {
        {"theme",
         prefs.theme == ThemePreference::Dark
             ? "dark"
             : (prefs.theme == ThemePreference::Auto ? "auto" : "light")},
        {"locale", prefs.locale == motrix::core::Locale::ZhCN ? "zh-CN" : "en-US"},
        {"runMode", static_cast<int>(prefs.runMode)},
        {"hideAppMenu", prefs.hideAppMenu},
        {"keepWindowState", prefs.keepWindowState},
        {"autoHideWindow", prefs.autoHideWindow},
        {"openAtLogin", prefs.openAtLogin},
        {"resumeAllWhenAppLaunched", prefs.resumeAllWhenAppLaunched},
        {"newTaskShowDownloading", prefs.newTaskShowDownloading},
        {"noConfirmBeforeDeleteTask", prefs.noConfirmBeforeDeleteTask},
        {"showProgressBar", prefs.showProgressBar},
        {"taskNotification", prefs.taskNotification},
        {"traySpeedometer", prefs.traySpeedometer},
        {"enableUpnp", prefs.enableUpnp},
        {"autoSyncTracker", prefs.autoSyncTracker},
        {"useProxy", prefs.useProxy},
        {"keepSeeding", prefs.keepSeeding},
        {"autoCheckUpdate", prefs.autoCheckUpdate},
        {"lastCheckUpdateTime", prefs.lastCheckUpdateTime},
        {"engineBinPath", prefs.engineBinPath},
        {"logLevel", prefs.logLevel},
        {"favoriteDirectories", prefs.favoriteDirectories},
        {"historyDirectories", prefs.historyDirectories},
        {"trackerSource", prefs.trackerSource},
        {"lastSyncTrackerTime", prefs.lastSyncTrackerTime},
        {"protocols",
         {
             {"magnet", prefs.protocolMagnet},
             {"thunder", prefs.protocolThunder},
             {"motrix", prefs.protocolMotrix},
         }},
        {"windowState", windowState},
    };
    return j.dump(2);
}

motrix::core::Result<UserPreferences> ParseUserPreferencesJson(std::string_view jsonText) {
    const auto parsed = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid UserPreferences JSON"));
    }

    UserPreferences prefs = DefaultUserPreferences();
    std::string themeStr;
    ReadString(parsed, "theme", themeStr);
    if (themeStr == "dark") {
        prefs.theme = ThemePreference::Dark;
    } else if (themeStr == "auto") {
        prefs.theme = ThemePreference::Auto;
    } else if (themeStr == "light") {
        prefs.theme = ThemePreference::Light;
    }

    std::string localeStr;
    ReadString(parsed, "locale", localeStr);
    if (localeStr == "zh-CN" || localeStr == "zh") {
        prefs.locale = motrix::core::Locale::ZhCN;
    } else if (!localeStr.empty()) {
        prefs.locale = motrix::core::Locale::EnUS;
    }

    int runModeVal = static_cast<int>(prefs.runMode);
    ReadInt(parsed, "runMode", runModeVal);
    if (runModeVal >= 1 && runModeVal <= 3) {
        prefs.runMode = static_cast<RunMode>(runModeVal);
    }

    ReadBool(parsed, "hideAppMenu", prefs.hideAppMenu);
    ReadBool(parsed, "keepWindowState", prefs.keepWindowState);
    ReadBool(parsed, "autoHideWindow", prefs.autoHideWindow);
    ReadBool(parsed, "openAtLogin", prefs.openAtLogin);
    ReadBool(parsed, "resumeAllWhenAppLaunched", prefs.resumeAllWhenAppLaunched);
    ReadBool(parsed, "newTaskShowDownloading", prefs.newTaskShowDownloading);
    ReadBool(parsed, "noConfirmBeforeDeleteTask", prefs.noConfirmBeforeDeleteTask);
    ReadBool(parsed, "showProgressBar", prefs.showProgressBar);
    ReadBool(parsed, "taskNotification", prefs.taskNotification);
    ReadBool(parsed, "traySpeedometer", prefs.traySpeedometer);
    ReadBool(parsed, "enableUpnp", prefs.enableUpnp);
    ReadBool(parsed, "autoSyncTracker", prefs.autoSyncTracker);
    ReadBool(parsed, "useProxy", prefs.useProxy);
    ReadBool(parsed, "keepSeeding", prefs.keepSeeding);
    ReadBool(parsed, "autoCheckUpdate", prefs.autoCheckUpdate);
    ReadInt64(parsed, "lastCheckUpdateTime", prefs.lastCheckUpdateTime);
    ReadString(parsed, "engineBinPath", prefs.engineBinPath);
    ReadString(parsed, "logLevel", prefs.logLevel);
    ReadStringArray(parsed, "favoriteDirectories", prefs.favoriteDirectories);
    ReadStringArray(parsed, "historyDirectories", prefs.historyDirectories);
    ReadStringArray(parsed, "trackerSource", prefs.trackerSource);
    ReadInt64(parsed, "lastSyncTrackerTime", prefs.lastSyncTrackerTime);

    ReadBool(parsed, "protocolMagnet", prefs.protocolMagnet);
    ReadBool(parsed, "protocolThunder", prefs.protocolThunder);
    ReadBool(parsed, "protocolMotrix", prefs.protocolMotrix);
    if (const auto protoIt = parsed.find("protocols"); protoIt != parsed.end() && protoIt->is_object()) {
        ReadBool(*protoIt, "magnet", prefs.protocolMagnet);
        ReadBool(*protoIt, "thunder", prefs.protocolThunder);
        ReadBool(*protoIt, "motrix", prefs.protocolMotrix);
    }

    if (const auto winIt = parsed.find("windowState"); winIt != parsed.end() && winIt->is_object()) {
        ReadOptionalInt(*winIt, "x", prefs.windowX);
        ReadOptionalInt(*winIt, "y", prefs.windowY);
        ReadInt(*winIt, "width", prefs.windowWidth);
        ReadInt(*winIt, "height", prefs.windowHeight);
        ReadBool(*winIt, "maximized", prefs.windowMaximized);
    }
    return prefs;
}

std::string SerializeSystemConfigJson(const SystemConfig& cfg) {
    nlohmann::json j = {
        {"rpc-listen-port", cfg.rpcListenPort},
        {"rpc-secret", cfg.rpcSecret},
        {"dir", cfg.dir},
        {"max-concurrent-downloads", cfg.maxConcurrentDownloads},
        {"max-connection-per-server", cfg.maxConnectionPerServer},
        {"split", cfg.split},
        {"listen-port", cfg.listenPort},
        {"dht-listen-port", cfg.dhtListenPort},
        {"seed-ratio", cfg.seedRatio},
        {"seed-time", cfg.seedTime},
        {"max-overall-download-limit", cfg.maxOverallDownloadLimit},
        {"max-overall-upload-limit", cfg.maxOverallUploadLimit},
        {"continue", cfg.continueDownloads},
        {"follow-torrent", cfg.followTorrent},
        {"bt-save-metadata", cfg.btSaveMetadata},
        {"bt-force-encryption", cfg.btForceEncryption},
        {"pause-metadata", cfg.pauseMetadata},
        {"bt-tracker", cfg.btTracker},
        {"all-proxy", cfg.allProxy},
        {"no-proxy", cfg.noProxy},
        {"user-agent", cfg.userAgent},
    };
    return j.dump(2);
}

motrix::core::Result<SystemConfig> ParseSystemConfigJson(std::string_view jsonText) {
    const auto parsed = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid SystemConfig JSON"));
    }

    SystemConfig cfg = DefaultSystemConfig();
    ReadUInt16(parsed, "rpc-listen-port", cfg.rpcListenPort);
    ReadString(parsed, "rpc-secret", cfg.rpcSecret);
    ReadString(parsed, "dir", cfg.dir);
    ReadInt(parsed, "max-concurrent-downloads", cfg.maxConcurrentDownloads);
    ReadInt(parsed, "max-connection-per-server", cfg.maxConnectionPerServer);
    ReadInt(parsed, "split", cfg.split);
    ReadInt(parsed, "listen-port", cfg.listenPort);
    ReadInt(parsed, "dht-listen-port", cfg.dhtListenPort);
    ReadDouble(parsed, "seed-ratio", cfg.seedRatio);
    ReadInt(parsed, "seed-time", cfg.seedTime);
    ReadUInt64(parsed, "max-overall-download-limit", cfg.maxOverallDownloadLimit);
    ReadUInt64(parsed, "max-overall-upload-limit", cfg.maxOverallUploadLimit);
    ReadBool(parsed, "continue", cfg.continueDownloads);
    ReadBool(parsed, "follow-torrent", cfg.followTorrent);
    ReadBool(parsed, "bt-save-metadata", cfg.btSaveMetadata);
    ReadBool(parsed, "bt-force-encryption", cfg.btForceEncryption);
    ReadBool(parsed, "pause-metadata", cfg.pauseMetadata);
    ReadString(parsed, "bt-tracker", cfg.btTracker);
    ReadString(parsed, "all-proxy", cfg.allProxy);
    ReadString(parsed, "no-proxy", cfg.noProxy);
    ReadString(parsed, "user-agent", cfg.userAgent);
    return cfg;
}

}  // namespace motrix::domain
