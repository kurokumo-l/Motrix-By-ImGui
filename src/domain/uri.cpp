module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

[[nodiscard]] bool HasAsciiControlChars(std::string_view text) noexcept {
    for (const unsigned char ch : text) {
        if (ch < 0x20u || ch == 0x7Fu) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int Base64Value(unsigned char ch) noexcept {
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 52;
    if (ch == '+' || ch == '-')
        return 62;
    if (ch == '/' || ch == '_')
        return 63;
    return -1;
}

[[nodiscard]] motrix::core::Result<std::string> DecodeBase64(std::string_view input) {
    std::size_t padCount = 0;
    while (padCount < input.size() && input[input.size() - 1u - padCount] == '=') {
        ++padCount;
    }
    if (padCount > 2u || (padCount > 0u && input.size() % 4u != 0u)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid base64 padding in thunder URI"));
    }

    const std::string_view payload = input.substr(0, input.size() - padCount);
    const std::size_t rem = payload.size() % 4u;
    if (rem == 1u || (padCount > 0u && padCount != (4u - rem))) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid base64 length in thunder URI"));
    }

    std::string out;
    out.reserve((payload.size() / 4u) * 3u + 2u);
    std::uint32_t val = 0;
    int valb = -8;
    for (const unsigned char ch : payload) {
        const int d = Base64Value(ch);
        if (d < 0) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                           motrix::core::ErrorCode::ParseFailure,
                                                           "Invalid base64 character in thunder URI"));
        }
        val = (val << 6) | static_cast<std::uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFFu));
            val &= (valb == 0) ? 0u : ((1u << valb) - 1u);
            valb -= 8;
        }
    }
    if (val != 0u) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Non-zero base64 padding bits in thunder URI"));
    }
    return out;
}

[[nodiscard]] int HexDigit(char ch) noexcept {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string PercentDecodeImpl(std::string_view text, bool plusAsSpace) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (plusAsSpace && text[i] == '+') {
            out.push_back(' ');
            continue;
        }
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = HexDigit(text[i + 1]);
            const int lo = HexDigit(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

[[nodiscard]] std::string UrlDecode(std::string_view text) {
    return PercentDecodeImpl(text, true);
}

[[nodiscard]] std::string PercentDecodeUri(std::string_view text) {
    return PercentDecodeImpl(text, false);
}

[[nodiscard]] std::string StripControlChars(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        if (ch >= 0x20u && ch != 0x7Fu) {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

[[nodiscard]] bool IsMotrixDeepLinkScheme(std::string_view text, std::size_t& prefixLen) noexcept {
    if (motrix::core::StartsWithAsciiInsensitive(text, "motrix://")) {
        prefixLen = 9;
        return true;
    }
    if (motrix::core::StartsWithAsciiInsensitive(text, "mo://")) {
        prefixLen = 5;
        return true;
    }
    return false;
}

}  // namespace

using motrix::core::EndsWithAsciiInsensitive;
using motrix::core::StartsWithAsciiInsensitive;

motrix::core::Result<std::string> DecodeThunderUri(std::string_view uri) {
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    if (HasAsciiControlChars(trimmed)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "URI contains control characters"));
    }
    if (!StartsWithAsciiInsensitive(trimmed, "thunder://")) {
        return std::string(trimmed);
    }

    auto decoded = DecodeBase64(trimmed.substr(10));
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    if (decoded->size() < 4 || decoded->substr(0, 2) != "AA"
        || decoded->substr(decoded->size() - 2) != "ZZ") {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Thunder payload missing AA/ZZ delimiters"));
    }
    const std::string inner = decoded->substr(2, decoded->size() - 4);
    const std::string_view innerTrimmed = motrix::core::TrimAscii(inner);
    if (HasAsciiControlChars(inner) || innerTrimmed.empty() || innerTrimmed.starts_with('-')
        || StartsWithAsciiInsensitive(innerTrimmed, "thunder://")
        || !IsSupportedDownloadUri(innerTrimmed)) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Thunder payload is not a supported download URI"));
    }
    return std::string(innerTrimmed);
}

std::vector<std::string> SplitAndNormalizeUris(std::string_view rawInput) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < rawInput.size()) {
        const std::size_t nl = rawInput.find('\n', pos);
        const std::size_t end = (nl == std::string_view::npos) ? rawInput.size() : nl;
        const std::string_view line = motrix::core::TrimAscii(rawInput.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || HasAsciiControlChars(line)) {
            continue;
        }
        if (StartsWithAsciiInsensitive(line, "thunder://")) {
            auto decoded = DecodeThunderUri(line);
            if (decoded && !decoded->empty()) {
                result.push_back(std::move(*decoded));
            }
            continue;
        }
        result.emplace_back(line);
    }
    return result;
}

bool IsMagnetUri(std::string_view uri) noexcept {
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    if (trimmed.size() < 8) {
        return false;
    }
    constexpr std::string_view kPrefix = "magnet:?";
    for (std::size_t i = 0; i < kPrefix.size(); ++i) {
        char c = trimmed[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != kPrefix[i]) {
            return false;
        }
    }
    return true;
}

std::string ExtractMagnetDisplayName(std::string_view uri) {
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    if (!IsMagnetUri(trimmed)) {
        return {};
    }
    const std::size_t q = trimmed.find('?');
    if (q == std::string_view::npos) {
        return {};
    }
    std::string_view query = trimmed.substr(q + 1);
    if (const std::size_t hashPos = query.find('#'); hashPos != std::string_view::npos) {
        query = query.substr(0, hashPos);
    }
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view part = (amp == std::string_view::npos) ? query : query.substr(0, amp);
        if (StartsWithAsciiInsensitive(part, "dn=")) {
            return StripControlChars(UrlDecode(part.substr(3)));
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query.remove_prefix(amp + 1);
    }
    return {};
}

bool IsSupportedDownloadUri(std::string_view uri) noexcept {
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    if (trimmed.empty() || HasAsciiControlChars(trimmed)) {
        return false;
    }
    constexpr std::array<std::string_view, 6> kSchemes = {
        "http://",
        "https://",
        "ftp://",
        "sftp://",
        "magnet:",
        "thunder://",
    };
    for (const auto scheme : kSchemes) {
        if (StartsWithAsciiInsensitive(trimmed, scheme)) {
            return true;
        }
    }
    return false;
}

ParsedDeepLink ParseMotrixDeepLink(std::string_view uri) {
    ParsedDeepLink out{};
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    std::size_t prefixLen = 0;
    if (HasAsciiControlChars(trimmed) || !IsMotrixDeepLinkScheme(trimmed, prefixLen)) {
        return out;
    }

    std::string_view rest = trimmed.substr(prefixLen);
    if (const std::size_t hashPos = rest.find('#'); hashPos != std::string_view::npos) {
        rest = rest.substr(0, hashPos);
    }

    std::string_view route = rest;
    std::string_view query;
    if (const std::size_t qPos = rest.find('?'); qPos != std::string_view::npos) {
        route = rest.substr(0, qPos);
        query = rest.substr(qPos + 1);
    }

    while (!route.empty() && route.front() == '/') {
        route.remove_prefix(1);
    }
    while (!route.empty() && route.back() == '/') {
        route.remove_suffix(1);
    }

    const std::string routeLower = motrix::core::AsciiLower(route);
    if (routeLower == "task-list" || routeLower == "tasks") {
        out.action = DeepLinkAction::TaskList;
    } else if (routeLower == "new-task") {
        out.action = DeepLinkAction::NewTask;
    } else if (routeLower == "new-bt-task") {
        out.action = DeepLinkAction::NewBtTask;
    } else if (routeLower == "pause-all-task" || routeLower == "pause-all") {
        out.action = DeepLinkAction::PauseAll;
    } else if (routeLower == "resume-all-task" || routeLower == "resume-all") {
        out.action = DeepLinkAction::ResumeAll;
    } else if (routeLower == "preferences" || routeLower == "prefs" || routeLower == "settings") {
        out.action = DeepLinkAction::Preferences;
    } else if (routeLower == "about") {
        out.action = DeepLinkAction::About;
    } else {
        return out;
    }

    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view part = (amp == std::string_view::npos) ? query : query.substr(0, amp);
        if (const std::size_t eq = part.find('='); eq != std::string_view::npos) {
            const std::string keyLower = motrix::core::AsciiLower(part.substr(0, eq));
            if (keyLower == "uri") {
                const std::string decodedUri = PercentDecodeUri(part.substr(eq + 1));
                const std::string_view decodedTrimmed = motrix::core::TrimAscii(decodedUri);
                if (!HasAsciiControlChars(decodedUri) && !decodedTrimmed.starts_with('-')
                    && IsSupportedDownloadUri(decodedTrimmed)) {
                    out.uriParam = std::string(decodedTrimmed);
                } else {
                    out.uriParam.clear();
                }
            }
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query.remove_prefix(amp + 1);
    }
    return out;
}

ClassifiedStartupArg ClassifyStartupArg(std::string_view arg) {
    ClassifiedStartupArg out{};
    const std::string_view cleaned = motrix::core::TrimQuotedArg(arg);
    if (cleaned.empty() || cleaned.starts_with('-') || HasAsciiControlChars(cleaned)) {
        return out;
    }

    std::size_t deepPrefixLen = 0;
    if (IsMotrixDeepLinkScheme(cleaned, deepPrefixLen)) {
        out.kind = StartupArgKind::DeepLink;
        out.uri = std::string(cleaned);
        out.deepLink = ParseMotrixDeepLink(cleaned);
        return out;
    }

    if (IsSupportedDownloadUri(cleaned)) {
        if (StartsWithAsciiInsensitive(cleaned, "thunder://")) {
            auto decoded = DecodeThunderUri(cleaned);
            if (!decoded || decoded->empty()) {
                return out;
            }
            out.kind = StartupArgKind::ResourceUri;
            out.uri = std::move(*decoded);
            return out;
        }
        out.kind = StartupArgKind::ResourceUri;
        out.uri = std::string(cleaned);
        return out;
    }

    if (EndsWithAsciiInsensitive(cleaned, ".torrent")) {
        out.kind = StartupArgKind::TorrentFile;
        out.filePath = motrix::core::PathFromUtf8(cleaned);
        return out;
    }

    return out;
}

}  // namespace motrix::domain
