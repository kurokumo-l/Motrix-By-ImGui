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
    std::optional<motrix::domain::TorrentMetadata> torrentMeta;
    std::vector<bool> torrentFileSelected;
    int torrentFileFilter = 0;  // 0=All, 1=Video, 2=Audio, 3=Image
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
    std::array<char, 2048> btTrackerBuffer{};
    std::array<char, 512> userAgentBuffer{};
    std::array<char, 512> enginePathBuffer{};
    float seedRatio = 1.0f;
    int seedTime = 60;
    int rpcPort = 16800;
    std::array<char, 256> rpcSecretBuffer{};
    int btPort = 21301;
    int dhtPort = 26701;
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
export void RenderPreferencesView(
    UiFrameContext& ctx, float x, float y, float w, float h, int prefTab, PrefsFormState& prefsState);
export void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm);

}  // namespace motrix::ui
