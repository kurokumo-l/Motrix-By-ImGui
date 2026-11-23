module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on

module motrix.test_server_util;

import std;
import motrix.core;

namespace motrix::test {
namespace {

std::mutex gWsaMu;
int gWsaRefs = 0;
std::atomic<std::uint64_t> gTempDirSeq{0};

[[nodiscard]] std::uint16_t PickFreePortImpl(int sockType, int protocol) {
    WsaLease lease;
    SOCKET s = ::socket(AF_INET, sockType, protocol);
    if (s == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    std::uint16_t port = 0;
    socklen_t len = sizeof(addr);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0
        && ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ::ntohs(addr.sin_port);
    }
    ::closesocket(s);
    return port;
}

[[nodiscard]] std::uint8_t HexVal(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    return 0;
}

}  // namespace

WsaLease::WsaLease() {
    std::scoped_lock lock(gWsaMu);
    if (gWsaRefs++ == 0) {
        WSADATA wsa{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
    }
}

WsaLease::~WsaLease() {
    std::scoped_lock lock(gWsaMu);
    if (--gWsaRefs == 0) {
        ::WSACleanup();
    }
}

std::vector<std::uint8_t> HexToBytes(std::string_view hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((HexVal(hex[i]) << 4) | HexVal(hex[i + 1])));
    }
    return out;
}

std::string UrlDecodeRaw(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back(static_cast<char>((HexVal(s[i + 1]) << 4) | HexVal(s[i + 2])));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> ParseQueryRaw(std::string_view q) {
    std::unordered_map<std::string, std::string> m;
    std::size_t pos = 0;
    while (pos <= q.size()) {
        const std::size_t amp = q.find('&', pos);
        const std::string_view pair =
            (amp == std::string_view::npos) ? q.substr(pos) : q.substr(pos, amp - pos);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            m[UrlDecodeRaw(pair.substr(0, eq))] = UrlDecodeRaw(pair.substr(eq + 1));
        }
        if (amp == std::string_view::npos) {
            break;
        }
        pos = amp + 1;
    }
    return m;
}

bool SendAll(std::uintptr_t sock, const char* data, std::size_t len) {
    const auto s = static_cast<SOCKET>(sock);
    std::size_t sent = 0;
    while (sent < len) {
        const int n = ::send(s, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool SendText(std::uintptr_t sock, std::string_view sv) {
    return SendAll(sock, sv.data(), sv.size());
}

void AppendBencodeStr(std::vector<std::uint8_t>& dst, std::string_view s) {
    const std::string pfx = std::format("{}:", s.size());
    dst.insert(dst.end(), pfx.begin(), pfx.end());
    dst.insert(dst.end(), s.begin(), s.end());
}

void AppendBencodeInt(std::vector<std::uint8_t>& dst, std::uint64_t v) {
    const std::string tok = std::format("i{}e", v);
    dst.insert(dst.end(), tok.begin(), tok.end());
}

ParsedRange ParseByteRange(std::string_view hdr, std::size_t total) {
    if (!hdr.starts_with("bytes=") || total == 0) {
        return {};
    }
    std::string_view spec = hdr.substr(6);
    if (const auto comma = spec.find(','); comma != std::string_view::npos) {
        spec = spec.substr(0, comma);
    }
    const auto dash = spec.find('-');
    if (dash == std::string_view::npos) {
        return {};
    }
    const auto sPart = motrix::core::TrimAscii(spec.substr(0, dash));
    const auto ePart = motrix::core::TrimAscii(spec.substr(dash + 1));
    std::size_t start = 0;
    std::size_t end = total - 1;
    if (sPart.empty()) {
        std::size_t suffix = 0;
        const auto [p, ec] = std::from_chars(ePart.data(), ePart.data() + ePart.size(), suffix);
        if (ec != std::errc{} || p != ePart.data() + ePart.size() || suffix == 0) {
            return {};
        }
        start = (suffix >= total) ? 0 : (total - suffix);
    } else {
        const auto [p1, ec1] = std::from_chars(sPart.data(), sPart.data() + sPart.size(), start);
        if (ec1 != std::errc{} || p1 != sPart.data() + sPart.size()) {
            return {};
        }
        if (!ePart.empty()) {
            const auto [p2, ec2] = std::from_chars(ePart.data(), ePart.data() + ePart.size(), end);
            if (ec2 != std::errc{} || p2 != ePart.data() + ePart.size()) {
                return {};
            }
            end = std::min(end, total - 1);
        }
    }
    if (start > end || start >= total) {
        return {};
    }
    return {true, start, end};
}

std::uint16_t PickFreeLoopbackPort() {
    return PickFreePortImpl(SOCK_STREAM, IPPROTO_TCP);
}

std::uint16_t PickFreeUdpLoopbackPort() {
    return PickFreePortImpl(SOCK_DGRAM, IPPROTO_UDP);
}

std::filesystem::path MakeUniqueTempDir(std::string_view prefix) {
    const std::uint64_t seq = gTempDirSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto pid = static_cast<std::uint32_t>(::GetCurrentProcessId());
    const auto dir = std::filesystem::temp_directory_path() / std::format("{}_{}_{}", prefix, pid, seq);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool ShouldRequireLiveTests() {
    wchar_t buf[32]{};
    const DWORD n = ::GetEnvironmentVariableW(L"MOTRIX_REQUIRE_LIVE_TESTS", buf, 32);
    if (n == 0 || n >= 32) {
        return false;
    }
    const std::wstring_view val(buf, n);
    return val == L"1" || val == L"true" || val == L"TRUE";
}

}  // namespace motrix::test
