module motrix.domain;

import std;

namespace motrix::domain {

namespace {

constexpr std::string_view kSizeUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};

}  // namespace

std::string FormatBytes(std::uint64_t bytes) {
    if (bytes == 0) {
        return "0 B";
    }
    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (value >= 1024.0 && unitIndex + 1 < std::size(kSizeUnits)) {
        value /= 1024.0;
        ++unitIndex;
    }
    if (unitIndex == 0) {
        return std::format("{} {}", bytes, kSizeUnits[0]);
    }
    return std::format("{:.1f} {}", value, kSizeUnits[unitIndex]);
}

std::string FormatBytesFixed(std::uint64_t bytes, int precision, bool zeroAsKb) {
    if (bytes == 0) {
        return zeroAsKb ? "0 KB" : "0 B";
    }
    double value = static_cast<double>(bytes);
    std::size_t unitIndex = 0;
    while (value >= 1024.0 && unitIndex + 1 < std::size(kSizeUnits)) {
        value /= 1024.0;
        ++unitIndex;
    }
    if (unitIndex == 0) {
        return std::format("{} {}", bytes, kSizeUnits[0]);
    }
    const int p = std::clamp(precision, 0, 4);
    if (p == 2) {
        return std::format("{:.2f} {}", value, kSizeUnits[unitIndex]);
    }
    if (p == 0) {
        return std::format("{:.0f} {}", value, kSizeUnits[unitIndex]);
    }
    return std::format("{:.1f} {}", value, kSizeUnits[unitIndex]);
}

std::string FormatSpeed(std::uint64_t bytesPerSec) {
    if (bytesPerSec == 0) {
        return "0 KB/s";
    }
    return std::format("{}/s", FormatBytes(bytesPerSec));
}

std::string FormatDurationCompact(std::uint64_t seconds) {
    if (seconds < 60) {
        return std::format("{}s", seconds);
    }
    const std::uint64_t minutes = seconds / 60;
    const std::uint64_t secs = seconds % 60;
    if (minutes < 60) {
        return std::format("{}m{}s", minutes, secs);
    }
    const std::uint64_t hours = minutes / 60;
    const std::uint64_t mins = minutes % 60;
    if (hours < 24) {
        return std::format("{}h{}m", hours, mins);
    }
    const std::uint64_t days = hours / 24;
    const std::uint64_t hrs = hours % 24;
    return std::format("{}d{}h", days, hrs);
}

std::string
FormatRemainingEta(std::uint64_t remainingBytes, std::uint64_t downloadSpeed, std::string_view prefix) {
    if (remainingBytes == 0 || downloadSpeed == 0) {
        return {};
    }
    const std::uint64_t seconds = (remainingBytes + downloadSpeed - 1) / downloadSpeed;
    const std::string dur = FormatDurationCompact(seconds);
    if (prefix.empty()) {
        return dur;
    }
    return std::format("{} {}", prefix, dur);
}

}  // namespace motrix::domain
