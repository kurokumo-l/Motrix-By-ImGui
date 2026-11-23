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

struct AddonSlotSpec {
    const char* btnId = "";
    ImVec2 minPt{};
    ImVec2 maxPt{};
    MotrixIcon icon = MotrixIcon::Folder;
    bool isLeft = true;
};

struct NumericBoxLayout {
    ImVec2 rMin{};
    ImVec2 rMax{};
    float width = 128.0f;
    float h = 30.0f;
    float capW = 28.0f;
    float numW = 100.0f;
};

struct NumericStepperResult {
    int delta = 0;
    bool hovered = false;
};

struct NumericOutcome {
    bool valueChanged = false;
    int stepDelta = 0;
    bool editing = false;
};

bool DrawAddonSlot(const ElStyle& st, const AddonSlotSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    ImGui::SetCursorScreenPos(spec.minPt);
    const ImVec2 sz(spec.maxPt.x - spec.minPt.x, spec.maxPt.y - spec.minPt.y);
    const bool clicked = ImGui::InvisibleButton(spec.btnId, sz);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImDrawFlags corners =
        spec.isLeft ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;
    dl->AddRectFilled(ImVec2(spec.minPt.x + 1.0f, spec.minPt.y + 1.0f),
                      ImVec2(spec.maxPt.x - 1.0f, spec.maxPt.y - 1.0f),
                      ToAbgrU32(p.inputAddonBg),
                      3.5f * s,
                      corners);
    const float divX = spec.isLeft ? spec.maxPt.x : spec.minPt.x;
    dl->AddLine(ImVec2(divX, spec.minPt.y + 1.0f),
                ImVec2(divX, spec.maxPt.y - 1.0f),
                ToAbgrU32(p.inputBorder),
                1.0f);
    const float hovT =
        AnimStep(st.anim, {ImGui::GetItemID() ^ 0x1A01u, hovered ? 1.0f : 0.0f, 22.0f, 0.0f});
    const float icoSz = 13.0f * s;
    const RgbaColor icoCol = LerpColor(p.taskMetaText, p.primary, hovT);
    DrawMotrixIcon(IconSpec{
        spec.icon,
        spec.minPt.x + (sz.x - icoSz) * 0.5f,
        spec.minPt.y + (sz.y - icoSz) * 0.5f,
        icoSz,
        icoSz,
        icoCol,
        s,
    });
    return clicked;
}

void DrawNumericChrome(ImDrawList* dl, const NumericBoxLayout& lay, const MotrixPalette& p, float s) {
    dl->AddRectFilled(lay.rMin, lay.rMax, ToAbgrU32(p.inputBg), 4.0f * s);
    dl->AddRectFilled(ImVec2(lay.rMax.x - lay.capW, lay.rMin.y + 1.0f),
                      ImVec2(lay.rMax.x - 1.0f, lay.rMax.y - 1.0f),
                      ToAbgrU32(p.inputAddonBg),
                      3.5f * s,
                      ImDrawFlags_RoundCornersRight);
    dl->AddLine(ImVec2(lay.rMax.x - lay.capW, lay.rMin.y + 1.0f),
                ImVec2(lay.rMax.x - lay.capW, lay.rMax.y - 1.0f),
                ToAbgrU32(p.inputBorder),
                1.0f);
    dl->AddLine(ImVec2(lay.rMax.x - lay.capW, lay.rMin.y + lay.h * 0.5f),
                ImVec2(lay.rMax.x - 1.0f, lay.rMin.y + lay.h * 0.5f),
                ToAbgrU32(p.inputBorder),
                1.0f);
}

NumericStepperResult
RenderNumericSteppers(const ElStyle& st, const char* id, const NumericBoxLayout& lay) {
    NumericStepperResult res{};
    const char* safeId = (id != nullptr && id[0] != '\0') ? id : "##num";
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const float halfH = lay.h * 0.5f;
    const float icoSz = 10.0f * s;
    const float icoX = lay.rMax.x - lay.capW + (lay.capW - icoSz) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(lay.rMax.x - lay.capW, lay.rMin.y));
    if (ImGui::InvisibleButton(std::format("{}_up", safeId).c_str(), ImVec2(lay.capW, halfH))) {
        res.delta = 1;
    }
    const bool upHov = ImGui::IsItemHovered();
    if (upHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float upT =
        AnimStep(st.anim, {ImGui::GetItemID() ^ 0x1A02u, upHov ? 1.0f : 0.0f, 22.0f, 0.0f});
    DrawMotrixIcon(IconSpec{
        MotrixIcon::ChevronUp,
        icoX,
        lay.rMin.y + (halfH - icoSz) * 0.5f + 1.0f * s,
        icoSz,
        icoSz,
        LerpColor(p.subLabelText, p.primary, upT),
        s,
    });

    ImGui::SetCursorScreenPos(ImVec2(lay.rMax.x - lay.capW, lay.rMin.y + halfH));
    if (ImGui::InvisibleButton(std::format("{}_dn", safeId).c_str(), ImVec2(lay.capW, halfH))) {
        res.delta = -1;
    }
    const bool dnHov = ImGui::IsItemHovered();
    if (dnHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float dnT =
        AnimStep(st.anim, {ImGui::GetItemID() ^ 0x1A03u, dnHov ? 1.0f : 0.0f, 22.0f, 0.0f});
    DrawMotrixIcon(IconSpec{
        MotrixIcon::ChevronDown,
        icoX,
        lay.rMin.y + halfH + (halfH - icoSz) * 0.5f - 1.0f * s,
        icoSz,
        icoSz,
        LerpColor(p.subLabelText, p.primary, dnT),
        s,
    });
    res.hovered = upHov || dnHov;
    return res;
}

struct NumericBoxSpec {
    const char* id = nullptr;
    std::string_view displayText{};
    float width = 128.0f;
};

template <typename DrawFieldFn>
NumericOutcome
RenderNumericBoxImpl(const ElStyle& st, const NumericBoxSpec& spec, DrawFieldFn&& drawField) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const float h = 32.0f * s;
    const float capW = 28.0f * s;
    const float numW = std::max(36.0f * s, spec.width - capW);
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const NumericBoxLayout lay{rMin, ImVec2(rMin.x + spec.width, rMin.y + h), spec.width, h, capW, numW};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawNumericChrome(dl, lay, p, s);

    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const float padY = std::max(3.0f * s, (h - ImGui::GetFontSize()) * 0.5f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, 8.0f * s, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});

    const char* safeId = (spec.id != nullptr && spec.id[0] != '\0') ? spec.id : "##num";
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID editKey = ImGui::GetID(std::format("{}_edit", safeId).c_str());
    const ImGuiID focusKey = ImGui::GetID(std::format("{}_focus", safeId).c_str());
    NumericOutcome out{};
    bool inputFocused = false;
    bool inputHovered = false;

    if (store->GetBool(editKey, false)) {
        if (store->GetBool(focusKey, false)) {
            store->SetBool(focusKey, false);
            ImGui::SetKeyboardFocusHere(0);
        }
        StyleColorGuard txtCol(ImGuiCol_Text, p.panelTitle);
        ImGui::SetCursorScreenPos(rMin);
        ImGui::SetNextItemWidth(numW);
        out.valueChanged = drawField();
        inputFocused = ImGui::IsItemActive() || ImGui::IsItemFocused();
        inputHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemDeactivated() || (!inputFocused && !inputHovered && ImGui::IsMouseClicked(0))) {
            store->SetBool(editKey, false);
            inputFocused = false;
        }
        out.editing = store->GetBool(editKey, false);
    } else {
        ImGui::SetCursorScreenPos(rMin);
        if (ImGui::InvisibleButton(std::format("{}_hit", safeId).c_str(), ImVec2(numW, h))) {
            store->SetBool(editKey, true);
            store->SetBool(focusKey, true);
        }
        inputHovered = ImGui::IsItemHovered();
        if (!spec.displayText.empty()) {
            const ImVec2 txtSz = ImGui::CalcTextSize(
                spec.displayText.data(), spec.displayText.data() + spec.displayText.size(), false);
            dl->AddText(ImVec2(rMin.x + (numW - txtSz.x) * 0.5f, rMin.y + (h - txtSz.y) * 0.5f),
                        ToAbgrU32(p.panelTitle),
                        spec.displayText.data(),
                        spec.displayText.data() + spec.displayText.size());
        }
    }

    const NumericStepperResult stepRes = RenderNumericSteppers(st, safeId, lay);
    out.stepDelta = stepRes.delta;
    dl->AddRect(
        rMin,
        lay.rMax,
        ToAbgrU32(AnimateElBorderColor(st, safeId, inputFocused, inputHovered || stepRes.hovered)),
        4.0f * s,
        0,
        1.0f);
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(spec.width, h));
    return out;
}

}  // namespace

ElCompositeResult ElCompositeInput(const ElStyle& st, const ElCompositeSpec& spec) {
    ElCompositeResult res{};
    const char* safeId = (spec.id != nullptr && spec.id[0] != '\0') ? spec.id : "##comp";
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float h = 32.0f * s;
    const float leftW = spec.leftIcon ? (38.0f * s) : 0.0f;
    const float rightW = spec.rightIcon ? (40.0f * s) : 0.0f;
    const float midW = std::max(60.0f * s, spec.width - leftW - rightW);
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + spec.width, rMin.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool mutedBg = spec.readOnly && spec.mutedReadOnly;
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(mutedBg ? p.inputAddonBg : p.inputBg), 4.0f * s);
    if (spec.leftIcon) {
        const std::string leftId = std::format("{}_L", safeId);
        res.leftClicked = DrawAddonSlot(
            st, {leftId.c_str(), rMin, ImVec2(rMin.x + leftW, rMax.y), *spec.leftIcon, true});
    }

    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const float padY = std::max(3.0f * s, (h - ImGui::GetFontSize()) * 0.5f);
    StyleVarGuard fp(ImGuiStyleVar_FramePadding, 10.0f * s, padY);
    StyleColorGuard bgClear(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
    StyleColorGuard txtCol(ImGuiCol_Text,
                           (spec.readOnly && spec.mutedReadOnly) ? p.taskMetaText : p.panelTitle);
    StyleColorGuard hintCol(ImGuiCol_TextDisabled, p.inputPlaceholder);

    ImGui::SetCursorScreenPos(ImVec2(rMin.x + leftW, rMin.y));
    ImGui::SetNextItemWidth(midW);
    const int flags = (spec.readOnly ? ImGuiInputTextFlags_ReadOnly : 0)
                      | (spec.password ? ImGuiInputTextFlags_Password : 0);
    const std::string hint(spec.placeholder);
    const bool validBuf = spec.buf != nullptr && spec.cap > 0
                          && spec.cap <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    bool focused = false;
    bool hovered = false;
    if (validBuf) {
        res.textChanged = ImGui::InputTextWithHint(safeId, hint.c_str(), spec.buf, spec.cap, flags);
        res.active = ImGui::IsItemActive();
        res.deactivated = ImGui::IsItemDeactivated();
        focused = res.active || ImGui::IsItemFocused();
        hovered = ImGui::IsItemHovered();
    } else {
        ImGui::Dummy(ImVec2(midW, h));
    }

    if (spec.rightIcon) {
        const std::string rightId = std::format("{}_R", safeId);
        res.rightClicked = DrawAddonSlot(
            st, {rightId.c_str(), ImVec2(rMax.x - rightW, rMin.y), rMax, *spec.rightIcon, false});
    }
    dl->AddRect(
        rMin, rMax, ToAbgrU32(AnimateElBorderColor(st, safeId, focused, hovered)), 4.0f * s, 0, 1.0f);
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(spec.width, h));
    return res;
}

bool ElInputNumberInt(const ElStyle& st, const char* id, const ElIntSpec& spec) {
    if (spec.value == nullptr) {
        return false;
    }
    const char* safeId = (id != nullptr && id[0] != '\0') ? id : "##int";
    const int lo = std::min(spec.minVal, spec.maxVal);
    const int hi = std::max(spec.minVal, spec.maxVal);
    const NumericOutcome out =
        RenderNumericBoxImpl(st, NumericBoxSpec{safeId, std::to_string(*spec.value), spec.width}, [&]() {
            return ImGui::InputInt(safeId, spec.value, 0, 0);
        });
    bool changed = out.valueChanged;
    if (out.stepDelta != 0) {
        const std::int64_t stepped =
            static_cast<std::int64_t>(*spec.value)
            + static_cast<std::int64_t>(out.stepDelta) * static_cast<std::int64_t>(spec.step);
        const int clamped = static_cast<int>(
            std::clamp(stepped, static_cast<std::int64_t>(lo), static_cast<std::int64_t>(hi)));
        if (clamped != *spec.value) {
            *spec.value = clamped;
            changed = true;
        }
    }
    if (!out.editing || out.stepDelta != 0) {
        const int clamped = std::clamp(*spec.value, lo, hi);
        if (clamped != *spec.value) {
            *spec.value = clamped;
            changed = true;
        }
    }
    return changed;
}

bool ElInputNumberFloat(const ElStyle& st, const char* id, const ElFloatSpec& spec) {
    if (spec.value == nullptr) {
        return false;
    }
    const char* safeId = (id != nullptr && id[0] != '\0') ? id : "##flt";
    const float rawLo = std::isfinite(spec.minVal) ? spec.minVal : 0.0f;
    const float rawHi = std::isfinite(spec.maxVal) ? spec.maxVal : rawLo;
    const float lo = std::min(rawLo, rawHi);
    const float hi = std::max(rawLo, rawHi);
    bool changed = false;
    if (!std::isfinite(*spec.value)) {
        *spec.value = lo;
        changed = true;
    }
    std::string disp = std::format("{:.1f}", *spec.value);
    if (disp.ends_with(".0")) {
        disp.resize(disp.size() - 2);
    }
    const NumericOutcome out = RenderNumericBoxImpl(st, NumericBoxSpec{safeId, disp, spec.width}, [&]() {
        return ImGui::InputFloat(
            safeId, spec.value, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_CharsDecimal);
    });
    changed = changed || out.valueChanged;
    if (!std::isfinite(*spec.value)) {
        *spec.value = lo;
        changed = true;
    }
    if (out.stepDelta != 0 && std::isfinite(spec.step)) {
        const double stepped = static_cast<double>(*spec.value)
                               + static_cast<double>(out.stepDelta) * static_cast<double>(spec.step);
        const float nextVal = std::isfinite(stepped) ? static_cast<float>(stepped) : hi;
        if (nextVal != *spec.value) {
            *spec.value = nextVal;
            changed = true;
        }
    }
    if (!out.editing || out.stepDelta != 0) {
        const float clamped = std::isfinite(*spec.value) ? std::clamp(*spec.value, lo, hi) : lo;
        if (clamped != *spec.value) {
            *spec.value = clamped;
            changed = true;
        }
    }
    return changed;
}

}  // namespace motrix::ui
