module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.core;
import motrix.app;
import motrix.platform;
import :views_internal;

namespace motrix::ui {

struct MotrixUiShell::Impl {
    explicit Impl(bool initialDark) : theme(initialDark) {}

    ThemeController theme;
    FontRegistry fonts;
    AnimState anim;
    motrix::core::Translator translator;
    AddTaskFormState addForm;
    DrawerUiState drawerState;
    PrefsFormState prefsForm;
    DeleteConfirmState deleteConfirm;
    int prefTab = 0;
    float lastDpiScale = 0.0f;
    std::filesystem::path assetsFontsDir;
    std::string fontFamily = "Microsoft YaHei UI";
    std::string monoFontFamily;
    bool fontReloadPending = false;
};

MotrixUiShell::MotrixUiShell(bool initialDark) : impl_(std::make_unique<Impl>(initialDark)) {
}

MotrixUiShell::~MotrixUiShell() = default;

void MotrixUiShell::InitializeImGui(motrix::platform::IWindowHost& host,
                                    bool initialDark,
                                    std::string_view fontFamily,
                                    std::string_view monoFontFamily) {
    impl_->lastDpiScale = host.DpiScale();
    impl_->assetsFontsDir = ResolveAssetsFontsDir();
    impl_->fontFamily = fontFamily.empty() ? std::string("Microsoft YaHei UI") : std::string(fontFamily);
    impl_->monoFontFamily = std::string(monoFontFamily);
    impl_->fontReloadPending = false;
    impl_->theme.SetDark(initialDark, true);
    impl_->theme.ApplyImGuiStyleGeometry(impl_->lastDpiScale);
    impl_->theme.ApplyImGuiStyleColors();
    impl_->fonts.LoadAll(impl_->assetsFontsDir, impl_->fontFamily, impl_->monoFontFamily);
    host.SetDarkFrame(initialDark);
}

void MotrixUiShell::PrepareFrame() {
    if (!impl_->fontReloadPending) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts) {
        io.Fonts->Clear();
        impl_->fonts.LoadAll(impl_->assetsFontsDir, impl_->fontFamily, impl_->monoFontFamily);
    }
    impl_->fontReloadPending = false;
}

void MotrixUiShell::PrefillAddTaskUri(std::string_view uri) {
    const std::size_t n = std::min(uri.size(), impl_->addForm.uriBuffer.size() - 1);
    std::copy_n(uri.data(), n, impl_->addForm.uriBuffer.data());
    impl_->addForm.uriBuffer[n] = '\0';
    impl_->addForm.focusRequested = true;
}

void MotrixUiShell::SetPreferencesTab(int tab) {
    impl_->prefTab = std::clamp(tab, 0, 2);
}

namespace {

void DispatchShellShortcuts(UiFrameContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::AddTask});
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        ctx.sink.Dispatch(motrix::app::ToggleThemeCmd{});
    }
    const bool inTaskList = !io.WantTextInput && ctx.state.activeSection == 0
                            && ctx.state.activeModal == motrix::app::ActiveModal::None
                            && ctx.state.detailedTaskGid.empty()
                            && (ctx.deleteConfirm == nullptr || !ctx.deleteConfirm->open);
    if (inTaskList && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        ctx.sink.Dispatch(motrix::app::SelectAllTasksCmd{});
    }
    if (inTaskList && !ctx.state.selectedTaskGids.empty()
        && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        RequestTasksRemoval(ctx, ctx.state.selectedTaskGids, {});
    }
}

void RenderRootWorkspace(UiFrameContext& ctx, int& prefTab, PrefsFormState& prefsForm) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float dpi = ctx.dpiScale;
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    StyleVarGuard padGuard(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard roundGuard(ImGuiStyleVar_WindowRounding, 0.0f);
    StyleVarGuard borderGuard(ImGuiStyleVar_WindowBorderSize, 0.0f);
    StyleColorGuard bgGuard(ImGuiCol_WindowBg, ctx.palette.mainBg);

    constexpr int kRootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
                               | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    WindowGuard root("##MotrixRoot", nullptr, kRootFlags);
    if (!root.IsOpen()) {
        return;
    }
    float curX = vp->Pos.x;
    const float topY = vp->Pos.y;
    const float totalH = vp->Size.y;

    if (vp->Size.x >= LayoutMetrics::kAsideHideThreshold * dpi) {
        RenderAsideRail(ctx, curX, topY, totalH);
        curX += LayoutMetrics::kAsideWidth * dpi;
    }
    if (vp->Size.x >= LayoutMetrics::kSubnavHideThreshold * dpi) {
        RenderSubnav(ctx, {curX, topY, LayoutMetrics::kSubnavWidth * dpi, totalH}, prefTab);
        curX += LayoutMetrics::kSubnavWidth * dpi;
    }

    const PanelRect mainRect{
        curX, topY, std::max(120.0f * dpi, (vp->Pos.x + vp->Size.x) - curX), totalH};
    if (ctx.state.activeSection == 0) {
        RenderTaskPanel(ctx, mainRect);
    } else {
        RenderPreferencesView(ctx, mainRect, prefTab, prefsForm);
    }
    RenderSpeedometer(ctx, vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    RenderTitlebarOverlay(ctx);
}

}  // namespace

void MotrixUiShell::RenderFrame(const motrix::app::AppStateSnapshot& state,
                                motrix::app::ICommandSink& sink,
                                motrix::platform::IWindowHost& host) {
    const float dpi = host.DpiScale();
    if (std::fabs(dpi - impl_->lastDpiScale) > 0.01f) {
        impl_->lastDpiScale = dpi;
        impl_->theme.ApplyImGuiStyleGeometry(dpi);
    }

    const bool targetDark = ResolveThemeIsDark(state.prefs.theme, host.IsSystemDarkMode());
    if (targetDark != impl_->theme.IsDark()) {
        impl_->theme.SetDark(targetDark, false);
        host.SetDarkFrame(targetDark);
    }

    const std::string targetFont =
        state.prefs.fontFamily.empty() ? std::string("Microsoft YaHei UI") : state.prefs.fontFamily;
    if (targetFont != impl_->fontFamily || state.prefs.monoFontFamily != impl_->monoFontFamily) {
        impl_->fontFamily = targetFont;
        impl_->monoFontFamily = state.prefs.monoFontFamily;
        impl_->fontReloadPending = true;
        host.NotifyAnimationActive();
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool themeMoving = impl_->theme.Update(io.DeltaTime);
    impl_->theme.ApplyImGuiStyleColors();
    impl_->anim.BeginFrame(io.DeltaTime);
    impl_->anim.SetTooltipsEnabled(state.prefs.showTooltips);
    impl_->translator.SetLocale(state.prefs.locale);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    UiFrameContext ctx{
        .state = state,
        .sink = sink,
        .host = host,
        .tr = impl_->translator,
        .fonts = impl_->fonts,
        .palette = impl_->theme.Current(),
        .anim = impl_->anim,
        .deleteConfirm = &impl_->deleteConfirm,
        .dpiScale = dpi,
        .viewportW = vp->Size.x,
        .viewportH = vp->Size.y,
    };
    DispatchShellShortcuts(ctx);

    RenderRootWorkspace(ctx, impl_->prefTab, impl_->prefsForm);
    RenderTaskDetailDrawer(ctx, impl_->drawerState);
    RenderModals(ctx, impl_->addForm);
    impl_->anim.FlushDeferredOverlay();

    const bool overlayActive = (impl_->drawerState.slideProgress > 0.01f)
                               || (state.activeModal != motrix::app::ActiveModal::None)
                               || impl_->deleteConfirm.open;
    host.SetTitlebarBlocked(overlayActive);

    if (themeMoving || impl_->anim.AnyActive()) {
        host.NotifyAnimationActive();
    }
}

}  // namespace motrix::ui
