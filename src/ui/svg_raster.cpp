module motrix.ui;

import std;
import :svg_internal;

namespace motrix::ui {

namespace {

struct CubicSeg {
    UfoPt p0{};
    UfoPt p1{};
    UfoPt p2{};
    UfoPt p3{};
};

struct SubRowSpan {
    float subY = 0.0f;
    int xMin = 0;
    int xMax = 0;
};

[[nodiscard]] SvgMat InvertMat(const SvgMat& m) noexcept {
    const float det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-8f) {
        return {};
    }
    const float inv = 1.0f / det;
    return {m.d * inv,
            -m.b * inv,
            -m.c * inv,
            m.a * inv,
            (m.c * m.ty - m.d * m.tx) * inv,
            (m.b * m.tx - m.a * m.ty) * inv};
}

[[nodiscard]] UfoPt ApplyMat(const SvgMat& m, UfoPt p) noexcept {
    return {m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty};
}

void BlendStraight(UfoRgba& dst, const UfoRgba& src, float cov) noexcept {
    const float sa = std::clamp(src.a * cov, 0.0f, 1.0f);
    if (sa <= 0.0002f) {
        return;
    }
    const float na = sa + dst.a * (1.0f - sa);
    const float wSrc = sa / na;
    const float wDst = (dst.a * (1.0f - sa)) / na;
    dst.r = src.r * wSrc + dst.r * wDst;
    dst.g = src.g * wSrc + dst.g * wDst;
    dst.b = src.b * wSrc + dst.b * wDst;
    dst.a = na;
}

[[nodiscard]] UfoRgba EvalGrad(const SvgGrad& g, UfoPt pxPt, float scaleX, float scaleY) noexcept {
    const UfoPt lp = ApplyMat(g.invMat, {pxPt.x / scaleX, pxPt.y / scaleY});
    const float gx1 = g.minX + g.x1 * g.w;
    const float gy1 = g.minY + g.y1 * g.h;
    const float gx2 = g.minX + g.x2 * g.w;
    const float gy2 = g.minY + g.y2 * g.h;
    const float vx = gx2 - gx1;
    const float vy = gy2 - gy1;
    const float len2 = vx * vx + vy * vy;
    const float t =
        len2 > 1e-8f ? std::clamp(((lp.x - gx1) * vx + (lp.y - gy1) * vy) / len2, 0.0f, 1.0f) : 0.0f;
    return {g.stop0.r + (g.stop1.r - g.stop0.r) * t,
            g.stop0.g + (g.stop1.g - g.stop0.g) * t,
            g.stop0.b + (g.stop1.b - g.stop0.b) * t,
            g.stop0.a + (g.stop1.a - g.stop0.a) * t};
}

void FlattenCubic(const CubicSeg& seg, std::vector<UfoPt>& out, int depth) {
    const auto perpErr = [](UfoPt a, UfoPt b, UfoPt p) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float d2 = dx * dx + dy * dy;
        const float num = (p.x - a.x) * dy - (p.y - a.y) * dx;
        return d2 > 1e-8f ? (num * num) / d2 : (p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y);
    };
    if (depth >= 7
        || (perpErr(seg.p0, seg.p3, seg.p1) < 0.01f && perpErr(seg.p0, seg.p3, seg.p2) < 0.01f)) {
        out.push_back(seg.p3);
        return;
    }
    const auto mid = [](UfoPt u, UfoPt v) { return UfoPt{(u.x + v.x) * 0.5f, (u.y + v.y) * 0.5f}; };
    const UfoPt a = mid(seg.p0, seg.p1);
    const UfoPt b = mid(seg.p1, seg.p2);
    const UfoPt c = mid(seg.p2, seg.p3);
    const UfoPt d = mid(a, b);
    const UfoPt e = mid(b, c);
    const UfoPt f = mid(d, e);
    FlattenCubic({seg.p0, a, d, f}, out, depth + 1);
    FlattenCubic({f, e, c, seg.p3}, out, depth + 1);
}

[[nodiscard]] bool ReadSvgFloat(std::string_view& sv, float& out) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == ',' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    if (sv.empty()) {
        return false;
    }
    const char ch = sv.front();
    if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.')) {
        return false;
    }
    std::size_t len = (ch == '-' || ch == '+') ? 1u : 0u;
    bool seenDot = false;
    bool seenDigit = false;
    while (len < sv.size()) {
        const char c = sv[len];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            ++len;
        } else if (c == '.' && !seenDot) {
            seenDot = true;
            ++len;
        } else {
            break;
        }
    }
    if (!seenDigit || std::from_chars(sv.data(), sv.data() + len, out).ec != std::errc{}) {
        return false;
    }
    sv.remove_prefix(len);
    return true;
}

void AccumSubRow(const SubRowSpan& span, const std::vector<UfoPt>& poly, std::vector<float>& rowCov) {
    std::array<float, 64> hits{};
    std::size_t hitCount = 0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n && hitCount < hits.size(); ++i) {
        const UfoPt a = poly[i];
        const UfoPt b = poly[(i + 1u) % n];
        if ((a.y <= span.subY && b.y > span.subY) || (b.y <= span.subY && a.y > span.subY)) {
            hits[hitCount++] = a.x + (span.subY - a.y) * (b.x - a.x) / (b.y - a.y);
        }
    }
    std::sort(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(hitCount));
    for (std::size_t i = 0; i + 1u < hitCount; i += 2u) {
        const int sx0 = std::max(span.xMin, static_cast<int>(std::floor(hits[i])));
        const int sx1 = std::min(span.xMax, static_cast<int>(std::floor(hits[i + 1u])));
        for (int x = sx0; x <= sx1; ++x) {
            const float cov = std::min(static_cast<float>(x + 1), hits[i + 1u])
                              - std::max(static_cast<float>(x), hits[i]);
            if (cov > 0.0f) {
                rowCov[static_cast<std::size_t>(x - span.xMin)] += cov;
            }
        }
    }
}

void FillPolyScanlines(SvgCanvas& cv, const std::vector<UfoPt>& pxPts, const SvgPaint& paint) {
    float pMinX = 1e9f;
    float pMinY = 1e9f;
    float pMaxX = -1e9f;
    float pMaxY = -1e9f;
    for (const auto& pp : pxPts) {
        pMinX = std::min(pMinX, pp.x);
        pMinY = std::min(pMinY, pp.y);
        pMaxX = std::max(pMaxX, pp.x);
        pMaxY = std::max(pMaxY, pp.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(pMinX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(pMinY)));
    const int x1 = std::min(cv.width - 1, static_cast<int>(std::ceil(pMaxX)));
    const int y1 = std::min(cv.height - 1, static_cast<int>(std::ceil(pMaxY)));
    if (x0 > x1 || y0 > y1) {
        return;
    }
    constexpr int kSubY = 8;
    std::vector<float> rowCov(static_cast<std::size_t>(x1 - x0 + 1));
    for (int y = y0; y <= y1; ++y) {
        std::fill(rowCov.begin(), rowCov.end(), 0.0f);
        for (int sy = 0; sy < kSubY; ++sy) {
            const float subY = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSubY;
            AccumSubRow({subY, x0, x1}, pxPts, rowCov);
        }
        for (int x = x0; x <= x1; ++x) {
            const float cov = std::clamp(rowCov[static_cast<std::size_t>(x - x0)] / kSubY, 0.0f, 1.0f);
            if (cov <= 0.001f) {
                continue;
            }
            const UfoPt pxPt{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            UfoRgba col = paint.useGrad ? EvalGrad(paint.grad, pxPt, cv.scaleX, cv.scaleY) : paint.solid;
            col.a *= paint.opacity;
            BlendStraight(cv.pixels[static_cast<std::size_t>(y * cv.width + x)], col, cov);
        }
    }
}

[[nodiscard]] float MinSegDist2(UfoPt pt, const std::vector<UfoPt>& poly, float capD2) noexcept {
    float bestD2 = capD2;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const UfoPt a = poly[i];
        const UfoPt b = poly[(i + 1u) % poly.size()];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float l2 = dx * dx + dy * dy;
        const float t =
            l2 > 1e-8f ? std::clamp(((pt.x - a.x) * dx + (pt.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
        const float ex = pt.x - (a.x + dx * t);
        const float ey = pt.y - (a.y + dy * t);
        bestD2 = std::min(bestD2, ex * ex + ey * ey);
    }
    return bestD2;
}

void RasterizeStroke(SvgCanvas& cv, const std::vector<UfoPt>& poly, const SvgPaint& paint) {
    if (paint.strokeW <= 0.0f || poly.size() < 2u) {
        return;
    }
    const float halfW = paint.strokeW * 0.5f * cv.scaleX;
    float minX = 1e9f;
    float minY = 1e9f;
    float maxX = -1e9f;
    float maxY = -1e9f;
    for (const auto& p : poly) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minX - halfW - 2.0f)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY - halfW - 2.0f)));
    const int x1 = std::min(cv.width - 1, static_cast<int>(std::ceil(maxX + halfW + 2.0f)));
    const int y1 = std::min(cv.height - 1, static_cast<int>(std::ceil(maxY + halfW + 2.0f)));
    const float pad2 = (halfW + 1.1f) * (halfW + 1.1f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float bestD2 =
                MinSegDist2({static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f}, poly, pad2);
            if (bestD2 >= pad2) {
                continue;
            }
            const float sd = std::sqrt(bestD2) - halfW;
            const float u = std::clamp(0.5f - sd / 1.15f, 0.0f, 1.0f);
            UfoRgba col = paint.strokeCol;
            col.a *= paint.opacity;
            BlendStraight(
                cv.pixels[static_cast<std::size_t>(y * cv.width + x)], col, u * u * (3.0f - 2.0f * u));
        }
    }
}

}  // namespace

SvgMat MulMat(const SvgMat& m1, const SvgMat& m2) noexcept {
    return {m1.a * m2.a + m1.c * m2.b,
            m1.b * m2.a + m1.d * m2.b,
            m1.a * m2.c + m1.c * m2.d,
            m1.b * m2.c + m1.d * m2.d,
            m1.a * m2.tx + m1.c * m2.ty + m1.tx,
            m1.b * m2.tx + m1.d * m2.ty + m1.ty};
}

SvgMat MakeRotateAt(float deg, float cx, float cy) noexcept {
    const float rad = deg * (3.1415926535f / 180.0f);
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    return MulMat({1.0f, 0.0f, 0.0f, 1.0f, cx, cy},
                  MulMat({cs, sn, -sn, cs, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f, -cx, -cy}));
}

std::vector<UfoPt> ParseSvgPath(std::string_view d) {
    std::vector<UfoPt> pts;
    UfoPt cur{};
    UfoPt start{};
    UfoPt prevCp2{};
    char cmd = 0;
    char prevCmd = 0;
    while (!d.empty()) {
        while (!d.empty() && (d.front() == ' ' || d.front() == ',')) {
            d.remove_prefix(1);
        }
        if (d.empty()) {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(d.front()))) {
            cmd = d.front();
            d.remove_prefix(1);
            if (cmd == 'Z' || cmd == 'z') {
                cur = start;
                prevCmd = cmd;
                continue;
            }
        }
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        if (up == 'M' || up == 'L') {
            float x = 0.0f;
            float y = 0.0f;
            if (!ReadSvgFloat(d, x) || !ReadSvgFloat(d, y)) {
                break;
            }
            cur = rel ? UfoPt{cur.x + x, cur.y + y} : UfoPt{x, y};
            if (up == 'M') {
                start = cur;
                cmd = rel ? 'l' : 'L';
            }
            pts.push_back(cur);
        } else if (up == 'V') {
            float y = 0.0f;
            if (!ReadSvgFloat(d, y)) {
                break;
            }
            cur.y = rel ? (cur.y + y) : y;
            pts.push_back(cur);
        } else if (up == 'C' || up == 'S') {
            float v[6]{};
            const int need = (up == 'C') ? 6 : 4;
            bool ok = true;
            for (int i = 0; i < need; ++i) {
                ok = ok && ReadSvgFloat(d, v[i]);
            }
            if (!ok) {
                break;
            }
            const auto pt = [&](float a, float b) {
                return rel ? UfoPt{cur.x + a, cur.y + b} : UfoPt{a, b};
            };
            const bool hadC = (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's');
            const UfoPt cp1 =
                (up == 'C') ? pt(v[0], v[1])
                            : (hadC ? UfoPt{2.0f * cur.x - prevCp2.x, 2.0f * cur.y - prevCp2.y} : cur);
            const UfoPt cp2 = (up == 'C') ? pt(v[2], v[3]) : pt(v[0], v[1]);
            const UfoPt end = (up == 'C') ? pt(v[4], v[5]) : pt(v[2], v[3]);
            FlattenCubic({cur, cp1, cp2, end}, pts, 0);
            prevCp2 = cp2;
            cur = end;
        }
        prevCmd = cmd;
    }
    return pts;
}

std::vector<UfoPt> BuildEllipsePoly(float cx, float cy, float rx, float ry) {
    constexpr int kSegs = 96;
    std::vector<UfoPt> pts(static_cast<std::size_t>(kSegs));
    for (int i = 0; i < kSegs; ++i) {
        const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(kSegs);
        pts[static_cast<std::size_t>(i)] = {cx + std::cos(a) * rx, cy + std::sin(a) * ry};
    }
    return pts;
}

void RenderSvgPoly(SvgCanvas& cv,
                   const std::vector<UfoPt>& localPts,
                   const SvgMat& mat,
                   SvgPaint paint) {
    if (localPts.size() < 3u) {
        return;
    }
    float lMinX = 1e9f;
    float lMinY = 1e9f;
    float lMaxX = -1e9f;
    float lMaxY = -1e9f;
    std::vector<UfoPt> pxPts(localPts.size());
    for (std::size_t i = 0; i < localPts.size(); ++i) {
        const UfoPt lp = localPts[i];
        lMinX = std::min(lMinX, lp.x);
        lMinY = std::min(lMinY, lp.y);
        lMaxX = std::max(lMaxX, lp.x);
        lMaxY = std::max(lMaxY, lp.y);
        const UfoPt sp = ApplyMat(mat, lp);
        pxPts[i] = {sp.x * cv.scaleX, sp.y * cv.scaleY};
    }
    paint.grad.minX = lMinX;
    paint.grad.minY = lMinY;
    paint.grad.w = std::max(1e-4f, lMaxX - lMinX);
    paint.grad.h = std::max(1e-4f, lMaxY - lMinY);
    paint.grad.invMat = InvertMat(mat);

    FillPolyScanlines(cv, pxPts, paint);
    RasterizeStroke(cv, pxPts, paint);
}

}  // namespace motrix::ui
