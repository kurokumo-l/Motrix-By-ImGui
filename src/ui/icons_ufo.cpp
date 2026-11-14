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

struct UfoCanvas {
    ImDrawList* dl = nullptr;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;

    [[nodiscard]] ImVec2 Pt(float vx, float vy) const noexcept {
        return ImVec2(x0 + vx * sx, y0 + vy * sy);
    }
};

struct QuadGradSpec {
    ImVec2 topL{};
    ImVec2 topR{};
    ImVec2 botR{};
    ImVec2 botL{};
    RgbaColor topCol{};
    RgbaColor botCol{};
    int slices = 24;
};

struct RotEllipseSpec {
    float cx = 0.0f;
    float cy = 0.0f;
    float rx = 1.0f;
    float ry = 1.0f;
    float angleRad = 0.0f;
    ImU32 fillCol = 0;
    ImU32 strokeCol = 0;
    float strokeW = 0.0f;
};

struct PineTreeSpec {
    float baseX = 0.0f;
    float baseY = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    ImU32 col = 0;
};

struct BellHillSpec {
    float startX = 0.0f;
    float endX = 0.0f;
    float peakHeight = 0.0f;
    float baseY = 0.0f;
    RgbaColor topCol{};
    RgbaColor botCol{};
};

void DrawGradientQuadStrip(ImDrawList* dl, const QuadGradSpec& q) {
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
    for (int i = 0; i < q.slices; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(q.slices);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(q.slices);
        const ImVec2 p0 = lerpPt(q.topL, q.botL, t0);
        const ImVec2 p1 = lerpPt(q.topR, q.botR, t0);
        const ImVec2 p2 = lerpPt(q.topR, q.botR, t1);
        const ImVec2 p3 = lerpPt(q.topL, q.botL, t1);
        const ImU32 cTop = lerpCol(q.topCol, q.botCol, t0);
        const ImU32 cBot = lerpCol(q.topCol, q.botCol, t1);

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

void DrawEllipseFilledRot(ImDrawList* dl, const RotEllipseSpec& e) {
    constexpr int kSegs = 40;
    std::array<ImVec2, kSegs> pts{};
    for (int i = 0; i < kSegs; ++i) {
        const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(kSegs);
        const float lx = e.cx + std::cos(a) * e.rx;
        const float ly = e.cy + std::sin(a) * e.ry;
        pts[static_cast<std::size_t>(i)] = RotPt(e.cx, e.cy, lx, ly, e.angleRad);
    }
    dl->AddConvexPolyFilled(pts.data(), kSegs, e.fillCol);
    if ((e.strokeCol >> 24) > 0 && e.strokeW > 0.0f) {
        dl->AddPolyline(pts.data(), kSegs, e.strokeCol, ImDrawFlags_Closed, e.strokeW);
    }
}

void DrawPineTree(ImDrawList* dl, const PineTreeSpec& t) {
    // Two-tier stylized Motrix conifer matching no-task.svg
    const float lowerTopY = t.baseY - t.h * 0.64f;
    const float upperTopY = t.baseY - t.h;
    const float upperBotY = t.baseY - t.h * 0.34f;
    dl->AddTriangleFilled(ImVec2(t.baseX + t.w * 0.03f, lowerTopY),
                          ImVec2(t.baseX - t.w * 0.50f, t.baseY),
                          ImVec2(t.baseX + t.w * 0.50f, t.baseY),
                          t.col);
    dl->AddTriangleFilled(ImVec2(t.baseX, upperTopY),
                          ImVec2(t.baseX - t.w * 0.44f, upperBotY),
                          ImVec2(t.baseX + t.w * 0.44f, upperBotY),
                          t.col);
}

void DrawSoftBellHill(const UfoCanvas& uc, const BellHillSpec& h) {
    constexpr float kPi = 3.14159265f;
    constexpr int kSegs = 32;
    for (int i = 0; i < kSegs; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(kSegs);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegs);
        const float s0 = std::sin(t0 * kPi);
        const float s1 = std::sin(t1 * kPi);
        const float w0 = s0 * s0;
        const float w1 = s1 * s1;
        const float xA = h.startX + (h.endX - h.startX) * t0;
        const float xB = h.startX + (h.endX - h.startX) * t1;
        const float yA = h.baseY - h.peakHeight * w0;
        const float yB = h.baseY - h.peakHeight * w1;
        const RgbaColor topA{h.topCol.r, h.topCol.g, h.topCol.b, h.topCol.a * w0};
        const RgbaColor topB{h.topCol.r, h.topCol.g, h.topCol.b, h.topCol.a * w1};
        const RgbaColor midTop{
            (topA.r + topB.r) * 0.5f,
            (topA.g + topB.g) * 0.5f,
            (topA.b + topB.b) * 0.5f,
            (topA.a + topB.a) * 0.5f,
        };
        DrawGradientQuadStrip(
            uc.dl,
            {uc.Pt(xA, yA), uc.Pt(xB, yB), uc.Pt(xB, h.baseY), uc.Pt(xA, h.baseY), midTop, h.botCol, 6});
    }
}

void DrawUfoLandscape(const UfoCanvas& uc, bool dark) {
    const RgbaColor hillLeftTop = dark ? HexColor(0xFFFFFF, 0.13f) : HexColor(0xD4DAE2, 0.42f);
    const RgbaColor hillRightTop = dark ? HexColor(0xFFFFFF, 0.18f) : HexColor(0xC9D0DA, 0.52f);
    const RgbaColor hillBot = dark ? HexColor(0x343434, 0.0f) : HexColor(0xFFFFFF, 0.0f);
    DrawSoftBellHill(uc, {48.0f, 326.0f, 52.0f, 296.0f, hillLeftTop, hillBot});
    DrawSoftBellHill(uc, {165.0f, 474.0f, 78.0f, 294.0f, hillRightTop, hillBot});

    const ImU32 treeDark = ToAbgrU32(dark ? HexColor(0x7B7B7B, 0.92f) : HexColor(0x5E5E5E, 0.94f));
    const ImU32 treeSoft = ToAbgrU32(dark ? HexColor(0x6C6C6C, 0.48f) : HexColor(0xB4B4B4, 0.55f));
    uc.dl->AddTriangleFilled(
        uc.Pt(130.0f, 232.0f), uc.Pt(124.5f, 255.0f), uc.Pt(135.5f, 255.0f), treeSoft);
    DrawPineTree(
        uc.dl, {uc.x0 + 148.0f * uc.sx, uc.y0 + 252.0f * uc.sy, 18.0f * uc.sx, 38.0f * uc.sy, treeDark});
    uc.dl->AddTriangleFilled(
        uc.Pt(337.0f, 209.0f), uc.Pt(331.0f, 234.0f), uc.Pt(343.0f, 234.0f), treeSoft);
    DrawPineTree(
        uc.dl, {uc.x0 + 364.0f * uc.sx, uc.y0 + 236.0f * uc.sy, 23.0f * uc.sx, 49.0f * uc.sy, treeDark});
}

void DrawUfoBeamAndSaucer(const UfoCanvas& uc, bool dark) {
    const RgbaColor outerTop = dark ? HexColor(0xBBBBBB, 0.20f) : HexColor(0x707070, 0.25f);
    const RgbaColor outerBot = dark ? HexColor(0x404040, 0.03f) : HexColor(0x9A9A9A, 0.06f);
    DrawGradientQuadStrip(uc.dl,
                          {uc.Pt(191.0f, 67.0f),
                           uc.Pt(243.0f, 51.0f),
                           uc.Pt(318.0f, 188.0f),
                           uc.Pt(210.0f, 224.0f),
                           outerTop,
                           outerBot,
                           24});
    const RgbaColor innerTop = HexColor(0x5B5AEA, 0.56f);
    const RgbaColor innerBot = dark ? HexColor(0x5B5AEA, 0.0f) : HexColor(0xE7D1AF, 0.0f);
    DrawGradientQuadStrip(uc.dl,
                          {uc.Pt(199.0f, 65.0f),
                           uc.Pt(235.0f, 54.0f),
                           uc.Pt(296.0f, 182.0f),
                           uc.Pt(222.0f, 206.0f),
                           innerTop,
                           innerBot,
                           24});

    const float ufoCx = uc.x0 + 216.0f * uc.sx;
    const float ufoCy = uc.y0 + 51.0f * uc.sy;
    constexpr float kTilt = -0.34f;
    DrawEllipseFilledRot(uc.dl,
                         {ufoCx - 3.5f * uc.sx,
                          ufoCy - 11.5f * uc.sy,
                          25.0f * uc.sx,
                          13.5f * uc.sy,
                          kTilt,
                          ToAbgrU32(HexColor(0x4C4C4C)),
                          ToAbgrU32(HexColor(0x1B1B1B)),
                          1.4f * uc.sx});
    DrawEllipseFilledRot(uc.dl,
                         {ufoCx,
                          ufoCy,
                          55.5f * uc.sx,
                          16.0f * uc.sy,
                          kTilt,
                          ToAbgrU32(HexColor(0x3E3E3E)),
                          ToAbgrU32(HexColor(0x1B1B1B)),
                          1.5f * uc.sx});
    DrawEllipseFilledRot(uc.dl,
                         {ufoCx - 0.5f * uc.sx,
                          ufoCy - 2.0f * uc.sy,
                          51.0f * uc.sx,
                          12.5f * uc.sy,
                          kTilt,
                          ToAbgrU32(HexColor(0x6C6971, 0.45f))});
    DrawEllipseFilledRot(uc.dl,
                         {ufoCx + 2.0f * uc.sx,
                          ufoCy + 6.0f * uc.sy,
                          29.0f * uc.sx,
                          7.2f * uc.sy,
                          kTilt,
                          ToAbgrU32(HexColor(0x1C1C1C))});
    DrawEllipseFilledRot(uc.dl,
                         {ufoCx + 3.0f * uc.sx,
                          ufoCy + 7.5f * uc.sy,
                          21.0f * uc.sx,
                          5.0f * uc.sy,
                          kTilt,
                          ToAbgrU32(HexColor(0x5B5AEA, 0.72f))});
}

}  // namespace

void DrawEmptyUfoIllustration(const UfoIllustrationSpec& spec) {
    const UfoCanvas uc{
        ImGui::GetWindowDrawList(),
        spec.centerX - spec.width * 0.5f,
        spec.topY,
        spec.width / 504.0f,
        spec.height / 309.0f,
    };
    DrawUfoLandscape(uc, spec.dark);
    DrawUfoBeamAndSaucer(uc, spec.dark);
}

}  // namespace motrix::ui
