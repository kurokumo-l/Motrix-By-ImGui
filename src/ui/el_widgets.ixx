export module motrix.ui:el_widgets;

import std;
import motrix.domain;
import :theme;
import :fonts;
import :icons;

namespace motrix::ui {

export struct ElCompositeResult {
    bool textChanged = false;
    bool leftClicked = false;
    bool rightClicked = false;
};

export [[nodiscard]] RgbaColor
ResolveElBorderColor(const MotrixPalette& p, bool focused, bool hovered) noexcept;

export void ElDrawLinkText(const MotrixPalette& p,
                           const FontRegistry& fonts,
                           float dpiScale,
                           std::string_view label,
                           const std::function<void()>& onClick,
                           float fontSize = 12.0f,
                           bool accentColor = false);

export bool ElCheckbox(const char* id,
                       std::string_view label,
                       bool* value,
                       const MotrixPalette& p,
                       const FontRegistry& fonts,
                       float dpiScale);

export bool ElSwitch(const char* id,
                     std::string_view label,
                     bool* value,
                     const MotrixPalette& p,
                     const FontRegistry& fonts,
                     float dpiScale);

export bool ElInputText(const char* id,
                        char* buf,
                        std::size_t cap,
                        float width,
                        const MotrixPalette& p,
                        const FontRegistry& fonts,
                        float dpiScale,
                        std::string_view placeholder = {},
                        int flags = 0,
                        float height = 0.0f);

export bool ElInputTextMultiline(const char* id,
                                 char* buf,
                                 std::size_t cap,
                                 float width,
                                 float height,
                                 const MotrixPalette& p,
                                 const FontRegistry& fonts,
                                 float dpiScale,
                                 std::string_view placeholder = {},
                                 int flags = 0);

export ElCompositeResult ElCompositeInput(const char* id,
                                          char* buf,
                                          std::size_t cap,
                                          float width,
                                          const MotrixPalette& p,
                                          const FontRegistry& fonts,
                                          float dpiScale,
                                          std::optional<MotrixIcon> leftIcon,
                                          std::optional<MotrixIcon> rightIcon,
                                          std::string_view placeholder = {},
                                          bool readOnly = false,
                                          bool password = false);

export bool ElInputNumberInt(const char* id,
                             int* value,
                             int minVal,
                             int maxVal,
                             int step,
                             float width,
                             const MotrixPalette& p,
                             const FontRegistry& fonts,
                             float dpiScale);

export bool ElInputNumberFloat(const char* id,
                               float* value,
                               float minVal,
                               float maxVal,
                               float step,
                               float width,
                               const MotrixPalette& p,
                               const FontRegistry& fonts,
                               float dpiScale);

export bool ElSelectCombo(const char* id,
                          int* currentIdx,
                          std::span<const char* const> items,
                          float width,
                          const MotrixPalette& p,
                          const FontRegistry& fonts,
                          float dpiScale);

export bool ElDirectoryInput(const char* id,
                             char* buf,
                             std::size_t cap,
                             float width,
                             std::span<const std::string> historyDirs,
                             std::span<const std::string> favoriteDirs,
                             const std::function<std::optional<std::string>(std::string_view)>& onBrowse,
                             const MotrixPalette& p,
                             const FontRegistry& fonts,
                             float dpiScale);

export bool ElTrackerTagSelect(const char* id,
                               std::vector<bool>& selected,
                               float width,
                               const MotrixPalette& p,
                               const FontRegistry& fonts,
                               float dpiScale);

export bool ElSecondaryButton(const char* id,
                              std::string_view label,
                              float width,
                              float height,
                              const MotrixPalette& p,
                              const FontRegistry& fonts,
                              float dpiScale);

export bool ElPrimaryButton(const char* id,
                            std::string_view label,
                            float width,
                            float height,
                            const MotrixPalette& p,
                            const FontRegistry& fonts,
                            float dpiScale);

}  // namespace motrix::ui
