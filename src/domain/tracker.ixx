export module motrix.domain:tracker;

import std;
import :config_schema;

namespace motrix::domain {

export inline constexpr std::size_t kMaxBtTrackerLength = 6144;
export inline constexpr std::int64_t kAutoSyncTrackerIntervalSec = 12 * 3600;

export struct TrackerSourceOption {
    std::string_view key;
    std::string_view label;
    std::string_view url;
};

export [[nodiscard]] std::span<const TrackerSourceOption> AllTrackerSourceOptions() noexcept;
export [[nodiscard]] std::vector<std::string> DefaultTrackerSources();
export [[nodiscard]] std::string ConvertTrackerDataToLine(std::string_view raw);
export [[nodiscard]] std::string ReduceTrackerString(std::string_view commaTrackers,
                                                     std::size_t maxLen = kMaxBtTrackerLength);
export [[nodiscard]] std::string ConvertTrackerDataToComma(std::string_view raw,
                                                           std::size_t maxLen = kMaxBtTrackerLength);
export [[nodiscard]] bool ShouldAutoSyncTrackers(const UserPreferences& prefs,
                                                 std::int64_t nowEpochSec) noexcept;

}  // namespace motrix::domain
