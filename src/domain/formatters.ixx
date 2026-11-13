export module motrix.domain:formatters;

import std;

namespace motrix::domain {

export [[nodiscard]] std::string FormatBytes(std::uint64_t bytes);
export [[nodiscard]] std::string FormatSpeed(std::uint64_t bytesPerSec);
export [[nodiscard]] std::string FormatDurationCompact(std::uint64_t seconds);
export [[nodiscard]] std::string FormatRemainingEta(std::uint64_t remainingBytes,
                                                    std::uint64_t downloadSpeed,
                                                    std::string_view prefix = "Remaining");

}  // namespace motrix::domain
