#include "imgui.h"
#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.platform;
import motrix.app;
import motrix.ui;

namespace {

class HeadlessViewHost final : public motrix::platform::IWindowHost {
public:
    bool darkMode = false;
    bool allowUpload = true;
    int uploadCount = 0;
    int uploadedW = 0;
    int uploadedH = 0;
    std::vector<std::uint8_t> lastPixels;

    [[nodiscard]] float DpiScale() const noexcept override { return 1.0f; }
    [[nodiscard]] bool IsMaximized() const noexcept override { return false; }
    [[nodiscard]] bool IsHiddenToTray() const noexcept override { return false; }
    [[nodiscard]] bool IsSystemDarkMode() const noexcept override { return darkMode; }
    [[nodiscard]] motrix::platform::HostMetricsSnapshot Metrics() const noexcept override { return {}; }
    [[nodiscard]] motrix::platform::WindowBounds CurrentWindowBounds() const override { return {}; }

    void Minimize() override {}
    void ToggleMaximize() override {}
    void Close() override {}
    void RequestQuit() override {}
    void ShowFromTray() override {}
    void HideToTray() override {}
    void SetDarkFrame(bool /*dark*/) override {}
    void SetWindowTitle(std::string_view /*utf8Title*/) override {}
    void SetBindCloseToHide(bool /*enabled*/) noexcept override {}
    void SetAutoHideOnBlur(bool /*enabled*/) noexcept override {}
    void SetTrayEnabled(bool /*enabled*/) override {}
    void UpdateTrayStatus(bool /*hasActiveDownloads*/,
                          bool /*isZhCn*/,
                          std::string_view /*tooltipText*/) override {}
    void UpdateTaskbarProgress(motrix::platform::TaskbarProgressMode /*mode*/,
                               std::uint64_t /*completed*/,
                               std::uint64_t /*total*/) override {}
    void ShowToastNotification(std::string_view /*title*/,
                               std::string_view /*body*/,
                               std::string_view /*clickTargetDir*/) override {}
    void SetSleepBlockerEnabled(bool /*active*/) noexcept override {}
    void RequestFrame() override {}
    void NotifyAnimationActive() override {}
    void SetTitlebarHeight(float /*heightPx*/) override {}
    void SetTitlebarBlocked(bool /*blocked*/) override {}
    void RegisterTitlebarInteractiveRect(motrix::platform::TitlebarRect /*rect*/) override {}
    void CopyTextToClipboard(std::string_view /*utf8*/) override {}
    void OpenPathInExplorer(std::string_view /*utf8Path*/) override {}
    [[nodiscard]] std::optional<std::string> PickTorrentFile() override { return std::nullopt; }
    [[nodiscard]] std::optional<std::string> PickFolder(std::string_view /*initialDir*/) override {
        return std::nullopt;
    }

    [[nodiscard]] std::uint64_t UploadRgbaTexture(std::uint64_t /*existingId*/,
                                                  int width,
                                                  int height,
                                                  std::span<const std::uint8_t> rgba) override {
        if (!allowUpload || width <= 0 || height <= 0) {
            return 0;
        }
        ++uploadCount;
        uploadedW = width;
        uploadedH = height;
        lastPixels.assign(rgba.begin(), rgba.end());
        return static_cast<std::uint64_t>(uploadCount + 100);
    }
};

class NullViewSink final : public motrix::app::ICommandSink {
public:
    std::vector<motrix::app::Command> commands;
    void Dispatch(motrix::app::Command cmd) override { commands.push_back(std::move(cmd)); }
};

class HeadlessUiRunner {
public:
    HeadlessViewHost host;
    NullViewSink sink;
    motrix::ui::MotrixUiShell shell;

    HeadlessUiRunner(float width, float height) : shell(false) {
        IMGUI_CHECKVERSION();
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;
        shell.InitializeImGui(host, false);
        unsigned char* pixels = nullptr;
        int w = 0;
        int h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        io.Fonts->SetTexID(1);
    }

    ~HeadlessUiRunner() {
        if (ctx_ != nullptr) {
            ImGui::DestroyContext(ctx_);
        }
    }

    HeadlessUiRunner(const HeadlessUiRunner&) = delete;
    HeadlessUiRunner& operator=(const HeadlessUiRunner&) = delete;

    [[nodiscard]] ImDrawData* Step(const motrix::app::AppStateSnapshot& state, float w, float h) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(w, h);
        io.DeltaTime = 1.0f / 60.0f;
        shell.PrepareFrame();
        ImGui::NewFrame();
        shell.RenderFrame(state, sink, host);
        ImGui::Render();
        return ImGui::GetDrawData();
    }

private:
    ImGuiContext* ctx_ = nullptr;
};

[[nodiscard]] std::uint8_t PixelAlphaAt(const HeadlessViewHost& host, int x, int y) {
    const std::size_t idx = static_cast<std::size_t>(y * host.uploadedW + x) * 4u;
    return host.lastPixels.at(idx + 3u);
}

[[nodiscard]] motrix::domain::DownloadTaskView MakeSampleTask(bool bitTorrent) {
    motrix::domain::DownloadTaskView t;
    t.gid = bitTorrent ? "bt-001" : "http-001";
    t.name = bitTorrent ? "archlinux-x86_64.iso" : "ubuntu-24.04-live-server.iso";
    t.status = motrix::domain::TaskStatus::Active;
    t.taskType = bitTorrent ? motrix::domain::TaskType::BitTorrent : motrix::domain::TaskType::Http;
    t.totalLength = 851'443'712ull;
    t.completedLength = 371'510'476ull;
    t.downloadSpeed = 19'922'944ull;
    t.uploadSpeed = bitTorrent ? 420'000ull : 0ull;
    t.connections = bitTorrent ? 24 : 16;
    t.numSeeders = bitTorrent ? 18 : 0;
    return t;
}

}  // namespace

TEST_CASE("motrix.ui About modal renders transparent app icon corners and engine pills",
          "[ui][about][appicon]") {
    HeadlessUiRunner ui(1024.0f, 768.0f);
    motrix::app::AppStateSnapshot state;
    state.activeModal = motrix::app::ActiveModal::About;
    state.engineState = motrix::app::EngineConnectionState::Connected;
    state.engineInfo.version = "3.1.0";
    state.engineInfo.enabledFeatures = {"Async DNS", "BitTorrent", "HTTPS", "SQLiteState"};
    state.prefs.theme = motrix::domain::ThemePreference::Light;
    state.prefs.locale = motrix::core::Locale::ZhCN;

    ImDrawData* ddLight = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddLight != nullptr);
    CHECK(ddLight->Valid);
    CHECK(ddLight->TotalVtxCount > 0);
    REQUIRE(ui.host.uploadCount >= 1);
    CHECK(ui.host.uploadedW >= 128);
    CHECK(ui.host.uploadedW == ui.host.uploadedH);
    const int last = ui.host.uploadedW - 1;
    CHECK(PixelAlphaAt(ui.host, 0, 0) == 0);
    CHECK(PixelAlphaAt(ui.host, last, 0) == 0);
    CHECK(PixelAlphaAt(ui.host, 0, last) == 0);
    CHECK(PixelAlphaAt(ui.host, last, last) == 0);
    CHECK(PixelAlphaAt(ui.host, ui.host.uploadedW / 2, ui.host.uploadedH / 2) == 255);

    const int prevUploads = ui.host.uploadCount;
    state.prefs.theme = motrix::domain::ThemePreference::Dark;
    state.engineState = motrix::app::EngineConnectionState::Offline;
    state.prefs.locale = motrix::core::Locale::EnUS;
    ImDrawData* ddDark = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddDark != nullptr);
    CHECK(ddDark->TotalVtxCount > 0);
    CHECK(ui.host.uploadCount > prevUploads);
    CHECK(PixelAlphaAt(ui.host, 0, 0) == 0);
    CHECK(PixelAlphaAt(ui.host, last, last) == 0);

    ui.host.allowUpload = false;
    ImDrawData* ddFallback = ui.Step(state, 960.0f, 700.0f);
    REQUIRE(ddFallback != nullptr);
    CHECK(ddFallback->TotalVtxCount > 0);
}

TEST_CASE("motrix.ui Task cards render active HTTP and BT connection chips in wide and narrow widths",
          "[ui][taskcard][connections]") {
    HeadlessUiRunner ui(1024.0f, 768.0f);
    motrix::app::AppStateSnapshot state;
    state.downloadingTasks.push_back(MakeSampleTask(false));
    state.downloadingTasks.push_back(MakeSampleTask(true));
    state.prefs.locale = motrix::core::Locale::ZhCN;

    ImDrawData* ddWide = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddWide != nullptr);
    CHECK(ddWide->TotalVtxCount > 0);

    state.prefs.locale = motrix::core::Locale::EnUS;
    state.prefs.theme = motrix::domain::ThemePreference::Dark;
    ImDrawData* ddNarrow = ui.Step(state, 440.0f, 680.0f);
    REQUIRE(ddNarrow != nullptr);
    CHECK(ddNarrow->TotalVtxCount > 0);

    motrix::core::Translator tr(motrix::core::Locale::EnUS);
    CHECK_FALSE(tr.Tr("about.tagline").empty());
    CHECK_FALSE(tr.Tr("task.connections").empty());
    CHECK_FALSE(tr.Tr("task.seeders").empty());
    tr.SetLocale(motrix::core::Locale::ZhCN);
    CHECK_FALSE(tr.Tr("about.tagline").empty());
    CHECK(tr.Tr("task.connections") != "task.connections");
    CHECK(tr.Tr("prefs.show_tooltips") != "prefs.show_tooltips");
    CHECK(tr.Tr("speed.badge_max") == "MAX");
    CHECK(tr.Tr("speed.badge_cap") == "CAP");
}

TEST_CASE("motrix.ui ElDrawTooltip draws custom foreground bubble on hover",
          "[ui][tooltip][speedometer]") {
    HeadlessUiRunner ui(1024.0f, 768.0f);
    motrix::app::AppStateSnapshot state;
    state.prefs.locale = motrix::core::Locale::ZhCN;
    state.downloadingTasks.push_back(MakeSampleTask(false));

    // ImGui::GetDrawData() reuses a single DrawData instance per viewport across frames, so
    // snapshot TotalVtxCount as ints immediately after each Step().
    (void)ui.Step(state, 1024.0f, 768.0f);
    ImDrawData* ddNoHover = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddNoHover != nullptr);
    const int baseVtx = ddNoHover->TotalVtxCount;

    // Hover over header Resume All action button near top-right (box around x=932..956, y=44..68)
    ImGui::GetIO().AddFocusEvent(true);
    ImGui::GetIO().AddMousePosEvent(944.0f, 56.0f);
    ImDrawData* ddHover = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddHover != nullptr);
    const int hoverVtx = ddHover->TotalVtxCount;
    CHECK(hoverVtx > baseVtx);

    // Disabling hover tooltips in preferences suppresses tooltip rendering while preserving button UI.
    state.prefs.showTooltips = false;
    ImDrawData* ddSuppressed = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddSuppressed != nullptr);
    const int suppressedVtx = ddSuppressed->TotalVtxCount;
    CHECK(suppressedVtx < hoverVtx);
    CHECK(suppressedVtx == baseVtx);

    state.prefs.showTooltips = true;
    ImDrawData* ddReenabled = ui.Step(state, 1024.0f, 768.0f);
    REQUIRE(ddReenabled != nullptr);
    const int reenabledVtx = ddReenabled->TotalVtxCount;
    CHECK(reenabledVtx == hoverVtx);
}

TEST_CASE("motrix.ui Detail drawer suppresses background list shortcuts and clips error cards",
          "[ui][drawer][shortcuts][errorcard]") {
    HeadlessUiRunner ui(800.0f, 600.0f);
    motrix::app::AppStateSnapshot state;
    auto errTask = MakeSampleTask(false);
    errTask.status = motrix::domain::TaskStatus::Error;
    errTask.errorMessage =
        "HTTP 502 Bad Gateway: upstream connection refused after multiple retry attempts on mirror";
    state.stoppedTasks.push_back(errTask);

    auto activeTask = MakeSampleTask(true);
    state.downloadingTasks.push_back(activeTask);
    state.selectedTaskGid = activeTask.gid;
    state.selectedTaskGids = {activeTask.gid};
    state.detailedTaskGid = activeTask.gid;
    state.detailedTask = activeTask;

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    ImDrawData* ddDrawer = ui.Step(state, 800.0f, 600.0f);
    REQUIRE(ddDrawer != nullptr);
    CHECK(ddDrawer->Valid);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    (void)ui.Step(state, 800.0f, 600.0f);

    io.AddKeyEvent(ImGuiKey_Delete, true);
    (void)ui.Step(state, 800.0f, 600.0f);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    (void)ui.Step(state, 800.0f, 600.0f);

    for (const auto& cmd : ui.sink.commands) {
        CHECK_FALSE(std::holds_alternative<motrix::app::SelectAllTasksCmd>(cmd));
        CHECK_FALSE(std::holds_alternative<motrix::app::RemoveTaskCmd>(cmd));
    }

    state.detailedTaskGid.clear();
    state.detailedTask.reset();
    state.activeCategory = motrix::domain::TaskCategory::Stopped;
    ImDrawData* ddStopped = ui.Step(state, 440.0f, 600.0f);
    REQUIRE(ddStopped != nullptr);
    CHECK(ddStopped->Valid);
    CHECK(ddStopped->TotalVtxCount > 0);

    ui.sink.commands.clear();
    state.activeCategory = motrix::domain::TaskCategory::Downloading;
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    (void)ui.Step(state, 800.0f, 600.0f);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    const bool sawSelectAll = std::ranges::any_of(ui.sink.commands, [](const auto& cmd) {
        return std::holds_alternative<motrix::app::SelectAllTasksCmd>(cmd);
    });
    CHECK(sawSelectAll);
}
