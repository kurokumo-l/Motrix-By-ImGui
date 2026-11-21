export module motrix.ui:views_internal;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import motrix.platform;
import :theme;
import :anim;
import :fonts;
export import :prefs_model;

namespace motrix::ui {

export struct AddTaskFormState {
    std::array<char, 4096> uriBuffer{};
    std::array<char, 256> nameBuffer{};
    std::array<char, 512> dirBuffer{};
    int splitCount = 16;
    bool initialized = false;
    int activeTab = 0;  // 0 = URI, 1 = Torrent
    std::string torrentPath;
    std::string torrentBase64;
    std::string torrentError;
    std::string uriError;
    std::uint64_t seenDropSeq = 0;
    std::uint64_t seenPrefillSeq = 0;
    std::optional<motrix::domain::TorrentMetadata> torrentMeta;
    std::vector<bool> torrentFileSelected;
    int torrentFileFilter = 0;  // 0=All, 1=Video, 2=Audio, 3=Image
    bool showAdvanced = false;
    bool focusRequested = true;
    bool wasAddOpen = false;
    bool wasAboutOpen = false;
    std::array<char, 512> userAgentBuffer{};
    std::array<char, 512> authBuffer{};
    std::array<char, 512> refererBuffer{};
    std::array<char, 1024> cookieBuffer{};
    std::array<char, 512> proxyBuffer{};
    bool navigateToDownloading = true;
    std::string autoFilledTorrentName;
};

export struct DrawerUiState {
    std::string loadedGid;
    std::vector<bool> draftSelected;
    std::vector<int> draftFileIndices;
    std::optional<motrix::domain::DownloadTaskView> cachedTask;
    bool dirty = false;
    bool pendingApply = false;
    std::vector<bool> pendingExpected;
    std::chrono::steady_clock::time_point pendingUntil{};
    int fileFilter = 0;  // 0=All, 1=Video, 2=Audio, 3=Image
    int prevTab = -1;
    float slideProgress = 0.0f;
};

export struct DeleteConfirmState {
    bool open = false;
    bool requestOpen = false;
    bool deleteFiles = false;
    std::vector<std::string> gids;
    std::string taskName;
};

export struct UiFrameContext {
    const motrix::app::AppStateSnapshot& state;
    motrix::app::ICommandSink& sink;
    motrix::platform::IWindowHost& host;
    const motrix::core::Translator& tr;
    const FontRegistry& fonts;
    const MotrixPalette& palette;
    AnimState& anim;
    DeleteConfirmState* deleteConfirm = nullptr;
    float dpiScale = 1.0f;
    float viewportW = 1024.0f;
    float viewportH = 768.0f;
};

export struct PanelRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

export struct PrefsCheckboxItem {
    const char* id = nullptr;
    std::string_view key{};
    bool* val = nullptr;
};

export using PrefsRowBodyFn = std::function<void(float, float)>;

export void RenderTitlebarOverlay(UiFrameContext& ctx);
export void RenderAsideRail(UiFrameContext& ctx, float x, float y, float h);
export void RenderSubnav(UiFrameContext& ctx, const PanelRect& rect, int& prefTab);
export void RenderTaskPanel(UiFrameContext& ctx, const PanelRect& rect);
export [[nodiscard]] RgbaColor ProgressColorFor(const MotrixPalette& p,
                                                const motrix::domain::DownloadTaskView& task) noexcept;
export void RenderTaskCard(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& t, float w);
export void RenderSpeedometer(UiFrameContext& ctx, float rightEdgeX, float bottomEdgeY);
export void RenderGeneralTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task);
export void RenderActivityTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task);
export void RenderTrackersTab(UiFrameContext& ctx, std::span<const std::string> trackers);
export void RenderPeersTab(UiFrameContext& ctx, std::span<const motrix::domain::TaskPeerView> peers);
export void
RenderFilesTab(UiFrameContext& ctx, DrawerUiState& ds, const motrix::domain::DownloadTaskView& t);
export void RenderTaskDetailDrawer(UiFrameContext& ctx, DrawerUiState& drawerState);
export void RenderAddTorrentTab(UiFrameContext& ctx, AddTaskFormState& form);
export void LoadTorrentFileIntoForm(AddTaskFormState& form, std::string_view utf8Path);
export void DrawSubCaption(const UiFrameContext& ctx, float x, float y, std::string_view text);
export void DrawAlignedCaption(const UiFrameContext& ctx,
                               float x,
                               float y,
                               float rowH,
                               std::string_view text,
                               float fontSize = 14.5f);
export void
DrawPrefsCheckboxStack(UiFrameContext& ctx, float x, float& y, std::span<const PrefsCheckboxItem> items);
export [[nodiscard]] bool MatchesMediaFilter(std::string_view path, int filter) noexcept;
export void DrawPrefsFormRow(UiFrameContext& ctx, std::string_view lbl, float rowY, PrefsRowBodyFn fn);
export void RenderPrefsBasicTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps);
export void RenderPreferencesView(UiFrameContext& ctx, const PanelRect& r, int tab, PrefsFormState& ps);
export void RenderAboutModalDialog(UiFrameContext& ctx, bool justOpened = false);
export void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm);
export void RequestTaskRemoval(UiFrameContext& ctx, std::string_view gid, std::string_view taskName);
export void
RequestTasksRemoval(UiFrameContext& ctx, std::vector<std::string> gids, std::string_view name = {});
export void RenderDeleteConfirmModal(UiFrameContext& ctx);

}  // namespace motrix::ui
