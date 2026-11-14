export module motrix.ui:views_internal;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import motrix.platform;
import :theme;
import :anim;
import :fonts;

namespace motrix::ui {

export struct AddTaskFormState {
    std::array<char, 4096> uriBuffer{};
    std::array<char, 256> nameBuffer{};
    std::array<char, 512> dirBuffer{};
    int splitCount = 64;
    bool initialized = false;
    int activeTab = 0;  // 0 = URI, 1 = Torrent
    std::string torrentPath;
    std::string torrentBase64;
    std::string torrentError;
    std::uint64_t seenDropSeq = 0;
    std::uint64_t seenPrefillSeq = 0;
    std::optional<motrix::domain::TorrentMetadata> torrentMeta;
    std::vector<bool> torrentFileSelected;
    int torrentFileFilter = 0;  // 0=All, 1=Video, 2=Audio, 3=Image
    bool showAdvanced = false;
    bool focusRequested = true;
    std::array<char, 512> userAgentBuffer{};
    std::array<char, 512> authBuffer{};
    std::array<char, 512> refererBuffer{};
    std::array<char, 1024> cookieBuffer{};
    std::array<char, 512> proxyBuffer{};
    bool navigateToDownloading = true;
};

export struct DrawerUiState {
    std::string loadedGid;
    std::vector<bool> draftSelected;
    bool dirty = false;
    int fileFilter = 0;  // 0=All, 1=Video, 2=Audio, 3=Image
    float slideProgress = 0.0f;
};

export struct PrefsFormState {
    bool initialized = false;
    std::uint64_t loadedRevision = 0;
    int themeIndex = 1;   // 0=Auto, 1=Light, 2=Dark
    int localeIndex = 0;  // 0=EnUS, 1=ZhCN
    std::array<char, 512> dirBuffer{};
    int maxConcurrent = 5;
    int maxConnPerServer = 64;
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
    std::array<char, 8192> btTrackerBuffer{};
    std::array<char, 512> userAgentBuffer{};
    std::array<char, 512> enginePathBuffer{};
    float seedRatio = 1.0f;
    int seedTime = 60;
    int rpcPort = 16800;
    std::array<char, 256> rpcSecretBuffer{};
    int btPort = 21301;
    int dhtPort = 26701;
    int runModeIndex = 0;  // 0=Standard, 1=Tray, 2=HideTray
    bool keepWindowState = false;
    bool autoHideWindow = false;
    bool openAtLogin = false;
    bool resumeAllWhenAppLaunched = false;
    bool showProgressBar = true;
    bool taskNotification = true;
    bool traySpeedometer = false;
    bool autoSyncTracker = true;
    std::vector<bool> trackerSourceSelected;
    bool protocolMagnet = true;
    bool protocolThunder = true;
    bool protocolMotrix = true;
    std::int64_t loadedSyncTrackerTime = -1;
    bool hideAppMenu = true;
    bool autoCheckUpdate = false;
    bool btSaveMetadata = true;
    bool btAutoDownloadContent = true;
    bool btForceEncryption = false;
    bool dlUnitMb = false;
    bool ulUnitMb = false;
    int logLevelIndex = 2;  // 0=debug,1=info,2=notice,3=warn,4=error
};

export struct UiFrameContext {
    const motrix::app::AppStateSnapshot& state;
    motrix::app::ICommandSink& sink;
    motrix::platform::IWindowHost& host;
    const motrix::core::Translator& tr;
    const FontRegistry& fonts;
    const MotrixPalette& palette;
    AnimState& anim;
    float dpiScale = 1.0f;
    float viewportW = 1024.0f;
    float viewportH = 768.0f;
};

export void RenderTitlebarOverlay(UiFrameContext& ctx);
export void RenderAsideRail(UiFrameContext& ctx, float x, float y, float h);
export void RenderSubnav(UiFrameContext& ctx, float x, float y, float h, int& prefTab);
export void RenderTaskPanel(UiFrameContext& ctx, float x, float y, float w, float h);
export void
RenderTaskCard(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task, float width);
export void RenderSpeedometer(UiFrameContext& ctx, float rightEdgeX, float bottomEdgeY);
export void RenderGeneralTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task);
export void RenderActivityTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task);
export void RenderTrackersTab(UiFrameContext& ctx, std::span<const std::string> trackers);
export void RenderPeersTab(UiFrameContext& ctx, std::span<const motrix::domain::TaskPeerView> peers);
export void
RenderFilesTab(UiFrameContext& ctx, DrawerUiState& ds, const motrix::domain::DownloadTaskView& task);
export void RenderTaskDetailDrawer(UiFrameContext& ctx, DrawerUiState& drawerState);
export void RenderAddTorrentTab(UiFrameContext& ctx, AddTaskFormState& form);
export void LoadTorrentFileIntoForm(AddTaskFormState& form, std::string_view utf8Path);
export void CopyUtf8ToBuffer(std::string_view src, char* dst, std::size_t cap) noexcept;
export void DrawPrefsFieldLabel(UiFrameContext& ctx, std::string_view text);
export void DrawPrefsFormRow(UiFrameContext& ctx,
                             std::string_view label,
                             float rowY,
                             const std::function<void(float, float)>& body);
export void RenderPrefsBasicTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPrefsAdvancedTail(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPreferencesView(
    UiFrameContext& ctx, float x, float y, float w, float h, int prefTab, PrefsFormState& prefsState);
export void RenderAddTaskAdvancedFields(
    UiFrameContext& ctx, AddTaskFormState& form, float x0, float bodyW, float& curY);
export void RenderAboutModalDialog(UiFrameContext& ctx);
export void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm);

}  // namespace motrix::ui
