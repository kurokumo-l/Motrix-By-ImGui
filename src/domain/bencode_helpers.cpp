module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {
namespace {

[[nodiscard]] std::string_view NormalizedExtensionView(std::string_view extOrPath) noexcept {
    const std::size_t slash = extOrPath.find_last_of("/\\");
    const std::string_view name =
        (slash == std::string_view::npos) ? extOrPath : extOrPath.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos) {
        return name;
    }
    if (dot == 0) {
        return {};
    }
    return name.substr(dot + 1);
}

[[nodiscard]] std::string_view MatchExtensionView(std::string_view extOrPath) noexcept {
    if (extOrPath.find_last_of("/\\") == std::string_view::npos && extOrPath.size() > 1
        && extOrPath.front() == '.') {
        return extOrPath.substr(1);
    }
    return NormalizedExtensionView(extOrPath);
}

[[nodiscard]] bool ContainsExtension(std::span<const std::string_view> table,
                                     std::string_view extOrPath) noexcept {
    const std::string_view ext = MatchExtensionView(extOrPath);
    if (ext.empty()) {
        return false;
    }
    return std::ranges::any_of(table, [ext](std::string_view item) noexcept {
        return motrix::core::EqualsAsciiInsensitive(item, ext);
    });
}

int HexDigitValue(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

std::string PercentDecodeBytes(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const int hi = HexDigitValue(input[i + 1]);
            const int lo = HexDigitValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

std::string_view LookupAzureusClientName(std::string_view code) noexcept {
    struct Entry {
        std::string_view code;
        std::string_view name;
    };
    static constexpr std::array<Entry, 24> kClients{{
        {"qB", "qBittorrent"},    {"TR", "Transmission"},
        {"UT", "uTorrent"},       {"uT", "uTorrent"},
        {"UM", "uTorrent Mac"},   {"DE", "Deluge"},
        {"LT", "libtorrent"},     {"lt", "libtorrent (rTorrent)"},
        {"AZ", "Azureus / Vuze"}, {"BC", "BitComet"},
        {"BT", "Mainline"},       {"FD", "Free Download Manager"},
        {"XL", "Xunlei"},         {"SD", "Thunder"},
        {"A2", "aria2"},          {"MT", "MoonlightTorrent"},
        {"PT", "PHPTracker"},     {"KT", "KTorrent"},
        {"QT", "Qt4 Torrent"},    {"RT", "Retriever"},
        {"BI", "BiglyBT"},        {"PI", "PicoTorrent"},
        {"TW", "TorrentWeb"},     {"WD", "WebTorrent Desktop"},
    }};
    for (const auto& entry : kClients) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return {};
}

}  // namespace

std::string FormatSelectFileOption(std::span<const int> selectedIndices) {
    std::vector<int> sorted;
    sorted.reserve(selectedIndices.size());
    for (int idx : selectedIndices) {
        if (idx > 0) {
            sorted.push_back(idx);
        }
    }
    std::ranges::sort(sorted);
    const auto uniqueRange = std::ranges::unique(sorted);
    sorted.erase(uniqueRange.begin(), uniqueRange.end());

    std::string out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += std::to_string(sorted[i]);
    }
    return out;
}

std::string DecodePeerId(std::string_view rawOrPercentEncoded) {
    const std::string decoded = PercentDecodeBytes(rawOrPercentEncoded);
    if (decoded.empty()) {
        return "Unknown";
    }

    // Azureus-style: -XX1234-...
    if (decoded.size() >= 8 && decoded[0] == '-' && decoded[7] == '-') {
        const std::string_view code(decoded.data() + 1, 2);
        const std::string_view ver(decoded.data() + 3, 4);
        const std::string_view client = LookupAzureusClientName(code);
        auto formatVer = [](std::string_view v) -> std::string {
            if (std::all_of(v.begin(), v.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                std::string s;
                s.push_back(v[0]);
                s.push_back('.');
                s.push_back(v[1]);
                s.push_back('.');
                s.push_back(v[2]);
                if (v[3] != '0') {
                    s.push_back('.');
                    s.push_back(v[3]);
                }
                return s;
            }
            std::string clean;
            for (char c : v) {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                    clean.push_back(c);
                }
            }
            return clean;
        };
        const std::string verStr = formatVer(ver);
        if (!client.empty()) {
            return verStr.empty() ? std::string(client) : std::format("{} {}", client, verStr);
        }
        return verStr.empty() ? std::string(code) : std::format("{} {}", code, verStr);
    }

    // aria2 format: aria2/1.37.0 or A2-1-37-0
    if (decoded.starts_with("aria2")) {
        return "aria2";
    }
    if (decoded.size() >= 8 && decoded[0] == 'A' && decoded[1] == '2' && decoded[2] == '-') {
        return "aria2";
    }
    if (decoded.starts_with("exbc")) {
        return "BitComet";
    }
    if (decoded.starts_with("-FD")) {
        return "Free Download Manager";
    }

    std::string printable;
    for (unsigned char ch : decoded) {
        if (ch >= 0x20 && ch <= 0x7E) {
            printable.push_back(static_cast<char>(ch));
            if (printable.size() >= 20) {
                break;
            }
        }
    }
    return printable.empty() ? "Unknown" : printable;
}

std::string ExtractFileExtension(std::string_view path) {
    const std::string_view ext = NormalizedExtensionView(path);
    if (ext.empty()) {
        return {};
    }
    return "." + motrix::core::AsciiLower(ext);
}

bool IsVideoExtension(std::string_view extOrPath) noexcept {
    static constexpr std::array<std::string_view, 16> kVideoExts{{
        "mp4",
        "mkv",
        "avi",
        "mov",
        "wmv",
        "flv",
        "webm",
        "m4v",
        "ts",
        "m2ts",
        "mpg",
        "mpeg",
        "rmvb",
        "3gp",
        "ogv",
        "vob",
    }};
    return ContainsExtension(kVideoExts, extOrPath);
}

bool IsAudioExtension(std::string_view extOrPath) noexcept {
    static constexpr std::array<std::string_view, 13> kAudioExts{{
        "mp3",
        "flac",
        "wav",
        "aac",
        "ogg",
        "m4a",
        "wma",
        "opus",
        "ape",
        "alac",
        "aiff",
        "dts",
        "ac3",
    }};
    return ContainsExtension(kAudioExts, extOrPath);
}

bool IsImageExtension(std::string_view extOrPath) noexcept {
    static constexpr std::array<std::string_view, 12> kImageExts{{
        "jpg",
        "jpeg",
        "png",
        "gif",
        "webp",
        "bmp",
        "svg",
        "ico",
        "tif",
        "tiff",
        "avif",
        "heic",
    }};
    return ContainsExtension(kImageExts, extOrPath);
}

std::string FormatUnixTimestampUtc(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) {
        return {};
    }
    const auto tp = std::chrono::sys_seconds{std::chrono::seconds{unixSeconds}};
    return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
}

std::string FormatUnixTimestampLocal(std::int64_t unixSeconds, bool zhCn) {
    if (unixSeconds <= 0) {
        return {};
    }
    const auto tp = std::chrono::sys_seconds{std::chrono::seconds{unixSeconds}};
    try {
        const auto localTp = std::chrono::current_zone()->to_local(tp);
        if (zhCn) {
            const auto days = std::chrono::floor<std::chrono::days>(localTp);
            const std::chrono::year_month_day ymd{days};
            const std::chrono::hh_mm_ss tod{localTp - days};
            return std::format("{}年{}月{}日 {:02}:{:02}:{:02}",
                               static_cast<int>(ymd.year()),
                               static_cast<unsigned>(ymd.month()),
                               static_cast<unsigned>(ymd.day()),
                               tod.hours().count(),
                               tod.minutes().count(),
                               tod.seconds().count());
        }
        return std::format("{:%Y-%m-%d %H:%M:%S}", localTp);
    } catch (...) {
        return FormatUnixTimestampUtc(unixSeconds);
    }
}

float BitfieldCompletion(std::string_view bitfieldHex, std::uint32_t numPieces) noexcept {
    if (bitfieldHex.empty()) {
        return 0.0f;
    }
    std::uint32_t setBits = 0;
    std::uint32_t totalBits = 0;
    for (char ch : bitfieldHex) {
        const int v = HexDigitValue(ch);
        if (v < 0) {
            return 0.0f;
        }
        setBits += static_cast<std::uint32_t>(std::popcount(static_cast<unsigned int>(v)));
        totalBits += 4;
    }
    const std::uint32_t denom = (numPieces > 0 && numPieces <= totalBits) ? numPieces : totalBits;
    if (denom == 0) {
        return 0.0f;
    }
    const std::uint32_t clamped = std::min(setBits, denom);
    return static_cast<float>(clamped) / static_cast<float>(denom);
}

}  // namespace motrix::domain
