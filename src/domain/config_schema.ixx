export module motrix.domain:config_schema;

import std;
import motrix.core;

namespace motrix::domain {

export enum class ThemePreference {
    Auto = 0,
    Light,
    Dark,
};

export enum class RunMode {
    Standard = 1,
    Tray = 2,
    HideTray = 3,
};

export struct UserPreferences {
    ThemePreference theme = ThemePreference::Light;
    motrix::core::Locale locale = motrix::core::Locale::EnUS;
    RunMode runMode = RunMode::Standard;
    bool hideAppMenu = true;
    bool keepWindowState = false;
    bool autoHideWindow = false;
    bool openAtLogin = false;
    bool resumeAllWhenAppLaunched = false;
    bool newTaskShowDownloading = true;
    bool noConfirmBeforeDeleteTask = false;
    bool showProgressBar = true;
    bool taskNotification = true;
    bool traySpeedometer = false;
    bool enableUpnp = true;
    bool autoSyncTracker = true;
    bool useProxy = false;
    bool keepSeeding = false;
    std::string engineBinPath;
    std::string logLevel = "warn";
    std::vector<std::string> favoriteDirectories;
    std::vector<std::string> historyDirectories;
};

export struct SystemConfig {
    std::uint16_t rpcListenPort = 16800;
    std::string rpcSecret;
    std::string dir;
    int maxConcurrentDownloads = 5;
    int maxConnectionPerServer = 64;
    int split = 64;
    int listenPort = 21301;
    int dhtListenPort = 26701;
    double seedRatio = 1.0;
    int seedTime = 60;
    std::uint64_t maxOverallDownloadLimit = 0;
    std::uint64_t maxOverallUploadLimit = 0;
    bool continueDownloads = true;
    bool followTorrent = true;
    std::string btTracker;
    std::string allProxy;
    std::string noProxy;
    std::string userAgent;
};

export [[nodiscard]] UserPreferences DefaultUserPreferences();
export [[nodiscard]] SystemConfig DefaultSystemConfig();
export [[nodiscard]] std::vector<std::pair<std::string, std::string>>
SystemConfigToOptionPairs(const SystemConfig& cfg);

}  // namespace motrix::domain
