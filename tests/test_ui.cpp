// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format on

#include "imgui.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import motrix.platform;
import motrix.ui;

namespace {

std::atomic<std::uint64_t> g_uiTestDirSeq{0};

struct UiTempDirGuard {
    std::filesystem::path path;

    explicit UiTempDirGuard(std::string_view tag) {
        const auto seq = g_uiTestDirSeq.fetch_add(1, std::memory_order_relaxed);
        path = std::filesystem::temp_directory_path()
               / std::format("motrix_ui_test_{}_{}_{}", ::GetCurrentProcessId(), seq, tag);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~UiTempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    UiTempDirGuard(const UiTempDirGuard&) = delete;
    UiTempDirGuard& operator=(const UiTempDirGuard&) = delete;
};

struct ImGuiContextGuard {
    ImGuiContext* ctx = nullptr;
    bool inFrame = false;

    explicit ImGuiContextGuard(ImVec2 displaySize = ImVec2(1024.0f, 768.0f)) {
        ctx = ImGui::CreateContext();
        REQUIRE(ctx != nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = displaySize;
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }

    ~ImGuiContextGuard() {
        if (ctx != nullptr) {
            if (inFrame) {
                ImGui::EndFrame();
            }
            ImGui::DestroyContext(ctx);
        }
    }

    void BeginFrame() {
        ImGui::NewFrame();
        inFrame = true;
    }

    void EndFrame() {
        if (inFrame) {
            ImGui::EndFrame();
            inFrame = false;
        }
    }

    void RenderFrame() {
        if (inFrame) {
            ImGui::Render();
            inFrame = false;
        }
    }

    ImGuiContextGuard(const ImGuiContextGuard&) = delete;
    ImGuiContextGuard& operator=(const ImGuiContextGuard&) = delete;
};

}  // namespace

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
    ImGuiContextGuard imgui;
    ImGuiIO& io = ImGui::GetIO();

    motrix::ui::FontRegistry fonts;
    const auto assetsDir = motrix::ui::ResolveAssetsFontsDir();
    REQUIRE(fonts.LoadAll(assetsDir));
    REQUIRE(fonts.IsLoaded());
    REQUIRE(io.Fonts->FontLoaderName != nullptr);
    CHECK(std::string_view(io.Fonts->FontLoaderName) == "DirectWrite");

    imgui.BeginFrame();
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

    imgui.EndFrame();
}

TEST_CASE("motrix.ui FontRegistry supports custom fontFamily and generic file override", "[ui][fonts]") {
    ImGuiContextGuard imgui;
    ImGuiIO& io = ImGui::GetIO();

    REQUIRE(motrix::ui::IsFontFamilyInstalled("Microsoft YaHei UI"));
    const std::string customFamily = motrix::ui::IsFontFamilyInstalled("MiSans") ? "MiSans" : "Segoe UI";

    motrix::ui::FontRegistry fonts;
    const auto assetsDir = motrix::ui::ResolveAssetsFontsDir();
    REQUIRE(fonts.LoadAll(assetsDir, customFamily, {}));
    imgui.BeginFrame();
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
    imgui.EndFrame();

    const auto srcMono = assetsDir / "ui-mono.ttf";
    REQUIRE(std::filesystem::is_regular_file(srcMono));
    UiTempDirGuard tempDir("font_override");
    std::error_code ec;
    REQUIRE(std::filesystem::copy_file(srcMono,
                                       tempDir.path / "ui-regular.ttf",
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec));

    io.Fonts->Clear();
    motrix::ui::FontRegistry overrideFonts;
    REQUIRE(overrideFonts.LoadAll(tempDir.path, "Microsoft YaHei UI", {}));
    imgui.BeginFrame();
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
    imgui.EndFrame();
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
    [[nodiscard]] std::uint64_t TextureGeneration() const noexcept override { return textureGeneration; }
    [[nodiscard]] std::uint64_t UploadRgbaTexture(std::uint64_t existingId,
                                                  int width,
                                                  int height,
                                                  std::span<const std::uint8_t> /*rgba*/) override {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        uploadedExistingIds.push_back(existingId);
        return ++nextTexId;
    }

    int animationWakeups = 0;
    std::uint64_t textureGeneration = 1;
    std::uint64_t nextTexId = 100;
    std::vector<std::uint64_t> uploadedExistingIds;
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
    ImGuiContextGuard imgui;

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
        imgui.BeginFrame();
        shell.RenderFrame(*coord.Snapshot(), sink, host);
        imgui.RenderFrame();
        const ImDrawData* dd = ImGui::GetDrawData();
        REQUIRE(dd != nullptr);
        CHECK(dd->Valid);
        CHECK(dd->TotalVtxCount > 0);
    }

    // End-to-end check for ElInputNumberFloat NaN recovery via seedRatio on Basic tab.
    ImGuiContextGuard nanImgui;
    motrix::ui::MotrixUiShell nanShell(false);
    nanShell.InitializeImGui(host, false, "Microsoft YaHei UI", {});
    motrix::app::AppStateSnapshot nanSnap = *coord.Snapshot();
    nanSnap.activeSection = 1;
    nanSnap.prefs.keepSeeding = false;
    nanSnap.sysConfig.seedRatio = std::numeric_limits<double>::quiet_NaN();
    nanShell.SetPreferencesTab(0);
    for (int frame = 0; frame < 2; ++frame) {
        nanShell.PrepareFrame();
        nanImgui.BeginFrame();
        nanShell.RenderFrame(nanSnap, sink, host);
        nanImgui.RenderFrame();
        const ImDrawData* dd = ImGui::GetDrawData();
        REQUIRE(dd != nullptr);
        CHECK(dd->Valid);
        CHECK(dd->TotalVtxCount > 0);
        for (const ImDrawList* cmdList : dd->CmdLists) {
            REQUIRE(cmdList != nullptr);
            for (const ImDrawVert& vtx : cmdList->VtxBuffer) {
                CHECK(std::isfinite(vtx.pos.x));
                CHECK(std::isfinite(vtx.pos.y));
                CHECK(std::isfinite(vtx.uv.x));
                CHECK(std::isfinite(vtx.uv.y));
            }
        }
    }
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

TEST_CASE("motrix.ui IdGuard, AnimState, and ThemeController handle empty and NaN inputs safely",
          "[ui][guards][anim][theme]") {
    using namespace motrix::ui;

    ImGuiContextGuard imgui;
    ImGui::GetIO().Fonts->AddFontDefault();
    imgui.BeginFrame();
    {
        WindowGuard win("##id_guard_probe", nullptr, ImGuiWindowFlags_NoSavedSettings);
        REQUIRE(win.IsOpen());
        const ImGuiID beforeId = ImGui::GetID("after");
        {
            const std::string_view defaultEmpty{};
            IdGuard g0(defaultEmpty);
            (void)ImGui::GetID("inner0");
        }
        {
            const std::string src = "abc";
            IdGuard g1(std::string_view(src).substr(0, 0));
            (void)ImGui::GetID("inner1");
        }
        {
            IdGuard g2(std::string_view("valid_segment"));
            (void)ImGui::GetID("inner2");
        }
        CHECK(ImGui::GetID("after") == beforeId);
    }
    imgui.RenderFrame();

    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    AnimState nanAnim;
    nanAnim.BeginFrame(kNaN);
    CHECK(nanAnim.Float(HashId("nan_init"), 1.0f, 18.0f, 0.0f) == 0.0f);
    nanAnim.BeginFrame(kNaN);
    CHECK(nanAnim.Float(HashId("nan_init"), 1.0f, 18.0f, 0.0f) == 0.0f);
    nanAnim.BeginFrame(1.0f / 60.0f);
    const float recovered = nanAnim.Float(HashId("nan_init"), 1.0f, 18.0f, 0.0f);
    CHECK(std::isfinite(recovered));
    CHECK(recovered > 0.0f);

    nanAnim.Set(HashId("nan_set"), kNaN);
    const float setStep = nanAnim.Float(HashId("nan_set"), 1.0f, 18.0f);
    CHECK(std::isfinite(setStep));
    CHECK(setStep > 0.0f);
    CHECK(setStep <= 1.0f);
    for (int i = 0; i < 60; ++i) {
        nanAnim.BeginFrame(1.0f / 60.0f);
        (void)nanAnim.Float(HashId("nan_set"), 1.0f, 18.0f);
    }
    CHECK(nanAnim.Float(HashId("nan_set"), 1.0f, 18.0f) == 1.0f);
    CHECK(nanAnim.Float(HashId("nan_target"), kNaN, 18.0f, kNaN) == 0.0f);
    CHECK(nanAnim.Float(HashId("zero_speed"), 0.5f, 0.0f, 0.0f) == 0.0f);
    CHECK(nanAnim.Float(HashId("zero_speed"), 0.5f, 0.0f, 0.0f) == 0.5f);
    CHECK(AnimStep(nullptr, {HashId("null_nan"), kNaN}) == 0.0f);

    ThemeController theme(false);
    theme.SetDark(true, false);
    CHECK(theme.Update(kNaN));
    theme.ApplyImGuiStyleColors();
    const ImVec4 winBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    CHECK(std::isfinite(winBg.x));
    CHECK(std::isfinite(winBg.y));
    CHECK(std::isfinite(winBg.z));
    CHECK(std::isfinite(winBg.w));

    theme.ApplyImGuiStyleGeometry(kNaN);
    CHECK(ImGui::GetStyle().FontScaleDpi == 1.0f);
    CHECK(std::isfinite(ImGui::GetStyle().FrameRounding));
    theme.ApplyImGuiStyleGeometry(0.0f);
    CHECK(ImGui::GetStyle().FontScaleDpi == 1.0f);
}

TEST_CASE("motrix.ui PrefillAddTaskUri truncates cleanly on UTF-8 boundaries and modal gates Ctrl+N",
          "[ui][shell][utf8]") {
    ImGuiContextGuard imgui;
    StubWindowHost host;
    RecordingCommandSink sink;
    motrix::ui::MotrixUiShell shell(false);
    shell.InitializeImGui(host, false, "Microsoft YaHei UI", {});

    constexpr std::size_t kMaxUriBytes = 4095u;

    shell.PrefillAddTaskUri(std::string_view{});

    std::string threeByteCut(kMaxUriBytes - 1u, 'a');
    threeByteCut.append("\xE4\xB8\xAD_tail");
    shell.PrefillAddTaskUri(threeByteCut);

    motrix::app::AppStateSnapshot modalSnap;
    modalSnap.activeModal = motrix::app::ActiveModal::AddTask;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_N, true);
    shell.PrepareFrame();
    imgui.BeginFrame();
    shell.RenderFrame(modalSnap, sink, host);
    imgui.RenderFrame();
    const ImDrawData* dd3 = ImGui::GetDrawData();
    REQUIRE(dd3 != nullptr);
    CHECK(dd3->Valid);
    CHECK(dd3->TotalVtxCount > 0);
    CHECK(sink.commands.empty());

    io.AddKeyEvent(ImGuiKey_N, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    shell.PrepareFrame();
    imgui.BeginFrame();
    shell.RenderFrame(modalSnap, sink, host);
    imgui.RenderFrame();

    std::string fourByteCut(kMaxUriBytes - 2u, 'b');
    fourByteCut.append("\xF0\x9F\x9A\x80_end");
    shell.PrefillAddTaskUri(fourByteCut);
    shell.PrepareFrame();
    imgui.BeginFrame();
    shell.RenderFrame(modalSnap, sink, host);
    imgui.RenderFrame();
    const ImDrawData* dd4 = ImGui::GetDrawData();
    REQUIRE(dd4 != nullptr);
    CHECK(dd4->Valid);
    CHECK(dd4->TotalVtxCount > 0);

    motrix::app::AppStateSnapshot idleSnap;
    idleSnap.activeModal = motrix::app::ActiveModal::None;
    shell.PrepareFrame();
    imgui.BeginFrame();
    shell.RenderFrame(idleSnap, sink, host);
    imgui.RenderFrame();
    CHECK(sink.commands.empty());

    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_N, true);
    shell.PrepareFrame();
    imgui.BeginFrame();
    shell.RenderFrame(idleSnap, sink, host);
    imgui.RenderFrame();
    REQUIRE(sink.commands.size() == 1u);
    const auto* openCmd = std::get_if<motrix::app::OpenModalCmd>(&sink.commands.front());
    REQUIRE(openCmd != nullptr);
    CHECK(openCmd->modal == motrix::app::ActiveModal::AddTask);
}

TEST_CASE("motrix.ui FontRegistry falls back to system UI font when override file is invalid",
          "[ui][fonts]") {
    ImGuiContextGuard imgui;
    UiTempDirGuard tempDir("font_corrupt_fallback");
    {
        std::ofstream badFont(tempDir.path / "ui-regular.ttf", std::ios::binary | std::ios::trunc);
        REQUIRE(badFont.is_open());
        badFont << "not_a_valid_ttf_file";
    }

    motrix::ui::FontRegistry fonts;
    REQUIRE(fonts.LoadAll(tempDir.path, "Microsoft YaHei UI", {}));
    REQUIRE(fonts.IsLoaded());
    imgui.BeginFrame();
    auto* reg = static_cast<ImFont*>(fonts.FontPtr(motrix::ui::FontRole::Regular));
    REQUIRE(reg != nullptr);
    ImFontBaked* baked = reg->GetFontBaked(15.0f);
    REQUIRE(baked != nullptr);
    const ImFontGlyph* cjkPtr = baked->FindGlyph(static_cast<ImWchar>(0x4E0B));
    REQUIRE(cjkPtr != nullptr);
    CHECK(cjkPtr->Visible);
    imgui.EndFrame();
}

TEST_CASE("motrix.ui CachedTextureSlot invalidates stale texId with existingId == 0 across "
          "TextureGeneration bumps",
          "[ui][icons][dx11]") {
    using namespace motrix::ui;

    ImGuiContextGuard imgui;
    ImGui::GetIO().Fonts->AddFontDefault();
    StubWindowHost host;
    CachedTextureSlot ufoCache{};
    CachedTextureSlot appCache{};

    const auto drawFrame = [&](bool dark) {
        imgui.BeginFrame();
        {
            WindowGuard win("##tex_gen_probe", nullptr, ImGuiWindowFlags_NoSavedSettings);
            REQUIRE(win.IsOpen());
            DrawEmptyUfoIllustration({200.0f, 50.0f, 320.0f, 200.0f, dark, &host, &ufoCache});
            DrawMotrixAppIcon({20.0f, 20.0f, 72.0f, dark, &host, &appCache});
        }
        imgui.RenderFrame();
    };

    drawFrame(false);
    REQUIRE(host.uploadedExistingIds.size() == 2u);
    CHECK(host.uploadedExistingIds[0] == 0u);
    CHECK(host.uploadedExistingIds[1] == 0u);
    CHECK(ufoCache.texId == 101u);
    CHECK(ufoCache.generation == 1u);
    CHECK(appCache.texId == 102u);
    CHECK(appCache.generation == 1u);

    // Same generation, theme, and size must hit cache without uploading again.
    drawFrame(false);
    CHECK(host.uploadedExistingIds.size() == 2u);

    // Theme change on the same device generation passes previous existingId for reuse.
    drawFrame(true);
    REQUIRE(host.uploadedExistingIds.size() == 4u);
    CHECK(host.uploadedExistingIds[2] == 101u);
    CHECK(host.uploadedExistingIds[3] == 102u);
    CHECK(ufoCache.texId == 103u);
    CHECK(appCache.texId == 104u);

    // Incrementing TextureGeneration (e.g. DX11 device lost) must discard stale IDs and pass 0.
    host.textureGeneration = 2;
    drawFrame(true);
    REQUIRE(host.uploadedExistingIds.size() == 6u);
    CHECK(host.uploadedExistingIds[4] == 0u);
    CHECK(host.uploadedExistingIds[5] == 0u);
    CHECK(ufoCache.texId == 105u);
    CHECK(ufoCache.generation == 2u);
    CHECK(appCache.texId == 106u);
    CHECK(appCache.generation == 2u);
}
