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
    dl->AddTriangleFilled(
        ImVec2(baseX, baseY - h), ImVec2(baseX - w * 0.5f, baseY), ImVec2(baseX + w * 0.5f, baseY), col);
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

    // 1. Two overlapping misty hills tapering seamlessly to 0 alpha at boundaries
    const RgbaColor hillLeftTop = dark ? HexColor(0xFFFFFF, 0.11f) : HexColor(0xD2D8E0, 0.34f);
    const RgbaColor hillRightTop = dark ? HexColor(0xFFFFFF, 0.16f) : HexColor(0xCBD2DC, 0.44f);
    const RgbaColor hillBot = dark ? HexColor(0x343434, 0.0f) : HexColor(0xFFFFFF, 0.0f);
    DrawSoftBellHill(dl, x0, y0, sx, sy, 28.0f, 305.0f, 54.0f, 304.0f, hillLeftTop, hillBot);
    DrawSoftBellHill(dl, x0, y0, sx, sy, 156.0f, 476.0f, 78.0f, 306.0f, hillRightTop, hillBot);

    // 2. Geometric pine trees on left & right hill crests
    const ImU32 treeDark = ToAbgrU32(dark ? HexColor(0x787878, 0.90f) : HexColor(0x5E5E5E, 0.92f));
    const ImU32 treeSoft = ToAbgrU32(dark ? HexColor(0x6C6C6C, 0.50f) : HexColor(0x9A9A9A, 0.45f));
    DrawPineTree(dl, x0 + 132.0f * sx, y0 + 258.0f * sy, 11.0f * sx, 22.0f * sy, treeSoft);
    DrawPineTree(dl, x0 + 150.0f * sx, y0 + 255.0f * sy, 16.0f * sx, 36.0f * sy, treeDark);
    DrawPineTree(dl, x0 + 340.0f * sx, y0 + 234.0f * sy, 12.0f * sx, 24.0f * sy, treeSoft);
    DrawPineTree(dl, x0 + 365.0f * sx, y0 + 238.0f * sy, 22.0f * sx, 46.0f * sy, treeDark);

    // 3. UFO Tractor Beam (tilted ~17deg down-right, fading softly)
    const RgbaColor beamTop = HexColor(0x5B5AEA, 0.48f);
    const RgbaColor beamBot = dark ? HexColor(0x343434, 0.0f) : HexColor(0xFFFFFF, 0.0f);
    DrawGradientQuadStrip(dl,
                          pt(194.0f, 66.0f),
                          pt(238.0f, 52.0f),
                          pt(292.0f, 192.0f),
                          pt(206.0f, 212.0f),
                          beamTop,
                          beamBot,
                          24);

    // 4. Tilted UFO Saucer body
    const float ufoCx = x0 + 216.0f * sx;
    const float ufoCy = y0 + 50.0f * sy;
    const float tilt = -0.33f;  // ~ -19 degrees

    // Top cockpit dome
    DrawEllipseFilledRot(dl,
                         ufoCx - 3.0f * sx,
                         ufoCy - 11.0f * sy,
                         24.0f * sx,
                         13.0f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x4C4C4C)),
                         ToAbgrU32(HexColor(0x1B1B1B)),
                         1.3f * sx);
    // Main saucer hull
    DrawEllipseFilledRot(dl,
                         ufoCx,
                         ufoCy,
                         54.0f * sx,
                         15.5f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x3E3E3E)),
                         ToAbgrU32(HexColor(0x1B1B1B)),
                         1.5f * sx);
    // Metallic upper ring highlight
    DrawEllipseFilledRot(dl,
                         ufoCx - 0.5f * sx,
                         ufoCy - 2.0f * sy,
                         50.0f * sx,
                         12.0f * sy,
                         tilt,
                         ToAbgrU32(HexColor(0x5A5860, 0.55f)),
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
