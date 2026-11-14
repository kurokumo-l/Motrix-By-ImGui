export module motrix.ui:el_widgets;

import std;
import motrix.domain;
import :theme;
import :fonts;
import :icons;

namespace motrix::ui {

export struct ElStyle {
    const MotrixPalette& palette;
    const FontRegistry& fonts;
    float dpiScale = 1.0f;
};

export struct ElCompositeResult {
    bool textChanged = false;
    bool leftClicked = false;
    bool rightClicked = false;
};

export struct ElLinkSpec {
    std::string_view label;
    std::function<void()> onClick;
    float fontSize = 12.0f;
    bool accentColor = false;
};

export struct ElTextInputSpec {
    const char* id = "";
    char* buf = nullptr;
    std::size_t cap = 0;
    float width = 0.0f;
    std::string_view placeholder{};
    int flags = 0;
    float height = 0.0f;
};

export struct ElCompositeSpec {
    const char* id = "";
    char* buf = nullptr;
    std::size_t cap = 0;
    float width = 0.0f;
    std::optional<MotrixIcon> leftIcon{};
    std::optional<MotrixIcon> rightIcon{};
    std::string_view placeholder{};
    bool readOnly = false;
    bool password = false;
};

export struct ElIntSpec {
    int* value = nullptr;
    int minVal = 0;
    int maxVal = 100;
    int step = 1;
    float width = 128.0f;
};

export struct ElFloatSpec {
    float* value = nullptr;
    float minVal = 0.0f;
    float maxVal = 100.0f;
    float step = 0.5f;
    float width = 128.0f;
};

export struct ElSelectSpec {
    const char* id = "";
    int* currentIdx = nullptr;
    std::span<const char* const> items{};
    float width = 120.0f;
};

export struct ElDirInputSpec {
    const char* id = "";
    char* buf = nullptr;
    std::size_t cap = 0;
    float width = 0.0f;
    std::span<const std::string> historyDirs{};
    std::span<const std::string> favoriteDirs{};
    std::function<std::optional<std::string>(std::string_view)> onBrowse{};
};

export struct ElButtonSpec {
    const char* id = "";
    std::string_view label{};
    float width = 80.0f;
    float height = 30.0f;
};

export [[nodiscard]] RgbaColor
ResolveElBorderColor(const MotrixPalette& p, bool focused, bool hovered) noexcept;

export void ElDrawLinkText(const ElStyle& st, const ElLinkSpec& spec);
export bool ElCheckbox(const ElStyle& st, const char* id, std::string_view label, bool* value);
export bool ElSwitch(const ElStyle& st, const char* id, std::string_view label, bool* value);
export bool ElInputText(const ElStyle& st, const ElTextInputSpec& spec);
export bool ElInputTextMultiline(const ElStyle& st, const ElTextInputSpec& spec);
export ElCompositeResult ElCompositeInput(const ElStyle& st, const ElCompositeSpec& spec);
export bool ElInputNumberInt(const ElStyle& st, const char* id, const ElIntSpec& spec);
export bool ElInputNumberFloat(const ElStyle& st, const char* id, const ElFloatSpec& spec);
export bool ElSelectCombo(const ElStyle& st, const ElSelectSpec& spec);
export bool ElDirectoryInput(const ElStyle& st, const ElDirInputSpec& spec);
export bool
ElTrackerTagSelect(const ElStyle& st, const char* id, std::vector<bool>& selected, float width);
export bool ElSecondaryButton(const ElStyle& st, const ElButtonSpec& spec);
export bool ElPrimaryButton(const ElStyle& st, const ElButtonSpec& spec);

}  // namespace motrix::ui
