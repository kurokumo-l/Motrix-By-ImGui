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
    int prefTab = 0;
    float lastDpiScale = 0.0f;
};

MotrixUiShell::MotrixUiShell(bool initialDark) : impl_(std::make_unique<Impl>(initialDark)) {
}

MotrixUiShell::~MotrixUiShell() = default;

void MotrixUiShell::InitializeImGui(motrix::platform::IWindowHost& host, bool initialDark) {
    impl_->lastDpiScale = host.DpiScale();
    impl_->theme.SetDark(initialDark, true);
    impl_->theme.ApplyImGuiStyleGeometry(impl_->lastDpiScale);
    impl_->theme.ApplyImGuiStyleColors();
    impl_->fonts.LoadAll(ResolveAssetsFontsDir());
    host.SetDarkFrame(initialDark);
}

void MotrixUiShell::PrefillAddTaskUri(std::string_view uri) {
    const std::size_t n = std::min(uri.size(), impl_->addForm.uriBuffer.size() - 1);
    std::copy_n(uri.data(), n, impl_->addForm.uriBuffer.data());
    impl_->addForm.uriBuffer[n] = '\0';
}

void MotrixUiShell::SetPreferencesTab(int tab) {
    impl_->prefTab = std::clamp(tab, 0, 2);
}

namespace {

void DispatchShellShortcuts(motrix::app::ICommandSink& sink) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::AddTask});
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        sink.Dispatch(motrix::app::ToggleThemeCmd{});
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

    if (ctx.state.prefs.hideAppMenu) {
        RenderTitlebarOverlay(ctx);
    }
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

    const bool targetDark = ResolveThemeIsDark(state.prefs.theme, false);
    if (targetDark != impl_->theme.IsDark()) {
        impl_->theme.SetDark(targetDark, false);
        host.SetDarkFrame(targetDark);
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool themeMoving = impl_->theme.Update(io.DeltaTime);
    impl_->theme.ApplyImGuiStyleColors();
    impl_->anim.BeginFrame(io.DeltaTime);
    impl_->translator.SetLocale(state.prefs.locale);
    DispatchShellShortcuts(sink);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    UiFrameContext ctx{
        .state = state,
        .sink = sink,
        .host = host,
        .tr = impl_->translator,
        .fonts = impl_->fonts,
        .palette = impl_->theme.Current(),
        .anim = impl_->anim,
        .dpiScale = dpi,
        .viewportW = vp->Size.x,
        .viewportH = vp->Size.y,
    };

    RenderRootWorkspace(ctx, impl_->prefTab, impl_->prefsForm);
    RenderTaskDetailDrawer(ctx, impl_->drawerState);
    RenderModals(ctx, impl_->addForm);

    if (themeMoving || impl_->anim.AnyActive()) {
        host.NotifyAnimationActive();
    }
}

}  // namespace motrix::ui
