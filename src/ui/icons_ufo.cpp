module;

#include "imgui.h"

module motrix.ui;

import std;
import :theme;

namespace motrix::ui {

namespace {

[[nodiscard]] ImVec2 RotPt(float cx, float cy, float px, float py, float rad) noexcept {
    const float dx = px - cx;
    const float dy = py - cy;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return ImVec2(cx + dx * c - dy * s, cy + dx * s + dy * c);
}

void DrawGradientQuadStrip(ImDrawList* dl,
                           ImVec2 topL,
                           ImVec2 topR,
                           ImVec2 botR,
                           ImVec2 botL,
                           RgbaColor topCol,
                           RgbaColor botCol,
                           int slices) {
    for (int i = 0; i < slices; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(slices);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(slices);
        const auto lerpPt = [](ImVec2 a, ImVec2 b, float t) {
            return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
        const auto lerpCol = [](RgbaColor a, RgbaColor b, float t) {
            return ToAbgrU32(RgbaColor{
                a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t,
            });
        };
        const ImVec2 p0 = lerpPt(topL, botL, t0);
        const ImVec2 p1 = lerpPt(topR, botR, t0);
        const ImVec2 p2 = lerpPt(topR, botR, t1);
        const ImVec2 p3 = lerpPt(topL, botL, t1);
        const ImU32 cTop = lerpCol(topCol, botCol, t0);
        const ImU32 cBot = lerpCol(topCol, botCol, t1);

        dl->PrimReserve(6, 4);
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        const ImDrawIdx idx = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(p0, uv, cTop);
        dl->PrimWriteVtx(p1, uv, cTop);
        dl->PrimWriteVtx(p2, uv, cBot);
        dl->PrimWriteVtx(p3, uv, cBot);
        dl->PrimWriteIdx(idx);
        dl->PrimWriteIdx(idx + 1);
        dl->PrimWriteIdx(idx + 2);
        dl->PrimWriteIdx(idx);
        dl->PrimWriteIdx(idx + 2);
        dl->PrimWriteIdx(idx + 3);
    }
}

void DrawEllipseFilledRot(ImDrawList* dl,
                          float cx,
                          float cy,
                          float rx,
                          float ry,
                          float angleRad,
                          ImU32 fillCol,
                          ImU32 strokeCol,
                          float strokeW) {
    constexpr int kSegs = 40;
    std::array<ImVec2, kSegs> pts{};
    for (int i = 0; i < kSegs; ++i) {
        const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(kSegs);
        const float lx = cx + std::cos(a) * rx;
        const float ly = cy + std::sin(a) * ry;
        pts[static_cast<std::size_t>(i)] = RotPt(cx, cy, lx, ly, angleRad);
    }
    dl->AddConvexPolyFilled(pts.data(), kSegs, fillCol);
    if ((strokeCol >> 24) > 0 && strokeW > 0.0f) {
        dl->AddPolyline(pts.data(), kSegs, strokeCol, ImDrawFlags_Closed, strokeW);
    }
}

void DrawPineTree(ImDrawList* dl, float baseX, float baseY, float w, float h, ImU32 col) {
    // Two-tier stylized Motrix conifer matching no-task.svg
    const float lowerTopY = baseY - h * 0.64f;
    const float upperTopY = baseY - h;
    const float upperBotY = baseY - h * 0.34f;
    dl->AddTriangleFilled(ImVec2(baseX + w * 0.03f, lowerTopY),
                          ImVec2(baseX - w * 0.50f, baseY),
                          ImVec2(baseX + w * 0.50f, baseY),
                          col);
    dl->AddTriangleFilled(ImVec2(baseX, upperTopY),
                          ImVec2(baseX - w * 0.44f, upperBotY),
                          ImVec2(baseX + w * 0.44f, upperBotY),
                          col);
}

}  // namespace

void DrawSoftBellHill(ImDrawList* dl,
                      float x0,
                      float y0,
                      float sx,
                      float sy,
                      float startX,
                      float endX,
                      float peakHeight,
                      float baseY,
                      RgbaColor topCol,
                      RgbaColor botCol) {
    constexpr float kPi = 3.14159265f;
    constexpr int kSegs = 32;
    const auto pt = [&](float vx, float vy) { return ImVec2(x0 + vx * sx, y0 + vy * sy); };
    for (int i = 0; i < kSegs; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(kSegs);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegs);
        const float s0 = std::sin(t0 * kPi);
        const float s1 = std::sin(t1 * kPi);
        const float w0 = s0 * s0;
        const float w1 = s1 * s1;
        const float xA = startX + (endX - startX) * t0;
        const float xB = startX + (endX - startX) * t1;
        const float yA = baseY - peakHeight * w0;
        const float yB = baseY - peakHeight * w1;
        const RgbaColor topA{topCol.r, topCol.g, topCol.b, topCol.a * w0};
        const RgbaColor topB{topCol.r, topCol.g, topCol.b, topCol.a * w1};
        const RgbaColor midTop{
            (topA.r + topB.r) * 0.5f,
            (topA.g + topB.g) * 0.5f,
            (topA.b + topB.b) * 0.5f,
            (topA.a + topB.a) * 0.5f,
        };
        DrawGradientQuadStrip(
            dl, pt(xA, yA), pt(xB, yB), pt(xB, baseY), pt(xA, baseY), midTop, botCol, 6);
    }
}

void DrawEmptyUfoIllustration(float centerX, float topY, float width, float height, bool dark) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float sx = width / 504.0f;
    const float sy = height / 309.0f;
    const float x0 = centerX - width * 0.5f;
    const float y0 = topY;
    const auto pt = [&](float vx, float vy) { return ImVec2(x0 + vx * sx, y0 + vy * sy); };

    // 1. Two soft overlapping mist hills fading downward seamlessly
    const RgbaColor hillLeftTop = dark ? HexColor(0xFFFFFF, 0.13f) : HexColor(0xD4DAE2, 0.42f);
    const RgbaColor hillRightTop = dark ? HexColor(0xFFFFFF, 0.18f) : HexColor(0xC9D0DA, 0.52f);
    const RgbaColor hillBot = dark ? HexColor(0x343434, 0.0f) : HexColor(0xFFFFFF, 0.0f);
    DrawSoftBellHill(dl, x0, y0, sx, sy, 48.0f, 326.0f, 52.0f, 296.0f, hillLeftTop, hillBot);
    DrawSoftBellHill(dl, x0, y0, sx, sy, 165.0f, 474.0f, 78.0f, 294.0f, hillRightTop, hillBot);

    // 2. Two-tier stylized pine trees on hills (exact ground-truth placement)
    const ImU32 treeDark = ToAbgrU32(dark ? HexColor(0x7B7B7B, 0.92f) : HexColor(0x5E5E5E, 0.94f));
    const ImU32 treeSoft = ToAbgrU32(dark ? HexColor(0x6C6C6C, 0.48f) : HexColor(0xB4B4B4, 0.55f));
    dl->AddTriangleFilled(pt(130.0f, 232.0f), pt(124.5f, 255.0f), pt(135.5f, 255.0f), treeSoft);
    DrawPineTree(dl, x0 + 148.0f * sx, y0 + 252.0f * sy, 18.0f * sx, 38.0f * sy, treeDark);
    dl->AddTriangleFilled(pt(337.0f, 209.0f), pt(331.0f, 234.0f), pt(343.0f, 234.0f), treeSoft);
    DrawPineTree(dl, x0 + 364.0f * sx, y0 + 236.0f * sy, 23.0f * sx, 49.0f * sy, treeDark);

    // 3. UFO Outer wide soft grey tractor cone + vibrant #5B5AEA inner cone (no-task.svg)
    const RgbaColor outerTop = dark ? HexColor(0xBBBBBB, 0.20f) : HexColor(0x707070, 0.25f);
    const RgbaColor outerBot = dark ? HexColor(0x404040, 0.03f) : HexColor(0x9A9A9A, 0.06f);
    DrawGradientQuadStrip(dl,
                          pt(191.0f, 67.0f),
                          pt(243.0f, 51.0f),
                          pt(318.0f, 188.0f),
                          pt(210.0f, 224.0f),
                          outerTop,
                          outerBot,
                          24);
    const RgbaColor innerTop = HexColor(0x5B5AEA, 0.56f);
    const RgbaColor innerBot = dark ? HexColor(0x5B5AEA, 0.0f) : HexColor(0xE7D1AF, 0.0f);
    DrawGradientQuadStrip(dl,
                          pt(199.0f, 65.0f),
                          pt(235.0f, 54.0f),
                          pt(296.0f, 182.0f),
                          pt(222.0f, 206.0f),
                          innerTop,
                          innerBot,
                          24);

    // 4. Tilted UFO Saucer body
    const float ufoCx = x0 + 216.0f * sx;
    const float ufoCy = y0 + 51.0f * sy;
    const float tilt = -0.34f;

    // Top cockpit dome
    DrawEllipseFilledRot(dl,
                         ufoCx - 3.5f * sx,
                         ufoCy - 11.5f * sy,
                         25.0f * sx,
                         13.5f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x4C4C4C)),
                         ToAbgrU32(HexColor(0x1B1B1B)),
                         1.4f * sx);
    // Main saucer hull
    DrawEllipseFilledRot(dl,
                         ufoCx,
                         ufoCy,
                         55.5f * sx,
                         16.0f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x3E3E3E)),
                         ToAbgrU32(HexColor(0x1B1B1B)),
                         1.5f * sx);
    // Metallic upper ring highlight
    DrawEllipseFilledRot(dl,
                         ufoCx - 0.5f * sx,
                         ufoCy - 2.0f * sy,
                         51.0f * sx,
                         12.5f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x6C6971, 0.45f)),
                         0u,
                         0.0f);
    // Underside dark emitter cavity + purple glow
    DrawEllipseFilledRot(dl,
                         ufoCx + 2.0f * sx,
                         ufoCy + 6.0f * sy,
                         29.0f * sx,
                         7.2f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x1C1C1C)),
                         0u,
                         0.0f);
    DrawEllipseFilledRot(dl,
                         ufoCx + 3.0f * sx,
                         ufoCy + 7.5f * sy,
                         21.0f * sx,
                         5.0f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x5B5AEA, 0.72f)),
                         0u,
                         0.0f);
}

}  // namespace motrix::ui
