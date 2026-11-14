module;

#include "imgui.h"

module motrix.ui;

import std;
import :theme;

namespace motrix::ui {

namespace {

struct ExtraIconCanvas {
    ImDrawList* dl = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
    ImU32 col = 0;
    float sw = 1.0f;

    [[nodiscard]] ImVec2 Pt(float vx, float vy) const noexcept {
        return ImVec2(x + vx * sx, y + vy * sy);
    }
};

void DrawPrefExtraIcons(const ExtraIconCanvas& c, MotrixIcon icon) {
    switch (icon) {
        case MotrixIcon::PreferenceBasic: {
            // Dual horizontal toggle pills matching preference-basic.svg (filled glyphs)
            c.dl->AddRectFilled(c.Pt(1.0f, 1.0f), c.Pt(23.0f, 11.0f), c.col, 5.0f * c.sy);
            c.dl->AddCircleFilled(
                c.Pt(6.5f, 6.0f), 3.2f * c.sy, ToAbgrU32(HexColor(0xFFFFFF, 0.90f)), 16);
            c.dl->AddRectFilled(c.Pt(1.0f, 13.0f), c.Pt(23.0f, 23.0f), c.col, 5.0f * c.sy);
            c.dl->AddCircleFilled(
                c.Pt(17.5f, 18.0f), 3.2f * c.sy, ToAbgrU32(HexColor(0xFFFFFF, 0.90f)), 16);
            break;
        }
        case MotrixIcon::PreferenceAdvanced: {
            // Crossed wrench + screwdriver matching preference-advanced.svg silhouette
            const ImVec2 handle1[4] = {
                c.Pt(3.5f, 5.5f), c.Pt(6.5f, 2.5f), c.Pt(20.5f, 16.5f), c.Pt(17.5f, 19.5f)};
            c.dl->AddConvexPolyFilled(handle1, 4, c.col);
            const ImVec2 wrench[4] = {
                c.Pt(4.5f, 20.5f), c.Pt(2.0f, 18.0f), c.Pt(14.5f, 5.5f), c.Pt(17.0f, 8.0f)};
            c.dl->AddConvexPolyFilled(wrench, 4, c.col);
            c.dl->AddCircleFilled(c.Pt(18.0f, 6.0f), 4.8f * c.sx, c.col, 20);
            c.dl->AddCircleFilled(c.Pt(19.5f, 4.5f), 2.3f * c.sx, ToAbgrU32(HexColor(0xF4F5F7)), 14);
            c.dl->AddCircleFilled(c.Pt(19.5f, 19.5f), 3.0f * c.sx, c.col, 16);
            break;
        }
        case MotrixIcon::PreferenceLab: {
            // Erlenmeyer flask silhouette matching preference-lab.js
            const ImVec2 neck[4] = {
                c.Pt(8.5f, 1.5f), c.Pt(15.5f, 1.5f), c.Pt(15.5f, 4.5f), c.Pt(8.5f, 4.5f)};
            c.dl->AddConvexPolyFilled(neck, 4, c.col);
            const ImVec2 topStem[4] = {
                c.Pt(9.5f, 4.5f), c.Pt(14.5f, 4.5f), c.Pt(14.5f, 11.2f), c.Pt(9.5f, 11.2f)};
            c.dl->AddConvexPolyFilled(topStem, 4, c.col);
            const ImVec2 body[6] = {c.Pt(9.5f, 11.2f),
                                    c.Pt(14.5f, 11.2f),
                                    c.Pt(20.2f, 19.5f),
                                    c.Pt(18.5f, 22.8f),
                                    c.Pt(5.5f, 22.8f),
                                    c.Pt(3.8f, 19.5f)};
            c.dl->AddConvexPolyFilled(body, 6, c.col);
            c.dl->AddLine(
                c.Pt(6.2f, 16.2f), c.Pt(17.8f, 16.2f), ToAbgrU32(HexColor(0xFFFFFF, 0.55f)), c.sw);
            break;
        }
        default: break;
    }
}

void DrawUtilityExtraIcons(const ExtraIconCanvas& c, MotrixIcon icon, float dpiScale) {
    constexpr float kPi = 3.14159265f;
    switch (icon) {
        case MotrixIcon::TaskHistory:
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), 8.6f * c.sx, c.col, 24, c.sw);
            c.dl->AddLine(c.Pt(12.0f, 7.0f), c.Pt(12.0f, 12.5f), c.col, c.sw);
            c.dl->AddLine(c.Pt(12.0f, 12.5f), c.Pt(16.2f, 12.5f), c.col, c.sw);
            break;
        case MotrixIcon::Sync: {
            const ImVec2 doc[4] = {
                c.Pt(4.0f, 13.5f), c.Pt(4.0f, 4.0f), c.Pt(20.0f, 4.0f), c.Pt(20.0f, 10.0f)};
            c.dl->AddPolyline(doc, 4, c.col, 0, c.sw);
            c.dl->AddLine(c.Pt(4.0f, 18.0f), c.Pt(10.0f, 18.0f), c.col, c.sw);
            c.dl->PathArcTo(c.Pt(17.5f, 17.5f), 4.5f * c.sx, -kPi * 0.85f, -kPi * 0.05f, 12);
            c.dl->PathStroke(c.col, 0, c.sw);
            c.dl->PathArcTo(c.Pt(17.5f, 17.5f), 4.5f * c.sx, kPi * 0.15f, kPi * 0.95f, 12);
            c.dl->PathStroke(c.col, 0, c.sw);
            c.dl->AddTriangleFilled(c.Pt(22.8f, 16.5f), c.Pt(21.4f, 13.5f), c.Pt(19.2f, 16.0f), c.col);
            c.dl->AddTriangleFilled(c.Pt(12.2f, 18.5f), c.Pt(13.6f, 21.5f), c.Pt(15.8f, 19.0f), c.col);
            break;
        }
        case MotrixIcon::Dice: {
            c.dl->AddRect(c.Pt(3.0f, 8.0f), c.Pt(16.5f, 21.5f), c.col, 0.0f, 0, c.sw);
            const ImVec2 topSide[4] = {
                c.Pt(3.0f, 8.0f), c.Pt(8.5f, 2.5f), c.Pt(21.5f, 2.5f), c.Pt(16.5f, 8.0f)};
            c.dl->AddPolyline(topSide, 4, c.col, 0, c.sw);
            const ImVec2 rightSide[3] = {c.Pt(21.5f, 2.5f), c.Pt(21.5f, 16.0f), c.Pt(16.5f, 21.5f)};
            c.dl->AddPolyline(rightSide, 3, c.col, 0, c.sw);
            c.dl->AddCircleFilled(c.Pt(7.2f, 12.2f), 1.3f * c.sx, c.col, 10);
            c.dl->AddCircleFilled(c.Pt(12.3f, 12.2f), 1.3f * c.sx, c.col, 10);
            c.dl->AddCircleFilled(c.Pt(7.2f, 17.3f), 1.3f * c.sx, c.col, 10);
            c.dl->AddCircleFilled(c.Pt(12.3f, 17.3f), 1.3f * c.sx, c.col, 10);
            break;
        }
        case MotrixIcon::ChevronDown: {
            const ImVec2 ch[3] = {c.Pt(6.0f, 9.5f), c.Pt(12.0f, 15.5f), c.Pt(18.0f, 9.5f)};
            c.dl->AddPolyline(ch, 3, c.col, 0, std::max(1.25f * dpiScale, 1.5f * c.sx));
            break;
        }
        case MotrixIcon::ChevronUp: {
            const ImVec2 ch[3] = {c.Pt(6.0f, 14.5f), c.Pt(12.0f, 8.5f), c.Pt(18.0f, 14.5f)};
            c.dl->AddPolyline(ch, 3, c.col, 0, std::max(1.25f * dpiScale, 1.5f * c.sx));
            break;
        }
        case MotrixIcon::TagClose: {
            const float xw = std::max(1.1f * dpiScale, 2.4f * c.sx);
            c.dl->AddLine(c.Pt(6.8f, 6.8f), c.Pt(17.2f, 17.2f), c.col, xw);
            c.dl->AddLine(c.Pt(17.2f, 6.8f), c.Pt(6.8f, 17.2f), c.col, xw);
            break;
        }
        default: break;
    }
}

}  // namespace

void DrawExtraMotrixIcon(ImDrawList* dl, const IconSpec& spec) {
    const float sx = spec.width / 24.0f;
    const float sy = spec.height / 24.0f;
    const float sw = std::max(1.25f * spec.dpiScale, 1.8f * sx);
    const ExtraIconCanvas c{dl, spec.x, spec.y, sx, sy, ToAbgrU32(spec.color), sw};
    if (spec.icon == MotrixIcon::PreferenceBasic || spec.icon == MotrixIcon::PreferenceAdvanced
        || spec.icon == MotrixIcon::PreferenceLab) {
        DrawPrefExtraIcons(c, spec.icon);
        return;
    }
    DrawUtilityExtraIcons(c, spec.icon, spec.dpiScale);
}

}  // namespace motrix::ui
