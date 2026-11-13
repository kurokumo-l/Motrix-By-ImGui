module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

inline constexpr std::array<TrackerSourceOption, 14> kTrackerSourceOptions = {{
    {
        "ngosang_best_ip_cdn",
        "ngosang/trackerslist best_ip (CDN)",
        "https://fastly.jsdelivr.net/gh/ngosang/trackerslist/trackers_best_ip.txt",
    },
    {
        "ngosang_best_cdn",
        "ngosang/trackerslist best (CDN)",
        "https://fastly.jsdelivr.net/gh/ngosang/trackerslist/trackers_best.txt",
    },
    {
        "ngosang_all_ip_cdn",
        "ngosang/trackerslist all_ip (CDN)",
        "https://fastly.jsdelivr.net/gh/ngosang/trackerslist/trackers_all_ip.txt",
    },
    {
        "ngosang_all_cdn",
        "ngosang/trackerslist all (CDN)",
        "https://fastly.jsdelivr.net/gh/ngosang/trackerslist/trackers_all.txt",
    },
    {
        "ngosang_best_ip",
        "ngosang/trackerslist best_ip",
        "https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_best_ip.txt",
    },
    {
        "ngosang_best",
        "ngosang/trackerslist best",
        "https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_best.txt",
    },
    {
        "ngosang_all_ip",
        "ngosang/trackerslist all_ip",
        "https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_all_ip.txt",
    },
    {
        "ngosang_all",
        "ngosang/trackerslist all",
        "https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_all.txt",
    },
    {
        "xiu2_best_cdn",
        "XIU2/TrackersList best (CDN)",
        "https://fastly.jsdelivr.net/gh/XIU2/TrackersListCollection/best.txt",
    },
    {
        "xiu2_all_cdn",
        "XIU2/TrackersList all (CDN)",
        "https://fastly.jsdelivr.net/gh/XIU2/TrackersListCollection/all.txt",
    },
    {
        "xiu2_http_cdn",
        "XIU2/TrackersList http (CDN)",
        "https://fastly.jsdelivr.net/gh/XIU2/TrackersListCollection/http.txt",
    },
    {
        "xiu2_best",
        "XIU2/TrackersList best",
        "https://raw.githubusercontent.com/XIU2/TrackersListCollection/master/best.txt",
    },
    {
        "xiu2_all",
        "XIU2/TrackersList all",
        "https://raw.githubusercontent.com/XIU2/TrackersListCollection/master/all.txt",
    },
    {
        "xiu2_http",
        "XIU2/TrackersList http",
        "https://raw.githubusercontent.com/XIU2/TrackersListCollection/master/http.txt",
    },
}};

[[nodiscard]] std::vector<std::string> ParseNormalizedTrackers(std::string_view raw) {
    if (raw.starts_with("\xEF\xBB\xBF")) {
        raw.remove_prefix(3);
    }

    std::vector<std::string> tokens;
    std::unordered_set<std::string> seen;

    std::size_t lineStart = 0;
    while (lineStart < raw.size()) {
        const std::size_t lineEnd = raw.find_first_of("\r\n", lineStart);
        const std::size_t actualEnd = (lineEnd == std::string_view::npos) ? raw.size() : lineEnd;
        const std::string_view line =
            motrix::core::TrimAscii(raw.substr(lineStart, actualEnd - lineStart));
        lineStart = (lineEnd == std::string_view::npos) ? raw.size() : (lineEnd + 1);

        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        std::size_t itemStart = 0;
        while (itemStart < line.size()) {
            const std::size_t comma = line.find(',', itemStart);
            const std::size_t itemEnd = (comma == std::string_view::npos) ? line.size() : comma;
            const std::string_view tok =
                motrix::core::TrimAscii(line.substr(itemStart, itemEnd - itemStart));
            itemStart = (comma == std::string_view::npos) ? line.size() : (comma + 1);

            if (tok.empty() || tok.starts_with('#')) {
                continue;
            }
            std::string entry(tok);
            if (seen.insert(entry).second) {
                tokens.push_back(std::move(entry));
            }
        }
    }
    return tokens;
}

[[nodiscard]] std::string JoinTokens(std::span<const std::string> tokens, std::string_view sep) {
    if (tokens.empty()) {
        return {};
    }
    std::size_t total = (tokens.size() - 1) * sep.size();
    for (const auto& t : tokens) {
        total += t.size();
    }
    std::string out;
    out.reserve(total);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out.append(sep);
        }
        out.append(tokens[i]);
    }
    return out;
}

}  // namespace

std::span<const TrackerSourceOption> AllTrackerSourceOptions() noexcept {
    return std::span<const TrackerSourceOption>{kTrackerSourceOptions};
}

std::vector<std::string> DefaultTrackerSources() {
    return {
        std::string(kTrackerSourceOptions[0].url),
        std::string(kTrackerSourceOptions[1].url),
    };
}

std::string ConvertTrackerDataToLine(std::string_view raw) {
    const auto tokens = ParseNormalizedTrackers(raw);
    return JoinTokens(tokens, "\n");
}

std::string ReduceTrackerString(std::string_view commaTrackers, std::size_t maxLen) {
    if (maxLen == 0) {
        return {};
    }
    if (commaTrackers.size() <= maxLen) {
        return std::string(commaTrackers);
    }
    const std::string_view prefix = commaTrackers.substr(0, maxLen);
    if (commaTrackers[maxLen] == ',') {
        return std::string(prefix);
    }
    const std::size_t lastComma = prefix.rfind(',');
    if (lastComma != std::string_view::npos) {
        return std::string(prefix.substr(0, lastComma));
    }
    return std::string(prefix);
}

std::string ConvertTrackerDataToComma(std::string_view raw, std::size_t maxLen) {
    const auto tokens = ParseNormalizedTrackers(raw);
    const std::string joined = JoinTokens(tokens, ",");
    return ReduceTrackerString(joined, maxLen);
}

bool ShouldAutoSyncTrackers(const UserPreferences& prefs, std::int64_t nowEpochSec) noexcept {
    if (!prefs.autoSyncTracker || prefs.trackerSource.empty()) {
        return false;
    }
    if (prefs.lastSyncTrackerTime <= 0) {
        return true;
    }
    return (nowEpochSec - prefs.lastSyncTrackerTime) >= kAutoSyncTrackerIntervalSec;
}

}  // namespace motrix::domain
