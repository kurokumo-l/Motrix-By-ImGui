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

bool DrawAddonSlot(const char* btnId,
                   ImVec2 minPt,
                   ImVec2 maxPt,
                   MotrixIcon icon,
                   bool isLeft,
                   const MotrixPalette& p,
                   float s) {
    ImGui::SetCursorScreenPos(minPt);
    const bool clicked = ImGui::InvisibleButton(btnId, ImVec2(maxPt.x - minPt.x, maxPt.y - minPt.y));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImDrawFlags corners = isLeft ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;
    dl->AddRectFilled(ImVec2(minPt.x + 1.0f, minPt.y + 1.0f),
                      ImVec2(maxPt.x - 1.0f, maxPt.y - 1.0f),
                      ToAbgrU32(p.inputAddonBg),
                      3.5f * s,
                      corners);
    const float divX = isLeft ? maxPt.x : minPt.x;
    dl->AddLine(
        ImVec2(divX, minPt.y + 1.0f), ImVec2(divX, maxPt.y - 1.0f), ToAbgrU32(p.inputBorder), 1.0f);
    const float icoSz = 13.0f * s;
    const RgbaColor icoCol = hovered ? p.primary : p.taskMetaText;
    DrawMotrixIcon(icon,
                   minPt.x + ((maxPt.x - minPt.x) - icoSz) * 0.5f,
                   minPt.y + ((maxPt.y - minPt.y) - icoSz) * 0.5f,
                   icoSz,
                   icoSz,
                   icoCol,
                   s);
    return clicked;
}

template <typename DrawFieldFn>
bool RenderNumericBoxImpl(const char* id,
                          const std::string& displayText,
                          float width,
                          const MotrixPalette& p,
                          const FontRegistry& fonts,
                          float s,
                          int& stepDelta,
                          DrawFieldFn&& drawField) {
    stepDelta = 0;
    const float h = 30.0f * s;
    const float capW = 28.0f * s;
    const float numW = std::max(36.0f * s, width - capW);
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + width, rMin.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);
    dl->AddRectFilled(ImVec2(rMax.x - capW, rMin.y + 1.0f),
                      ImVec2(rMax.x - 1.0f, rMax.y - 1.0f),
                      ToAbgrU32(p.inputAddonBg),
                      3.5f * s,
                      ImDrawFlags_RoundCornersRight);
    dl->AddLine(ImVec2(rMax.x - capW, rMin.y + 1.0f),
                ImVec2(rMax.x - capW, rMax.y - 1.0f),
                ToAbgrU32(p.inputBorder),
                1.0f);
    dl->AddLine(ImVec2(rMax.x - capW, rMin.y + h * 0.5f),
                ImVec2(rMax.x - 1.0f, rMin.y + h * 0.5f),
                ToAbgrU32(p.inputBorder),
                1.0f);

    FontGuard fnt(fonts, FontRole::Regular, 13.5f);
    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const float padY = std::max(3.0f * s, (h - ImGui::GetFontSize()) * 0.5f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, 8.0f * s, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});

    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID editKey = ImGui::GetID(std::format("{}_edit", id).c_str());
    const ImGuiID focusKey = ImGui::GetID(std::format("{}_focus", id).c_str());
    bool isEditing = store->GetBool(editKey, false);
    bool valueChanged = false;
    bool inputFocused = false;
    bool inputHovered = false;

    if (isEditing) {
        if (store->GetBool(focusKey, false)) {
            store->SetBool(focusKey, false);
            ImGui::SetKeyboardFocusHere(0);
        }
        StyleColorGuard txtCol(ImGuiCol_Text, p.panelTitle);
        ImGui::SetCursorScreenPos(rMin);
        ImGui::SetNextItemWidth(numW);
        valueChanged = drawField();
        inputFocused = ImGui::IsItemActive() || ImGui::IsItemFocused();
        inputHovered = ImGui::IsItemHovered();
        if (!inputFocused && !inputHovered && ImGui::IsMouseClicked(0)) {
            store->SetBool(editKey, false);
        }
    } else {
        ImGui::SetCursorScreenPos(rMin);
        if (ImGui::InvisibleButton(std::format("{}_hit", id).c_str(), ImVec2(numW, h))) {
            store->SetBool(editKey, true);
            store->SetBool(focusKey, true);
        }
        inputHovered = ImGui::IsItemHovered();
        const ImVec2 txtSz = ImGui::CalcTextSize(displayText.c_str());
        dl->AddText(ImVec2(rMin.x + (numW - txtSz.x) * 0.5f, rMin.y + (h - txtSz.y) * 0.5f),
                    ToAbgrU32(p.panelTitle),
                    displayText.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(rMax.x - capW, rMin.y));
    if (ImGui::InvisibleButton(std::format("{}_up", id).c_str(), ImVec2(capW, h * 0.5f))) {
        stepDelta = 1;
    }
    const bool upHov = ImGui::IsItemHovered();
    DrawMotrixIcon(MotrixIcon::ChevronUp,
                   rMax.x - capW + (capW - 10.0f * s) * 0.5f,
                   rMin.y + (h * 0.5f - 10.0f * s) * 0.5f + 1.0f * s,
                   10.0f * s,
                   10.0f * s,
                   upHov ? p.primary : p.subLabelText,
                   s);

    ImGui::SetCursorScreenPos(ImVec2(rMax.x - capW, rMin.y + h * 0.5f));
    if (ImGui::InvisibleButton(std::format("{}_dn", id).c_str(), ImVec2(capW, h * 0.5f))) {
        stepDelta = -1;
    }
    const bool dnHov = ImGui::IsItemHovered();
    DrawMotrixIcon(MotrixIcon::ChevronDown,
                   rMax.x - capW + (capW - 10.0f * s) * 0.5f,
                   rMin.y + h * 0.5f + (h * 0.5f - 10.0f * s) * 0.5f - 1.0f * s,
                   10.0f * s,
                   10.0f * s,
                   dnHov ? p.primary : p.subLabelText,
                   s);

    dl->AddRect(rMin,
                rMax,
                ToAbgrU32(ResolveElBorderColor(p, inputFocused, inputHovered || upHov || dnHov)),
                4.0f * s,
                0,
                1.0f);
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(width, h));
    return valueChanged;
}

}  // namespace

ElCompositeResult ElCompositeInput(const char* id,
                                   char* buf,
                                   std::size_t cap,
                                   float width,
                                   const MotrixPalette& p,
                                   const FontRegistry& fonts,
                                   float s,
                                   std::optional<MotrixIcon> leftIcon,
                                   std::optional<MotrixIcon> rightIcon,
                                   std::string_view placeholder,
                                   bool readOnly,
                                   bool password) {
    ElCompositeResult res{};
    FontGuard fnt(fonts, FontRole::Regular, 13.5f);
    const float h = 30.0f * s;
    const float leftW = leftIcon ? (38.0f * s) : 0.0f;
    const float rightW = rightIcon ? (40.0f * s) : 0.0f;
    const float midW = std::max(60.0f * s, width - leftW - rightW);
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + width, rMin.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(rMin, rMax, ToAbgrU32(readOnly ? p.inputAddonBg : p.inputBg), 4.0f * s);
    if (leftIcon) {
        res.leftClicked = DrawAddonSlot(std::format("{}_L", id).c_str(),
                                        rMin,
                                        ImVec2(rMin.x + leftW, rMax.y),
                                        *leftIcon,
                                        true,
                                        p,
                                        s);
    }

    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const float padY = std::max(3.0f * s, (h - ImGui::GetFontSize()) * 0.5f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, 10.0f * s, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
    StyleColorGuard txtCol(ImGuiCol_Text, readOnly ? p.taskMetaText : p.panelTitle);
    StyleColorGuard hintCol(ImGuiCol_TextDisabled, p.inputPlaceholder);

    ImGui::SetCursorScreenPos(ImVec2(rMin.x + leftW, rMin.y));
    ImGui::SetNextItemWidth(midW);
    int flags =
        (readOnly ? ImGuiInputTextFlags_ReadOnly : 0) | (password ? ImGuiInputTextFlags_Password : 0);
    const std::string hint(placeholder);
    res.textChanged = ImGui::InputTextWithHint(id, hint.c_str(), buf, cap, flags);
    const bool focused = ImGui::IsItemActive() || ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();

    if (rightIcon) {
        res.rightClicked = DrawAddonSlot(std::format("{}_R", id).c_str(),
                                         ImVec2(rMax.x - rightW, rMin.y),
                                         rMax,
                                         *rightIcon,
                                         false,
                                         p,
                                         s);
    }
    dl->AddRect(rMin, rMax, ToAbgrU32(ResolveElBorderColor(p, focused, hovered)), 4.0f * s, 0, 1.0f);
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(width, h));
    return res;
}

bool ElInputNumberInt(const char* id,
                      int* value,
                      int minVal,
                      int maxVal,
                      int step,
                      float width,
                      const MotrixPalette& p,
                      const FontRegistry& fonts,
                      float s) {
    int delta = 0;
    bool changed = RenderNumericBoxImpl(id, std::to_string(*value), width, p, fonts, s, delta, [&]() {
        return ImGui::InputInt(id, value, 0, 0);
    });
    if (delta != 0) {
        *value += delta * step;
        changed = true;
    }
    *value = std::clamp(*value, minVal, maxVal);
    return changed;
}

bool ElInputNumberFloat(const char* id,
                        float* value,
                        float minVal,
                        float maxVal,
                        float step,
                        float width,
                        const MotrixPalette& p,
                        const FontRegistry& fonts,
                        float s) {
    int delta = 0;
    std::string disp = std::format("{:.1f}", *value);
    if (disp.ends_with(".0")) {
        disp.resize(disp.size() - 2);
    }
    bool changed = RenderNumericBoxImpl(id, disp, width, p, fonts, s, delta, [&]() {
        return ImGui::InputFloat(id, value, 0.0f, 0.0f, "%.1f");
    });
    if (delta != 0) {
        *value += static_cast<float>(delta) * step;
        changed = true;
    }
    *value = std::clamp(*value, minVal, maxVal);
    return changed;
}

}  // namespace motrix::ui
