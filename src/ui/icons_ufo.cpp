module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.platform;
import :anim;
import :icons;
import :svg_internal;
import :theme;

namespace motrix::ui {

namespace {

[[nodiscard]] std::vector<std::uint8_t> BuildUfoTexturePixels(int pxW, int pxH, bool dark) {
    SvgCanvas cv{pxW, pxH, static_cast<float>(pxW) / 504.0f, static_cast<float>(pxH) / 309.0f, {}};
    const UfoRgba clearBg =
        dark ? UfoRgba{0.141f, 0.141f, 0.141f, 0.0f} : UfoRgba{1.0f, 1.0f, 1.0f, 0.0f};
    cv.pixels.assign(static_cast<std::size_t>(pxW * pxH), clearBg);

    const SvgMat hillMat{-1.0f, 0.0f, 0.0f, 1.0f, 523.0f, -26.0f};
    const SvgMat flipX{-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const SvgMat ufoMat = MulMat(flipX, MakeRotateAt(-9.0f, -170.11f, 1540.559f));
    const SvgMat beamMat = MulMat(flipX, MakeRotateAt(17.0f, -176.317f, -766.798f));

    const SvgGrad gradA{
        0.50f, 0.33114f, 0.50f, 0.0f, HexStop(0x2D2D2D, 0.0f), HexStop(0xFFFFFF, 0.216f)};
    RenderSvgPoly(
        cv,
        ParseSvgPath(
            "M405.205 457C474.128 457 530 416.258 530 366s-55.872-91-124.795-91c-47.428 0-88.853 "
            "33.5-131.783 45-42.929 11.5 6.989 30.327 6.989 46 0 50.258 55.872 91 124.794 91Z"),
        hillMat,
        {true, {}, gradA});
    const SvgGrad gradB{
        0.54257f, 0.27941f, 0.54257f, 0.0f, HexStop(0x6D6D6D, 0.0f), HexStop(0xFFFFFF, 0.216f)};
    RenderSvgPoly(
        cv,
        ParseSvgPath("M187.397 506c103.496 0 187.396-61.113 187.396-136.5 0-26.12 63.465-69.5 "
                     "0-81.5s-119.76-55-187.396-55C83.9 233 0 294.113 0 369.5S83.9 506 187.397 506Z"),
        hillMat,
        {true, {}, gradB});

    RenderSvgPoly(cv,
                  ParseSvgPath("M54.668 52.983c16.709 8.885 33.389 10.19 37.256 2.918 "
                               "3.867-7.274-4.188-24.564-20.896-33.448-16.709-8.884-35.744-5.998-39.611 "
                               "1.275C27.55 31.001 37.96 44.1 54.668 52.983Z"),
                  ufoMat,
                  {false, HexStop(0x4C4C4C)});
    RenderSvgPoly(cv,
                  ParseSvgPath("M50.232 59.072C77.574 73.61 103.094 79.084 107.233 "
                               "71.3c4.139-7.784-12.393-29.774-39.735-44.312C40.157 12.451 12.36 10.87 "
                               "8.221 18.654c-4.14 7.784 14.67 25.88 42.011 40.418Z"),
                  ufoMat,
                  {false, HexStop(0x6C6971), {}, 1.0f, 1.5f, HexStop(0x1B1B1B)});
    RenderSvgPoly(cv,
                  BuildEllipsePoly(57.554f, 45.301f, 56.069f, 15.595f),
                  MulMat(ufoMat, MakeRotateAt(28.0f, 57.554f, 45.301f)),
                  {false, HexStop(0x3E3E3E), {}, 1.0f, 1.5f, HexStop(0x1B1B1B)});
    RenderSvgPoly(cv,
                  ParseSvgPath("M50.058 60.044c15.801 8.401 30.235 12.158 32.239 8.39 "
                               "2.003-3.768-8.508-14.957-24.31-23.36-15.801-8.4-30.91-10.834-32.913-7."
                               "066-2.003 3.768 9.182 13.634 24.984 22.036Z"),
                  ufoMat,
                  {false, HexStop(0x1C1C1C)});
    RenderSvgPoly(
        cv,
        ParseSvgPath(
            "M25.1 51.905c3.02 3.566 15.726 11.347 22.227 14.803 10.348 5.502 23.194 11.02 27.68 "
            "9.412v-5.57c-1.487-5.946-7.004-12.235-19.644-18.955-12.64-6.72-24.685-8.786-26.904-4.612-."
            "078.121-1.724 2.402-3.36 4.922Z"),
        ufoMat,
        {false, HexStop(0x5B5AEA), {}, 0.2f});

    const SvgGrad gradC{0.6764f, -0.01183f, 0.6764f, 0.87046f, HexStop(0xBBBBBB), HexStop(0x1B1B1B)};
    RenderSvgPoly(
        cv,
        ParseSvgPath("M.438 122.756C6.93 103.82 23.403 19.87 28.723 7.44 31.215 1.85 46.146.443 "
                     "57.129.606c10.983.164 20.506 1.666 22.646 9.54 2.824 10.395 20.857 115.671 20.857 "
                     "115.671s-15.168 9.99-49.377 10.392C17.047 136.61.438 122.756.438 122.756"),
        beamMat,
        {true, {}, gradC, 0.2f});
    const SvgGrad gradD{
        0.72034f, 0.00549f, 0.62937f, 0.70566f, HexStop(0x5B5AEA), HexStop(0xE7D1AF, 0.0f)};
    RenderSvgPoly(
        cv,
        ParseSvgPath("M16.982 126.328C21.493 108.529 34.185 14.868 37.859 3.32 40.685.698 48.114.374 "
                     "55.898.698 63.68 1.023 70.92 2.52 71.11 3.62 72.937 14.2 85.19 130.47 85.19 "
                     "130.47s-14.028 3.94-33.335 3.956c-19.307.017-34.873-8.099-34.873-8.099"),
        beamMat,
        {true, {}, gradD, 0.5f});

    const SvgMat id{};
    RenderSvgPoly(
        cv, ParseSvgPath("m365.733 181.133-13.422 28.495 22.47 1.966z"), id, {false, HexStop(0x646464)});
    RenderSvgPoly(
        cv, ParseSvgPath("m367.38 165.55-13.674 28.472 22.99 2.011z"), id, {false, HexStop(0x646464)});
    RenderSvgPoly(cv,
                  ParseSvgPath("m113.133 234.567 6.712 14.246-11.236.983z"),
                  id,
                  {false, HexStop(0x5D5D5D), {}, 0.7f});
    RenderSvgPoly(cv,
                  ParseSvgPath("m112.31 226.775 6.837 14.236-11.495 1.006z"),
                  id,
                  {false, HexStop(0x5D5D5D), {}, 0.7f});
    RenderSvgPoly(cv,
                  ParseSvgPath("m334.934 185.378-4.672 22.32 12.067-.633z"),
                  id,
                  {false, HexStop(0x6B6B6B), {}, 0.4f});
    RenderSvgPoly(
        cv, ParseSvgPath("m138.43 214.34 5.3 37.069-14.964-.784z"), id, {false, HexStop(0x686868)});

    std::vector<std::uint8_t> rgba(cv.pixels.size() * 4u);
    const auto toByte = [](float v) noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    for (std::size_t i = 0; i < cv.pixels.size(); ++i) {
        rgba[i * 4u + 0u] = toByte(cv.pixels[i].r);
        rgba[i * 4u + 1u] = toByte(cv.pixels[i].g);
        rgba[i * 4u + 2u] = toByte(cv.pixels[i].b);
        rgba[i * 4u + 3u] = toByte(cv.pixels[i].a);
    }
    return rgba;
}

void DrawEmptyUfoFallback(const UfoIllustrationSpec& spec) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ox = spec.centerX - spec.width * 0.5f;
    const float sx = spec.width / 504.0f;
    const float sy = spec.height / 309.0f;
    const float cx = ox + 179.0f * sx;
    const float cy = spec.topY + 28.0f * sy;
    dl->AddEllipseFilled(
        ImVec2(cx, cy), ImVec2(56.0f * sx, 16.0f * sy), ToAbgrU32(HexColor(0x3E3E3E)), -0.33f, 64);
    dl->AddEllipse(
        ImVec2(cx, cy), ImVec2(56.0f * sx, 16.0f * sy), ToAbgrU32(HexColor(0x1B1B1B)), -0.33f, 64, 1.5f);
}

}  // namespace

void DrawEmptyUfoIllustration(const UfoIllustrationSpec& spec) {
    if (spec.width <= 1.0f || spec.height <= 1.0f) {
        return;
    }
    if (spec.host != nullptr && spec.cache != nullptr) {
        CachedTextureSlot& cache = *spec.cache;
        const int rawW = static_cast<int>(std::lround(spec.width * 2.0f));
        const int targetW = std::clamp(((rawW + 63) / 64) * 64, 768, 1280);
        const int targetH = std::max(
            128, static_cast<int>(std::lround(static_cast<float>(targetW) * (309.0f / 504.0f))));
        if (cache.owner != spec.host) {
            cache.texId = 0;
            cache.owner = spec.host;
        }
        if (cache.texId == 0 || cache.texW != targetW || cache.texH != targetH
            || cache.dark != spec.dark) {
            const auto pixels = BuildUfoTexturePixels(targetW, targetH, spec.dark);
            const std::uint64_t uploaded =
                spec.host->UploadRgbaTexture(cache.texId, targetW, targetH, pixels);
            if (uploaded != 0) {
                cache.texId = uploaded;
                cache.texW = targetW;
                cache.texH = targetH;
                cache.dark = spec.dark;
            }
        }
        if (cache.texId != 0) {
            const float x0 = std::floor(spec.centerX - spec.width * 0.5f);
            const float y0 = std::floor(spec.topY);
            const float x1 = x0 + std::round(spec.width);
            const float y1 = y0 + std::round(spec.height);
            ImGui::GetWindowDrawList()->AddImage(
                ImTextureRef(static_cast<ImTextureID>(cache.texId)), ImVec2(x0, y0), ImVec2(x1, y1));
            return;
        }
    }
    DrawEmptyUfoFallback(spec);
}

}  // namespace motrix::ui
