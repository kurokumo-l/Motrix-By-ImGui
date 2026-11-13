export module motrix.engine:tracker;

import std;
import motrix.core;

namespace motrix::engine {

export struct TrackerFetchResult {
    std::string rawCombinedText;
    std::size_t succeededSources = 0;
    std::vector<std::string> errors;
};

export [[nodiscard]] motrix::core::Result<TrackerFetchResult>
FetchTrackersFromUrls(std::span<const std::string> urls,
                      std::string_view proxyUrl = {},
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(8000));

}  // namespace motrix::engine
