module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;

namespace motrix::ui {

namespace {

[[nodiscard]] float ExpFactor(float speed, float dt) noexcept {
    if (dt <= 0.0f) {
        return 0.0f;
    }
    return 1.0f - std::exp(-speed * std::min(dt, 0.1f));
}

[[nodiscard]] RgbaColor LerpRgba(RgbaColor a, RgbaColor b, float t) noexcept {
    return RgbaColor{
        .r = a.r + (b.r - a.r) * t,
        .g = a.g + (b.g - a.g) * t,
        .b = a.b + (b.b - a.b) * t,
        .a = a.a + (b.a - a.a) * t,
    };
}

[[nodiscard]] bool ColorNearlyEqual(RgbaColor a, RgbaColor b) noexcept {
    constexpr float kEps = 0.002f;
    return std::fabs(a.r - b.r) < kEps && std::fabs(a.g - b.g) < kEps && std::fabs(a.b - b.b) < kEps
           && std::fabs(a.a - b.a) < kEps;
}

[[nodiscard]] ImVec4 ToImVec4(RgbaColor c) noexcept {
    return ImVec4(c.r, c.g, c.b, c.a);
}

void PopulateDarkPalette(MotrixPalette& p) noexcept {
    p.asideBg = HexColor(0x1F1F1F);
    p.subnavBg = HexColor(0x2D2D2D);
    p.subnavTitle = HexColor(0xFFFFFF);
    p.subnavItemText = HexColor(0xAAAAAA);
    p.subnavActiveBg = HexColor(0x444444);
    p.subnavActiveText = HexColor(0xFFFFFF);

    p.mainBg = HexColor(0x343434);
    p.panelTitle = HexColor(0xFFFFFF);
    p.panelBorder = HexColor(0xFFFFFF, 0.10f);
    p.headerAction = HexColor(0xEEEEEE);
    p.headerActionHover = HexColor(0x5B5BFA);
    p.headerActionDisabled = HexColor(0xEEEEEE, 0.35f);

    p.taskCardBg = HexColor(0x2D2D2D);
    p.taskCardBorder = HexColor(0x555555);
    p.taskNameText = HexColor(0xEEEEEE);

    p.pillBg = HexColor(0x4A4A4A);
    p.pillBorder = HexColor(0x5F5F5F);
    p.pillIcon = HexColor(0xC0C0C0);

    p.progressTrack = HexColor(0x4A4A4A);
    p.emptyText = HexColor(0xAAAAAA);
    p.speedometerBg = HexColor(0x333333);
    p.speedometerBorder = HexColor(0x5F5F5F);

    p.titlebarActionFg = HexColor(0xEEEEEE);
    p.titlebarActionHoverBg = HexColor(0x444444);

    p.modalBg = HexColor(0x343434);
    p.modalFooterBg = HexColor(0x4A4A4A);
    p.inputBg = HexColor(0x373737);
    p.inputBorder = HexColor(0x5F5F5F);
    p.inputBorderHover = HexColor(0x7A7A7A);
    p.inputAddonBg = HexColor(0x2F2F2F);
    p.inputPlaceholder = HexColor(0x777777);
    p.switchOffBg = HexColor(0x5C5D5F);
    p.subLabelText = HexColor(0xDFDFDF);
    p.tagBg = HexColor(0x444444);
    p.borderSubtle = HexColor(0x565656);
    p.graphicBoxBg = HexColor(0x3F3F3F);
    p.graphicAtomOutline = HexColor(0x1B1F23, 0.24f);
    p.graphicS0 = HexColor(0x161B22);
    p.graphicS1 = HexColor(0x0E4429);
    p.graphicS2 = HexColor(0x006D32);
    p.graphicS3 = HexColor(0x26A641);
    p.graphicS4 = HexColor(0x39D353);
    p.tableHeaderBg = HexColor(0x1F2122);
    p.tableRowBg = HexColor(0x2A2B2C);
    p.tableRowAltBg = HexColor(0x1F2122);
    p.tableHoverBg = HexColor(0x565656);
    p.tableText = HexColor(0x9B9B9B);
}

void PopulateLightPalette(MotrixPalette& p) noexcept {
    p.asideBg = HexColor(0x333333);
    p.subnavBg = HexColor(0xF4F5F7);
    p.subnavTitle = HexColor(0x303133);
    p.subnavItemText = HexColor(0x4D515A);
    p.subnavActiveBg = HexColor(0xEAECF0);
    p.subnavActiveText = HexColor(0x5B5BFA);

    p.mainBg = HexColor(0xFFFFFF);
    p.panelTitle = HexColor(0x303133);
    p.panelBorder = HexColor(0x000000, 0.10f);
    p.headerAction = HexColor(0x4D515A);
    p.headerActionHover = HexColor(0x5B5BFA);
    p.headerActionDisabled = HexColor(0x4D515A, 0.50f);

    p.taskCardBg = HexColor(0xFFFFFF);
    p.taskCardBorder = HexColor(0xCCCCCC);
    p.taskNameText = HexColor(0x505753);

    p.pillBg = HexColor(0xFFFFFF);
    p.pillBorder = HexColor(0xE8E8E8);
    p.pillIcon = HexColor(0x9B9B9B);

    p.progressTrack = HexColor(0xEBEEF5);
    p.emptyText = HexColor(0x555555);
    p.speedometerBg = HexColor(0xFFFFFF);
    p.speedometerBorder = HexColor(0xCCCCCC);

    p.titlebarActionFg = HexColor(0x1F1F1F);
    p.titlebarActionHoverBg = HexColor(0xEEEEEE);

    p.modalBg = HexColor(0xFFFFFF);
    p.modalFooterBg = HexColor(0xF5F5F5);
    p.inputBg = HexColor(0xFFFFFF);
    p.inputBorder = HexColor(0xDCDFE6);
    p.inputBorderHover = HexColor(0xC0C4CC);
    p.inputAddonBg = HexColor(0xF5F7FA);
    p.inputPlaceholder = HexColor(0xC0C4CC);
    p.switchOffBg = HexColor(0xDCDFE6);
    p.subLabelText = HexColor(0x606266);
    p.tagBg = HexColor(0xF0F2F5);
    p.borderSubtle = HexColor(0xEBEEF5);
    p.graphicBoxBg = HexColor(0xFFFFFF);
    p.graphicAtomOutline = HexColor(0x1B1F23, 0.08f);
    p.graphicS0 = HexColor(0xEBEDF0);
    p.graphicS1 = HexColor(0x9BE9A8);
    p.graphicS2 = HexColor(0x40C463);
    p.graphicS3 = HexColor(0x30A14E);
    p.graphicS4 = HexColor(0x39D353);
    p.tableHeaderBg = HexColor(0xFAFAFA);
    p.tableRowBg = HexColor(0xFFFFFF);
    p.tableRowAltBg = HexColor(0xFAFAFA);
    p.tableHoverBg = HexColor(0xF5F7FA);
    p.tableText = HexColor(0x606266);
}

}  // namespace

MotrixPalette BuildMotrixPalette(bool dark) noexcept {
    MotrixPalette p{};
    p.isDark = dark;
    p.primary = HexColor(0x5B5BFA);
    p.primaryProgress = HexColor(0x5B5BEA);
    p.progressActive = HexColor(0x5B5BEA);
    p.progressWaiting = HexColor(0x737373);
    p.progressComplete = HexColor(0x2ACB42);
    p.progressError = HexColor(0xFF6157);
    p.asideHoverBg = HexColor(0xFFFFFF, 0.15f);
    p.asideIcon = HexColor(0xFFFFFF);
    p.taskCardHoverBorder = HexColor(0x5B5BFA);
    p.taskMetaText = HexColor(0x9B9B9B);
    p.pillHoverBg = HexColor(0x5B5BFA);
    p.pillHoverBorder = HexColor(0x5B5BFA);
    p.pillHoverIcon = HexColor(0xFFFFFF);
    p.speedometerHoverBorder = HexColor(0x9B9B9B);
    p.speedometerIdleIcon = HexColor(0x9B9B9B);
    p.speedometerUpText = HexColor(0x9B9B9B);
    p.titlebarCloseHoverBg = HexColor(0xFD0007);
    p.titlebarCloseHoverFg = HexColor(0xFFFFFF);

    if (dark) {
        PopulateDarkPalette(p);
    } else {
        PopulateLightPalette(p);
    }
    return p;
}

ThemeController::ThemeController(bool initialDark)
    : targetDark_(initialDark), current_(BuildMotrixPalette(initialDark)), target_(current_) {
}

void ThemeController::SetDark(bool dark, bool immediate) {
    if (targetDark_ == dark && !immediate) {
        return;
    }
    targetDark_ = dark;
    target_ = BuildMotrixPalette(dark);
    if (immediate) {
        current_ = target_;
    }
}

bool ThemeController::Update(float dt) {
    current_.isDark = target_.isDark;
    if (ColorNearlyEqual(current_.mainBg, target_.mainBg)
        && ColorNearlyEqual(current_.subnavBg, target_.subnavBg)) {
        current_ = target_;
        return false;
    }
    const float t = ExpFactor(16.0f, dt);
    current_.asideBg = LerpRgba(current_.asideBg, target_.asideBg, t);
    current_.subnavBg = LerpRgba(current_.subnavBg, target_.subnavBg, t);
    current_.subnavTitle = LerpRgba(current_.subnavTitle, target_.subnavTitle, t);
    current_.subnavItemText = LerpRgba(current_.subnavItemText, target_.subnavItemText, t);
    current_.subnavActiveBg = LerpRgba(current_.subnavActiveBg, target_.subnavActiveBg, t);
    current_.subnavActiveText = LerpRgba(current_.subnavActiveText, target_.subnavActiveText, t);
    current_.mainBg = LerpRgba(current_.mainBg, target_.mainBg, t);
    current_.panelTitle = LerpRgba(current_.panelTitle, target_.panelTitle, t);
    current_.panelBorder = LerpRgba(current_.panelBorder, target_.panelBorder, t);
    current_.headerAction = LerpRgba(current_.headerAction, target_.headerAction, t);
    current_.headerActionDisabled =
        LerpRgba(current_.headerActionDisabled, target_.headerActionDisabled, t);
    current_.taskCardBg = LerpRgba(current_.taskCardBg, target_.taskCardBg, t);
    current_.taskCardBorder = LerpRgba(current_.taskCardBorder, target_.taskCardBorder, t);
    current_.taskNameText = LerpRgba(current_.taskNameText, target_.taskNameText, t);
    current_.pillBg = LerpRgba(current_.pillBg, target_.pillBg, t);
    current_.pillBorder = LerpRgba(current_.pillBorder, target_.pillBorder, t);
    current_.pillIcon = LerpRgba(current_.pillIcon, target_.pillIcon, t);
    current_.progressTrack = LerpRgba(current_.progressTrack, target_.progressTrack, t);
    current_.emptyText = LerpRgba(current_.emptyText, target_.emptyText, t);
    current_.speedometerBg = LerpRgba(current_.speedometerBg, target_.speedometerBg, t);
    current_.speedometerBorder = LerpRgba(current_.speedometerBorder, target_.speedometerBorder, t);
    current_.titlebarActionFg = LerpRgba(current_.titlebarActionFg, target_.titlebarActionFg, t);
    current_.titlebarActionHoverBg =
        LerpRgba(current_.titlebarActionHoverBg, target_.titlebarActionHoverBg, t);
    current_.modalBg = LerpRgba(current_.modalBg, target_.modalBg, t);
    current_.modalFooterBg = LerpRgba(current_.modalFooterBg, target_.modalFooterBg, t);
    current_.inputBg = LerpRgba(current_.inputBg, target_.inputBg, t);
    current_.inputBorder = LerpRgba(current_.inputBorder, target_.inputBorder, t);
    current_.inputBorderHover = LerpRgba(current_.inputBorderHover, target_.inputBorderHover, t);
    current_.inputAddonBg = LerpRgba(current_.inputAddonBg, target_.inputAddonBg, t);
    current_.inputPlaceholder = LerpRgba(current_.inputPlaceholder, target_.inputPlaceholder, t);
    current_.switchOffBg = LerpRgba(current_.switchOffBg, target_.switchOffBg, t);
    current_.subLabelText = LerpRgba(current_.subLabelText, target_.subLabelText, t);
    current_.tagBg = LerpRgba(current_.tagBg, target_.tagBg, t);
    current_.borderSubtle = LerpRgba(current_.borderSubtle, target_.borderSubtle, t);
    current_.graphicBoxBg = LerpRgba(current_.graphicBoxBg, target_.graphicBoxBg, t);
    current_.graphicAtomOutline = LerpRgba(current_.graphicAtomOutline, target_.graphicAtomOutline, t);
    current_.graphicS0 = LerpRgba(current_.graphicS0, target_.graphicS0, t);
    current_.graphicS1 = LerpRgba(current_.graphicS1, target_.graphicS1, t);
    current_.graphicS2 = LerpRgba(current_.graphicS2, target_.graphicS2, t);
    current_.graphicS3 = LerpRgba(current_.graphicS3, target_.graphicS3, t);
    current_.graphicS4 = LerpRgba(current_.graphicS4, target_.graphicS4, t);
    current_.tableHeaderBg = LerpRgba(current_.tableHeaderBg, target_.tableHeaderBg, t);
    current_.tableRowBg = LerpRgba(current_.tableRowBg, target_.tableRowBg, t);
    current_.tableRowAltBg = LerpRgba(current_.tableRowAltBg, target_.tableRowAltBg, t);
    current_.tableHoverBg = LerpRgba(current_.tableHoverBg, target_.tableHoverBg, t);
    current_.tableText = LerpRgba(current_.tableText, target_.tableText, t);
    return true;
}

void ThemeController::ApplyImGuiStyleGeometry(float dpiScale) const {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.FontSizeBase = 15.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 7.0f;
    style.ScrollbarRounding = 6.0f;
    style.InputTextCursorSize = 1.5f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.ScaleAllSizes(dpiScale);
    style.InputTextCursorSize = std::max(1.5f, style.InputTextCursorSize);
    style.FontScaleDpi = dpiScale;
}

void ThemeController::ApplyImGuiStyleColors() const {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    const MotrixPalette& p = current_;

    c[ImGuiCol_Text] = ToImVec4(p.panelTitle);
    c[ImGuiCol_TextDisabled] = ToImVec4(p.inputPlaceholder);
    c[ImGuiCol_TextSelectedBg] = ToImVec4(WithAlpha(p.primary, 0.28f));
    c[ImGuiCol_InputTextCursor] = ToImVec4(p.panelTitle);
    c[ImGuiCol_WindowBg] = ToImVec4(p.mainBg);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ToImVec4(p.modalBg);
    c[ImGuiCol_Border] = ToImVec4(p.inputBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_NavCursor] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_FrameBg] = ToImVec4(p.inputBg);
    c[ImGuiCol_FrameBgHovered] = ToImVec4(p.inputBg);
    c[ImGuiCol_FrameBgActive] = ToImVec4(p.inputBg);

    c[ImGuiCol_Button] = ToImVec4(p.primary);
    c[ImGuiCol_ButtonHovered] =
        HexColor(0x6E6EFF).r > 0 ? ToImVec4(HexColor(0x6E6EFF)) : ToImVec4(p.primary);
    c[ImGuiCol_ButtonActive] = ToImVec4(HexColor(0x4A4AE8));
    c[ImGuiCol_SliderGrab] = ToImVec4(p.primary);
    c[ImGuiCol_SliderGrabActive] = ToImVec4(HexColor(0x4A4AE8));
    c[ImGuiCol_CheckMark] = ToImVec4(p.primary);

    c[ImGuiCol_TitleBg] = ToImVec4(p.modalBg);
    c[ImGuiCol_TitleBgActive] = ToImVec4(p.modalBg);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] =
        p.isDark ? ImVec4(1.0f, 1.0f, 1.0f, 0.22f) : ImVec4(0.0f, 0.0f, 0.0f, 0.22f);
    c[ImGuiCol_ScrollbarGrabHovered] =
        p.isDark ? ImVec4(1.0f, 1.0f, 1.0f, 0.36f) : ImVec4(0.0f, 0.0f, 0.0f, 0.38f);
    c[ImGuiCol_ScrollbarGrabActive] =
        p.isDark ? ImVec4(1.0f, 1.0f, 1.0f, 0.48f) : ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, p.isDark ? 0.56f : 0.45f);
    c[ImGuiCol_TableHeaderBg] = ToImVec4(p.tableHeaderBg);
    c[ImGuiCol_TableBorderStrong] = ToImVec4(p.borderSubtle);
    c[ImGuiCol_TableBorderLight] = ToImVec4(p.borderSubtle);
    c[ImGuiCol_TableRowBg] = ToImVec4(p.tableRowBg);
    c[ImGuiCol_TableRowBgAlt] = ToImVec4(p.tableRowAltBg);
    c[ImGuiCol_Header] = ToImVec4(p.subnavActiveBg);
    c[ImGuiCol_HeaderHovered] = ToImVec4(p.tableHoverBg);
    c[ImGuiCol_HeaderActive] = ToImVec4(p.subnavActiveBg);
}

bool ResolveThemeIsDark(motrix::domain::ThemePreference pref, bool systemDark) noexcept {
    switch (pref) {
        case motrix::domain::ThemePreference::Dark: return true;
        case motrix::domain::ThemePreference::Light: return false;
        case motrix::domain::ThemePreference::Auto: return systemDark;
    }
    return false;
}

}  // namespace motrix::ui
