module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
// clang-format on

module motrix.engine;

import std;
import motrix.core;

namespace motrix::engine {

namespace {

inline constexpr std::size_t kMaxTrackerPayloadBytes = 2u * 1024u * 1024u;
inline constexpr std::size_t kReadChunkBytes = 65536u;

class UniqueWinHttpHandle {
public:
    UniqueWinHttpHandle() noexcept = default;
    explicit UniqueWinHttpHandle(HINTERNET h) noexcept : handle_(h) {}
    ~UniqueWinHttpHandle() { Reset(); }

    UniqueWinHttpHandle(const UniqueWinHttpHandle&) = delete;
    UniqueWinHttpHandle& operator=(const UniqueWinHttpHandle&) = delete;

    UniqueWinHttpHandle(UniqueWinHttpHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    UniqueWinHttpHandle& operator=(UniqueWinHttpHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    void Reset(HINTERNET next = nullptr) noexcept {
        if (handle_ != nullptr) {
            ::WinHttpCloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HINTERNET handle_ = nullptr;
};

[[nodiscard]] std::string FormatWinHttpError(DWORD err) {
    if (err == ERROR_SUCCESS) {
        return "unknown error";
    }
    LPWSTR rawMsg = nullptr;
    DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const HMODULE winhttpMod = ::GetModuleHandleW(L"winhttp.dll");
    if (winhttpMod != nullptr) {
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
    }
    const DWORD len =
        ::FormatMessageW(flags, winhttpMod, err, 0, reinterpret_cast<LPWSTR>(&rawMsg), 0, nullptr);
    struct LocalFreeGuard {
        LPWSTR ptr = nullptr;
        ~LocalFreeGuard() {
            if (ptr != nullptr) {
                ::LocalFree(ptr);
            }
        }
    } guard{rawMsg};

    if (len == 0 || rawMsg == nullptr) {
        return std::format("WinHTTP error {}", err);
    }
    std::string utf8 = motrix::core::WideToUtf8(std::wstring_view(rawMsg, len));
    return std::format("{} (code {})", motrix::core::TrimAscii(utf8), err);
}

[[nodiscard]] std::string NormalizeWinHttpProxy(std::string_view proxyUrl) {
    std::string_view trimmed = motrix::core::TrimAscii(proxyUrl);
    if (trimmed.empty()) {
        return {};
    }
    const std::string lower = motrix::core::AsciiLower(trimmed);
    if (lower.starts_with("http://")) {
        trimmed.remove_prefix(7);
    } else if (lower.starts_with("https://")) {
        trimmed.remove_prefix(8);
    }
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.remove_suffix(1);
    }
    return std::string(trimmed);
}

[[nodiscard]] std::expected<std::string, std::string> FetchSingleTrackerUrl(HINTERNET session,
                                                                            std::string_view url) {
    const std::string_view trimmedUrl = motrix::core::TrimAscii(url);
    if (trimmedUrl.empty()) {
        return std::unexpected(std::string("Empty tracker URL"));
    }

    const std::wstring urlW = motrix::core::Utf8ToWide(trimmedUrl);
    if (urlW.empty()) {
        return std::unexpected(std::format("Invalid UTF-8 URL: {}", trimmedUrl));
    }

    std::wstring host(256, L'\0');
    std::wstring path((std::max<std::size_t>)(urlW.size() + 1, 2048), L'\0');

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = static_cast<DWORD>(host.size());
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = static_cast<DWORD>(path.size());

    if (!::WinHttpCrackUrl(urlW.c_str(), static_cast<DWORD>(urlW.size()), 0, &uc)) {
        return std::unexpected(
            std::format("Invalid URL '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::unexpected(std::format("Unsupported scheme for '{}'", trimmedUrl));
    }

    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);
    if (host.empty()) {
        return std::unexpected(std::format("Missing host in URL '{}'", trimmedUrl));
    }
    if (path.empty()) {
        path = L"/";
    }

    UniqueWinHttpHandle connect(::WinHttpConnect(session, host.c_str(), uc.nPort, 0));
    if (!connect) {
        return std::unexpected(std::format(
            "Connect failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }

    const DWORD reqFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0u;
    UniqueWinHttpHandle request(::WinHttpOpenRequest(connect.Get(),
                                                     L"GET",
                                                     path.c_str(),
                                                     nullptr,
                                                     WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     reqFlags));
    if (!request) {
        return std::unexpected(std::format(
            "OpenRequest failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }

    if (!::WinHttpSendRequest(
            request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return std::unexpected(std::format(
            "SendRequest failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }

    if (!::WinHttpReceiveResponse(request.Get(), nullptr)) {
        return std::unexpected(std::format(
            "ReceiveResponse failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!::WinHttpQueryHeaders(request.Get(),
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &statusCode,
                               &statusSize,
                               WINHTTP_NO_HEADER_INDEX)) {
        return std::unexpected(std::format(
            "QueryHeaders failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
    }
    if (statusCode < 200u || statusCode >= 300u) {
        return std::unexpected(std::format("HTTP {} for '{}'", statusCode, trimmedUrl));
    }

    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(request.Get(), &avail)) {
            return std::unexpected(std::format("QueryDataAvailable failed for '{}': {}",
                                               trimmedUrl,
                                               FormatWinHttpError(::GetLastError())));
        }
        if (avail == 0) {
            break;
        }
        const std::size_t remaining = kMaxTrackerPayloadBytes - body.size();
        if (remaining == 0) {
            break;
        }
        const DWORD toRead = static_cast<DWORD>(
            (std::min<std::size_t>)(avail, (std::min<std::size_t>)(remaining, kReadChunkBytes)));
        const std::size_t oldSize = body.size();
        body.resize(oldSize + toRead);

        DWORD bytesRead = 0;
        if (!::WinHttpReadData(request.Get(), body.data() + oldSize, toRead, &bytesRead)) {
            return std::unexpected(std::format(
                "ReadData failed for '{}': {}", trimmedUrl, FormatWinHttpError(::GetLastError())));
        }
        body.resize(oldSize + bytesRead);
        if (bytesRead == 0) {
            break;
        }
    }

    return body;
}

}  // namespace

motrix::core::Result<TrackerFetchResult> FetchTrackersFromUrls(std::span<const std::string> urls,
                                                               std::string_view proxyUrl,
                                                               std::chrono::milliseconds timeout) {
    TrackerFetchResult result{};
    if (urls.empty()) {
        return result;
    }

    const std::string normalizedProxy = NormalizeWinHttpProxy(proxyUrl);
    const std::wstring proxyW = motrix::core::Utf8ToWide(normalizedProxy);
    const DWORD accessType =
        proxyW.empty() ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY;

    UniqueWinHttpHandle session(::WinHttpOpen(L"MotrixNative/1.0",
                                              accessType,
                                              proxyW.empty() ? WINHTTP_NO_PROXY_NAME : proxyW.c_str(),
                                              WINHTTP_NO_PROXY_BYPASS,
                                              0));
    if (!session) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to initialize WinHTTP session",
                                                       FormatWinHttpError(::GetLastError())));
    }

    const int timeoutMs = static_cast<int>(std::clamp<std::int64_t>(timeout.count(), 100, 600000));
    ::WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    for (const auto& url : urls) {
        auto fetched = FetchSingleTrackerUrl(session.Get(), url);
        if (fetched) {
            if (!fetched->empty()) {
                result.rawCombinedText.append(*fetched);
                if (result.rawCombinedText.back() != '\n') {
                    result.rawCombinedText.push_back('\n');
                }
            }
            ++result.succeededSources;
        } else {
            result.errors.push_back(std::move(fetched.error()));
        }
    }

    if (result.succeededSources == 0 && !result.errors.empty()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ConnectionFailed,
                                                       result.errors.front()));
    }
    return result;
}

}  // namespace motrix::engine
