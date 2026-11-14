module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import :el_widgets;
import :theme;
import :fonts;
import :icons;
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

void ElDrawLinkText(const ElStyle& st, const ElLinkSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float dpiScale = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, spec.fontSize);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    if (ImGui::InvisibleButton(std::string(spec.label).c_str(),
                               ImVec2(sz.x + 2.0f * dpiScale, sz.y + 2.0f * dpiScale))) {
        if (spec.onClick) {
            spec.onClick();
        }
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const RgbaColor col = (hov || spec.accentColor) ? p.primary : p.taskMetaText;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(pos, ToAbgrU32(col), spec.label.data(), spec.label.data() + spec.label.size());
    if (hov) {
        dl->AddLine(
            ImVec2(pos.x, pos.y + sz.y), ImVec2(pos.x + sz.x, pos.y + sz.y), ToAbgrU32(col), 1.0f);
    }
}

bool ElCheckbox(const ElStyle& st, const char* id, std::string_view label, bool* value) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 13.5f);
    const float boxSz = 14.0f * s;
    const float rowH = 20.0f * s;
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

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bMin(pos.x, pos.y + (rowH - boxSz) * 0.5f);
    const ImVec2 bMax(bMin.x + boxSz, bMin.y + boxSz);
    if (*value) {
        dl->AddRectFilled(bMin, bMax, ToAbgrU32(p.primary), 2.5f * s);
        DrawCheckmarkGlyph(dl, {bMin, boxSz, ToAbgrU32(HexColor(0xFFFFFF)), s});
    } else {
        dl->AddRectFilled(bMin, bMax, ToAbgrU32(p.inputBg), 2.5f * s);
        dl->AddRect(bMin, bMax, ToAbgrU32(hovered ? p.primary : p.inputBorder), 2.5f * s, 0, 1.0f);
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
    FontGuard fnt(st.fonts, FontRole::Regular, 13.5f);
    const float trackW = 40.0f * s;
    const float trackH = 20.0f * s;
    const float rowH = 24.0f * s;
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

    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID animKey = ImGui::GetID(std::format("{}_anim", id).c_str());
    float anim = store->GetFloat(animKey, *value ? 1.0f : 0.0f);
    const float target = *value ? 1.0f : 0.0f;
    anim += (target - anim) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    store->SetFloat(animKey, anim);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 tMin(pos.x, pos.y + (rowH - trackH) * 0.5f);
    const ImVec2 tMax(tMin.x + trackW, tMin.y + trackH);
    const RgbaColor offC = p.switchOffBg;
    const RgbaColor onC = p.primary;
    const RgbaColor trackCol{offC.r + (onC.r - offC.r) * anim,
                             offC.g + (onC.g - offC.g) * anim,
                             offC.b + (onC.b - offC.b) * anim,
                             1.0f};
    dl->AddRectFilled(tMin, tMax, ToAbgrU32(trackCol), trackH * 0.5f);
    const float knobR = (trackH - 4.0f * s) * 0.5f;
    const float knobX = tMin.x + 2.0f * s + knobR + anim * (trackW - 4.0f * s - knobR * 2.0f);
    dl->AddCircleFilled(ImVec2(knobX, tMin.y + trackH * 0.5f), knobR, ToAbgrU32(HexColor(0xFFFFFF)), 20);
    if (!label.empty()) {
        dl->AddText(ImVec2(tMax.x + gap, pos.y + (rowH - txtSz.y) * 0.5f),
                    ToAbgrU32(*value ? p.primary : p.subLabelText),
                    label.data(),
                    label.data() + label.size());
    }
    return clicked;
}

bool ElInputText(const ElStyle& st, const ElTextInputSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 13.5f);
    const float h = (spec.height > 0.0f) ? spec.height : (30.0f * s);
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
        rMin, rMax, ToAbgrU32(ResolveElBorderColor(p, focused, hovered)), 4.0f * s, 0, 1.0f);
    return changed;
}

bool ElInputTextMultiline(const ElStyle& st, const ElTextInputSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 13.5f);
    const float h = (spec.height > 0.0f) ? spec.height : (82.0f * s);
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
    dl->AddRect(rMin, rMax, ToAbgrU32(ResolveElBorderColor(p, focused, hovered)), 4.0f * s, 0, 1.0f);
    return changed;
}

bool ElSecondaryButton(const ElStyle& st, const ElButtonSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 13.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(spec.width, spec.height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor bgCol = hovered ? WithAlpha(p.primary, p.isDark ? 0.14f : 0.08f) : p.inputBg;
    const RgbaColor bdCol = hovered ? p.primary : p.inputBorder;
    const RgbaColor fgCol = hovered ? p.primary : p.subLabelText;
    dl->AddRectFilled(pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bgCol), 4.0f * s);
    dl->AddRect(
        pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bdCol), 4.0f * s, 0, 1.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    dl->AddText(ImVec2(pos.x + (spec.width - tSz.x) * 0.5f, pos.y + (spec.height - tSz.y) * 0.5f),
                ToAbgrU32(fgCol),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return clicked;
}

bool ElPrimaryButton(const ElStyle& st, const ElButtonSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Medium, 13.5f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(spec.width, spec.height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor bgCol = hovered ? HexColor(0x6E6EFF) : p.primary;
    dl->AddRectFilled(pos, ImVec2(pos.x + spec.width, pos.y + spec.height), ToAbgrU32(bgCol), 4.0f * s);
    const ImVec2 tSz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    dl->AddText(ImVec2(pos.x + (spec.width - tSz.x) * 0.5f, pos.y + (spec.height - tSz.y) * 0.5f),
                ToAbgrU32(HexColor(0xFFFFFF)),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return clicked;
}

}  // namespace motrix::ui
