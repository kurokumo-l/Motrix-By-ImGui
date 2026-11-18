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

export inline constexpr std::string_view kDefaultAria2NextUpdateUrl =
    "https://api.github.com/repos/AnInsomniacy/aria2-next/releases/latest";

export struct UserPreferences {
    ThemePreference theme = ThemePreference::Auto;
    motrix::core::Locale locale = motrix::core::Locale::EnUS;
    RunMode runMode = RunMode::Standard;
    bool keepWindowState = false;
    bool autoHideWindow = false;
    bool openAtLogin = false;
    bool resumeAllWhenAppLaunched = false;
    bool newTaskShowDownloading = true;
    bool noConfirmBeforeDeleteTask = false;
    bool deleteFilesWhenDeleteTask = false;
    bool showProgressBar = true;
    bool taskNotification = true;
    bool traySpeedometer = true;
    bool showTooltips = true;
    bool enableUpnp = true;
    bool autoSyncTracker = true;
    bool useProxy = false;
    bool keepSeeding = false;
    std::string engineBinPath;
    std::string aria2NextUpdateUrl = std::string(kDefaultAria2NextUpdateUrl);
    std::string aria2NextMirrorPrefix;
    std::string fontFamily = "Microsoft YaHei UI";
    std::string monoFontFamily;
    std::string logLevel = "warn";
    std::vector<std::string> favoriteDirectories;
    std::vector<std::string> historyDirectories;
    std::vector<std::string> trackerSource;
    std::int64_t lastSyncTrackerTime = 0;
    bool protocolMagnet = true;
    bool protocolThunder = true;
    bool protocolMotrix = true;
    std::optional<int> windowX;
    std::optional<int> windowY;
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowMaximized = false;
};

export struct SystemConfig {
    std::uint16_t rpcListenPort = 16800;
    std::string rpcSecret;
    std::string dir;
    int maxConcurrentDownloads = 5;
    int maxConnectionPerServer = 16;
    int split = 16;
    int listenPort = 21301;
    int dhtListenPort = 26701;
    double seedRatio = 1.0;
    int seedTime = 60;
    std::uint64_t maxOverallDownloadLimit = 0;
    std::uint64_t maxOverallUploadLimit = 0;
    bool continueDownloads = true;
    bool followTorrent = true;
    bool btSaveMetadata = true;
    bool rpcSaveUploadMetadata = true;
    bool btForceEncryption = false;
    std::string btEncryption = "preferred";
    std::string btTransport = "both";
    std::string fileAllocation = "none";
    bool autoFileRenaming = true;
    bool allowOverwrite = false;
    bool checkIntegrity = false;
    bool enableDht = true;
    bool enablePeerExchange = true;
    bool btEnableLpd = true;
    bool btAnonymousMode = false;
    int btMaxPeers = 64;
    bool checkCertificate = true;
    bool disableIpv6 = false;
    bool remoteTime = false;
    std::string mediaMode = "auto";
    std::string mediaFormat = "mp4";
    bool btFirstLastPieceFirst = false;
    bool detachShareOnly = false;
    bool pauseMetadata = false;
    std::string btTracker;
    std::string allProxy;
    std::string noProxy;
    std::string userAgent;
};

export [[nodiscard]] UserPreferences DefaultUserPreferences();
export [[nodiscard]] SystemConfig DefaultSystemConfig();
export [[nodiscard]] SystemConfig BuildEffectiveSystemConfig(const SystemConfig& cfg,
                                                             const UserPreferences& prefs,
                                                             bool suspendSpeedLimit = false);
export [[nodiscard]] std::vector<std::pair<std::string, std::string>>
SystemConfigToOptionPairs(const SystemConfig& cfg);
export [[nodiscard]] std::string SerializeUserPreferencesJson(const UserPreferences& prefs);
export [[nodiscard]] motrix::core::Result<UserPreferences>
ParseUserPreferencesJson(std::string_view jsonText);
export [[nodiscard]] std::string SerializeSystemConfigJson(const SystemConfig& cfg);
export [[nodiscard]] motrix::core::Result<SystemConfig> ParseSystemConfigJson(std::string_view jsonText);

}  // namespace motrix::domain
