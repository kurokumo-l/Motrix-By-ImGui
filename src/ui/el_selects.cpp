module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import :el_widgets;
import :prefs_model;
import :theme;
import :fonts;
import :icons;
import :anim;
import :scope_guard;

namespace motrix::ui {

namespace {

struct ChipLayout {
    std::size_t idx = 0;
    std::string text;
    float relX = 0.0f;
    float relY = 0.0f;
    float w = 0.0f;
};

struct ChipDrawState {
    const char* id = "";
    ImVec2 rMin{};
    float tagH = 22.0f;
    bool changed = false;
    bool closeClicked = false;
};

[[nodiscard]] std::string ShortTrackerTagName(std::string_view url) {
    const std::size_t slash = url.rfind('/');
    return (slash != std::string_view::npos && slash + 1 < url.size())
               ? std::string(url.substr(slash + 1))
               : std::string(url);
}

bool RenderSelectPopup(const ElStyle& st, const ElSelectSpec& spec, const char* popId) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const char* safeId = (spec.id != nullptr && spec.id[0] != '\0') ? spec.id : "##sel";
    PopupGuard pop(popId);
    if (!pop.IsOpen()) {
        return false;
    }
    if (spec.currentIdx == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
        return false;
    }
    bool changed = false;
    for (int i = 0; i < static_cast<int>(spec.items.size()); ++i) {
        const bool selected = (i == *spec.currentIdx);
        const ImVec2 itemPos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(std::format("{}_opt_{}", safeId, i).c_str(),
                                   ImVec2(spec.width, 32.0f * s))) {
            *spec.currentIdx = i;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        const float optHovT = AnimStep(
            st.anim, {ImGui::GetItemID() ^ 0x2C02u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 24.0f, 0.0f});
        if (optHovT > 0.01f) {
            pdl->AddRectFilled(itemPos,
                               ImVec2(itemPos.x + spec.width, itemPos.y + 32.0f * s),
                               ToAbgrU32(WithAlpha(p.inputAddonBg, p.inputAddonBg.a * optHovT)));
        }
        const char* rawLbl = spec.items[static_cast<std::size_t>(i)];
        const std::string_view lbl = (rawLbl != nullptr) ? std::string_view(rawLbl) : std::string_view{};
        if (!lbl.empty()) {
            const ImVec2 lSz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size(), false);
            ClipRectGuard clip(
                pdl,
                ClipRectSpec{itemPos.x + 10.0f * s,
                             itemPos.y,
                             std::max(itemPos.x + 12.0f * s, itemPos.x + spec.width - 10.0f * s),
                             itemPos.y + 32.0f * s,
                             true});
            pdl->AddText(ImVec2(itemPos.x + 14.0f * s, itemPos.y + (30.0f * s - lSz.y) * 0.5f),
                         ToAbgrU32(selected ? p.primary : p.panelTitle),
                         lbl.data(),
                         lbl.data() + lbl.size());
        }
    }
    return changed;
}

bool DrawDirEntryGroup(std::span<const std::string> list,
                       const char* prefix,
                       char* buf,
                       std::size_t cap) {
    IdGuard grpId(prefix != nullptr ? std::string_view(prefix) : std::string_view("dir"));
    bool changed = false;
    for (std::size_t i = 0; i < list.size(); ++i) {
        IdGuard rowId(static_cast<int>(i));
        const ImVec2 itemPos = ImGui::GetCursorScreenPos();
        const float itemW = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float itemH = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::Selectable("##dir_item", false, ImGuiSelectableFlags_None, ImVec2(itemW, itemH))) {
            if (buf != nullptr && cap > 0) {
                CopyUtf8ToBuffer(list[i], buf, cap);
                changed = true;
            }
        }
        if (!list[i].empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ClipRectGuard clip(
                dl, ClipRectSpec{itemPos.x, itemPos.y, itemPos.x + itemW, itemPos.y + itemH, true});
            dl->AddText(ImVec2(itemPos.x + ImGui::GetStyle().FramePadding.x,
                               itemPos.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text),
                        list[i].data(),
                        list[i].data() + list[i].size());
        }
    }
    return changed;
}

std::vector<ChipLayout> LayoutTrackerChips(std::span<const motrix::domain::TrackerSourceOption> allOpts,
                                           const std::vector<bool>& selected,
                                           float maxInnerW,
                                           float s) {
    const float padX = 6.0f * s;
    const float padY = 5.0f * s;
    const float gap = 5.0f * s;
    const float tagH = 22.0f * s;
    std::vector<ChipLayout> chips;
    float curX = padX;
    float curY = padY;
    for (std::size_t i = 0; i < allOpts.size(); ++i) {
        if (!selected[i]) {
            continue;
        }
        const std::string tagName = ShortTrackerTagName(allOpts[i].url);
        const float w = ImGui::CalcTextSize(tagName.c_str()).x + 24.0f * s;
        if (curX > padX && curX + w > padX + maxInnerW) {
            curX = padX;
            curY += tagH + gap;
        }
        chips.push_back({i, tagName, curX, curY, w});
        curX += w + gap;
    }
    return chips;
}

void DrawTrackerChipList(const ElStyle& st,
                         std::span<const ChipLayout> chips,
                         std::vector<bool>& selected,
                         ChipDrawState& stOut) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& c : chips) {
        const ImVec2 cMin(stOut.rMin.x + c.relX, stOut.rMin.y + c.relY);
        const ImVec2 cMax(cMin.x + c.w, cMin.y + stOut.tagH);
        dl->AddRectFilled(cMin, cMax, ToAbgrU32(p.tagBg), 3.0f * s);
        const ImVec2 tSz = ImGui::CalcTextSize(c.text.c_str());
        dl->AddText(ImVec2(cMin.x + 6.0f * s, cMin.y + (stOut.tagH - tSz.y) * 0.5f),
                    ToAbgrU32(p.taskMetaText),
                    c.text.c_str());
        const ImVec2 xCenter(cMax.x - 9.0f * s, cMin.y + stOut.tagH * 0.5f);
        dl->AddCircleFilled(xCenter, 5.5f * s, ToAbgrU32(p.inputPlaceholder), 12);
        DrawMotrixIcon(IconSpec{
            MotrixIcon::TagClose,
            xCenter.x - 5.5f * s,
            xCenter.y - 5.5f * s,
            11.0f * s,
            11.0f * s,
            p.inputBg,
            s,
        });
        ImGui::SetCursorScreenPos(ImVec2(xCenter.x - 7.0f * s, cMin.y));
        if (ImGui::InvisibleButton(std::format("{}_x_{}", stOut.id, c.idx).c_str(),
                                   ImVec2(14.0f * s, stOut.tagH))) {
            selected[c.idx] = false;
            stOut.changed = true;
            stOut.closeClicked = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }
}

bool RenderTrackerPopup(const ElStyle& st,
                        const char* popId,
                        std::span<const motrix::domain::TrackerSourceOption> allOpts,
                        std::vector<bool>& selected) {
    PopupGuard pop(popId);
    if (!pop.IsOpen()) {
        return false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < allOpts.size(); ++i) {
        bool on = selected[i];
        if (ImGui::Selectable(std::format("{}##tr_opt_{}", allOpts[i].label, i).c_str(),
                              &on,
                              ImGuiSelectableFlags_DontClosePopups)) {
            selected[i] = on;
            changed = true;
        }
    }
    (void)st;
    return changed;
}

}  // namespace

bool ElSelectCombo(const ElStyle& st, const ElSelectSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const char* safeId = (spec.id != nullptr && spec.id[0] != '\0') ? spec.id : "##sel";
    FontGuard fnt(st.fonts, FontRole::Regular, 14.5f);
    const float h = 32.0f * s;
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + spec.width, rMin.y + h);
    const std::string popId = std::format("{}_pop", safeId);
    if (ImGui::InvisibleButton(safeId, ImVec2(spec.width, h))) {
        ImGui::OpenPopup(popId.c_str());
    }
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const bool open = ImGui::IsPopupOpen(popId.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);
    dl->AddRect(
        rMin, rMax, ToAbgrU32(AnimateElBorderColor(st, safeId, open, hovered)), 4.0f * s, 0, 1.0f);

    const float chevSz = 12.0f * s;
    const float textMaxX = std::max(rMin.x + 12.0f * s, rMax.x - chevSz - 14.0f * s);
    if (spec.currentIdx != nullptr && !spec.items.empty()) {
        const int idx = std::clamp(*spec.currentIdx, 0, static_cast<int>(spec.items.size()) - 1);
        const char* rawCur = spec.items[static_cast<std::size_t>(idx)];
        const std::string_view curTxt =
            (rawCur != nullptr) ? std::string_view(rawCur) : std::string_view{};
        if (!curTxt.empty()) {
            const ImVec2 tSz = ImGui::CalcTextSize(curTxt.data(), curTxt.data() + curTxt.size(), false);
            ClipRectGuard clip(dl, ClipRectSpec{rMin.x + 8.0f * s, rMin.y, textMaxX, rMax.y, true});
            dl->AddText(ImVec2(rMin.x + 12.0f * s, rMin.y + (h - tSz.y) * 0.5f),
                        ToAbgrU32(p.panelTitle),
                        curTxt.data(),
                        curTxt.data() + curTxt.size());
        }
    }
    const float chevT = AnimStep(
        st.anim, {ImGui::GetID(safeId) ^ 0x2C01u, (open || hovered) ? 1.0f : 0.0f, 20.0f, 0.0f});
    DrawMotrixIcon(IconSpec{
        open ? MotrixIcon::ChevronUp : MotrixIcon::ChevronDown,
        rMax.x - chevSz - 10.0f * s,
        rMin.y + (h - chevSz) * 0.5f,
        chevSz,
        chevSz,
        LerpColor(p.inputPlaceholder, p.primary, chevT),
        s,
    });

    ImGui::SetNextWindowPos(ImVec2(rMin.x, rMax.y + 4.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(spec.width, 0.0f), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 0.0f, 4.0f * s);
    StyleVarGuard popRound(ImGuiStyleVar_PopupRounding, 4.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    return RenderSelectPopup(st, spec, popId.c_str());
}

bool ElDirectoryInput(const ElStyle& st, const ElDirInputSpec& spec) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const char* safeId = (spec.id != nullptr && spec.id[0] != '\0') ? spec.id : "##dir";
    const ImVec2 basePos = ImGui::GetCursorScreenPos();
    const ElCompositeResult r = ElCompositeInput(st,
                                                 {
                                                     safeId,
                                                     spec.buf,
                                                     spec.cap,
                                                     spec.width,
                                                     MotrixIcon::TaskHistory,
                                                     MotrixIcon::Folder,
                                                 });
    const std::string histPopId = std::format("{}_hist", safeId);
    if (r.leftClicked) {
        ImGui::OpenPopup(histPopId.c_str());
    }
    bool changed = r.textChanged;
    if (r.rightClicked && spec.onBrowse && spec.buf != nullptr && spec.cap > 0) {
        if (auto chosen = spec.onBrowse(spec.buf)) {
            CopyUtf8ToBuffer(*chosen, spec.buf, spec.cap);
            changed = true;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(basePos.x, basePos.y + 34.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::min(spec.width, 360.0f * s), 0.0f), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 4.0f * s, 4.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    PopupGuard pop(histPopId.c_str());
    if (pop.IsOpen()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
            return changed;
        }
        FontGuard lf(st.fonts, FontRole::Regular, 13.5f);
        changed = DrawDirEntryGroup(spec.favoriteDirs, "fav", spec.buf, spec.cap) || changed;
        changed = DrawDirEntryGroup(spec.historyDirs, "hist", spec.buf, spec.cap) || changed;
        if (spec.favoriteDirs.empty() && spec.historyDirs.empty()) {
            ImGui::TextDisabled("-");
        }
    }
    return changed;
}

bool ElTrackerTagSelect(const ElStyle& st, const char* id, std::vector<bool>& selected, float width) {
    const MotrixPalette& p = st.palette;
    const float s = st.dpiScale;
    const auto allOpts = motrix::domain::AllTrackerSourceOptions();
    if (selected.size() != allOpts.size()) {
        selected.assign(allOpts.size(), false);
    }

    FontGuard fnt(st.fonts, FontRole::Regular, 13.0f);
    const float padX = 6.0f * s;
    const float padY = 5.0f * s;
    const float tagH = 24.0f * s;
    const float maxInnerW = std::max(60.0f * s, width - padX - 24.0f * s);
    const std::vector<ChipLayout> chips = LayoutTrackerChips(allOpts, selected, maxInnerW, s);
    const float boxH = std::max(34.0f * s, chips.empty() ? 34.0f * s : chips.back().relY + tagH + padY);

    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + width, rMin.y + boxH);
    const std::string popId = std::format("{}_pop", id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);

    ImGui::SetCursorScreenPos(rMin);
    ImGui::SetNextItemAllowOverlap();
    const bool boxClicked = ImGui::InvisibleButton(id, ImVec2(width, boxH));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ChipDrawState drawState{id, rMin, tagH, false, false};
    DrawTrackerChipList(st, chips, selected, drawState);
    if (boxClicked && !drawState.closeClicked) {
        ImGui::OpenPopup(popId.c_str());
    }
    const bool open = ImGui::IsPopupOpen(popId.c_str());
    dl->AddRect(rMin, rMax, ToAbgrU32(AnimateElBorderColor(st, id, open, hovered)), 4.0f * s, 0, 1.0f);
    const float chevT =
        AnimStep(st.anim, {ImGui::GetID(id) ^ 0x2C01u, (open || hovered) ? 1.0f : 0.0f, 20.0f, 0.0f});
    DrawMotrixIcon(IconSpec{
        open ? MotrixIcon::ChevronUp : MotrixIcon::ChevronDown,
        rMax.x - 20.0f * s,
        rMin.y + (boxH - 12.0f * s) * 0.5f,
        12.0f * s,
        12.0f * s,
        LerpColor(p.inputPlaceholder, p.primary, chevT),
        s,
    });

    ImGui::SetNextWindowPos(ImVec2(rMin.x, rMax.y + 4.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 240.0f * s), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 6.0f * s, 6.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    const bool popChanged = RenderTrackerPopup(st, popId.c_str(), allOpts, selected);
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(width, boxH));
    return drawState.changed || popChanged;
}

}  // namespace motrix::ui
