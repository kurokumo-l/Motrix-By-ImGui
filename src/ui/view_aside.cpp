module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.app;
import :views_internal;
import :theme;
import :anim;
import :icons;

namespace motrix::ui {

namespace {

bool AsideIconButton(UiFrameContext& ctx,
                     const char* id,
                     MotrixIcon icon,
                     float centerX,
                     float topY,
                     bool active,
                     std::string_view tooltip) {
    const float s = ctx.dpiScale;
    const float btnSize = 32.0f * s;
    const float iconSize = 20.0f * s;
    const ImVec2 min(centerX - btnSize * 0.5f, topY);
    const ImVec2 max(min.x + btnSize, min.y + btnSize);

    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(btnSize, btnSize));
    const bool hovered = ImGui::IsItemHovered();

    const float targetAlpha = hovered ? 1.0f : (active ? 0.65f : 0.0f);
    const float bgT = ctx.anim.Float(HashId(id), targetAlpha, 18.0f, targetAlpha);
    if (bgT > 0.01f) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const RgbaColor bg = WithAlpha(ctx.palette.asideHoverBg, ctx.palette.asideHoverBg.a * bgT);
        dl->AddCircleFilled(ImVec2(centerX, topY + btnSize * 0.5f), btnSize * 0.5f, ToAbgrU32(bg), 28);
    }

    DrawMotrixIcon(icon,
                   min.x + (btnSize - iconSize) * 0.5f,
                   min.y + (btnSize - iconSize) * 0.5f,
                   iconSize,
                   iconSize,
                   ctx.palette.asideIcon,
                   s);

    if (hovered && !tooltip.empty()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(tooltip.size()), tooltip.data());
    }
    return clicked;
}

}  // namespace

void RenderAsideRail(UiFrameContext& ctx, float x, float y, float h) {
    const float s = ctx.dpiScale;
    const float w = LayoutMetrics::kAsideWidth * s;
    const float cx = x + w * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.asideBg));

    // Top Motrix "M" Logo (28x14 viewBox centered in 28x28 box at y + 40)
    const float logoW = 28.0f * s;
    const float logoH = 14.0f * s;
    const float logoBoxY = y + 40.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(cx - logoW * 0.5f, logoBoxY));
    if (ImGui::InvisibleButton("##aside_logo", ImVec2(logoW, 28.0f * s))) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::About});
    }
    DrawMotrixIcon(MotrixIcon::LogoMini,
                   cx - logoW * 0.5f,
                   logoBoxY + (28.0f * s - logoH) * 0.5f,
                   logoW,
                   logoH,
                   ctx.palette.asideIcon,
                   s);

    float curY = logoBoxY + 28.0f * s + 24.0f * s;
    if (AsideIconButton(ctx,
                        "##aside_tasks",
                        MotrixIcon::MenuTask,
                        cx,
                        curY,
                        ctx.state.activeSection == 0,
                        ctx.tr.Tr("app.tasks"))) {
        ctx.sink.Dispatch(motrix::app::SelectSectionCmd{0});
    }

    curY += 32.0f * s + 24.0f * s;
    if (AsideIconButton(ctx,
                        "##aside_add",
                        MotrixIcon::MenuAdd,
                        cx,
                        curY,
                        ctx.state.activeModal == motrix::app::ActiveModal::AddTask,
                        ctx.tr.Tr("app.add_task"))) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::AddTask});
    }

    const float aboutY = y + h - 24.0f * s - 32.0f * s;
    const float prefY = aboutY - 24.0f * s - 32.0f * s;

    if (AsideIconButton(ctx,
                        "##aside_prefs",
                        MotrixIcon::MenuPreference,
                        cx,
                        prefY,
                        ctx.state.activeSection == 1,
                        ctx.tr.Tr("app.preferences"))) {
        ctx.sink.Dispatch(motrix::app::SelectSectionCmd{1});
    }

    if (AsideIconButton(ctx,
                        "##aside_about",
                        MotrixIcon::MenuAbout,
                        cx,
                        aboutY,
                        ctx.state.activeModal == motrix::app::ActiveModal::About,
                        ctx.tr.Tr("app.about"))) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::About});
    }

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui
