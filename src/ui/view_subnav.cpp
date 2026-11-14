module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

bool SubnavItemButton(UiFrameContext& ctx,
                      const char* id,
                      std::optional<MotrixIcon> icon,
                      std::string_view label,
                      float x,
                      float y,
                      float w,
                      bool active) {
    const float s = ctx.dpiScale;
    const float h = 36.0f * s;
    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);

    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    const float targetHighlight = (active || hovered) ? 1.0f : 0.0f;
    const float hlT = ctx.anim.Float(HashId(id), targetHighlight, 20.0f, targetHighlight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hlT > 0.01f) {
        const RgbaColor bg = WithAlpha(ctx.palette.subnavActiveBg, ctx.palette.subnavActiveBg.a * hlT);
        dl->AddRectFilled(min, max, ToAbgrU32(bg), 3.0f * s);
    }

    const RgbaColor fg = (active || hovered) ? ctx.palette.subnavActiveText : ctx.palette.subnavItemText;

    float textX = x + 10.0f * s;
    if (icon.has_value()) {
        const float iconSz = 15.0f * s;
        DrawMotrixIcon(*icon, x + 12.0f * s, y + (h - iconSz) * 0.5f, iconSz, iconSz, fg, s);
        textX = x + 12.0f * s + iconSz + 12.0f * s;
    }

    FontGuard fgFont(ctx.fonts, FontRole::Regular, 15.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    dl->AddText(
        ImVec2(textX, y + (h - tSz.y) * 0.5f), ToAbgrU32(fg), label.data(), label.data() + label.size());

    return clicked;
}

}  // namespace

void RenderSubnav(UiFrameContext& ctx, float x, float y, float h, int& prefTab) {
    const float s = ctx.dpiScale;
    const float w = LayoutMetrics::kSubnavWidth * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.subnavBg));

    const float innerX = x + 16.0f * s;
    const float innerW = w - 32.0f * s;
    const float headerY = y + 44.0f * s;

    {
        FontGuard titleFont(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view title =
            ctx.state.activeSection == 0 ? ctx.tr.Tr("app.tasks") : ctx.tr.Tr("app.preferences");
        dl->AddText(ImVec2(innerX, headerY),
                    ToAbgrU32(ctx.palette.subnavTitle),
                    title.data(),
                    title.data() + title.size());
    }

    float itemY = headerY + 24.0f * s + 28.0f * s;
    const float stepY = 36.0f * s + 8.0f * s;

    if (ctx.state.activeSection == 0) {
        using Cat = motrix::domain::TaskCategory;
        if (SubnavItemButton(ctx,
                             "##subnav_downloading",
                             MotrixIcon::TaskStart,
                             ctx.tr.Tr("task.active"),
                             innerX,
                             itemY,
                             innerW,
                             ctx.state.activeCategory == Cat::Downloading)) {
            ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Downloading});
        }
        itemY += stepY;
        if (SubnavItemButton(ctx,
                             "##subnav_waiting",
                             MotrixIcon::TaskPause,
                             ctx.tr.Tr("task.waiting"),
                             innerX,
                             itemY,
                             innerW,
                             ctx.state.activeCategory == Cat::Waiting)) {
            ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Waiting});
        }
        itemY += stepY;
        if (SubnavItemButton(ctx,
                             "##subnav_stopped",
                             MotrixIcon::TaskStop,
                             ctx.tr.Tr("task.stopped"),
                             innerX,
                             itemY,
                             innerW,
                             ctx.state.activeCategory == Cat::Stopped)) {
            ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Stopped});
        }
    } else {
        if (SubnavItemButton(ctx,
                             "##subnav_pref_basic",
                             MotrixIcon::PreferenceBasic,
                             ctx.tr.Tr("prefs.basic"),
                             innerX,
                             itemY,
                             innerW,
                             prefTab == 0)) {
            prefTab = 0;
        }
        itemY += stepY;
        if (SubnavItemButton(ctx,
                             "##subnav_pref_adv",
                             MotrixIcon::PreferenceAdvanced,
                             ctx.tr.Tr("prefs.advanced"),
                             innerX,
                             itemY,
                             innerW,
                             prefTab == 1)) {
            prefTab = 1;
        }
        itemY += stepY;
        if (SubnavItemButton(ctx,
                             "##subnav_pref_lab",
                             MotrixIcon::PreferenceLab,
                             ctx.tr.Tr("prefs.lab"),
                             innerX,
                             itemY,
                             innerW,
                             prefTab == 2)) {
            prefTab = 2;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui
