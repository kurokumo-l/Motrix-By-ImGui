#include "imgui.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import motrix.platform;
import motrix.ui;

TEST_CASE("motrix.ui AnimState finite convergence and AnyActive lifecycle", "[ui][anim]") {
    using namespace motrix::ui;

    AnimState anim;
    CHECK(anim.FrameCount() == 0u);

    anim.BeginFrame(1.0f / 60.0f);
    CHECK(anim.FrameCount() == 1u);
    const float v0 = anim.Float(HashId("k1"), 1.0f, 18.0f, 0.0f);
    CHECK(v0 == 0.0f);
    CHECK(anim.AnyActive());

    for (int i = 0; i < 60; ++i) {
        anim.BeginFrame(1.0f / 60.0f);
        (void)anim.Float(HashId("k1"), 1.0f, 18.0f, 0.0f);
    }
    const float settled = anim.Float(HashId("k1"), 1.0f, 18.0f, 0.0f);
    CHECK(settled == 1.0f);
    anim.BeginFrame(1.0f / 60.0f);
    CHECK(anim.Float(HashId("k1"), 1.0f, 18.0f, 0.0f) == 1.0f);
    CHECK_FALSE(anim.AnyActive());

    CHECK(AnimStep(nullptr, {HashId("null"), 0.75f}) == 0.75f);
    CHECK(AnimStep(&anim, {HashId("k2"), 1.0f, 20.0f, 0.0f}) == 0.0f);
}

TEST_CASE("motrix.ui SmoothOpenProgress startup snap vs interactive fade", "[ui][anim]") {
    using namespace motrix::ui;

    AnimState startupAnim;
    startupAnim.BeginFrame(1.0f / 60.0f);
    CHECK(SmoothOpenProgress(startupAnim, HashId("modal.startup"), true) == 1.0f);
    CHECK_FALSE(startupAnim.AnyActive());

    AnimState runtimeAnim;
    for (int i = 0; i < 4; ++i) {
        runtimeAnim.BeginFrame(1.0f / 60.0f);
    }
    const float openStart = SmoothOpenProgress(runtimeAnim, HashId("modal.runtime"), true);
    CHECK(openStart > 0.0f);
    CHECK(openStart < 1.0f);
    CHECK(runtimeAnim.AnyActive());

    for (int i = 0; i < 40; ++i) {
        runtimeAnim.BeginFrame(1.0f / 60.0f);
        (void)SmoothOpenProgress(runtimeAnim, HashId("modal.runtime"), false);
    }
    CHECK(SmoothOpenProgress(runtimeAnim, HashId("modal.runtime"), false) == 1.0f);
}

TEST_CASE("motrix.ui LerpColor endpoints and interpolation", "[ui][theme]") {
    using namespace motrix::ui;

    const RgbaColor a{0.0f, 0.2f, 0.4f, 0.5f};
    const RgbaColor b{1.0f, 0.6f, 0.8f, 1.0f};
    const RgbaColor c0 = LerpColor(a, b, -0.5f);
    const RgbaColor cMid = LerpColor(a, b, 0.5f);
    const RgbaColor c1 = LerpColor(a, b, 1.5f);

    CHECK(c0.r == a.r);
    CHECK(c0.a == a.a);
    CHECK(cMid.r == Catch::Approx(0.5f));
    CHECK(cMid.g == Catch::Approx(0.4f));
    CHECK(cMid.a == Catch::Approx(0.75f));
    CHECK(c1.r == b.r);
    CHECK(c1.a == b.a);
}

TEST_CASE("motrix.ui FontRegistry loads Microsoft YaHei UI via DirectWrite", "[ui][fonts]") {
    ImGuiContext* ctx = ImGui::CreateContext();
    REQUIRE(ctx != nullptr);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024.0f, 768.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    motrix::ui::FontRegistry fonts;
    const auto assetsDir = motrix::ui::ResolveAssetsFontsDir();
    REQUIRE(fonts.LoadAll(assetsDir));
    REQUIRE(fonts.IsLoaded());
    REQUIRE(io.Fonts->FontLoaderName != nullptr);
    CHECK(std::string_view(io.Fonts->FontLoaderName) == "DirectWrite");

    ImGui::NewFrame();
    auto* regular = static_cast<ImFont*>(fonts.FontPtr(motrix::ui::FontRole::Regular));
    auto* semibold = static_cast<ImFont*>(fonts.FontPtr(motrix::ui::FontRole::Semibold));
    auto* mono = static_cast<ImFont*>(fonts.FontPtr(motrix::ui::FontRole::Mono));
    REQUIRE(regular != nullptr);
    REQUIRE(semibold != nullptr);
    REQUIRE(mono != nullptr);

    ImFontBaked* regBaked = regular->GetFontBaked(15.0f);
    ImFontBaked* boldBaked = semibold->GetFontBaked(16.0f);
    ImFontBaked* monoBaked = mono->GetFontBaked(13.0f);
    REQUIRE(regBaked != nullptr);
    REQUIRE(boldBaked != nullptr);
    REQUIRE(monoBaked != nullptr);

    const ImFontGlyph* latinPtr = regBaked->FindGlyph(static_cast<ImWchar>('A'));
    REQUIRE(latinPtr != nullptr);
    const ImFontGlyph latin = *latinPtr;
    const ImFontGlyph* cjkPtr = regBaked->FindGlyph(static_cast<ImWchar>(0x4E0B));  // '下'
    REQUIRE(cjkPtr != nullptr);
    const ImFontGlyph cjk = *cjkPtr;
    const ImFontGlyph* spacePtr = regBaked->FindGlyph(static_cast<ImWchar>(' '));
    REQUIRE(spacePtr != nullptr);
    const ImFontGlyph space = *spacePtr;
    const ImFontGlyph* boldCjkPtr = boldBaked->FindGlyph(static_cast<ImWchar>(0x4E0B));
    REQUIRE(boldCjkPtr != nullptr);
    const ImFontGlyph boldCjk = *boldCjkPtr;
    const ImFontGlyph* monoCjkPtr = monoBaked->FindGlyph(static_cast<ImWchar>(0x4E0B));
    REQUIRE(monoCjkPtr != nullptr);
    const ImFontGlyph monoCjk = *monoCjkPtr;

    CHECK(latin.Visible);
    CHECK(latin.AdvanceX > 0.0f);
    CHECK(cjk.Visible);
    CHECK(cjk.AdvanceX > 0.0f);
    CHECK_FALSE(space.Visible);
    CHECK(space.AdvanceX > 0.0f);
    CHECK(boldCjk.Visible);
    CHECK(monoCjk.Visible);
    CHECK(io.FontDefault == regular);
    REQUIRE_FALSE(regular->Sources.empty());
    REQUIRE_FALSE(mono->Sources.empty());
    CHECK(regular->Sources[0]->PixelSnapH);
    CHECK(mono->Sources[0]->PixelSnapH);
    CHECK(latin.AdvanceX == std::round(latin.AdvanceX));

    ImGui::EndFrame();
    ImGui::DestroyContext(ctx);
}

TEST_CASE("motrix.ui FontRegistry supports custom fontFamily and generic file override", "[ui][fonts]") {
    ImGuiContext* ctx = ImGui::CreateContext();
    REQUIRE(ctx != nullptr);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024.0f, 768.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    REQUIRE(motrix::ui::IsFontFamilyInstalled("Microsoft YaHei UI"));
    const std::string customFamily = motrix::ui::IsFontFamilyInstalled("MiSans") ? "MiSans" : "Segoe UI";

    motrix::ui::FontRegistry fonts;
    const auto assetsDir = motrix::ui::ResolveAssetsFontsDir();
    REQUIRE(fonts.LoadAll(assetsDir, customFamily, {}));
    ImGui::NewFrame();
    auto* sysReg = static_cast<ImFont*>(fonts.FontPtr(motrix::ui::FontRole::Regular));
    REQUIRE(sysReg != nullptr);
    ImFontBaked* sysBaked = sysReg->GetFontBaked(15.0f);
    REQUIRE(sysBaked != nullptr);
    const ImFontGlyph* sysLatinPtr = sysBaked->FindGlyph(static_cast<ImWchar>('A'));
    REQUIRE(sysLatinPtr != nullptr);
    const ImFontGlyph sysLatin = *sysLatinPtr;
    const ImFontGlyph* sysCjkPtr = sysBaked->FindGlyph(static_cast<ImWchar>(0x4E0B));
    REQUIRE(sysCjkPtr != nullptr);
    const ImFontGlyph sysCjk = *sysCjkPtr;
    CHECK(sysLatin.Visible);
    CHECK(sysLatin.AdvanceX > 0.0f);
    CHECK(sysCjk.Visible);
    CHECK(sysCjk.AdvanceX > 0.0f);
    ImGui::EndFrame();

    const auto srcMono = assetsDir / "ui-mono.ttf";
    REQUIRE(std::filesystem::is_regular_file(srcMono));
    const auto tempDir = std::filesystem::temp_directory_path() / "motrix_font_override_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    REQUIRE(std::filesystem::copy_file(
        srcMono, tempDir / "ui-regular.ttf", std::filesystem::copy_options::overwrite_existing, ec));

    io.Fonts->Clear();
    motrix::ui::FontRegistry overrideFonts;
    REQUIRE(overrideFonts.LoadAll(tempDir, "Microsoft YaHei UI", {}));
    ImGui::NewFrame();
    auto* fileReg = static_cast<ImFont*>(overrideFonts.FontPtr(motrix::ui::FontRole::Regular));
    REQUIRE(fileReg != nullptr);
    REQUIRE_FALSE(fileReg->Sources.empty());
    CHECK(std::string_view(fileReg->Sources[0]->Name) == "ui-regular.ttf");
    ImFontBaked* fileBaked = fileReg->GetFontBaked(15.0f);
    REQUIRE(fileBaked != nullptr);
    const ImFontGlyph* ovLatinPtr = fileBaked->FindGlyph(static_cast<ImWchar>('A'));
    REQUIRE(ovLatinPtr != nullptr);
    const ImFontGlyph ovLatin = *ovLatinPtr;
    const ImFontGlyph* ovCjkPtr = fileBaked->FindGlyph(static_cast<ImWchar>(0x4E0B));
    REQUIRE(ovCjkPtr != nullptr);
    const ImFontGlyph ovCjk = *ovCjkPtr;
    CHECK(ovLatin.Visible);
    CHECK(ovLatin.AdvanceX > 0.0f);
    CHECK(ovCjk.Visible);
    CHECK(ovCjk.AdvanceX > 0.0f);
    ImGui::EndFrame();

    std::filesystem::remove_all(tempDir, ec);
    ImGui::DestroyContext(ctx);
}

namespace {

class StubWindowHost final : public motrix::platform::IWindowHost {
public:
    float DpiScale() const noexcept override { return 1.0f; }
    bool IsMaximized() const noexcept override { return false; }
    bool IsHiddenToTray() const noexcept override { return false; }
    bool IsSystemDarkMode() const noexcept override { return false; }
    motrix::platform::HostMetricsSnapshot Metrics() const noexcept override { return {}; }
    motrix::platform::WindowBounds CurrentWindowBounds() const override { return {}; }
    void Minimize() override {}
    void ToggleMaximize() override {}
    void Close() override {}
    void RequestQuit() override {}
    void ShowFromTray() override {}
    void HideToTray() override {}
    void SetDarkFrame(bool) override {}
    void SetWindowTitle(std::string_view) override {}
    void SetBindCloseToHide(bool) noexcept override {}
    void SetAutoHideOnBlur(bool) noexcept override {}
    void SetTrayEnabled(bool) override {}
    void UpdateTrayStatus(bool, bool, std::string_view) override {}
    void UpdateTaskbarProgress(motrix::platform::TaskbarProgressMode,
                               std::uint64_t,
                               std::uint64_t) override {}
    void ShowToastNotification(std::string_view, std::string_view, std::string_view) override {}
    void SetSleepBlockerEnabled(bool) noexcept override {}
    void RequestFrame() override {}
    void NotifyAnimationActive() override { ++animationWakeups; }
    void SetTitlebarHeight(float) override {}
    void SetTitlebarBlocked(bool) override {}
    void RegisterTitlebarInteractiveRect(motrix::platform::TitlebarRect) override {}
    void CopyTextToClipboard(std::string_view) override {}
    void OpenPathInExplorer(std::string_view) override {}
    std::optional<std::string> PickTorrentFile() override { return std::nullopt; }
    std::optional<std::string> PickFolder(std::string_view) override { return std::nullopt; }

    int animationWakeups = 0;
};

class RecordingCommandSink final : public motrix::app::ICommandSink {
public:
    void Dispatch(motrix::app::Command cmd) override { commands.push_back(std::move(cmd)); }
    std::vector<motrix::app::Command> commands;
};

}  // namespace

TEST_CASE("motrix.ui PrefsFormState tracks dirty state, discard, feedback, and ApplyPrefsDraft",
          "[ui][prefs]") {
    using namespace motrix::ui;

    motrix::core::Logger logger;
    motrix::app::AppCoordinator coord(
        motrix::app::CoordinatorOptions{.enableEngine = false, .loadPersistedConfig = false}, logger);

    PrefsFormState ps{};
    auto snap = coord.Snapshot();
    LoadPrefsFromSnapshot(*snap, ps);
    REQUIRE(ps.initialized);
    CHECK(ps.maxConnPerServer == 16);
    CHECK(ps.btMaxPeers == 64);
    CHECK(ps.fileAllocIdx == 0);
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));

    // Speed limit MB quantization & tracker newline formatting stay clean on reload.
    auto customSys = snap->sysConfig;
    customSys.maxOverallDownloadLimit = 2ULL * 1024ULL * 1024ULL;
    customSys.btTracker = "http://t1.example/announce,http://t2.example/announce";
    coord.Dispatch(motrix::app::SavePreferencesCmd{snap->prefs, std::move(customSys), false});
    snap = coord.Snapshot();
    LoadPrefsFromSnapshot(*snap, ps);
    CHECK(ps.dlUnitMb);
    CHECK(ps.dlLimitKbps == 2);
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));

    // Font family whitespace trimming stays consistent between dirty check and save.
    CopyUtf8ToBuffer("  Microsoft YaHei UI  ", ps.fontFamilyBuffer.data(), ps.fontFamilyBuffer.size());
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));
    CopyUtf8ToBuffer("   ", ps.fontFamilyBuffer.data(), ps.fontFamilyBuffer.size());
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));
    CopyUtf8ToBuffer("Segoe UI", ps.fontFamilyBuffer.data(), ps.fontFamilyBuffer.size());
    CHECK(IsPrefsDraftDirty(*snap, ps));
    LoadPrefsFromSnapshot(*snap, ps);
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));

    // Editing fields flips dirty; discarding restores clean snapshot and sets feedback.
    ps.maxConnPerServer = 32;
    CHECK(IsPrefsDraftDirty(*snap, ps));
    LoadPrefsFromSnapshot(*snap, ps);
    SetPrefsFeedback(ps, 2, true);
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));
    CHECK(ps.feedbackKind == 2);
    CHECK(ps.feedbackHadChanges);
    CHECK(ps.feedbackUntil > std::chrono::steady_clock::now());

    // Saving non-engine preference dispatches SavePreferencesCmd without engine restart.
    RecordingCommandSink sink;
    ps.openAtLogin = true;
    ps.showTooltips = false;
    REQUIRE(IsPrefsDraftDirty(*snap, ps));
    CHECK_FALSE(ApplyPrefsDraft(*snap, sink, ps));
    CHECK_FALSE(ps.initialized);
    REQUIRE(sink.commands.size() == 1u);
    auto* saved = std::get_if<motrix::app::SavePreferencesCmd>(&sink.commands.front());
    REQUIRE(saved != nullptr);
    CHECK(saved->prefs.openAtLogin);
    CHECK_FALSE(saved->prefs.showTooltips);
    CHECK_FALSE(saved->restartEngineIfNeeded);
    coord.Dispatch(std::move(*saved));
    snap = coord.Snapshot();
    LoadPrefsFromSnapshot(*snap, ps);
    CHECK_FALSE(IsPrefsDraftDirty(*snap, ps));

    // Saving RPC port change signals engine restart required.
    sink.commands.clear();
    ps.rpcPort = 16900;
    CHECK(ApplyPrefsDraft(*snap, sink, ps));
    REQUIRE(sink.commands.size() == 1u);
    auto* restartCmd = std::get_if<motrix::app::SavePreferencesCmd>(&sink.commands.front());
    REQUIRE(restartCmd != nullptr);
    CHECK(restartCmd->restartEngineIfNeeded);
    CHECK(restartCmd->sysConfig.rpcListenPort == 16900);
}

TEST_CASE("motrix.ui MotrixUiShell renders all Preferences tabs and actions bar headlessly",
          "[ui][prefs][shell]") {
    ImGuiContext* ctx = ImGui::CreateContext();
    REQUIRE(ctx != nullptr);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024.0f, 768.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    motrix::core::Logger logger;
    motrix::app::AppCoordinator coord(
        motrix::app::CoordinatorOptions{.enableEngine = false, .loadPersistedConfig = false}, logger);
    coord.Dispatch(motrix::app::SelectSectionCmd{1});

    StubWindowHost host;
    RecordingCommandSink sink;
    motrix::ui::MotrixUiShell shell(false);
    shell.InitializeImGui(host, false, "Microsoft YaHei UI", {});
    for (int tab = 0; tab < 3; ++tab) {
        shell.SetPreferencesTab(tab);
        shell.PrepareFrame();
        ImGui::NewFrame();
        shell.RenderFrame(*coord.Snapshot(), sink, host);
        ImGui::EndFrame();
    }
    ImGui::DestroyContext(ctx);
}

TEST_CASE("motrix.ui AnimState evicts stale keys and AppCoordinator prunes removed completion statuses",
          "[ui][anim][app]") {
    using namespace motrix::ui;

    AnimState anim;
    anim.BeginFrame(1.0f / 60.0f);
    (void)anim.Float(HashId("active"), 1.0f, 18.0f, 0.0f);
    (void)anim.Float(HashId("stale"), 1.0f, 18.0f, 0.0f);
    CHECK(anim.EntryCount() == 2u);

    // Keep refreshing only "active" past the 120-frame TTL (modulo-64 sweeps run on frame 128).
    for (int f = 0; f < 130; ++f) {
        anim.BeginFrame(1.0f / 60.0f);
        (void)anim.Float(HashId("active"), 1.0f, 18.0f, 0.0f);
    }
    CHECK(anim.EntryCount() == 1u);
    anim.PruneStale(0u);
    CHECK(anim.EntryCount() == 1u);
    for (int f = 0; f < 130; ++f) {
        anim.BeginFrame(1.0f / 60.0f);
    }
    CHECK(anim.EntryCount() == 0u);

    motrix::app::TaskCompletionTracker tracker;
    motrix::app::AppStateSnapshot snap;
    snap.downloadingTasks.push_back(motrix::domain::DownloadTaskView{
        .gid = "g1", .name = "a.bin", .status = motrix::domain::TaskStatus::Active});
    CHECK(motrix::app::UpdateTaskCompletionTracker(tracker, snap).empty());
    CHECK(tracker.initialSyncSeen);
    CHECK(tracker.lastStatuses.size() == 1u);

    snap.downloadingTasks.clear();
    snap.stoppedTasks.push_back(
        motrix::domain::DownloadTaskView{.gid = "g1",
                                         .name = "a.bin",
                                         .status = motrix::domain::TaskStatus::Complete,
                                         .dir = R"(D:\Downloads)"});
    snap.stoppedTasks.push_back(
        motrix::domain::DownloadTaskView{.gid = "g2",
                                         .name = "b.bin",
                                         .status = motrix::domain::TaskStatus::Complete,
                                         .dir = R"(D:\Downloads)"});
    const auto firstNotices = motrix::app::UpdateTaskCompletionTracker(tracker, snap);
    REQUIRE(firstNotices.size() == 2u);
    CHECK(tracker.lastStatuses.size() == 2u);
    CHECK(motrix::app::UpdateTaskCompletionTracker(tracker, snap).empty());

    snap.stoppedTasks.pop_back();
    CHECK(motrix::app::UpdateTaskCompletionTracker(tracker, snap).empty());
    CHECK(tracker.lastStatuses.size() == 1u);
    CHECK(tracker.lastStatuses.contains("g1"));
    CHECK_FALSE(tracker.lastStatuses.contains("g2"));
}
