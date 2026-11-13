export module motrix.core:encoding;

import std;

namespace motrix::core {

export [[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8);
export [[nodiscard]] std::string WideToUtf8(std::wstring_view wide);
export [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
export [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view utf8);
export [[nodiscard]] std::string AsciiLower(std::string_view text);
export [[nodiscard]] std::string_view TrimAscii(std::string_view text) noexcept;

}  // namespace motrix::core
