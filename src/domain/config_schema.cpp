module;

#include <nlohmann/json.hpp>

module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

constexpr std::array<std::string_view, 5> kLogLevelOpts{"debug", "info", "warn", "error", "notice"};
constexpr std::array<std::string_view, 4> kBtEncryptionOpts{
    "preferred", "required", "disabled", "optional"};
constexpr std::array<std::string_view, 3> kBtTransportOpts{"both", "tcp", "utp"};
constexpr std::array<std::string_view, 4> kFileAllocOpts{"none", "prealloc", "falloc", "trunc"};
constexpr std::array<std::string_view, 5> kMediaModeOpts{"auto", "file", "hls", "dash", "collection"};
constexpr std::array<std::string_view, 4> kMediaFormatOpts{"mp4", "mkv", "webm", "ts"};

void SanitizeSingleLine(std::string& value) {
    std::erase_if(value, [](char ch) noexcept {
        const auto uch = static_cast<unsigned char>(ch);
        return uch < 0x20u || uch == 0x7Fu;
    });
}

[[nodiscard]] std::string NormalizeEnum(std::string_view raw,
                                        std::span<const std::string_view> allowed,
                                        std::string_view fallback) {
    std::string lower = motrix::core::AsciiLower(motrix::core::TrimAscii(raw));
    for (const auto item : allowed) {
        if (lower == item) {
            return lower;
        }
    }
    return std::string(fallback);
}

void NormalizeUserPreferences(UserPreferences& prefs) {
    SanitizeSingleLine(prefs.engineBinPath);
    SanitizeSingleLine(prefs.aria2NextUpdateUrl);
    if (motrix::core::TrimAscii(prefs.aria2NextUpdateUrl).empty()) {
        prefs.aria2NextUpdateUrl = std::string(kDefaultAria2NextUpdateUrl);
    }
    SanitizeSingleLine(prefs.aria2NextMirrorPrefix);
    SanitizeSingleLine(prefs.fontFamily);
    SanitizeSingleLine(prefs.monoFontFamily);
    prefs.logLevel = NormalizeEnum(prefs.logLevel, kLogLevelOpts, "warn");
    if (prefs.logLevel == "notice") {
        prefs.logLevel = "warn";
    }
    for (auto& d : prefs.favoriteDirectories) {
        SanitizeSingleLine(d);
    }
    for (auto& d : prefs.historyDirectories) {
        SanitizeSingleLine(d);
    }
    for (auto& s : prefs.trackerSource) {
        SanitizeSingleLine(s);
    }
    prefs.windowWidth = (prefs.windowWidth <= 0) ? 1024 : std::clamp(prefs.windowWidth, 478, 16384);
    prefs.windowHeight = (prefs.windowHeight <= 0) ? 768 : std::clamp(prefs.windowHeight, 420, 16384);
}

void NormalizeSystemConfig(SystemConfig& cfg) {
    if (cfg.rpcListenPort == 0) {
        cfg.rpcListenPort = 16800;
    }
    if (cfg.listenPort < 1 || cfg.listenPort > 65535) {
        cfg.listenPort = 21301;
    }
    if (cfg.dhtListenPort < 1 || cfg.dhtListenPort > 65535) {
        cfg.dhtListenPort = 26701;
    }
    cfg.maxConcurrentDownloads = std::clamp(cfg.maxConcurrentDownloads, 1, 1024);
    cfg.maxConnectionPerServer = std::clamp(cfg.maxConnectionPerServer, 1, 1024);
    cfg.split = std::clamp(cfg.split, 1, 1024);
    cfg.btMaxPeers = std::clamp(cfg.btMaxPeers, 0, 2000);
    cfg.seedTime = std::max(0, cfg.seedTime);
    if (!std::isfinite(cfg.seedRatio) || cfg.seedRatio < 0.0) {
        cfg.seedRatio = 1.0;
    }

    cfg.btEncryption = NormalizeEnum(cfg.btEncryption, kBtEncryptionOpts, "preferred");
    if (cfg.btForceEncryption) {
        cfg.btEncryption = "required";
    }
    cfg.btForceEncryption = (cfg.btEncryption == "required");
    cfg.btTransport = NormalizeEnum(cfg.btTransport, kBtTransportOpts, "both");
    cfg.fileAllocation = NormalizeEnum(cfg.fileAllocation, kFileAllocOpts, "none");
    cfg.mediaMode = NormalizeEnum(cfg.mediaMode, kMediaModeOpts, "auto");
    cfg.mediaFormat = NormalizeEnum(cfg.mediaFormat, kMediaFormatOpts, "mp4");
    cfg.btSaveMetadata = cfg.rpcSaveUploadMetadata;

    SanitizeSingleLine(cfg.rpcSecret);
    SanitizeSingleLine(cfg.dir);
    SanitizeSingleLine(cfg.btTracker);
    SanitizeSingleLine(cfg.allProxy);
    SanitizeSingleLine(cfg.noProxy);
    SanitizeSingleLine(cfg.userAgent);
}

void ApplyEncryptionFromJson(const nlohmann::json& parsed, SystemConfig& cfg) {
    bool hasForceEnc = false;
    bool forceEncVal = false;
    if (const auto it = parsed.find("bt-force-encryption"); it != parsed.end() && it->is_boolean()) {
        hasForceEnc = true;
        forceEncVal = it->get<bool>();
    }
    if (const auto it = parsed.find("bt-encryption"); it != parsed.end() && it->is_string()) {
        cfg.btEncryption =
            NormalizeEnum(it->get_ref<const std::string&>(), kBtEncryptionOpts, "preferred");
    } else if (hasForceEnc && forceEncVal) {
        cfg.btEncryption = "required";
    }
    cfg.btForceEncryption = (cfg.btEncryption == "required");
}

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
        out = it->get_ref<const std::string&>();
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
            out.push_back(item.get_ref<const std::string&>());
        }
    }
}

}  // namespace

UserPreferences DefaultUserPreferences() {
    UserPreferences prefs{};
    prefs.trackerSource = DefaultTrackerSources();
    return prefs;
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
        {"keepWindowState", prefs.keepWindowState},
        {"autoHideWindow", prefs.autoHideWindow},
        {"openAtLogin", prefs.openAtLogin},
        {"resumeAllWhenAppLaunched", prefs.resumeAllWhenAppLaunched},
        {"newTaskShowDownloading", prefs.newTaskShowDownloading},
        {"noConfirmBeforeDeleteTask", prefs.noConfirmBeforeDeleteTask},
        {"deleteFilesWhenDeleteTask", prefs.deleteFilesWhenDeleteTask},
        {"showProgressBar", prefs.showProgressBar},
        {"taskNotification", prefs.taskNotification},
        {"traySpeedometer", prefs.traySpeedometer},
        {"showTooltips", prefs.showTooltips},
        {"enableUpnp", prefs.enableUpnp},
        {"autoSyncTracker", prefs.autoSyncTracker},
        {"useProxy", prefs.useProxy},
        {"keepSeeding", prefs.keepSeeding},
        {"engineBinPath", prefs.engineBinPath},
        {"aria2NextUpdateUrl",
         prefs.aria2NextUpdateUrl.empty() ? std::string(kDefaultAria2NextUpdateUrl)
                                          : prefs.aria2NextUpdateUrl},
        {"aria2NextMirrorPrefix", prefs.aria2NextMirrorPrefix},
        {"fontFamily", prefs.fontFamily},
        {"monoFontFamily", prefs.monoFontFamily},
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
             {"torrent", prefs.protocolTorrent},
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

    ReadBool(parsed, "keepWindowState", prefs.keepWindowState);
    ReadBool(parsed, "autoHideWindow", prefs.autoHideWindow);
    ReadBool(parsed, "openAtLogin", prefs.openAtLogin);
    ReadBool(parsed, "resumeAllWhenAppLaunched", prefs.resumeAllWhenAppLaunched);
    ReadBool(parsed, "newTaskShowDownloading", prefs.newTaskShowDownloading);
    ReadBool(parsed, "noConfirmBeforeDeleteTask", prefs.noConfirmBeforeDeleteTask);
    ReadBool(parsed, "deleteFilesWhenDeleteTask", prefs.deleteFilesWhenDeleteTask);
    ReadBool(parsed, "showProgressBar", prefs.showProgressBar);
    ReadBool(parsed, "taskNotification", prefs.taskNotification);
    ReadBool(parsed, "traySpeedometer", prefs.traySpeedometer);
    ReadBool(parsed, "showTooltips", prefs.showTooltips);
    ReadBool(parsed, "enableUpnp", prefs.enableUpnp);
    ReadBool(parsed, "autoSyncTracker", prefs.autoSyncTracker);
    ReadBool(parsed, "useProxy", prefs.useProxy);
    ReadBool(parsed, "keepSeeding", prefs.keepSeeding);
    ReadString(parsed, "engineBinPath", prefs.engineBinPath);
    ReadString(parsed, "aria2NextUpdateUrl", prefs.aria2NextUpdateUrl);
    if (motrix::core::TrimAscii(prefs.aria2NextUpdateUrl).empty()) {
        prefs.aria2NextUpdateUrl = std::string(kDefaultAria2NextUpdateUrl);
    }
    ReadString(parsed, "aria2NextMirrorPrefix", prefs.aria2NextMirrorPrefix);
    ReadString(parsed, "fontFamily", prefs.fontFamily);
    ReadString(parsed, "monoFontFamily", prefs.monoFontFamily);
    ReadString(parsed, "logLevel", prefs.logLevel);
    ReadStringArray(parsed, "favoriteDirectories", prefs.favoriteDirectories);
    ReadStringArray(parsed, "historyDirectories", prefs.historyDirectories);
    ReadStringArray(parsed, "trackerSource", prefs.trackerSource);
    ReadInt64(parsed, "lastSyncTrackerTime", prefs.lastSyncTrackerTime);

    ReadBool(parsed, "protocolMagnet", prefs.protocolMagnet);
    ReadBool(parsed, "protocolThunder", prefs.protocolThunder);
    ReadBool(parsed, "protocolMotrix", prefs.protocolMotrix);
    ReadBool(parsed, "protocolTorrent", prefs.protocolTorrent);
    if (const auto protoIt = parsed.find("protocols"); protoIt != parsed.end() && protoIt->is_object()) {
        ReadBool(*protoIt, "magnet", prefs.protocolMagnet);
        ReadBool(*protoIt, "thunder", prefs.protocolThunder);
        ReadBool(*protoIt, "motrix", prefs.protocolMotrix);
        ReadBool(*protoIt, "torrent", prefs.protocolTorrent);
    }

    if (const auto winIt = parsed.find("windowState"); winIt != parsed.end() && winIt->is_object()) {
        ReadOptionalInt(*winIt, "x", prefs.windowX);
        ReadOptionalInt(*winIt, "y", prefs.windowY);
        ReadInt(*winIt, "width", prefs.windowWidth);
        ReadInt(*winIt, "height", prefs.windowHeight);
        ReadBool(*winIt, "maximized", prefs.windowMaximized);
    }
    NormalizeUserPreferences(prefs);
    return prefs;
}

std::string SerializeSystemConfigJson(const SystemConfig& cfg) {
    const std::string normEnc = NormalizeEnum(cfg.btEncryption, kBtEncryptionOpts, "preferred");
    const bool forceEnc = cfg.btForceEncryption || (normEnc == "required");
    const std::string effectiveEnc = forceEnc ? "required" : normEnc;

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
        {"rpc-save-upload-metadata", cfg.rpcSaveUploadMetadata},
        {"bt-force-encryption", forceEnc},
        {"bt-encryption", effectiveEnc},
        {"bt-transport", cfg.btTransport},
        {"file-allocation", cfg.fileAllocation},
        {"auto-file-renaming", cfg.autoFileRenaming},
        {"allow-overwrite", cfg.allowOverwrite},
        {"check-integrity", cfg.checkIntegrity},
        {"enable-dht", cfg.enableDht},
        {"enable-peer-exchange", cfg.enablePeerExchange},
        {"bt-enable-lpd", cfg.btEnableLpd},
        {"bt-anonymous-mode", cfg.btAnonymousMode},
        {"bt-max-peers", cfg.btMaxPeers},
        {"check-certificate", cfg.checkCertificate},
        {"disable-ipv6", cfg.disableIpv6},
        {"remote-time", cfg.remoteTime},
        {"media", cfg.mediaMode},
        {"media-format", cfg.mediaFormat},
        {"bt-first-last-piece-first", cfg.btFirstLastPieceFirst},
        {"detach-share-only", cfg.detachShareOnly},
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
    cfg.rpcSaveUploadMetadata = cfg.btSaveMetadata;
    ReadBool(parsed, "rpc-save-upload-metadata", cfg.rpcSaveUploadMetadata);
    cfg.btSaveMetadata = cfg.rpcSaveUploadMetadata;
    ApplyEncryptionFromJson(parsed, cfg);
    ReadString(parsed, "bt-transport", cfg.btTransport);
    ReadString(parsed, "file-allocation", cfg.fileAllocation);
    ReadBool(parsed, "auto-file-renaming", cfg.autoFileRenaming);
    ReadBool(parsed, "allow-overwrite", cfg.allowOverwrite);
    ReadBool(parsed, "check-integrity", cfg.checkIntegrity);
    ReadBool(parsed, "enable-dht", cfg.enableDht);
    ReadBool(parsed, "enable-peer-exchange", cfg.enablePeerExchange);
    ReadBool(parsed, "bt-enable-lpd", cfg.btEnableLpd);
    ReadBool(parsed, "bt-anonymous-mode", cfg.btAnonymousMode);
    ReadInt(parsed, "bt-max-peers", cfg.btMaxPeers);
    ReadBool(parsed, "check-certificate", cfg.checkCertificate);
    ReadBool(parsed, "disable-ipv6", cfg.disableIpv6);
    ReadBool(parsed, "remote-time", cfg.remoteTime);
    ReadString(parsed, "media", cfg.mediaMode);
    ReadString(parsed, "media-format", cfg.mediaFormat);
    ReadBool(parsed, "bt-first-last-piece-first", cfg.btFirstLastPieceFirst);
    ReadBool(parsed, "detach-share-only", cfg.detachShareOnly);
    ReadBool(parsed, "pause-metadata", cfg.pauseMetadata);
    ReadString(parsed, "bt-tracker", cfg.btTracker);
    ReadString(parsed, "all-proxy", cfg.allProxy);
    ReadString(parsed, "no-proxy", cfg.noProxy);
    ReadString(parsed, "user-agent", cfg.userAgent);
    NormalizeSystemConfig(cfg);
    return cfg;
}

SystemConfig DefaultSystemConfig() {
    return SystemConfig{};
}

SystemConfig BuildEffectiveSystemConfig(const SystemConfig& cfg,
                                        const UserPreferences& prefs,
                                        bool suspendSpeedLimit) {
    SystemConfig out = cfg;
    if (!prefs.useProxy) {
        out.allProxy.clear();
        out.noProxy.clear();
    }
    if (prefs.keepSeeding) {
        out.seedRatio = 0.0;
        out.seedTime = 0;
    }
    NormalizeSystemConfig(out);
    if (suspendSpeedLimit) {
        out.maxOverallDownloadLimit = 0;
        out.maxOverallUploadLimit = 0;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> SystemConfigToOptionPairs(const SystemConfig& cfg) {
    SystemConfig norm = cfg;
    NormalizeSystemConfig(norm);

    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(36);
    out.emplace_back("max-concurrent-downloads", std::to_string(norm.maxConcurrentDownloads));
    out.emplace_back("max-connection-per-server", std::to_string(norm.maxConnectionPerServer));
    out.emplace_back("split", std::to_string(norm.split));
    out.emplace_back("listen-port", std::to_string(norm.listenPort));
    out.emplace_back("dht-listen-port", std::to_string(norm.dhtListenPort));
    out.emplace_back("seed-ratio", std::format("{:.2f}", norm.seedRatio));
    out.emplace_back("seed-time", std::to_string(norm.seedTime));
    out.emplace_back("max-overall-download-limit", std::to_string(norm.maxOverallDownloadLimit));
    out.emplace_back("max-overall-upload-limit", std::to_string(norm.maxOverallUploadLimit));
    out.emplace_back("continue", norm.continueDownloads ? "true" : "false");
    out.emplace_back("follow-torrent", norm.followTorrent ? "true" : "false");
    out.emplace_back("rpc-save-upload-metadata", norm.rpcSaveUploadMetadata ? "true" : "false");
    out.emplace_back("pause-metadata", norm.pauseMetadata ? "true" : "false");
    out.emplace_back("bt-encryption", norm.btEncryption);
    out.emplace_back("bt-transport", norm.btTransport);
    out.emplace_back("file-allocation", norm.fileAllocation);
    out.emplace_back("auto-file-renaming", norm.autoFileRenaming ? "true" : "false");
    out.emplace_back("allow-overwrite", norm.allowOverwrite ? "true" : "false");
    out.emplace_back("check-integrity", norm.checkIntegrity ? "true" : "false");
    out.emplace_back("enable-dht", norm.enableDht ? "true" : "false");
    out.emplace_back("enable-peer-exchange", norm.enablePeerExchange ? "true" : "false");
    out.emplace_back("bt-enable-lpd", norm.btEnableLpd ? "true" : "false");
    out.emplace_back("bt-anonymous-mode", norm.btAnonymousMode ? "true" : "false");
    out.emplace_back("bt-max-peers", std::to_string(norm.btMaxPeers));
    out.emplace_back("check-certificate", norm.checkCertificate ? "true" : "false");
    out.emplace_back("disable-ipv6", norm.disableIpv6 ? "true" : "false");
    out.emplace_back("remote-time", norm.remoteTime ? "true" : "false");
    out.emplace_back("media", norm.mediaMode);
    out.emplace_back("media-format", norm.mediaFormat);
    out.emplace_back("bt-first-last-piece-first", norm.btFirstLastPieceFirst ? "true" : "false");
    out.emplace_back("detach-share-only", norm.detachShareOnly ? "true" : "false");

    if (!norm.dir.empty()) {
        out.emplace_back("dir", norm.dir);
    }
    if (!norm.btTracker.empty()) {
        out.emplace_back("bt-tracker", norm.btTracker);
    }
    if (!norm.allProxy.empty()) {
        out.emplace_back("all-proxy", norm.allProxy);
    }
    if (!norm.noProxy.empty()) {
        out.emplace_back("no-proxy", norm.noProxy);
    }
    if (!norm.userAgent.empty()) {
        out.emplace_back("user-agent", norm.userAgent);
    }
    return out;
}

}  // namespace motrix::domain
