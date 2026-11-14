module;

#include "imgui.h"

module motrix.ui;

import std;
import :theme;

namespace motrix::ui {

void DrawExtraMotrixIcon(ImDrawList* dl,
                         MotrixIcon icon,
                         float x,
                         float y,
                         float width,
                         float height,
                         RgbaColor color,
                         float dpiScale) {
    const float sx = width / 24.0f;
    const float sy = height / 24.0f;
    const auto pt = [&](float vx, float vy) noexcept { return ImVec2(x + vx * sx, y + vy * sy); };
    const ImU32 col = ToAbgrU32(color);
    const float sw = std::max(1.25f * dpiScale, 1.8f * sx);
    constexpr float kPi = 3.14159265f;

    switch (icon) {
        case MotrixIcon::PreferenceBasic: {
            // Dual horizontal toggle pills matching preference-basic.svg (filled glyphs)
            dl->AddRectFilled(pt(1.0f, 1.0f), pt(23.0f, 11.0f), col, 5.0f * sy);
            dl->AddCircleFilled(pt(6.5f, 6.0f), 3.2f * sy, ToAbgrU32(HexColor(0xFFFFFF, 0.90f)), 16);
            dl->AddRectFilled(pt(1.0f, 13.0f), pt(23.0f, 23.0f), col, 5.0f * sy);
            dl->AddCircleFilled(pt(17.5f, 18.0f), 3.2f * sy, ToAbgrU32(HexColor(0xFFFFFF, 0.90f)), 16);
            break;
        }
        case MotrixIcon::PreferenceAdvanced: {
            // Crossed wrench + screwdriver matching preference-advanced.svg silhouette
            const ImVec2 handle1[4] = {
                pt(3.5f, 5.5f), pt(6.5f, 2.5f), pt(20.5f, 16.5f), pt(17.5f, 19.5f)};
            dl->AddConvexPolyFilled(handle1, 4, col);
            const ImVec2 wrench[4] = {
                pt(4.5f, 20.5f), pt(2.0f, 18.0f), pt(14.5f, 5.5f), pt(17.0f, 8.0f)};
            dl->AddConvexPolyFilled(wrench, 4, col);
            dl->AddCircleFilled(pt(18.0f, 6.0f), 4.8f * sx, col, 20);
            dl->AddCircleFilled(pt(19.5f, 4.5f), 2.3f * sx, ToAbgrU32(HexColor(0xF4F5F7)), 14);
            dl->AddCircleFilled(pt(19.5f, 19.5f), 3.0f * sx, col, 16);
            break;
        }
        case MotrixIcon::PreferenceLab: {
            // Erlenmeyer flask silhouette matching preference-lab.js
            const ImVec2 neck[4] = {pt(8.5f, 1.5f), pt(15.5f, 1.5f), pt(15.5f, 4.5f), pt(8.5f, 4.5f)};
            dl->AddConvexPolyFilled(neck, 4, col);
            const ImVec2 topStem[4] = {
                pt(9.5f, 4.5f), pt(14.5f, 4.5f), pt(14.5f, 11.2f), pt(9.5f, 11.2f)};
            dl->AddConvexPolyFilled(topStem, 4, col);
            const ImVec2 body[6] = {pt(9.5f, 11.2f),
                                    pt(14.5f, 11.2f),
                                    pt(20.2f, 19.5f),
                                    pt(18.5f, 22.8f),
                                    pt(5.5f, 22.8f),
                                    pt(3.8f, 19.5f)};
            dl->AddConvexPolyFilled(body, 6, col);
            dl->AddLine(pt(6.2f, 16.2f), pt(17.8f, 16.2f), ToAbgrU32(HexColor(0xFFFFFF, 0.55f)), sw);
            break;
        }
        case MotrixIcon::TaskHistory: {
            // Clock ring + L-shaped hands matching task-history.svg
            dl->AddCircle(pt(12.0f, 12.0f), 8.6f * sx, col, 24, sw);
            dl->AddLine(pt(12.0f, 7.0f), pt(12.0f, 12.5f), col, sw);
            dl->AddLine(pt(12.0f, 12.5f), pt(16.2f, 12.5f), col, sw);
            break;
        }
        case MotrixIcon::Sync: {
            // Document/screen + circular refresh arrows matching sync.svg
            const ImVec2 doc[4] = {pt(4.0f, 13.5f), pt(4.0f, 4.0f), pt(20.0f, 4.0f), pt(20.0f, 10.0f)};
            dl->AddPolyline(doc, 4, col, 0, sw);
            dl->AddLine(pt(4.0f, 18.0f), pt(10.0f, 18.0f), col, sw);
            dl->PathArcTo(pt(17.5f, 17.5f), 4.5f * sx, -kPi * 0.85f, -kPi * 0.05f, 12);
            dl->PathStroke(col, 0, sw);
            dl->PathArcTo(pt(17.5f, 17.5f), 4.5f * sx, kPi * 0.15f, kPi * 0.95f, 12);
            dl->PathStroke(col, 0, sw);
            dl->AddTriangleFilled(pt(22.8f, 16.5f), pt(21.4f, 13.5f), pt(19.2f, 16.0f), col);
            dl->AddTriangleFilled(pt(12.2f, 18.5f), pt(13.6f, 21.5f), pt(15.8f, 19.0f), col);
            break;
        }
        case MotrixIcon::Dice: {
            // 3D isometric die with pips matching dice.svg
            dl->AddRect(pt(3.0f, 8.0f), pt(16.5f, 21.5f), col, 0.0f, 0, sw);
            const ImVec2 topSide[4] = {pt(3.0f, 8.0f), pt(8.5f, 2.5f), pt(21.5f, 2.5f), pt(16.5f, 8.0f)};
            dl->AddPolyline(topSide, 4, col, 0, sw);
            const ImVec2 rightSide[3] = {pt(21.5f, 2.5f), pt(21.5f, 16.0f), pt(16.5f, 21.5f)};
            dl->AddPolyline(rightSide, 3, col, 0, sw);
            dl->AddCircleFilled(pt(7.2f, 12.2f), 1.3f * sx, col, 10);
            dl->AddCircleFilled(pt(12.3f, 12.2f), 1.3f * sx, col, 10);
            dl->AddCircleFilled(pt(7.2f, 17.3f), 1.3f * sx, col, 10);
            dl->AddCircleFilled(pt(12.3f, 17.3f), 1.3f * sx, col, 10);
            break;
        }
        case MotrixIcon::ChevronDown: {
            const ImVec2 ch[3] = {pt(6.0f, 9.5f), pt(12.0f, 15.5f), pt(18.0f, 9.5f)};
            dl->AddPolyline(ch, 3, col, 0, std::max(1.25f * dpiScale, 1.5f * sx));
            break;
        }
        case MotrixIcon::ChevronUp: {
            const ImVec2 ch[3] = {pt(6.0f, 14.5f), pt(12.0f, 8.5f), pt(18.0f, 14.5f)};
            dl->AddPolyline(ch, 3, col, 0, std::max(1.25f * dpiScale, 1.5f * sx));
            break;
        }
        default: break;
    }
}

}  // namespace motrix::ui
