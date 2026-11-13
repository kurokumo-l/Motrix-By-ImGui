module motrix.domain;

import std;

namespace motrix::domain {

UserPreferences DefaultUserPreferences() {
    return UserPreferences{};
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

}  // namespace motrix::domain
