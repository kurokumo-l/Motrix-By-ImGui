module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

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
    std::string out;
    out.reserve((input.size() * 3) / 4);
    int val = 0;
    int valb = -8;
    for (unsigned char ch : input) {
        if (ch == '=') {
            break;
        }
        const int d = Base64Value(ch);
        if (d < 0) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Domain,
                                                           motrix::core::ErrorCode::ParseFailure,
                                                           "Invalid base64 character in thunder URI"));
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
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

[[nodiscard]] std::string UrlDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
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

[[nodiscard]] bool StartsWithAsciiInsensitive(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char a = text[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool EndsWithAsciiInsensitive(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    return StartsWithAsciiInsensitive(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] std::string_view TrimStartupArg(std::string_view text) noexcept {
    text = motrix::core::TrimAscii(text);
    while (text.size() >= 2
           && ((text.front() == '"' && text.back() == '"')
               || (text.front() == '\'' && text.back() == '\''))) {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = motrix::core::TrimAscii(text);
    }
    while (!text.empty() && text.front() == '"') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    return motrix::core::TrimAscii(text);
}

[[nodiscard]] bool IsMotrixDeepLinkScheme(std::string_view text, std::size_t& prefixLen) noexcept {
    if (StartsWithAsciiInsensitive(text, "motrix://")) {
        prefixLen = 9;
        return true;
    }
    if (StartsWithAsciiInsensitive(text, "mo://")) {
        prefixLen = 5;
        return true;
    }
    return false;
}

}  // namespace

motrix::core::Result<std::string> DecodeThunderUri(std::string_view uri) {
    const std::string_view trimmed = motrix::core::TrimAscii(uri);
    const std::string lowerPrefix = motrix::core::AsciiLower(trimmed.substr(0, 10));
    if (lowerPrefix != "thunder://") {
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
    return decoded->substr(2, decoded->size() - 4);
}

std::vector<std::string> SplitAndNormalizeUris(std::string_view rawInput) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < rawInput.size()) {
        const std::size_t nl = rawInput.find('\n', pos);
        const std::size_t end = (nl == std::string_view::npos) ? rawInput.size() : nl;
        const std::string_view line = motrix::core::TrimAscii(rawInput.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty()) {
            continue;
        }
        auto decoded = DecodeThunderUri(line);
        if (decoded && !decoded->empty()) {
            result.push_back(std::move(*decoded));
        } else {
            result.emplace_back(line);
        }
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
    if (!IsMagnetUri(uri)) {
        return {};
    }
    const std::size_t q = uri.find('?');
    if (q == std::string_view::npos) {
        return {};
    }
    std::string_view query = uri.substr(q + 1);
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view part = (amp == std::string_view::npos) ? query : query.substr(0, amp);
        if (part.starts_with("dn=")) {
            return UrlDecode(part.substr(3));
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
    if (trimmed.empty()) {
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
    if (!IsMotrixDeepLinkScheme(trimmed, prefixLen)) {
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
                out.uriParam = UrlDecode(part.substr(eq + 1));
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
    const std::string_view cleaned = TrimStartupArg(arg);
    if (cleaned.empty() || cleaned.starts_with('-')) {
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
        out.kind = StartupArgKind::ResourceUri;
        if (StartsWithAsciiInsensitive(cleaned, "thunder://")) {
            auto decoded = DecodeThunderUri(cleaned);
            if (decoded && !decoded->empty()) {
                out.uri = std::move(*decoded);
                return out;
            }
        }
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
