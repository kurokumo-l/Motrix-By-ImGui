module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import :views_internal;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

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
    ChildGuard overlay("##motrix_speedometer", w, h, ImGuiChildFlags_None, kFlags);
    if (!overlay.IsVisible()) {
        return;
    }

    ImGui::SetCursorScreenPos(pMin);
    ImGui::InvisibleButton("##speedometer_pill", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    const RgbaColor border =
        hovered ? ctx.palette.speedometerHoverBorder : ctx.palette.speedometerBorder;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pMin, pMax, ToAbgrU32(ctx.palette.speedometerBg), h * 0.5f);
    dl->AddRect(pMin, pMax, ToAbgrU32(border), h * 0.5f, 0, 1.0f);

    const float iconSz = 24.0f * s;
    const float iconX = pMin.x + (active ? 7.0f * s : (w - iconSz) * 0.5f);
    const float iconY = pMin.y + (h - iconSz) * 0.5f;
    const RgbaColor iconCol = active ? ctx.palette.primary : ctx.palette.speedometerIdleIcon;
    DrawMotrixIcon(MotrixIcon::Speedometer, iconX, iconY, iconSz, iconSz, iconCol, s);

    if (active && expandT > 0.65f) {
        const float textAlpha = (expandT - 0.65f) / 0.35f;
        {
            FontGuard modeFont(ctx.fonts, FontRole::Semibold, 9.0f);
            const std::string_view modeText = ctx.tr.Tr("speed.max");
            dl->AddText(ImVec2(pMin.x + 35.0f * s, pMin.y + 6.0f * s),
                        ToAbgrU32(WithAlpha(ctx.palette.primary, textAlpha)),
                        modeText.data(),
                        modeText.data() + modeText.size());
        }
        {
            FontGuard upFont(ctx.fonts, FontRole::Regular, 10.5f);
            const std::string upText = motrix::domain::FormatSpeed(ctx.state.globalStat.uploadSpeed);
            const ImVec2 uSz = ImGui::CalcTextSize(upText.c_str());
            dl->AddText(ImVec2(pMax.x - 11.0f * s - uSz.x, pMin.y + 5.0f * s),
                        ToAbgrU32(WithAlpha(ctx.palette.speedometerUpText, textAlpha)),
                        upText.c_str());
        }
        {
            FontGuard downFont(ctx.fonts, FontRole::Regular, 13.0f);
            const std::string downText = motrix::domain::FormatSpeed(ctx.state.globalStat.downloadSpeed);
            const ImVec2 dSz = ImGui::CalcTextSize(downText.c_str());
            dl->AddText(ImVec2(pMax.x - 11.0f * s - dSz.x, pMin.y + 19.0f * s),
                        ToAbgrU32(WithAlpha(ctx.palette.primary, textAlpha)),
                        downText.c_str());
        }
    }
}

}  // namespace motrix::ui
