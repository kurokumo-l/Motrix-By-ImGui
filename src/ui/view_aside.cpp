module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.app;
import :views_internal;
import :el_widgets;
import :theme;
import :anim;
import :icons;

namespace motrix::ui {

namespace {

struct AsideBtnSpec {
    const char* id = "";
    MotrixIcon icon = MotrixIcon::MenuTask;
    float centerX = 0.0f;
    float topY = 0.0f;
    bool active = false;
    std::string_view tooltip{};
};

bool AsideIconButton(UiFrameContext& ctx, const AsideBtnSpec& spec) {
    const float s = ctx.dpiScale;
    const float btnSize = 32.0f * s;
    const float iconSize = 20.0f * s;
    const ImVec2 min(spec.centerX - btnSize * 0.5f, spec.topY);

    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(btnSize, btnSize));
    const bool hovered = ImGui::IsItemHovered();

    const float targetAlpha = hovered ? 1.0f : (spec.active ? 0.65f : 0.0f);
    const float bgT = ctx.anim.Float(HashId(spec.id), targetAlpha, 18.0f, targetAlpha);
    if (bgT > 0.01f) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const RgbaColor bg = WithAlpha(ctx.palette.asideHoverBg, ctx.palette.asideHoverBg.a * bgT);
        dl->AddCircleFilled(
            ImVec2(spec.centerX, spec.topY + btnSize * 0.5f), btnSize * 0.5f, ToAbgrU32(bg), 28);
    }

    DrawMotrixIcon(IconSpec{
        spec.icon,
        min.x + (btnSize - iconSize) * 0.5f,
        min.y + (btnSize - iconSize) * 0.5f,
        iconSize,
        iconSize,
        ctx.palette.asideIcon,
        s,
    });

    if (hovered && !spec.tooltip.empty()) {
        ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim}, {spec.tooltip, ElTooltipPlacement::Right});
    }
    return clicked;
}

void RenderAsideLogo(UiFrameContext& ctx, float cx, float logoBoxY) {
    const float s = ctx.dpiScale;
    const float logoW = 28.0f * s;
    const float logoH = 14.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(cx - logoW * 0.5f, logoBoxY));
    if (ImGui::InvisibleButton("##aside_logo", ImVec2(logoW, 28.0f * s))) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::About});
    }
    const float logoHovT =
        ctx.anim.Float(HashId("##aside_logo"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f, 0.0f);
    if (logoHovT > 0.01f) {
        const RgbaColor halo =
            WithAlpha(ctx.palette.asideHoverBg, ctx.palette.asideHoverBg.a * 0.6f * logoHovT);
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cx, logoBoxY + 14.0f * s), 18.0f * s, ToAbgrU32(halo), 28);
    }
    DrawMotrixIcon(IconSpec{
        MotrixIcon::LogoMini,
        cx - logoW * 0.5f,
        logoBoxY + (28.0f * s - logoH) * 0.5f,
        logoW,
        logoH,
        LerpColor(ctx.palette.asideIcon, HexColor(0xFFFFFF), logoHovT * 0.25f),
        s,
    });
}

}  // namespace

void RenderAsideRail(UiFrameContext& ctx, float x, float y, float h) {
    const float s = ctx.dpiScale;
    const float w = LayoutMetrics::kAsideWidth * s;
    const float cx = x + w * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.asideBg));

    const float logoBoxY = y + 40.0f * s;
    RenderAsideLogo(ctx, cx, logoBoxY);

    float curY = logoBoxY + 28.0f * s + 24.0f * s;
    if (AsideIconButton(ctx,
                        {"##aside_tasks",
                         MotrixIcon::MenuTask,
                         cx,
                         curY,
                         ctx.state.activeSection == 0,
                         ctx.tr.Tr("app.tasks")})) {
        ctx.sink.Dispatch(motrix::app::SelectSectionCmd{0});
    }

    curY += 32.0f * s + 24.0f * s;
    if (AsideIconButton(ctx,
                        {"##aside_add",
                         MotrixIcon::MenuAdd,
                         cx,
                         curY,
                         ctx.state.activeModal == motrix::app::ActiveModal::AddTask,
                         ctx.tr.Tr("app.add_task")})) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::AddTask});
    }

    const float aboutY = y + h - 24.0f * s - 32.0f * s;
    const float prefY = aboutY - 24.0f * s - 32.0f * s;

    if (AsideIconButton(ctx,
                        {"##aside_prefs",
                         MotrixIcon::MenuPreference,
                         cx,
                         prefY,
                         ctx.state.activeSection == 1,
                         ctx.tr.Tr("app.preferences")})) {
        ctx.sink.Dispatch(motrix::app::SelectSectionCmd{1});
    }

    if (AsideIconButton(ctx,
                        {"##aside_about",
                         MotrixIcon::MenuAbout,
                         cx,
                         aboutY,
                         ctx.state.activeModal == motrix::app::ActiveModal::About,
                         ctx.tr.Tr("app.about")})) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::About});
    }

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui
