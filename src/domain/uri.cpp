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

}  // namespace motrix::domain
