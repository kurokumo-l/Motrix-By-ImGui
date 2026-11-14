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

[[nodiscard]] std::string ShortTrackerTagName(std::string_view url) {
    const std::size_t slash = url.rfind('/');
    return (slash != std::string_view::npos && slash + 1 < url.size())
               ? std::string(url.substr(slash + 1))
               : std::string(url);
}

}  // namespace

bool ElSelectCombo(const char* id,
                   int* currentIdx,
                   std::span<const char* const> items,
                   float width,
                   const MotrixPalette& p,
                   const FontRegistry& fonts,
                   float s) {
    FontGuard fnt(fonts, FontRole::Regular, 13.5f);
    const float h = 30.0f * s;
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + width, rMin.y + h);
    const std::string popId = std::format("{}_pop", id);
    if (ImGui::InvisibleButton(id, ImVec2(width, h))) {
        ImGui::OpenPopup(popId.c_str());
    }
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const bool open = ImGui::IsPopupOpen(popId.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);
    dl->AddRect(rMin, rMax, ToAbgrU32(ResolveElBorderColor(p, open, hovered)), 4.0f * s, 0, 1.0f);

    if (!items.empty()) {
        const int idx = std::clamp(*currentIdx, 0, static_cast<int>(items.size()) - 1);
        const char* curTxt = items[static_cast<std::size_t>(idx)];
        const ImVec2 tSz = ImGui::CalcTextSize(curTxt);
        dl->AddText(
            ImVec2(rMin.x + 12.0f * s, rMin.y + (h - tSz.y) * 0.5f), ToAbgrU32(p.panelTitle), curTxt);
    }
    const float chevSz = 12.0f * s;
    DrawMotrixIcon(open ? MotrixIcon::ChevronUp : MotrixIcon::ChevronDown,
                   rMax.x - chevSz - 10.0f * s,
                   rMin.y + (h - chevSz) * 0.5f,
                   chevSz,
                   chevSz,
                   p.inputPlaceholder,
                   s);

    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(rMin.x, rMax.y + 4.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 0.0f, 4.0f * s);
    StyleVarGuard popRound(ImGuiStyleVar_PopupRounding, 4.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    if (ImGui::BeginPopup(popId.c_str())) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const bool selected = (i == *currentIdx);
            const ImVec2 itemPos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(std::format("{}_opt_{}", id, i).c_str(),
                                       ImVec2(width, 30.0f * s))) {
                *currentIdx = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            const bool rowHov = ImGui::IsItemHovered();
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            if (rowHov) {
                pdl->AddRectFilled(itemPos,
                                   ImVec2(itemPos.x + width, itemPos.y + 30.0f * s),
                                   ToAbgrU32(p.inputAddonBg));
            }
            const char* lbl = items[static_cast<std::size_t>(i)];
            const ImVec2 lSz = ImGui::CalcTextSize(lbl);
            pdl->AddText(ImVec2(itemPos.x + 14.0f * s, itemPos.y + (30.0f * s - lSz.y) * 0.5f),
                         ToAbgrU32(selected ? p.primary : p.panelTitle),
                         lbl);
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool ElDirectoryInput(const char* id,
                      char* buf,
                      std::size_t cap,
                      float width,
                      std::span<const std::string> historyDirs,
                      std::span<const std::string> favoriteDirs,
                      const std::function<std::optional<std::string>(std::string_view)>& onBrowse,
                      const MotrixPalette& p,
                      const FontRegistry& fonts,
                      float s) {
    const ImVec2 basePos = ImGui::GetCursorScreenPos();
    const ElCompositeResult r = ElCompositeInput(
        id, buf, cap, width, p, fonts, s, MotrixIcon::TaskHistory, MotrixIcon::Folder, {}, false, false);
    const std::string histPopId = std::format("{}_hist", id);
    if (r.leftClicked) {
        ImGui::OpenPopup(histPopId.c_str());
    }
    bool changed = r.textChanged;
    if (r.rightClicked && onBrowse) {
        if (auto chosen = onBrowse(buf)) {
            const std::size_t n = std::min(chosen->size(), cap > 0 ? cap - 1 : 0);
            std::memcpy(buf, chosen->data(), n);
            buf[n] = '\0';
            changed = true;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(basePos.x, basePos.y + 34.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::min(width, 360.0f * s), 0.0f), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 4.0f * s, 4.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    if (ImGui::BeginPopup(histPopId.c_str())) {
        FontGuard lf(fonts, FontRole::Regular, 13.0f);
        auto drawDirs = [&](std::span<const std::string> list, const char* prefix) {
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (ImGui::Selectable(std::format("{}##{}{}", list[i], prefix, i).c_str())) {
                    const std::size_t n = std::min(list[i].size(), cap > 0 ? cap - 1 : 0);
                    std::memcpy(buf, list[i].data(), n);
                    buf[n] = '\0';
                    changed = true;
                }
            }
        };
        drawDirs(favoriteDirs, "fav");
        drawDirs(historyDirs, "hist");
        if (favoriteDirs.empty() && historyDirs.empty()) {
            ImGui::TextDisabled("—");
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool ElTrackerTagSelect(const char* id,
                        std::vector<bool>& selected,
                        float width,
                        const MotrixPalette& p,
                        const FontRegistry& fonts,
                        float s) {
    const auto allOpts = motrix::domain::AllTrackerSourceOptions();
    if (selected.size() != allOpts.size()) {
        selected.assign(allOpts.size(), false);
    }

    FontGuard fnt(fonts, FontRole::Regular, 12.0f);
    const float padX = 6.0f * s;
    const float padY = 5.0f * s;
    const float gap = 5.0f * s;
    const float tagH = 22.0f * s;
    const float rightPad = 24.0f * s;
    const float maxInnerW = std::max(60.0f * s, width - padX - rightPad);

    struct ChipLayout {
        std::size_t idx;
        std::string text;
        float relX;
        float relY;
        float w;
    };
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

    const float boxH = std::max(32.0f * s, (chips.empty() ? 32.0f * s : curY + tagH + padY));
    const ImVec2 rMin = ImGui::GetCursorScreenPos();
    const ImVec2 rMax(rMin.x + width, rMin.y + boxH);
    const std::string popId = std::format("{}_pop", id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rMin, rMax, ToAbgrU32(p.inputBg), 4.0f * s);

    ImGui::SetCursorScreenPos(rMin);
    ImGui::SetNextItemAllowOverlap();
    const bool boxClicked = ImGui::InvisibleButton(id, ImVec2(width, boxH));
    const bool hovered = ImGui::IsItemHovered();

    bool changed = false;
    bool chipCloseClicked = false;
    for (const auto& c : chips) {
        const ImVec2 cMin(rMin.x + c.relX, rMin.y + c.relY);
        const ImVec2 cMax(cMin.x + c.w, cMin.y + tagH);
        dl->AddRectFilled(cMin, cMax, ToAbgrU32(p.tagBg), 3.0f * s);
        const ImVec2 tSz = ImGui::CalcTextSize(c.text.c_str());
        dl->AddText(ImVec2(cMin.x + 6.0f * s, cMin.y + (tagH - tSz.y) * 0.5f),
                    ToAbgrU32(p.taskMetaText),
                    c.text.c_str());
        const ImVec2 xCenter(cMax.x - 9.0f * s, cMin.y + tagH * 0.5f);
        dl->AddCircleFilled(xCenter, 5.5f * s, ToAbgrU32(p.inputPlaceholder), 12);
        dl->AddLine(ImVec2(xCenter.x - 2.3f * s, xCenter.y - 2.3f * s),
                    ImVec2(xCenter.x + 2.3f * s, xCenter.y + 2.3f * s),
                    ToAbgrU32(p.inputBg),
                    1.1f * s);
        dl->AddLine(ImVec2(xCenter.x + 2.3f * s, xCenter.y - 2.3f * s),
                    ImVec2(xCenter.x - 2.3f * s, xCenter.y + 2.3f * s),
                    ToAbgrU32(p.inputBg),
                    1.1f * s);
        ImGui::SetCursorScreenPos(ImVec2(xCenter.x - 7.0f * s, cMin.y));
        if (ImGui::InvisibleButton(std::format("{}_x_{}", id, c.idx).c_str(), ImVec2(14.0f * s, tagH))) {
            selected[c.idx] = false;
            changed = true;
            chipCloseClicked = true;
        }
    }

    if (boxClicked && !chipCloseClicked) {
        ImGui::OpenPopup(popId.c_str());
    }
    const bool open = ImGui::IsPopupOpen(popId.c_str());
    dl->AddRect(rMin, rMax, ToAbgrU32(ResolveElBorderColor(p, open, hovered)), 4.0f * s, 0, 1.0f);
    DrawMotrixIcon(open ? MotrixIcon::ChevronUp : MotrixIcon::ChevronDown,
                   rMax.x - 20.0f * s,
                   rMin.y + (boxH - 12.0f * s) * 0.5f,
                   12.0f * s,
                   12.0f * s,
                   p.inputPlaceholder,
                   s);

    ImGui::SetNextWindowPos(ImVec2(rMin.x, rMax.y + 4.0f * s), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 240.0f * s), ImGuiCond_Always);
    StyleVarGuard popPad(ImGuiStyleVar_WindowPadding, 6.0f * s, 6.0f * s);
    StyleColorGuard popBg(ImGuiCol_PopupBg, p.modalBg);
    if (ImGui::BeginPopup(popId.c_str())) {
        for (std::size_t i = 0; i < allOpts.size(); ++i) {
            bool on = selected[i];
            if (ImGui::Selectable(std::format("{}##tr_opt_{}", allOpts[i].label, i).c_str(),
                                  &on,
                                  ImGuiSelectableFlags_DontClosePopups)) {
                selected[i] = on;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(rMin);
    ImGui::Dummy(ImVec2(width, boxH));
    return changed;
}

}  // namespace motrix::ui
