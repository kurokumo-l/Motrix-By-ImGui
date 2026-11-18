module;

#include <imgui.h>

export module motrix.ui:fonts_dwrite;

import std;

namespace motrix::ui {

/// Returns a Windows DirectWrite ImFontLoader with native TrueType hinting,
/// or nullptr if DirectWrite initialization is unavailable.
[[nodiscard]] const ImFontLoader* GetDWriteFontLoader() noexcept;
[[nodiscard]] bool HasDWriteSystemFontFamily(std::string_view familyUtf8);

}  // namespace motrix::ui
