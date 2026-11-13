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
    ImGui::PushID(strId.data(), strId.data() + strId.size());
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

ChildGuard::ChildGuard(const char* id, float width, float height, int childFlags, int windowFlags)
    : visible_(ImGui::BeginChild(id, ImVec2(width, height), childFlags, windowFlags)) {
}

ChildGuard::~ChildGuard() {
    ImGui::EndChild();
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
