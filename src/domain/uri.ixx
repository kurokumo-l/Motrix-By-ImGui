export module motrix.domain:uri;

import std;
import motrix.core;

namespace motrix::domain {

export [[nodiscard]] motrix::core::Result<std::string> DecodeThunderUri(std::string_view uri);
export [[nodiscard]] std::vector<std::string> SplitAndNormalizeUris(std::string_view rawInput);
export [[nodiscard]] bool IsMagnetUri(std::string_view uri) noexcept;
export [[nodiscard]] std::string ExtractMagnetDisplayName(std::string_view uri);

}  // namespace motrix::domain
