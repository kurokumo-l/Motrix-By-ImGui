module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :el_widgets;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

struct SpeedometerMetrics {
    ImVec2 pMin{};
    ImVec2 pMax{};
    float expandT = 0.0f;
    bool capped = false;
};

void HandleSpeedometerInteraction(UiFrameContext& ctx, bool hovered, bool hasLimit) {
    if (!hasLimit) {
        return;
    }
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const char* tipKey = ctx.state.speedLimitPaused ? "speed.limit_off" : "speed.limit_on";
        ElDrawTooltip({ctx.palette, ctx.fonts, ctx.dpiScale, &ctx.anim},
                      {ctx.tr.Tr(tipKey), ElTooltipPlacement::Top});
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ctx.sink.Dispatch(motrix::app::Command{motrix::app::ToggleSpeedLimitCmd{}});
    }
}

void RenderSpeedometerStats(UiFrameContext& ctx, const SpeedometerMetrics& m) {
    if (m.expandT <= 0.55f) {
        return;
    }
    const float s = ctx.dpiScale;
    const float textAlpha = std::clamp((m.expandT - 0.55f) / 0.45f, 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor modeCol = m.capped ? HexColor(0xE6A23C) : ctx.palette.primary;
    {
        FontGuard modeFont(ctx.fonts, FontRole::Semibold, 9.0f);
        const std::string_view modeText = ctx.tr.Tr(m.capped ? "speed.badge_cap" : "speed.badge_max");
        dl->AddText(ImVec2(m.pMin.x + 35.0f * s, m.pMin.y + 6.0f * s),
                    ToAbgrU32(WithAlpha(modeCol, textAlpha)),
                    modeText.data(),
                    modeText.data() + modeText.size());
    }
    {
        FontGuard upFont(ctx.fonts, FontRole::Regular, 10.5f);
        const std::string upText = motrix::domain::FormatSpeed(ctx.state.globalStat.uploadSpeed);
        const ImVec2 uSz = ImGui::CalcTextSize(upText.c_str());
        dl->AddText(ImVec2(m.pMax.x - 11.0f * s - uSz.x, m.pMin.y + 5.0f * s),
                    ToAbgrU32(WithAlpha(ctx.palette.speedometerUpText, textAlpha)),
                    upText.c_str());
    }
    {
        FontGuard downFont(ctx.fonts, FontRole::Regular, 13.0f);
        const std::string downText = motrix::domain::FormatSpeed(ctx.state.globalStat.downloadSpeed);
        const ImVec2 dSz = ImGui::CalcTextSize(downText.c_str());
        dl->AddText(ImVec2(m.pMax.x - 11.0f * s - dSz.x, m.pMin.y + 19.0f * s),
                    ToAbgrU32(WithAlpha(ctx.palette.primary, textAlpha)),
                    downText.c_str());
    }
}

}  // namespace

void RenderSpeedometer(UiFrameContext& ctx, float rightEdgeX, float bottomEdgeY) {
    const float s = ctx.dpiScale;
    const bool wide = ctx.viewportW >= LayoutMetrics::kSubnavHideThreshold * s;
    const float rightInset =
        (wide ? LayoutMetrics::kSpeedometerRightWide : LayoutMetrics::kSpeedometerRightNarrow) * s;
    const float bottomInset = LayoutMetrics::kSpeedometerBottom * s;

    const bool active = ctx.state.globalStat.numActive > 0;
    const float expandT =
        ctx.anim.Float(HashId("speedometer.expand"), active ? 1.0f : 0.0f, 16.0f, active ? 1.0f : 0.0f);

    const float h = LayoutMetrics::kSpeedometerSize * s;
    const float w =
        (LayoutMetrics::kSpeedometerSize
         + (LayoutMetrics::kSpeedometerExpandedW - LayoutMetrics::kSpeedometerSize) * expandT)
        * s;

    const ImVec2 pMax(rightEdgeX - rightInset, bottomEdgeY - bottomInset);
    const ImVec2 pMin(pMax.x - w, pMax.y - h);

    ImGui::SetCursorScreenPos(pMin);
    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr int kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
                           | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ChildGuard overlay("##motrix_speedometer", ChildWindowSpec{w, h, ImGuiChildFlags_None, kFlags});
    if (!overlay.IsVisible()) {
        return;
    }

    ImGui::SetCursorScreenPos(pMin);
    ImGui::InvisibleButton("##speedometer_pill", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool hasLimit =
        ctx.state.sysConfig.maxOverallDownloadLimit > 0 || ctx.state.sysConfig.maxOverallUploadLimit > 0;
    const bool capped = hasLimit && !ctx.state.speedLimitPaused;
    HandleSpeedometerInteraction(ctx, hovered, hasLimit);

    const float hovT =
        ctx.anim.Float(HashId("speedometer.hover"), (hovered && hasLimit) ? 1.0f : 0.0f, 20.0f, 0.0f);
    const RgbaColor baseBorder =
        capped ? WithAlpha(HexColor(0xE6A23C), 0.72f) : ctx.palette.speedometerBorder;
    const RgbaColor border = LerpColor(baseBorder, ctx.palette.speedometerHoverBorder, hovT);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pMin, pMax, ToAbgrU32(ctx.palette.speedometerBg), h * 0.5f);
    dl->AddRect(pMin, pMax, ToAbgrU32(border), h * 0.5f, 0, 1.0f);

    const float iconSz = 24.0f * s;
    const float iconX = pMin.x + std::lerp((w - iconSz) * 0.5f, 7.0f * s, expandT);
    const float iconY = pMin.y + (h - iconSz) * 0.5f;
    const RgbaColor activeCol = capped ? HexColor(0xE6A23C) : ctx.palette.primary;
    const RgbaColor idleCol = capped ? HexColor(0xE6A23C) : ctx.palette.speedometerIdleIcon;
    const RgbaColor iconCol = LerpColor(idleCol, activeCol, expandT);
    DrawMotrixIcon(IconSpec{MotrixIcon::Speedometer, iconX, iconY, iconSz, iconSz, iconCol, s});

    RenderSpeedometerStats(ctx, SpeedometerMetrics{pMin, pMax, expandT, capped});
}

}  // namespace motrix::ui
