module;

#include "imgui.h"

module motrix.ui;

import std;
import :theme;
import :fonts;

namespace motrix::ui {

FontGuard::FontGuard(const FontRegistry& fonts, FontRole role, float unscaledSize) {
    ImGui::PushFont(static_cast<ImFont*>(fonts.FontPtr(role)), unscaledSize);
}

FontGuard::~FontGuard() {
    ImGui::PopFont();
}

StyleColorGuard::StyleColorGuard(int imguiCol, RgbaColor color) {
    ImGui::PushStyleColor(imguiCol, ImVec4(color.r, color.g, color.b, color.a));
}

StyleColorGuard::~StyleColorGuard() {
    ImGui::PopStyleColor();
}

StyleVarGuard::StyleVarGuard(int imguiVar, float value) {
    ImGui::PushStyleVar(imguiVar, value);
}

StyleVarGuard::StyleVarGuard(int imguiVar, float x, float y) {
    ImGui::PushStyleVar(imguiVar, ImVec2(x, y));
}

StyleVarGuard::~StyleVarGuard() {
    ImGui::PopStyleVar();
}

IdGuard::IdGuard(int intId) {
    ImGui::PushID(intId);
}

IdGuard::IdGuard(std::string_view strId) {
    if (strId.empty()) {
        ImGui::PushID("");
    } else {
        ImGui::PushID(strId.data(), strId.data() + strId.size());
    }
}

IdGuard::~IdGuard() {
    ImGui::PopID();
}

WindowGuard::WindowGuard(const char* name, bool* open, int windowFlags)
    : open_(ImGui::Begin(name, open, windowFlags)) {
}

WindowGuard::~WindowGuard() {
    ImGui::End();
}

ChildGuard::ChildGuard(const char* id, const ChildWindowSpec& spec)
    : visible_(
          ImGui::BeginChild(id, ImVec2(spec.width, spec.height), spec.childFlags, spec.windowFlags)) {
}

ChildGuard::~ChildGuard() {
    ImGui::EndChild();
}

PopupGuard::PopupGuard(const char* id, int windowFlags) : open_(ImGui::BeginPopup(id, windowFlags)) {
}

PopupGuard::~PopupGuard() {
    if (open_) {
        ImGui::EndPopup();
    }
}

TableGuard::TableGuard(const char* id, const TableSpec& spec)
    : open_(ImGui::BeginTable(id, spec.columns, spec.flags, ImVec2(spec.width, spec.height))) {
}

TableGuard::~TableGuard() {
    if (open_) {
        ImGui::EndTable();
    }
}

GroupGuard::GroupGuard() {
    ImGui::BeginGroup();
}

GroupGuard::~GroupGuard() {
    ImGui::EndGroup();
}

TextWrapGuard::TextWrapGuard(float wrapLocalPosX) {
    ImGui::PushTextWrapPos(wrapLocalPosX);
}

TextWrapGuard::~TextWrapGuard() {
    ImGui::PopTextWrapPos();
}

ClipRectGuard::ClipRectGuard(void* drawList, const ClipRectSpec& spec) : drawList_(drawList) {
    if (drawList_ != nullptr) {
        static_cast<ImDrawList*>(drawList_)->PushClipRect(
            ImVec2(spec.minX, spec.minY), ImVec2(spec.maxX, spec.maxY), spec.intersect);
    }
}

ClipRectGuard::~ClipRectGuard() {
    if (drawList_ != nullptr) {
        static_cast<ImDrawList*>(drawList_)->PopClipRect();
    }
}

PopupModalGuard::PopupModalGuard(const char* title, bool* open, int windowFlags)
    : open_(ImGui::BeginPopupModal(title, open, windowFlags)) {
}

PopupModalGuard::~PopupModalGuard() {
    if (open_) {
        ImGui::EndPopup();
    }
}

}  // namespace motrix::ui
