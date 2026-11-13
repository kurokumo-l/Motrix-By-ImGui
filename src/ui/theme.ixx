export module motrix.ui:theme;

import std;
import motrix.domain;

namespace motrix::ui {

export struct RgbaColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

export [[nodiscard]] constexpr RgbaColor HexColor(std::uint32_t rgb, float alpha = 1.0f) noexcept {
    return RgbaColor{
        .r = static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f,
        .g = static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f,
        .b = static_cast<float>(rgb & 0xFFu) / 255.0f,
        .a = alpha,
    };
}

export [[nodiscard]] constexpr RgbaColor WithAlpha(RgbaColor c, float alpha) noexcept {
    c.a = alpha;
    return c;
}

export [[nodiscard]] constexpr std::uint32_t ToAbgrU32(RgbaColor c) noexcept {
    const auto clampByte = [](float v) constexpr noexcept -> std::uint32_t {
        const float clamped = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    };
    return (clampByte(c.a) << 24) | (clampByte(c.b) << 16) | (clampByte(c.g) << 8) | clampByte(c.r);
}

export struct MotrixPalette {
    bool isDark = false;

    RgbaColor primary;
    RgbaColor primaryProgress;

    RgbaColor asideBg;
    RgbaColor asideHoverBg;
    RgbaColor asideIcon;

    RgbaColor subnavBg;
    RgbaColor subnavTitle;
    RgbaColor subnavItemText;
    RgbaColor subnavActiveBg;
    RgbaColor subnavActiveText;

    RgbaColor mainBg;
    RgbaColor panelTitle;
    RgbaColor panelBorder;
    RgbaColor headerAction;
    RgbaColor headerActionHover;
    RgbaColor headerActionDisabled;

    RgbaColor taskCardBg;
    RgbaColor taskCardBorder;
    RgbaColor taskCardHoverBorder;
    RgbaColor taskNameText;
    RgbaColor taskMetaText;

    RgbaColor pillBg;
    RgbaColor pillBorder;
    RgbaColor pillIcon;
    RgbaColor pillHoverBg;
    RgbaColor pillHoverBorder;
    RgbaColor pillHoverIcon;

    RgbaColor progressTrack;
    RgbaColor progressActive;
    RgbaColor progressWaiting;
    RgbaColor progressComplete;
    RgbaColor progressError;

    RgbaColor emptyText;
    RgbaColor speedometerBg;
    RgbaColor speedometerBorder;
    RgbaColor speedometerHoverBorder;
    RgbaColor speedometerIdleIcon;
    RgbaColor speedometerUpText;

    RgbaColor titlebarActionFg;
    RgbaColor titlebarActionHoverBg;
    RgbaColor titlebarCloseHoverBg;
    RgbaColor titlebarCloseHoverFg;

    RgbaColor modalBg;
    RgbaColor modalFooterBg;
    RgbaColor inputBg;
    RgbaColor inputBorder;
    RgbaColor borderSubtle;
};

export struct LayoutMetrics {
    static constexpr float kTitlebarHeight = 36.0f;
    static constexpr float kWinActionBtnWidth = 48.0f;
    static constexpr float kWinActionBtnHeight = 26.0f;

    static constexpr float kAsideWidth = 78.0f;
    static constexpr float kAsideHideThreshold = 792.0f;
    static constexpr float kSubnavWidth = 200.0f;
    static constexpr float kSubnavHideThreshold = 568.0f;

    static constexpr float kPanelHeaderHeight = 84.0f;
    static constexpr float kPanelHeaderTopPad = 44.0f;
    static constexpr float kPanelHeaderBottomPad = 12.0f;
    static constexpr float kPanelMarginWide = 36.0f;
    static constexpr float kPanelMarginNarrow = 16.0f;

    static constexpr float kTaskCardMinHeight = 78.0f;
    static constexpr float kTaskCardPadX = 12.0f;
    static constexpr float kTaskCardPadY = 16.0f;
    static constexpr float kTaskCardMarginBottom = 16.0f;
    static constexpr float kTaskCardRadius = 6.0f;

    static constexpr float kPillHeight = 24.0f;
    static constexpr float kPillPadX = 10.0f;
    static constexpr float kPillRadius = 14.0f;

    static constexpr float kProgressHeight = 6.0f;
    static constexpr float kProgressRadius = 3.0f;

    static constexpr float kSpeedometerSize = 40.0f;
    static constexpr float kSpeedometerExpandedW = 150.0f;
    static constexpr float kSpeedometerRightWide = 36.0f;
    static constexpr float kSpeedometerRightNarrow = 16.0f;
    static constexpr float kSpeedometerBottom = 24.0f;
};

export [[nodiscard]] MotrixPalette BuildMotrixPalette(bool dark) noexcept;

export class ThemeController {
public:
    explicit ThemeController(bool initialDark = false);

    void SetDark(bool dark, bool immediate = false);
    [[nodiscard]] bool IsDark() const noexcept { return targetDark_; }
    [[nodiscard]] const MotrixPalette& Current() const noexcept { return current_; }
    [[nodiscard]] bool Update(float dt);

    void ApplyImGuiStyleGeometry(float dpiScale) const;
    void ApplyImGuiStyleColors() const;

private:
    bool targetDark_ = false;
    MotrixPalette current_{};
    MotrixPalette target_{};
};

export [[nodiscard]] bool ResolveThemeIsDark(motrix::domain::ThemePreference pref,
                                             bool systemDark = false) noexcept;

}  // namespace motrix::ui
