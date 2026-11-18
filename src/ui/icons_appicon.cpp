module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.platform;
import :anim;
import :icons;
import :theme;

namespace motrix::ui {

namespace {

[[nodiscard]] float SdRoundedBox(float px, float py, float halfW, float halfH, float radius) noexcept {
    const float qx = std::abs(px) - halfW + radius;
    const float qy = std::abs(py) - halfH + radius;
    const float mx = std::max(qx, 0.0f);
    const float my = std::max(qy, 0.0f);
    return std::hypot(mx, my) + std::min(std::max(qx, qy), 0.0f) - radius;
}

[[nodiscard]] float AaCoverage(float sdf, float aaPx) noexcept {
    return std::clamp(0.5f - sdf / std::max(aaPx, 1e-4f), 0.0f, 1.0f);
}

void BlendLayer(RgbaColor& dst, RgbaColor src, float coverage) noexcept {
    const float a = std::clamp(src.a * coverage, 0.0f, 1.0f);
    if (a <= 0.0005f) {
        return;
    }
    const float outA = a + dst.a * (1.0f - a);
    const float wSrc = a / outA;
    const float wDst = dst.a * (1.0f - a) / outA;
    dst.r = src.r * wSrc + dst.r * wDst;
    dst.g = src.g * wSrc + dst.g * wDst;
    dst.b = src.b * wSrc + dst.b * wDst;
    dst.a = outA;
}

void ShadeBodyAndVisor(RgbaColor& out, float u, float v, float aa) noexcept {
    const float cx = u - 0.5f;
    const float cy = v - 0.5f + 0.006f;
    // Soft ambient shadow below robot head
    const float shSdf = SdRoundedBox(cx, cy - 0.022f, 0.295f, 0.295f, 0.110f);
    const float shAlpha = std::clamp(1.0f - (shSdf + 0.020f) / 0.065f, 0.0f, 1.0f);
    BlendLayer(out, HexColor(0x161238, 0.30f), shAlpha * shAlpha);

    // Cream-white M-O face tile
    const float faceSdf = SdRoundedBox(cx, cy, 0.292f, 0.292f, 0.105f);
    const float faceCov = AaCoverage(faceSdf, aa);
    if (faceCov <= 0.0f) {
        return;
    }
    const float faceT = std::clamp((v - 0.202f) / 0.584f, 0.0f, 1.0f);
    const RgbaColor faceCol = LerpColor(HexColor(0xFAFBFD), HexColor(0xDFE4EE), faceT);
    BlendLayer(out, faceCol, faceCov);

    // Flush-top black visor extending from robot top edge to v=0.330 with rounded bottom corners
    const float visorSdf = SdRoundedBox(cx, v - 0.238f, 0.184f, 0.092f, 0.024f);
    const float visorClip = std::max(visorSdf, faceSdf);
    const float visorCov = AaCoverage(visorClip, aa);
    const float visorGrad = std::clamp((v - 0.202f) / 0.128f, 0.0f, 1.0f);
    BlendLayer(out, LerpColor(HexColor(0x0D0E12), HexColor(0x1C1E23), visorGrad), visorCov);

    // Two golden LCD eyes centered vertically in the visor
    const float eyeSdfL = SdRoundedBox(u - 0.440f, v - 0.266f, 0.038f, 0.017f, 0.005f);
    const float eyeSdfR = SdRoundedBox(u - 0.560f, v - 0.266f, 0.038f, 0.017f, 0.005f);
    const float eyeCov = std::max(AaCoverage(eyeSdfL, aa), AaCoverage(eyeSdfR, aa));
    BlendLayer(out, HexColor(0xF3D026), eyeCov);
}

[[nodiscard]] RgbaColor SampleAppIconPixel(float u, float v, bool dark, float aa) noexcept {
    RgbaColor out{0.0f, 0.0f, 0.0f, 0.0f};
    const float cx = u - 0.5f;
    const float cy = v - 0.5f;

    // Soft outer drop shadow with unclipped margin around the squircle
    const float outerSdf = SdRoundedBox(cx, cy + 0.006f, 0.466f, 0.466f, 0.198f);
    const float shSdf = SdRoundedBox(cx, cy - 0.008f, 0.466f, 0.466f, 0.198f);
    const float dropAlpha = std::clamp(1.0f - (shSdf + 0.006f) / 0.030f, 0.0f, 1.0f);
    BlendLayer(out, HexColor(0x000000, dark ? 0.34f : 0.16f), dropAlpha * dropAlpha);

    // Outer light rim + silver-white squircle body
    const std::uint32_t frameTop = dark ? 0xECEEF3u : 0xFAFBFDu;
    const std::uint32_t frameBot = dark ? 0xD2D6E0u : 0xE2E5EEu;
    const RgbaColor frameCol = LerpColor(HexColor(frameTop), HexColor(frameBot), v);
    const float outerCov = AaCoverage(outerSdf, aa);
    BlendLayer(out, frameCol, outerCov);

    // Subtle hairline rim so the light squircle separates cleanly from white cards
    if (!dark && outerCov > 0.0f) {
        const float innerRimMask = 1.0f - AaCoverage(outerSdf + 0.012f, aa);
        BlendLayer(out, HexColor(0xB8C0D0, 0.42f), outerCov * innerRimMask);
    }

    // Inner purple-to-indigo gradient squircle + subtle inset edge bevel
    const float gradSdf = SdRoundedBox(cx, cy + 0.006f, 0.414f, 0.414f, 0.162f);
    const float gradCov = AaCoverage(gradSdf, aa);
    if (gradCov > 0.0f) {
        const float diagT = std::clamp((u * 0.58f + v * 0.82f - 0.15f) / 1.10f, 0.0f, 1.0f);
        const RgbaColor gradCol = LerpColor(HexColor(0xCF7BE0), HexColor(0x525CEB), diagT);
        BlendLayer(out, gradCol, gradCov);

        const float insetRimMask = 1.0f - AaCoverage(gradSdf + 0.014f, aa);
        BlendLayer(out, HexColor(0x261B58, 0.22f), gradCov * insetRimMask);
    }

    ShadeBodyAndVisor(out, u, v, aa);
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> BuildAppIconPixels(int size, bool dark) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size * size) * 4u);
    const float aa = 1.4f / static_cast<float>(size);
    const auto toByte = [](float v) noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    for (int py = 0; py < size; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(size);
        for (int px = 0; px < size; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(size);
            const RgbaColor c = SampleAppIconPixel(u, v, dark, aa);
            const std::size_t idx = (static_cast<std::size_t>(py * size + px)) * 4u;
            rgba[idx + 0u] = toByte(c.r);
            rgba[idx + 1u] = toByte(c.g);
            rgba[idx + 2u] = toByte(c.b);
            rgba[idx + 3u] = toByte(c.a);
        }
    }
    return rgba;
}

void DrawAppIconVectorFallback(const AppIconSpec& spec) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = spec.size;
    const ImVec2 p0(spec.x, spec.y);
    const ImVec2 p1(spec.x + s, spec.y + s);
    dl->AddRectFilled(p0, p1, ToAbgrU32(HexColor(0xF5F6FA)), s * 0.21f);
    dl->AddRectFilled(ImVec2(p0.x + s * 0.080f, p0.y + s * 0.080f),
                      ImVec2(p1.x - s * 0.080f, p1.y - s * 0.080f),
                      ToAbgrU32(HexColor(0x6D63E8)),
                      s * 0.165f);
    dl->AddRectFilled(ImVec2(p0.x + s * 0.208f, p0.y + s * 0.202f),
                      ImVec2(p1.x - s * 0.208f, p1.y - s * 0.214f),
                      ToAbgrU32(HexColor(0xF7F8FB)),
                      s * 0.105f);
    dl->AddRectFilled(ImVec2(p0.x + s * 0.316f, p0.y + s * 0.202f),
                      ImVec2(p1.x - s * 0.316f, p0.y + s * 0.330f),
                      ToAbgrU32(HexColor(0x141519)),
                      s * 0.02f);
}

void DrawMiniWordmarkOnBadge(const AppIconSpec& spec) {
    const float markH = spec.size * 0.068f;
    const float markW = markH * (62.0f / 14.0f);
    const float markX = spec.x + spec.size * 0.422f;
    const float markY = spec.y + spec.size * 0.642f;
    DrawMotrixIcon(IconSpec{MotrixIcon::LogoFull, markX, markY, markW, markH, HexColor(0x8E96A4), 1.0f});
}

}  // namespace

void DrawMotrixAppIcon(const AppIconSpec& spec) {
    if (spec.size <= 1.0f) {
        return;
    }
    if (spec.host != nullptr && spec.cache != nullptr) {
        CachedTextureSlot& cache = *spec.cache;
        const int targetPx = std::clamp(static_cast<int>(std::lround(spec.size * 2.5f)), 128, 256);
        if (cache.owner != spec.host) {
            cache.texId = 0;
            cache.owner = spec.host;
        }
        if (cache.texId == 0 || cache.texW != targetPx || cache.dark != spec.dark) {
            const auto pixels = BuildAppIconPixels(targetPx, spec.dark);
            const std::uint64_t uploaded =
                spec.host->UploadRgbaTexture(cache.texId, targetPx, targetPx, pixels);
            if (uploaded != 0) {
                cache.texId = uploaded;
                cache.texW = targetPx;
                cache.texH = targetPx;
                cache.dark = spec.dark;
            }
        }
        if (cache.texId != 0) {
            const ImVec2 p0(std::floor(spec.x), std::floor(spec.y));
            const ImVec2 p1(p0.x + std::round(spec.size), p0.y + std::round(spec.size));
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef(static_cast<ImTextureID>(cache.texId)), p0, p1);
            DrawMiniWordmarkOnBadge(spec);
            return;
        }
    }
    DrawAppIconVectorFallback(spec);
    DrawMiniWordmarkOnBadge(spec);
}

}  // namespace motrix::ui
