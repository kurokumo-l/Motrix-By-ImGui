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

struct SubnavBtnSpec {
    const char* id = "";
    std::optional<MotrixIcon> icon{};
    std::string_view label{};
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    bool active = false;
};

struct SubnavListLayout {
    float innerX = 0.0f;
    float innerW = 0.0f;
    float itemY = 0.0f;
    float stepY = 0.0f;
};

bool SubnavItemButton(UiFrameContext& ctx, const SubnavBtnSpec& spec) {
    const float s = ctx.dpiScale;
    const float h = 36.0f * s;
    const ImVec2 min(spec.x, spec.y);
    const ImVec2 max(spec.x + spec.w, spec.y + h);

    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(spec.w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const float targetHighlight = (spec.active || hovered) ? 1.0f : 0.0f;
    const float hlT = ctx.anim.Float(HashId(spec.id), targetHighlight, 20.0f, targetHighlight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hlT > 0.01f) {
        const RgbaColor bg = WithAlpha(ctx.palette.subnavActiveBg, ctx.palette.subnavActiveBg.a * hlT);
        dl->AddRectFilled(min, max, ToAbgrU32(bg), 3.0f * s);
    }

    const RgbaColor fg = LerpColor(ctx.palette.subnavItemText, ctx.palette.subnavActiveText, hlT);
    float textX = spec.x + 10.0f * s;
    if (spec.icon.has_value()) {
        const float iconSz = 15.0f * s;
        DrawMotrixIcon(IconSpec{
            *spec.icon,
            spec.x + 12.0f * s,
            spec.y + (h - iconSz) * 0.5f,
            iconSz,
            iconSz,
            fg,
            s,
        });
        textX = spec.x + 12.0f * s + iconSz + 12.0f * s;
    }

    FontGuard fgFont(ctx.fonts, FontRole::Regular, 15.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    dl->AddText(ImVec2(textX, spec.y + (h - tSz.y) * 0.5f),
                ToAbgrU32(fg),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return clicked;
}

void RenderTaskCategorySubnav(UiFrameContext& ctx, const SubnavListLayout& lay) {
    using Cat = motrix::domain::TaskCategory;
    float y = lay.itemY;
    if (SubnavItemButton(ctx,
                         {"##subnav_downloading",
                          MotrixIcon::TaskStart,
                          ctx.tr.Tr("task.active"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          ctx.state.activeCategory == Cat::Downloading})) {
        ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Downloading});
    }
    y += lay.stepY;
    if (SubnavItemButton(ctx,
                         {"##subnav_waiting",
                          MotrixIcon::TaskPause,
                          ctx.tr.Tr("task.waiting"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          ctx.state.activeCategory == Cat::Waiting})) {
        ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Waiting});
    }
    y += lay.stepY;
    if (SubnavItemButton(ctx,
                         {"##subnav_stopped",
                          MotrixIcon::TaskStop,
                          ctx.tr.Tr("task.stopped"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          ctx.state.activeCategory == Cat::Stopped})) {
        ctx.sink.Dispatch(motrix::app::SelectCategoryCmd{Cat::Stopped});
    }
}

void RenderPrefsSubnav(UiFrameContext& ctx, const SubnavListLayout& lay, int& prefTab) {
    float y = lay.itemY;
    if (SubnavItemButton(ctx,
                         {"##subnav_pref_basic",
                          MotrixIcon::PreferenceBasic,
                          ctx.tr.Tr("prefs.basic"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          prefTab == 0})) {
        prefTab = 0;
    }
    y += lay.stepY;
    if (SubnavItemButton(ctx,
                         {"##subnav_pref_adv",
                          MotrixIcon::PreferenceAdvanced,
                          ctx.tr.Tr("prefs.advanced"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          prefTab == 1})) {
        prefTab = 1;
    }
    y += lay.stepY;
    if (SubnavItemButton(ctx,
                         {"##subnav_pref_lab",
                          MotrixIcon::PreferenceLab,
                          ctx.tr.Tr("prefs.lab"),
                          lay.innerX,
                          y,
                          lay.innerW,
                          prefTab == 2})) {
        prefTab = 2;
    }
}

}  // namespace

void RenderSubnav(UiFrameContext& ctx, const PanelRect& rect, int& prefTab) {
    const float s = ctx.dpiScale;
    const float w = rect.w > 0.0f ? rect.w : LayoutMetrics::kSubnavWidth * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(rect.x, rect.y), ImVec2(rect.x + w, rect.y + rect.h), ToAbgrU32(ctx.palette.subnavBg));

    const float innerX = rect.x + 16.0f * s;
    const float innerW = w - 32.0f * s;
    const float headerY = rect.y + 44.0f * s;

    {
        FontGuard titleFont(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view title =
            ctx.state.activeSection == 0 ? ctx.tr.Tr("app.tasks") : ctx.tr.Tr("app.preferences");
        dl->AddText(ImVec2(innerX, headerY),
                    ToAbgrU32(ctx.palette.subnavTitle),
                    title.data(),
                    title.data() + title.size());
    }

    const SubnavListLayout lay{innerX, innerW, headerY + 52.0f * s, 44.0f * s};
    if (ctx.state.activeSection == 0) {
        RenderTaskCategorySubnav(ctx, lay);
    } else {
        RenderPrefsSubnav(ctx, lay, prefTab);
    }

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui
