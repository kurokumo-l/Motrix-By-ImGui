module;

#include "imgui.h"

module motrix.ui;

import std;
import :theme;

namespace motrix::ui {

namespace {

constexpr float kPi = 3.14159265f;

struct IconCanvas {
    ImDrawList* dl;
    float x;
    float y;
    float sx;
    float sy;
    ImU32 col;
    float sw;

    [[nodiscard]] ImVec2 Pt(float vx, float vy) const noexcept {
        return ImVec2(x + vx * sx, y + vy * sy);
    }
};

void DrawRoundLine(const IconCanvas& c, ImVec2 v1, ImVec2 v2) {
    const ImVec2 p1 = c.Pt(v1.x, v1.y);
    const ImVec2 p2 = c.Pt(v2.x, v2.y);
    const float capR = c.sw * 0.5f;
    c.dl->AddLine(p1, p2, c.col, c.sw);
    c.dl->AddCircleFilled(p1, capR, c.col, 10);
    c.dl->AddCircleFilled(p2, capR, c.col, 10);
}

void DrawRoundHorizPill(const IconCanvas& c, float x1, float x2, float y) {
    const float capR = c.sw * 0.5f;
    const ImVec2 p1 = c.Pt(x1, y);
    const ImVec2 p2 = c.Pt(x2, y);
    c.dl->AddRectFilled(ImVec2(p1.x - capR, p1.y - capR), ImVec2(p2.x + capR, p2.y + capR), c.col, capR);
}

void DrawRoundPolyline(const IconCanvas& c, const ImVec2* pts, int count, bool closed = false) {
    c.dl->AddPolyline(pts, count, c.col, closed ? ImDrawFlags_Closed : 0, c.sw);
    const float capR = c.sw * 0.5f;
    for (int i = 0; i < count; ++i) {
        c.dl->AddCircleFilled(pts[i], capR, c.col, 10);
    }
}

struct ArcSpec {
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 1.0f;
    float aMin = 0.0f;
    float aMax = 0.0f;
    int segs = 16;
};

void DrawArcWithCaps(const IconCanvas& c, const ArcSpec& arc) {
    const ImVec2 center = c.Pt(arc.cx, arc.cy);
    const float rPx = arc.radius * c.sx;
    c.dl->PathArcTo(center, rPx, arc.aMin, arc.aMax, arc.segs);
    c.dl->PathStroke(c.col, 0, c.sw);
    const float capR = c.sw * 0.5f;
    c.dl->AddCircleFilled(
        ImVec2(center.x + std::cos(arc.aMin) * rPx, center.y + std::sin(arc.aMin) * rPx),
        capR,
        c.col,
        10);
    c.dl->AddCircleFilled(
        ImVec2(center.x + std::cos(arc.aMax) * rPx, center.y + std::sin(arc.aMax) * rPx),
        capR,
        c.col,
        10);
}

void DrawLogoMini(const IconCanvas& c) {
    // SVG viewBox 0 0 28 14: clean rounded-top M + three flat-bottom pillars
    const float r = std::min(2.2f * c.sx, 2.2f * c.sy);
    c.dl->AddRectFilled(c.Pt(0.0f, 0.0f), c.Pt(28.0f, 3.0f), c.col, r, ImDrawFlags_RoundCornersTop);
    c.dl->AddRectFilled(
        c.Pt(0.0f, 1.8f), c.Pt(4.2f, 14.0f), c.col, 0.6f * r, ImDrawFlags_RoundCornersBottom);
    c.dl->AddRectFilled(
        c.Pt(11.9f, 1.8f), c.Pt(16.1f, 14.0f), c.col, 0.6f * r, ImDrawFlags_RoundCornersBottom);
    c.dl->AddRectFilled(
        c.Pt(23.8f, 1.8f), c.Pt(28.0f, 14.0f), c.col, 0.6f * r, ImDrawFlags_RoundCornersBottom);
}

void DrawLogoFull(const IconCanvas& c) {
    // SVG viewBox 0 0 62 14: M glyph + hyphen + hollow rounded O
    const IconCanvas mPart{c.dl, c.x, c.y, c.sx, c.sy, c.col, c.sw};
    DrawLogoMini(mPart);
    const float r = std::min(2.2f * c.sx, 2.2f * c.sy);
    c.dl->AddRectFilled(c.Pt(31.5f, 5.6f), c.Pt(36.5f, 8.4f), c.col, 1.1f * c.sy);
    c.dl->AddRectFilled(c.Pt(40.0f, 0.0f), c.Pt(62.0f, 3.0f), c.col, r, ImDrawFlags_RoundCornersTop);
    c.dl->AddRectFilled(
        c.Pt(40.0f, 11.0f), c.Pt(62.0f, 14.0f), c.col, r, ImDrawFlags_RoundCornersBottom);
    c.dl->AddRectFilled(c.Pt(40.0f, 2.2f), c.Pt(44.2f, 11.8f), c.col, 0.0f);
    c.dl->AddRectFilled(c.Pt(57.8f, 2.2f), c.Pt(62.0f, 11.8f), c.col, 0.0f);
}

void DrawMenuIcons(MotrixIcon icon, const IconCanvas& c) {
    switch (icon) {
        case MotrixIcon::MenuTask:
            c.dl->AddRectFilled(c.Pt(0.0f, 4.0f), c.Pt(24.0f, 6.0f), c.col, 1.0f * c.sx);
            c.dl->AddRectFilled(c.Pt(0.0f, 11.0f), c.Pt(15.0f, 13.0f), c.col, 1.0f * c.sx);
            c.dl->AddRectFilled(c.Pt(0.0f, 18.0f), c.Pt(24.0f, 20.0f), c.col, 1.0f * c.sx);
            break;
        case MotrixIcon::MenuAdd:
            DrawRoundLine(c, {12.0f, 2.0f}, {12.0f, 22.0f});
            DrawRoundLine(c, {2.0f, 12.0f}, {22.0f, 12.0f});
            break;
        case MotrixIcon::MenuPreference:
            // Exact Motrix menu-preference.js SVG coordinates with round line caps
            DrawRoundHorizPill(c, 1.0f, 4.0f, 4.0f);
            DrawRoundHorizPill(c, 14.0f, 23.0f, 4.0f);
            c.dl->AddCircle(c.Pt(7.0f, 4.0f), 3.0f * c.sx, c.col, 24, c.sw);

            DrawRoundHorizPill(c, 1.0f, 12.0f, 12.0f);
            DrawRoundHorizPill(c, 22.0f, 23.0f, 12.0f);
            c.dl->AddCircle(c.Pt(15.0f, 12.0f), 3.0f * c.sx, c.col, 24, c.sw);

            DrawRoundHorizPill(c, 1.0f, 4.0f, 20.0f);
            DrawRoundHorizPill(c, 14.0f, 23.0f, 20.0f);
            c.dl->AddCircle(c.Pt(7.0f, 20.0f), 3.0f * c.sx, c.col, 24, c.sw);
            break;
        case MotrixIcon::MenuAbout: {
            // Exact Motrix menu-about.js: r=11 circle + circular ? arc centered at (12,10) r=3 + stem +
            // dot
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), 11.0f * c.sx, c.col, 32, c.sw);
            DrawArcWithCaps(c, {12.0f, 10.0f, 3.0f, -2.773f, kPi * 0.5f, 16});
            DrawRoundLine(c, {12.0f, 13.0f}, {12.0f, 15.0f});
            c.dl->AddCircleFilled(c.Pt(12.0f, 18.0f), 1.15f * c.sx, c.col, 12);
            break;
        }
        default: break;
    }
}

void DrawTaskStatusIcons(MotrixIcon icon, const IconCanvas& c) {
    switch (icon) {
        case MotrixIcon::TaskStart:
            c.dl->AddTriangleFilled(c.Pt(4.5f, 2.5f), c.Pt(20.5f, 12.0f), c.Pt(4.5f, 21.5f), c.col);
            break;
        case MotrixIcon::TaskStartLine: {
            const ImVec2 pts[3] = {c.Pt(5.0f, 2.0f), c.Pt(20.0f, 12.0f), c.Pt(5.0f, 22.0f)};
            DrawRoundPolyline(c, pts, 3, true);
            break;
        }
        case MotrixIcon::TaskPause:
            c.dl->AddRectFilled(c.Pt(2.5f, 1.5f), c.Pt(9.5f, 22.5f), c.col, 1.0f * c.sx);
            c.dl->AddRectFilled(c.Pt(14.5f, 1.5f), c.Pt(21.5f, 22.5f), c.col, 1.0f * c.sx);
            break;
        case MotrixIcon::TaskPauseLine:
            c.dl->AddRect(c.Pt(3.0f, 2.0f), c.Pt(9.0f, 22.0f), c.col, 0.5f * c.sx, 0, c.sw);
            c.dl->AddRect(c.Pt(15.0f, 2.0f), c.Pt(21.0f, 22.0f), c.col, 0.5f * c.sx, 0, c.sw);
            break;
        case MotrixIcon::TaskStop:
            c.dl->AddRectFilled(c.Pt(1.5f, 1.5f), c.Pt(22.5f, 22.5f), c.col, 1.5f * c.sx);
            break;
        case MotrixIcon::TaskStopLine:
            c.dl->AddRect(c.Pt(2.0f, 2.0f), c.Pt(22.0f, 22.0f), c.col, 0.5f * c.sx, 0, c.sw);
            break;
        case MotrixIcon::TaskRestart: {
            // SVG task-restart.js: arc CCW from (6.12, 20.12) [2.197 rad] to (3.08, 7.49) [-2.674 rad]
            c.dl->PathArcTo(c.Pt(12.0f, 12.0f), 10.0f * c.sx, -2.674f, 2.197f, 28);
            c.dl->PathStroke(c.col, 0, c.sw);
            const ImVec2 arrow[3] = {c.Pt(2.278f, 1.588f), c.Pt(3.078f, 7.488f), c.Pt(9.078f, 6.688f)};
            DrawRoundPolyline(c, arrow, 3, false);
            c.dl->AddCircle(c.Pt(4.0f, 18.0f), 3.0f * c.sx, c.col, 16, c.sw);
            break;
        }
        default: break;
    }
}

void DrawActionIcons(MotrixIcon icon, const IconCanvas& c) {
    switch (icon) {
        case MotrixIcon::Delete:
            DrawRoundLine(c, {5.0f, 5.0f}, {19.0f, 19.0f});
            DrawRoundLine(c, {19.0f, 5.0f}, {5.0f, 19.0f});
            break;
        case MotrixIcon::Trash: {
            DrawRoundLine(c, {1.0f, 5.0f}, {23.0f, 5.0f});
            const ImVec2 lid[4] = {
                c.Pt(8.0f, 5.0f), c.Pt(8.0f, 1.0f), c.Pt(16.0f, 1.0f), c.Pt(16.0f, 5.0f)};
            DrawRoundPolyline(c, lid, 4, false);
            const ImVec2 body[4] = {
                c.Pt(4.0f, 9.0f), c.Pt(4.0f, 23.0f), c.Pt(20.0f, 23.0f), c.Pt(20.0f, 9.0f)};
            DrawRoundPolyline(c, body, 4, false);
            DrawRoundLine(c, {8.0f, 12.0f}, {8.0f, 18.0f});
            DrawRoundLine(c, {12.0f, 12.0f}, {12.0f, 18.0f});
            DrawRoundLine(c, {16.0f, 12.0f}, {16.0f, 18.0f});
            break;
        }
        case MotrixIcon::Refresh: {
            // SVG refresh.js: M22,12 CW through 6, 9, 12 o'clock to (20.9, 7.5) [2*pi - 0.468 rad]
            DrawArcWithCaps(c, {12.0f, 12.0f, 10.0f, 0.0f, kPi * 2.0f - 0.468f, 28});
            const ImVec2 arrow[3] = {c.Pt(21.8f, 1.7f), c.Pt(21.0f, 7.6f), c.Pt(15.0f, 6.8f)};
            DrawRoundPolyline(c, arrow, 3, false);
            break;
        }
        case MotrixIcon::Purge:
            DrawRoundLine(c, {1.0f, 3.0f}, {23.0f, 3.0f});
            DrawRoundLine(c, {1.0f, 9.0f}, {23.0f, 9.0f});
            DrawRoundLine(c, {1.0f, 15.0f}, {11.0f, 15.0f});
            DrawRoundLine(c, {1.0f, 21.0f}, {11.0f, 21.0f});
            DrawRoundLine(c, {16.0f, 15.0f}, {22.0f, 21.0f});
            DrawRoundLine(c, {22.0f, 15.0f}, {16.0f, 21.0f});
            break;
        case MotrixIcon::Folder: {
            const ImVec2 pts[6] = {c.Pt(1.0f, 1.0f),
                                   c.Pt(10.0f, 1.0f),
                                   c.Pt(12.0f, 4.0f),
                                   c.Pt(23.0f, 4.0f),
                                   c.Pt(23.0f, 23.0f),
                                   c.Pt(1.0f, 23.0f)};
            DrawRoundPolyline(c, pts, 6, true);
            DrawRoundLine(c, {1.0f, 8.0f}, {23.0f, 8.0f});
            break;
        }
        case MotrixIcon::Link: {
            // SVG link.js: diagonal chain at -45 deg with exact circular 180-deg end arcs
            const ImVec2 p1(c.Pt(11.0f, 6.0f));
            c.dl->PathLineTo(p1);
            c.dl->PathArcTo(c.Pt(17.0f, 7.0f), 4.95f * c.sx, -kPi * 0.75f, kPi * 0.25f, 16);
            c.dl->PathLineTo(c.Pt(18.0f, 13.0f));
            c.dl->PathStroke(c.col, 0, c.sw);

            c.dl->PathLineTo(c.Pt(13.0f, 18.0f));
            c.dl->PathArcTo(c.Pt(7.0f, 17.0f), 4.95f * c.sx, kPi * 0.25f, kPi * 1.25f, 16);
            c.dl->PathLineTo(c.Pt(6.0f, 11.0f));
            c.dl->PathStroke(c.col, 0, c.sw);

            DrawRoundLine(c, {9.0f, 15.0f}, {15.0f, 9.0f});
            break;
        }
        case MotrixIcon::InfoCircle:
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), 11.0f * c.sx, c.col, 28, c.sw);
            DrawRoundLine(c, {12.0f, 11.0f}, {12.0f, 17.0f});
            c.dl->AddCircleFilled(c.Pt(12.0f, 7.0f), 1.15f * c.sx, c.col, 12);
            break;
        case MotrixIcon::More: {
            const float r = 2.25f * c.sx;
            const float w = 1.5f * c.sx;
            c.dl->AddCircle(c.Pt(3.0f, 12.0f), r, c.col, 16, w);
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), r, c.col, 16, w);
            c.dl->AddCircle(c.Pt(21.0f, 12.0f), r, c.col, 16, w);
            break;
        }
        default: break;
    }
}

void DrawMetaAndWindowIcons(MotrixIcon icon, const IconCanvas& c, float dpiScale) {
    switch (icon) {
        case MotrixIcon::Speedometer: {
            const float stroke = 1.5f * c.sx;
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), 11.25f * c.sx, c.col, 32, stroke);
            c.dl->PathArcTo(c.Pt(12.0f, 12.0f), 7.64f * c.sx, kPi, kPi + 0.59f, 8);
            c.dl->PathStroke(c.col, 0, stroke);
            c.dl->PathArcTo(c.Pt(12.0f, 12.0f), 7.64f * c.sx, -1.96f, 0.0f, 16);
            c.dl->PathStroke(c.col, 0, stroke);
            c.dl->AddCircle(c.Pt(12.0f, 12.0f), 2.25f * c.sx, c.col, 16, stroke);
            c.dl->AddLine(c.Pt(10.3f, 10.3f), c.Pt(6.3f, 6.3f), c.col, stroke);
            break;
        }
        case MotrixIcon::ArrowUp: {
            DrawRoundLine(c, {12.0f, 21.0f}, {12.0f, 3.0f});
            const ImVec2 head[3] = {c.Pt(5.5f, 9.5f), c.Pt(12.0f, 3.0f), c.Pt(18.5f, 9.5f)};
            DrawRoundPolyline(c, head, 3, false);
            break;
        }
        case MotrixIcon::ArrowDown: {
            DrawRoundLine(c, {12.0f, 3.0f}, {12.0f, 21.0f});
            const ImVec2 head[3] = {c.Pt(5.5f, 14.5f), c.Pt(12.0f, 21.0f), c.Pt(18.5f, 14.5f)};
            DrawRoundPolyline(c, head, 3, false);
            break;
        }
        case MotrixIcon::Node:
            c.dl->AddCircle(c.Pt(12.0f, 5.5f), 3.6f * c.sx, c.col, 16, c.sw);
            c.dl->AddCircle(c.Pt(5.5f, 18.5f), 3.6f * c.sx, c.col, 16, c.sw);
            c.dl->AddCircle(c.Pt(18.5f, 18.5f), 3.6f * c.sx, c.col, 16, c.sw);
            DrawRoundLine(c, {12.0f, 9.1f}, {12.0f, 13.0f});
            DrawRoundLine(c, {12.0f, 13.0f}, {7.8f, 16.0f});
            DrawRoundLine(c, {12.0f, 13.0f}, {16.2f, 16.0f});
            break;
        case MotrixIcon::Magnet:
            c.dl->PathArcTo(c.Pt(9.0f, 15.0f), 5.5f * c.sx, kPi * 0.25f, kPi * 1.25f, 16);
            c.dl->PathStroke(c.col, 0, c.sw);
            DrawRoundLine(c, {5.1f, 11.1f}, {12.5f, 3.7f});
            DrawRoundLine(c, {12.9f, 18.9f}, {20.3f, 11.5f});
            break;
        case MotrixIcon::WinMinimize:
            c.dl->AddLine(c.Pt(1.0f, 6.0f), c.Pt(11.0f, 6.0f), c.col, 1.15f * dpiScale);
            break;
        case MotrixIcon::WinMaximize: {
            const float w = 1.15f * dpiScale;
            const ImVec2 p1[3] = {c.Pt(5.5f, 1.5f), c.Pt(10.5f, 1.5f), c.Pt(10.5f, 6.5f)};
            const ImVec2 p2[3] = {c.Pt(1.5f, 5.5f), c.Pt(1.5f, 10.5f), c.Pt(6.5f, 10.5f)};
            c.dl->AddPolyline(p1, 3, c.col, 0, w);
            c.dl->AddPolyline(p2, 3, c.col, 0, w);
            break;
        }
        case MotrixIcon::WinRestore: {
            const float w = 1.15f * dpiScale;
            c.dl->AddRect(c.Pt(2.0f, 3.5f), c.Pt(8.5f, 10.0f), c.col, 0.0f, 0, w);
            const ImVec2 back[4] = {
                c.Pt(4.0f, 3.5f), c.Pt(4.0f, 1.5f), c.Pt(10.5f, 1.5f), c.Pt(10.5f, 8.0f)};
            c.dl->AddPolyline(back, 4, c.col, 0, w);
            break;
        }
        case MotrixIcon::WinClose: {
            const float w = 1.15f * dpiScale;
            c.dl->AddLine(c.Pt(1.5f, 1.5f), c.Pt(10.5f, 10.5f), c.col, w);
            c.dl->AddLine(c.Pt(10.5f, 1.5f), c.Pt(1.5f, 10.5f), c.col, w);
            break;
        }
        default: break;
    }
}

void DrawMediaAndInboxIcons(MotrixIcon icon, const IconCanvas& c) {
    switch (icon) {
        case MotrixIcon::Inbox: {
            const ImVec2 tray[4] = {
                c.Pt(3.0f, 14.0f), c.Pt(3.0f, 20.0f), c.Pt(21.0f, 20.0f), c.Pt(21.0f, 14.0f)};
            DrawRoundPolyline(c, tray, 4, false);
            DrawRoundLine(c, {3.0f, 14.0f}, {8.5f, 14.0f});
            DrawRoundLine(c, {8.5f, 14.0f}, {10.0f, 16.5f});
            DrawRoundLine(c, {10.0f, 16.5f}, {14.0f, 16.5f});
            DrawRoundLine(c, {14.0f, 16.5f}, {15.5f, 14.0f});
            DrawRoundLine(c, {15.5f, 14.0f}, {21.0f, 14.0f});
            DrawRoundLine(c, {12.0f, 3.0f}, {12.0f, 12.5f});
            const ImVec2 arr[3] = {c.Pt(8.5f, 9.0f), c.Pt(12.0f, 12.5f), c.Pt(15.5f, 9.0f)};
            DrawRoundPolyline(c, arr, 3, false);
            break;
        }
        case MotrixIcon::Video: {
            c.dl->AddRect(c.Pt(2.5f, 5.5f), c.Pt(16.0f, 18.5f), c.col, 2.0f * c.sx, 0, c.sw);
            const ImVec2 tri[4] = {
                c.Pt(16.5f, 10.0f), c.Pt(21.5f, 7.0f), c.Pt(21.5f, 17.0f), c.Pt(16.5f, 14.0f)};
            DrawRoundPolyline(c, tri, 4, true);
            break;
        }
        case MotrixIcon::Audio:
            DrawRoundLine(c, {9.0f, 17.5f}, {9.0f, 5.5f});
            DrawRoundLine(c, {9.0f, 5.5f}, {19.0f, 3.5f});
            DrawRoundLine(c, {19.0f, 3.5f}, {19.0f, 15.5f});
            c.dl->AddCircle(c.Pt(6.5f, 17.5f), 2.5f * c.sx, c.col, 16, c.sw);
            c.dl->AddCircle(c.Pt(16.5f, 15.5f), 2.5f * c.sx, c.col, 16, c.sw);
            break;
        case MotrixIcon::Image: {
            c.dl->AddRect(c.Pt(3.0f, 4.0f), c.Pt(21.0f, 20.0f), c.col, 2.0f * c.sx, 0, c.sw);
            c.dl->AddCircleFilled(c.Pt(8.2f, 9.0f), 1.6f * c.sx, c.col, 12);
            const ImVec2 mtn[5] = {c.Pt(4.0f, 18.5f),
                                   c.Pt(9.5f, 12.5f),
                                   c.Pt(13.5f, 16.0f),
                                   c.Pt(16.5f, 13.0f),
                                   c.Pt(20.0f, 18.5f)};
            DrawRoundPolyline(c, mtn, 5, false);
            break;
        }
        default: break;
    }
}

[[nodiscard]] bool IsExtraIcon(MotrixIcon icon) noexcept {
    return icon == MotrixIcon::PreferenceBasic || icon == MotrixIcon::PreferenceAdvanced
           || icon == MotrixIcon::PreferenceLab || icon == MotrixIcon::TaskHistory
           || icon == MotrixIcon::Sync || icon == MotrixIcon::Dice || icon == MotrixIcon::ChevronDown
           || icon == MotrixIcon::ChevronUp || icon == MotrixIcon::TagClose
           || icon == MotrixIcon::DetailInfo || icon == MotrixIcon::DetailGrid
           || icon == MotrixIcon::DetailCompass || icon == MotrixIcon::DetailUser
           || icon == MotrixIcon::DetailFiles;
}

}  // namespace

void DrawExtraMotrixIcon(ImDrawList* dl, const IconSpec& spec);

void DrawMotrixIcon(const IconSpec& spec) {
    ImDrawList* dl = spec.foreground ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
    if (IsExtraIcon(spec.icon)) {
        DrawExtraMotrixIcon(dl, spec);
        return;
    }
    float vbW = 24.0f;
    float vbH = 24.0f;
    if (spec.icon == MotrixIcon::LogoMini) {
        vbW = 28.0f;
        vbH = 14.0f;
    } else if (spec.icon == MotrixIcon::LogoFull) {
        vbW = 62.0f;
        vbH = 14.0f;
    } else if (spec.icon == MotrixIcon::WinMinimize || spec.icon == MotrixIcon::WinMaximize
               || spec.icon == MotrixIcon::WinRestore || spec.icon == MotrixIcon::WinClose) {
        vbW = 12.0f;
        vbH = 12.0f;
    }

    const float sx = spec.width / vbW;
    const float sy = spec.height / vbH;
    const float strokeW = std::max(1.25f * spec.dpiScale, 2.0f * sx);
    const IconCanvas canvas{dl, spec.x, spec.y, sx, sy, ToAbgrU32(spec.color), strokeW};

    if (spec.icon == MotrixIcon::LogoMini) {
        DrawLogoMini(canvas);
    } else if (spec.icon == MotrixIcon::LogoFull) {
        DrawLogoFull(canvas);
    } else if (spec.icon == MotrixIcon::MenuTask || spec.icon == MotrixIcon::MenuAdd
               || spec.icon == MotrixIcon::MenuPreference || spec.icon == MotrixIcon::MenuAbout) {
        DrawMenuIcons(spec.icon, canvas);
    } else if (spec.icon == MotrixIcon::TaskStart || spec.icon == MotrixIcon::TaskStartLine
               || spec.icon == MotrixIcon::TaskPause || spec.icon == MotrixIcon::TaskPauseLine
               || spec.icon == MotrixIcon::TaskStop || spec.icon == MotrixIcon::TaskStopLine
               || spec.icon == MotrixIcon::TaskRestart) {
        DrawTaskStatusIcons(spec.icon, canvas);
    } else if (spec.icon == MotrixIcon::Delete || spec.icon == MotrixIcon::Trash
               || spec.icon == MotrixIcon::Refresh || spec.icon == MotrixIcon::Purge
               || spec.icon == MotrixIcon::Folder || spec.icon == MotrixIcon::Link
               || spec.icon == MotrixIcon::InfoCircle || spec.icon == MotrixIcon::More) {
        DrawActionIcons(spec.icon, canvas);
    } else if (spec.icon == MotrixIcon::Inbox || spec.icon == MotrixIcon::Video
               || spec.icon == MotrixIcon::Audio || spec.icon == MotrixIcon::Image) {
        DrawMediaAndInboxIcons(spec.icon, canvas);
    } else {
        DrawMetaAndWindowIcons(spec.icon, canvas, spec.dpiScale);
    }
}

}  // namespace motrix::ui
