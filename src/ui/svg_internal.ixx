export module motrix.ui:svg_internal;

import std;

namespace motrix::ui {

export struct UfoRgba {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 0.0f;
};

export struct UfoPt {
    float x = 0.0f;
    float y = 0.0f;
};

export struct SvgMat {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
};

export struct SvgGrad {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    UfoRgba stop0{};
    UfoRgba stop1{};
    float minX = 0.0f;
    float minY = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    SvgMat invMat{};
};

export struct SvgCanvas {
    int width = 0;
    int height = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    std::vector<UfoRgba> pixels;
};

export struct SvgPaint {
    bool useGrad = false;
    UfoRgba solid{};
    SvgGrad grad{};
    float opacity = 1.0f;
    float strokeW = 0.0f;
    UfoRgba strokeCol{};
};

export [[nodiscard]] constexpr UfoRgba HexStop(std::uint32_t rgb, float a = 1.0f) noexcept {
    return {static_cast<float>((rgb >> 16u) & 0xFFu) / 255.0f,
            static_cast<float>((rgb >> 8u) & 0xFFu) / 255.0f,
            static_cast<float>(rgb & 0xFFu) / 255.0f,
            a};
}

export [[nodiscard]] SvgMat MulMat(const SvgMat& m1, const SvgMat& m2) noexcept;
export [[nodiscard]] SvgMat MakeRotateAt(float deg, float cx, float cy) noexcept;
export [[nodiscard]] std::vector<UfoPt> ParseSvgPath(std::string_view d);
export [[nodiscard]] std::vector<UfoPt> BuildEllipsePoly(float cx, float cy, float rx, float ry);
export void
RenderSvgPoly(SvgCanvas& cv, const std::vector<UfoPt>& localPts, const SvgMat& mat, SvgPaint paint);

}  // namespace motrix::ui
