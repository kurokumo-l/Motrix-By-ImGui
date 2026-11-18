module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import :el_widgets;
import :theme;
import :fonts;
import :icons;
import :anim;
import :scope_guard;

namespace motrix::ui {

namespace {

struct CheckGlyphSpec {
    ImVec2 minPt{};
    float size = 0.0f;
    ImU32 col = 0;
    float s = 1.0f;
};

void DrawCheckmarkGlyph(ImDrawList* dl, const CheckGlyphSpec& g) {
    const ImVec2 p0(g.minPt.x + g.size * 0.22f, g.minPt.y + g.size * 0.52f);
    const ImVec2 p1(g.minPt.x + g.size * 0.43f, g.minPt.y + g.size * 0.74f);
    const ImVec2 p2(g.minPt.x + g.size * 0.79f, g.minPt.y + g.size * 0.29f);
    const float w = std::max(1.4f * g.s, 1.5f);
    dl->AddLine(p0, p1, g.col, w);
    dl->AddLine(p1, p2, g.col, w);
}

}  // namespace

RgbaColor ResolveElBorderColor(const MotrixPalette& p, bool focused, bool hovered) noexcept {
    if (focused) {
        return p.primary;
    }
    if (hovered) {
        return p.inputBorderHover;
    }
    return p.inputBorder;
}

RgbaColor AnimateElBorderColor(const ElStyle& st, const char* id, bool focused, bool hovered) {
    const MotrixPalette& p = st.palette;
    const std::uint32_t baseId = (id != nullptr) ? ImGui::GetID(id) : 0u;
    const float focT =
        AnimStep(st.anim, {baseId ^ 0x4244u, focused ? 1.0f : 0.0f, 24.0f, focused ? 1.0f : 0.0f});
    const float hovT = AnimStep(st.anim, {baseId ^ 0x4248u, hovered ? 1.0f : 0.0f, 20.0f, 0.0f});
    return LerpColor(LerpColor(p.inputBorder, p.inputBorderHover, hovT), p.primary, focT);
}

void ElDrawLinkText(const ElStyle& st, const ElLinkSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float dpiScale = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, spec.fontSize);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    const std::string btnId(spec.label);
    if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(sz.x + 2.0f * dpiScale, sz.y + 2.0f * dpiScale))) {
        if (spec.onClick) {
            spec.onClick();
        }
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float hovT =
        AnimStep(st.anim, {ImGui::GetID(btnId.c_str()) ^ 0x4C4Bu, hov ? 1.0f : 0.0f, 22.0f, 0.0f});
    const RgbaColor col = spec.accentColor ? p.primary : LerpColor(p.taskMetaText, p.primary, hovT);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(pos, ToAbgrU32(col), spec.label.data(), spec.label.data() + spec.label.size());
    if (hovT > 0.02f) {
        dl->AddLine(ImVec2(pos.x, pos.y + sz.y),
                    ImVec2(pos.x + sz.x, pos.y + sz.y),
                    ToAbgrU32(WithAlpha(col, col.a * hovT)),
                    1.0f);
    }
}

bool ElCheckbox(const ElStyle& st, const char* id, std::string_view label, bool* value) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float boxSz = 15.5f * s;
    const float rowH = 24.0f * s;
    const ImVec2 txtSz = label.empty() ? ImVec2(0.0f, 0.0f)
                                       : ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const float gap = label.empty() ? 0.0f : 8.0f * s;
    const float totalW = boxSz + gap + txtSz.x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(totalW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    if (clicked) {
        *value = !(*value);
    }
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const std::uint32_t baseId = ImGui::GetID(id);
    const float chkT =
        AnimStep(st.anim, {baseId ^ 0x434Bu, *value ? 1.0f : 0.0f, 22.0f, *value ? 1.0f : 0.0f});
    const float hovT = AnimStep(st.anim, {baseId ^ 0x4348u, hovered ? 1.0f : 0.0f, 22.0f, 0.0f});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bMin(pos.x, pos.y + (rowH - boxSz) * 0.5f);
    const ImVec2 bMax(bMin.x + boxSz, bMin.y + boxSz);
    const RgbaColor fillCol = LerpColor(p.inputBg, p.primary, chkT);
    const RgbaColor bdCol = LerpColor(LerpColor(p.inputBorder, p.primary, hovT), p.primary, chkT);
    dl->AddRectFilled(bMin, bMax, ToAbgrU32(fillCol), 2.5f * s);
    if (chkT < 0.99f) {
        dl->AddRect(bMin, bMax, ToAbgrU32(bdCol), 2.5f * s, 0, 1.0f);
    }
    if (chkT > 0.05f) {
        DrawCheckmarkGlyph(dl, {bMin, boxSz, ToAbgrU32(WithAlpha(HexColor(0xFFFFFF), chkT)), s});
    }
    if (!label.empty()) {
        dl->AddText(ImVec2(bMax.x + gap, pos.y + (rowH - txtSz.y) * 0.5f),
                    ToAbgrU32(p.subLabelText),
                    label.data(),
                    label.data() + label.size());
    }
    return clicked;
}

bool ElSwitch(const ElStyle& st, const char* id, std::string_view label, bool* value) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float trackW = 42.0f * s;
    const float trackH = 22.0f * s;
    const float rowH = 26.0f * s;
    const ImVec2 txtSz = label.empty() ? ImVec2(0.0f, 0.0f)
                                       : ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const float gap = label.empty() ? 0.0f : 10.0f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(trackW + gap + txtSz.x, rowH));
    if (clicked) {
        *value = !(*value);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const float target = *value ? 1.0f : 0.0f;
    const float anim = AnimStep(st.anim, {ImGui::GetID(id) ^ 0x5717u, target, 18.0f, target});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 tMin(pos.x, pos.y + (rowH - trackH) * 0.5f);
    const ImVec2 tMax(tMin.x + trackW, tMin.y + trackH);
    const RgbaColor trackCol = LerpColor(p.switchOffBg, p.primary, anim);
    dl->AddRectFilled(tMin, tMax, ToAbgrU32(trackCol), trackH * 0.5f);
    const float knobR = (trackH - 4.0f * s) * 0.5f;
    const float knobX = tMin.x + 2.0f * s + knobR + anim * (trackW - 4.0f * s - knobR * 2.0f);
    dl->AddCircleFilled(ImVec2(knobX, tMin.y + trackH * 0.5f), knobR, ToAbgrU32(HexColor(0xFFFFFF)), 20);
    if (!label.empty()) {
        dl->AddText(ImVec2(tMax.x + gap, pos.y + (rowH - txtSz.y) * 0.5f),
                    ToAbgrU32(LerpColor(p.subLabelText, p.primary, anim)),
                    label.data(),
                    label.data() + label.size());
    }
    return clicked;
}

bool ElInputText(const ElStyle& st, const ElTextInputSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float h = (spec.height > 0.0f) ? spec.height : (32.0f * s);
    const bool readOnly = (spec.flags & ImGuiInputTextFlags_ReadOnly) != 0;
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + spec.width, rMin.y + h);
    ImGui::GetWindowDrawList()->AddRectFilled(
        rMin, rMax, ToAbgrU32(readOnly ? p.inputAddonBg : p.inputBg), 4.0f * s);

    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const float padY = std::max(3.0f * s, (h - ImGui::GetFontSize()) * 0.5f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, 10.0f * s, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
    StyleColorGuard txtCol(ImGuiCol_Text, readOnly ? p.taskMetaText : p.panelTitle);
    StyleColorGuard hintCol(ImGuiCol_TextDisabled, p.inputPlaceholder);

    ImGui::SetNextItemWidth(spec.width);
    const std::string hint(spec.placeholder);
    const bool changed = ImGui::InputTextWithHint(spec.id, hint.c_str(), spec.buf, spec.cap, spec.flags);
    const bool focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::GetWindowDrawList()->AddRect(
        rMin, rMax, ToAbgrU32(AnimateElBorderColor(st, spec.id, focused, hovered)), 4.0f * s, 0, 1.0f);
    return changed;
}

bool ElInputTextMultiline(const ElStyle& st, const ElTextInputSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float h = (spec.height > 0.0f) ? spec.height : (88.0f * s);
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + spec.width, rMin.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);

    const float padX = 12.0f * s;
    const float padY = 8.0f * s;
    if (spec.buf != nullptr && spec.buf[0] == '\0' && !spec.placeholder.empty()) {
        const float wrapW = std::max(40.0f * s, spec.width - padX * 2.0f);
        dl->AddText(ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    ImVec2(rMin.x + padX, rMin.y + padY),
                    ToAbgrU32(p.inputPlaceholder),
                    spec.placeholder.data(),
                    spec.placeholder.data() + spec.placeholder.size(),
                    wrapW);
    }

    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    StyleVarGuard noChildBorder(ImGuiStyleVar_ChildBorderSize, 0.0f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, padX, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
    StyleColorGuard txtCol(ImGuiCol_Text, p.panelTitle);

    const int mlFlags =
        spec.flags | ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_NoHorizontalScroll;
    const bool changed =
        ImGui::InputTextMultiline(spec.id, spec.buf, spec.cap, ImVec2(spec.width, h), mlFlags);
    const bool focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    dl->AddRect(
        rMin, rMax, ToAbgrU32(AnimateElBorderColor(st, spec.id, focused, hovered)), 4.0f * s, 0, 1.0f);
    return changed;
}

bool ElSecondaryButton(const ElStyle& st, const ElButtonSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool rawClick = ImGui::InvisibleButton(spec.id, ImVec2(spec.width, spec.height));
    const bool hovered = !spec.disabled && ImGui::IsItemHovered();
    const bool pressed = !spec.disabled && ImGui::IsItemActive();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float hovT =
        spec.disabled
            ? 0.0f
            : AnimStep(st.anim, {ImGui::GetID(spec.id) ^ 0x5342u, hovered ? 1.0f : 0.0f, 22.0f, 0.0f});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RgbaColor bgCol = spec.disabled
                          ? p.inputAddonBg
                          : LerpColor(p.inputBg, WithAlpha(p.primary, p.isDark ? 0.14f : 0.08f), hovT);
    const RgbaColor bdCol =
        spec.disabled ? p.inputBorder : LerpColor(p.inputBorder, p.primary, pressed ? 1.0f : hovT);
    const RgbaColor fgCol =
        spec.disabled ? p.taskMetaText : LerpColor(p.subLabelText, p.primary, pressed ? 1.0f : hovT);
    if (pressed) {
        bgCol = LerpColor(bgCol, WithAlpha(p.primary, p.isDark ? 0.28f : 0.18f), 0.7f);
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bgCol), 4.0f * s);
    dl->AddRect(
        pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bdCol), 4.0f * s, 0, 1.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    dl->AddText(ImVec2(pos.x + (spec.width - tSz.x) * 0.5f, pos.y + (spec.height - tSz.y) * 0.5f),
                ToAbgrU32(fgCol),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return !spec.disabled && rawClick;
}

bool ElPrimaryButton(const ElStyle& st, const ElButtonSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Medium, 14.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool rawClick = ImGui::InvisibleButton(spec.id, ImVec2(spec.width, spec.height));
    const bool hovered = !spec.disabled && ImGui::IsItemHovered();
    const bool pressed = !spec.disabled && ImGui::IsItemActive();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float hovT =
        spec.disabled
            ? 0.0f
            : AnimStep(st.anim, {ImGui::GetID(spec.id) ^ 0x5042u, hovered ? 1.0f : 0.0f, 22.0f, 0.0f});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RgbaColor bgCol = spec.disabled ? WithAlpha(p.primary, 0.45f)
                                    : LerpColor(p.primary, HexColor(0xFFFFFF), 0.14f * hovT);
    if (pressed) {
        bgCol = LerpColor(p.primary, HexColor(0x000000), 0.16f);
    }
    const RgbaColor fgCol = spec.disabled ? WithAlpha(HexColor(0xFFFFFF), 0.72f) : HexColor(0xFFFFFF);
    dl->AddRectFilled(pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bgCol), 4.0f * s);
    const ImVec2 tSz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    dl->AddText(ImVec2(pos.x + (spec.width - tSz.x) * 0.5f, pos.y + (spec.height - tSz.y) * 0.5f),
                ToAbgrU32(fgCol),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return !spec.disabled && rawClick;
}

namespace {

struct TooltipLayoutArgs {
    ImVec2 anchorMin{};
    ImVec2 anchorMax{};
    ImVec2 boxSize{};
    ElTooltipPlacement pref = ElTooltipPlacement::Top;
    float s = 1.0f;
};

struct TooltipRectSpec {
    ImVec2 minPt{};
    ImVec2 maxPt{};
    ElTooltipPlacement actual = ElTooltipPlacement::Top;
    bool adjacent = false;
};

struct TooltipCursorArgs {
    ImVec2& pos;
    ImVec2 size{};
    ImVec2 vpMin{};
    ImVec2 vpMax{};
    ElTooltipPlacement actual = ElTooltipPlacement::Top;
    float s = 1.0f;
};

[[nodiscard]] bool PushTooltipClearOfCursor(TooltipCursorArgs& a) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid(&io.MousePos)) {
        return false;
    }
    const ImVec2 cMin(io.MousePos.x - 2.0f * a.s, io.MousePos.y - 2.0f * a.s);
    const ImVec2 cMax(io.MousePos.x + 14.0f * a.s, io.MousePos.y + 20.0f * a.s);
    if (a.pos.x >= cMax.x || (a.pos.x + a.size.x) <= cMin.x || a.pos.y >= cMax.y
        || (a.pos.y + a.size.y) <= cMin.y) {
        return false;
    }
    switch (a.actual) {
        case ElTooltipPlacement::Bottom:
            if (cMax.y + 2.0f * a.s + a.size.y <= a.vpMax.y) {
                a.pos.y = cMax.y + 2.0f * a.s;
            }
            break;
        case ElTooltipPlacement::Top:
            if (cMin.y - 4.0f * a.s - a.size.y >= a.vpMin.y) {
                a.pos.y = cMin.y - 4.0f * a.s - a.size.y;
            }
            break;
        case ElTooltipPlacement::Right:
            if (cMax.x + 4.0f * a.s + a.size.x <= a.vpMax.x) {
                a.pos.x = cMax.x + 4.0f * a.s;
            }
            break;
        case ElTooltipPlacement::Left:
            if (cMin.x - 4.0f * a.s - a.size.x >= a.vpMin.x) {
                a.pos.x = cMin.x - 4.0f * a.s - a.size.x;
            }
            break;
    }
    return true;
}

[[nodiscard]] TooltipRectSpec ComputeTooltipRect(const TooltipLayoutArgs& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float margin = 8.0f * a.s;
    const float gap = 7.0f * a.s;
    const ImVec2 vpMin(vp->Pos.x + margin, vp->Pos.y + margin);
    const ImVec2 vpMax(vp->Pos.x + vp->Size.x - margin, vp->Pos.y + vp->Size.y - margin);
    const float w = a.boxSize.x;
    const float h = a.boxSize.y;
    const float acx = (a.anchorMin.x + a.anchorMax.x) * 0.5f;
    const float acy = (a.anchorMin.y + a.anchorMax.y) * 0.5f;

    ElTooltipPlacement actual = a.pref;
    if (actual == ElTooltipPlacement::Bottom && (a.anchorMax.y + gap + h > vpMax.y)) {
        actual = ElTooltipPlacement::Top;
    } else if (actual == ElTooltipPlacement::Top && (a.anchorMin.y - gap - h < vpMin.y)) {
        actual = ElTooltipPlacement::Bottom;
    } else if (actual == ElTooltipPlacement::Right && (a.anchorMax.x + gap + w > vpMax.x)) {
        actual = ElTooltipPlacement::Left;
    } else if (actual == ElTooltipPlacement::Left && (a.anchorMin.x - gap - w < vpMin.x)) {
        actual = ElTooltipPlacement::Right;
    }

    ImVec2 pos(acx - w * 0.5f, a.anchorMin.y - gap - h);
    if (actual == ElTooltipPlacement::Bottom) {
        pos.y = a.anchorMax.y + gap;
    } else if (actual == ElTooltipPlacement::Right) {
        pos = ImVec2(a.anchorMax.x + gap, acy - h * 0.5f);
    } else if (actual == ElTooltipPlacement::Left) {
        pos = ImVec2(a.anchorMin.x - gap - w, acy - h * 0.5f);
    }

    pos.x = std::clamp(pos.x, vpMin.x, std::max(vpMin.x, vpMax.x - w));
    pos.y = std::clamp(pos.y, vpMin.y, std::max(vpMin.y, vpMax.y - h));
    TooltipCursorArgs curArgs{pos, a.boxSize, vpMin, vpMax, actual, a.s};
    const bool pushed = PushTooltipClearOfCursor(curArgs);
    return {pos, ImVec2(pos.x + w, pos.y + h), actual, !pushed};
}

struct TooltipArrowArgs {
    TooltipRectSpec rect{};
    ImVec2 anchorCenter{};
    RgbaColor fill{};
    RgbaColor border{};
    bool drawBorder = false;
    float s = 1.0f;
};

void DrawTooltipArrow(ImDrawList* dl, const TooltipArrowArgs& a) {
    if (!a.rect.adjacent) {
        return;
    }
    const float aw = 4.5f * a.s;
    const ImU32 fillU32 = ToAbgrU32(a.fill);
    const ImU32 bdrU32 = ToAbgrU32(a.border);
    const float cx =
        std::clamp(a.anchorCenter.x, a.rect.minPt.x + 10.0f * a.s, a.rect.maxPt.x - 10.0f * a.s);
    const float cy =
        std::clamp(a.anchorCenter.y, a.rect.minPt.y + 10.0f * a.s, a.rect.maxPt.y - 10.0f * a.s);

    ImVec2 p0{};
    ImVec2 p1{};
    ImVec2 b0{};
    ImVec2 b1{};
    ImVec2 tip{};
    switch (a.rect.actual) {
        case ElTooltipPlacement::Top:
            p0 = ImVec2(cx - aw, a.rect.maxPt.y - 1.2f);
            p1 = ImVec2(cx + aw, a.rect.maxPt.y - 1.2f);
            b0 = ImVec2(cx - aw, a.rect.maxPt.y);
            b1 = ImVec2(cx + aw, a.rect.maxPt.y);
            tip = ImVec2(cx, a.rect.maxPt.y + aw);
            break;
        case ElTooltipPlacement::Bottom:
            p0 = ImVec2(cx - aw, a.rect.minPt.y + 1.2f);
            p1 = ImVec2(cx + aw, a.rect.minPt.y + 1.2f);
            b0 = ImVec2(cx - aw, a.rect.minPt.y);
            b1 = ImVec2(cx + aw, a.rect.minPt.y);
            tip = ImVec2(cx, a.rect.minPt.y - aw);
            break;
        case ElTooltipPlacement::Right:
            p0 = ImVec2(a.rect.minPt.x + 1.2f, cy - aw);
            p1 = ImVec2(a.rect.minPt.x + 1.2f, cy + aw);
            b0 = ImVec2(a.rect.minPt.x, cy - aw);
            b1 = ImVec2(a.rect.minPt.x, cy + aw);
            tip = ImVec2(a.rect.minPt.x - aw, cy);
            break;
        case ElTooltipPlacement::Left:
            p0 = ImVec2(a.rect.maxPt.x - 1.2f, cy - aw);
            p1 = ImVec2(a.rect.maxPt.x - 1.2f, cy + aw);
            b0 = ImVec2(a.rect.maxPt.x, cy - aw);
            b1 = ImVec2(a.rect.maxPt.x, cy + aw);
            tip = ImVec2(a.rect.maxPt.x + aw, cy);
            break;
    }
    dl->AddTriangleFilled(p0, p1, tip, fillU32);
    if (a.drawBorder) {
        dl->AddLine(b0, tip, bdrU32, 1.0f);
        dl->AddLine(b1, tip, bdrU32, 1.0f);
    }
}

struct TooltipPaintSpec {
    TooltipRectSpec rect{};
    ImVec2 anchorCenter{};
    std::string text;
    const FontRegistry* fonts = nullptr;
    bool dark = false;
    float s = 1.0f;
    float padX = 10.0f;
    float padY = 5.0f;
    float wrapW = 320.0f;
};

void PaintTooltipOverlay(const TooltipPaintSpec& p) {
    const RgbaColor fillCol = p.dark ? HexColor(0x3D3D3D) : HexColor(0xFFFFFF);
    const RgbaColor borderCol = p.dark ? HexColor(0x606060) : HexColor(0xE4E7ED);
    const RgbaColor textCol = p.dark ? HexColor(0xEEEEEE) : HexColor(0x303133);

    FontGuard tipFont(*p.fonts, FontRole::Regular, 12.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const TooltipRectSpec& r = p.rect;
    dl->AddRectFilled(ImVec2(r.minPt.x - 1.5f * p.s, r.minPt.y + 1.0f * p.s),
                      ImVec2(r.maxPt.x + 1.5f * p.s, r.maxPt.y + 3.5f * p.s),
                      ToAbgrU32(RgbaColor{0.0f, 0.0f, 0.0f, p.dark ? 0.16f : 0.06f}),
                      5.5f * p.s);
    dl->AddRectFilled(ImVec2(r.minPt.x, r.minPt.y + 1.0f * p.s),
                      ImVec2(r.maxPt.x, r.maxPt.y + 2.0f * p.s),
                      ToAbgrU32(RgbaColor{0.0f, 0.0f, 0.0f, p.dark ? 0.22f : 0.08f}),
                      4.5f * p.s);
    dl->AddRectFilled(r.minPt, r.maxPt, ToAbgrU32(fillCol), 4.0f * p.s);
    dl->AddRect(r.minPt, r.maxPt, ToAbgrU32(borderCol), 4.0f * p.s, 0, 1.0f);
    DrawTooltipArrow(dl, {r, p.anchorCenter, fillCol, borderCol, true, p.s});
    dl->AddText(ImGui::GetFont(),
                ImGui::GetFontSize(),
                ImVec2(r.minPt.x + p.padX, r.minPt.y + p.padY),
                ToAbgrU32(textCol),
                p.text.data(),
                p.text.data() + p.text.size(),
                p.wrapW);
}

}  // namespace

void ElDrawTooltip(const ElStyle& st, const ElTooltipSpec& spec) {
    if (spec.text.empty() || (st.anim != nullptr && !st.anim->TooltipsEnabled())) {
        return;
    }
    const ImVec2 anchorMin = ImGui::GetItemRectMin();
    const ImVec2 anchorMax = ImGui::GetItemRectMax();
    const float s = st.dpiScale;

    float wrapW = 0.0f;
    ImVec2 txtSz{};
    {
        FontGuard tipFont(st.fonts, FontRole::Regular, 12.0f);
        wrapW = std::min(320.0f * s, ImGui::GetMainViewport()->Size.x * 0.45f);
        txtSz = ImGui::CalcTextSize(spec.text.data(), spec.text.data() + spec.text.size(), false, wrapW);
    }
    const float padX = 10.0f * s;
    const float padY = 5.0f * s;
    const ImVec2 boxSize(txtSz.x + padX * 2.0f, txtSz.y + padY * 2.0f);
    const TooltipRectSpec r = ComputeTooltipRect({anchorMin, anchorMax, boxSize, spec.placement, s});
    const ImVec2 anchorCenter((anchorMin.x + anchorMax.x) * 0.5f, (anchorMin.y + anchorMax.y) * 0.5f);

    TooltipPaintSpec paint{
        r, anchorCenter, std::string(spec.text), &st.fonts, st.palette.isDark, s, padX, padY, wrapW};
    if (st.anim != nullptr) {
        st.anim->SetDeferredOverlay([p = std::move(paint)]() { PaintTooltipOverlay(p); });
    } else {
        PaintTooltipOverlay(paint);
    }
}

}  // namespace motrix::ui
