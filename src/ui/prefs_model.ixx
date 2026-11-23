export module motrix.ui:prefs_model;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;

namespace motrix::ui {

export struct PrefsFormState {
    bool initialized = false;
    std::uint64_t loadedRevision = 0;
    int themeIndex = 1;   // 0=Auto, 1=Light, 2=Dark
    int localeIndex = 0;  // 0=EnUS, 1=ZhCN
    std::array<char, 512> dirBuffer{};
    int maxConcurrent = 5;
    int maxConnPerServer = 16;
    int dlLimitKbps = 0;
    int ulLimitKbps = 0;
    bool continueDownloads = true;
    bool newTaskShowDownloading = true;
    bool noConfirmBeforeDelete = false;
    bool followTorrent = true;
    bool enableUpnp = true;
    bool useProxy = false;
    bool keepSeeding = false;
    std::array<char, 512> allProxyBuffer{};
    std::array<char, 1024> noProxyBuffer{};
    std::array<char, 32768> btTrackerBuffer{};
    std::array<char, 512> userAgentBuffer{};
    std::array<char, 512> enginePathBuffer{};
    float seedRatio = 1.0f;
    int seedTime = 60;
    int rpcPort = 16800;
    std::array<char, 16> rpcPortBuf{};
    std::array<char, 256> rpcSecretBuffer{};
    int btPort = 21301;
    std::array<char, 16> btPortBuf{};
    int dhtPort = 26701;
    std::array<char, 16> dhtPortBuf{};
    int runModeIndex = 0;  // 0=Standard, 1=Tray, 2=HideTray
    bool keepWindowState = false;
    bool autoHideWindow = false;
    bool openAtLogin = false;
    bool resumeAllWhenAppLaunched = false;
    bool showProgressBar = true;
    bool taskNotification = true;
    bool traySpeedometer = true;
    bool showTooltips = true;
    std::array<char, 512> aria2NextUrlBuffer{};
    std::array<char, 512> aria2NextMirrorBuffer{};
    std::array<char, 128> fontFamilyBuffer{};
    bool deleteFilesWhenDeleteTask = false;
    bool autoSyncTracker = true;
    std::vector<bool> trackerSourceSelected;
    bool protocolMagnet = false;
    bool protocolThunder = false;
    bool protocolMotrix = false;
    bool protocolTorrent = false;
    std::int64_t loadedSyncTrackerTime = -1;
    int lastSnapTheme = 1;
    int lastSnapLocale = 0;
    std::string lastSnapEnginePath;
    std::string lastSnapFontFamily;
    std::string lastSnapRpcSecret;
    std::string lastSnapBtTrackerRaw;
    std::string lastSnapBtTrackerLoadedComma;
    bool rpcSaveUploadMetadata = true;
    bool btAutoDownloadContent = true;
    int btEncryptionIdx = 0;  // 0=preferred,1=required,2=disabled
    int btTransportIdx = 0;   // 0=both,1=tcp,2=utp
    int fileAllocIdx = 0;     // 0=none,1=prealloc,2=falloc,3=trunc
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
    int mediaModeIdx = 0;    // 0=auto,1=file
    int mediaFormatIdx = 0;  // 0=mp4,1=mkv
    bool btFirstLastPieceFirst = false;
    bool detachShareOnly = false;
    bool dlUnitMb = false;
    bool ulUnitMb = false;
    int logLevelIndex = 2;  // 0=debug,1=info,2=warn,3=error
    int feedbackKind = 0;   // 0=None,1=Saved,2=Discarded,3=SessionReset,4=FactoryReset,5=SavedReload
    bool feedbackHadChanges = false;
    std::chrono::steady_clock::time_point feedbackUntil{};
};

export void CopyUtf8ToBuffer(std::string_view src, char* dst, std::size_t cap) noexcept;
export void LoadPrefsFromSnapshot(const motrix::app::AppStateSnapshot& st, PrefsFormState& ps);
export bool ApplyPrefsDraft(const motrix::app::AppStateSnapshot& st,
                            motrix::app::ICommandSink& sink,
                            PrefsFormState& ps);
export bool IsPrefsDraftDirty(const motrix::app::AppStateSnapshot& st, const PrefsFormState& ps);
export void SetPrefsFeedback(PrefsFormState& ps, int kind, bool hadChanges = true);

}  // namespace motrix::ui
