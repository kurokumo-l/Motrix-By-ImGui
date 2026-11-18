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
import :http_winhttp;

namespace motrix::engine::detail {

namespace {

inline constexpr std::size_t kReadChunkBytes = 65536u;

class UniqueWinHttpHandle {
public:
    UniqueWinHttpHandle() noexcept = default;
    explicit UniqueWinHttpHandle(HINTERNET h) noexcept : handle_(h) {}
    ~UniqueWinHttpHandle() { Reset(); }

    UniqueWinHttpHandle(const UniqueWinHttpHandle&) = delete;
    UniqueWinHttpHandle& operator=(const UniqueWinHttpHandle&) = delete;

    UniqueWinHttpHandle(UniqueWinHttpHandle&& other) noexcept : handle_(other.Release()) {}
    UniqueWinHttpHandle& operator=(UniqueWinHttpHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    HINTERNET Release() noexcept { return std::exchange(handle_, nullptr); }

    void Reset(HINTERNET next = nullptr) noexcept {
        if (handle_ != nullptr) {
            ::WinHttpCloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HINTERNET handle_ = nullptr;
};

struct CancelableWinHttp {
    std::mutex mu;
    HINTERNET handle = nullptr;
    explicit CancelableWinHttp(UniqueWinHttpHandle& u) noexcept : handle(u.Release()) {}
    ~CancelableWinHttp() { Close(); }
    HINTERNET Raw() noexcept {
        std::scoped_lock lk(mu);
        return handle;
    }
    HINTERNET Detach() noexcept {
        std::scoped_lock lk(mu);
        return std::exchange(handle, nullptr);
    }
    void Close() noexcept {
        if (HINTERNET raw = Detach()) {
            ::WinHttpCloseHandle(raw);
        }
    }
};

[[nodiscard]] std::string FormatWinHttpError(DWORD err) {
    if (err == ERROR_SUCCESS) {
        return "unknown error";
    }
    LPWSTR rawMsg = nullptr;
    DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const HMODULE winhttpMod = ::GetModuleHandleW(L"winhttp.dll");
    flags |= (winhttpMod != nullptr) ? FORMAT_MESSAGE_FROM_HMODULE : 0u;
    const DWORD len =
        ::FormatMessageW(flags, winhttpMod, err, 0, reinterpret_cast<LPWSTR>(&rawMsg), 0, nullptr);
    const motrix::core::UniqueLocalPtr<wchar_t> guard(rawMsg);
    if (len == 0 || !guard) {
        return std::format("WinHTTP error {}", err);
    }
    const std::string utf8 = motrix::core::WideToUtf8(std::wstring_view(guard.get(), len));
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

struct CrackedHttpUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_SCHEME scheme = INTERNET_SCHEME_HTTP;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
};

[[nodiscard]] std::string FormatUrlErr(std::string_view op, std::string_view url) {
    return std::format("{} failed for '{}': {}", op, url, FormatWinHttpError(::GetLastError()));
}

[[nodiscard]] std::expected<CrackedHttpUrl, std::string> CrackHttpUrl(std::string_view trimmedUrl) {
    const std::wstring urlW = motrix::core::Utf8ToWide(trimmedUrl);
    if (urlW.empty()) {
        return std::unexpected(std::format("Invalid UTF-8 URL: {}", trimmedUrl));
    }
    CrackedHttpUrl out{};
    out.host.resize(256, L'\0');
    out.path.resize((std::max<std::size_t>)(urlW.size() + 1, 2048), L'\0');
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = out.host.data();
    uc.dwHostNameLength = static_cast<DWORD>(out.host.size());
    uc.lpszUrlPath = out.path.data();
    uc.dwUrlPathLength = static_cast<DWORD>(out.path.size());
    if (!::WinHttpCrackUrl(urlW.c_str(), static_cast<DWORD>(urlW.size()), 0, &uc)) {
        return std::unexpected(FormatUrlErr("Invalid URL", trimmedUrl));
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::unexpected(std::format("Unsupported scheme for '{}'", trimmedUrl));
    }
    out.host.resize(uc.dwHostNameLength);
    out.path.resize(uc.dwUrlPathLength);
    if (out.host.empty()) {
        return std::unexpected(std::format("Missing host in URL '{}'", trimmedUrl));
    }
    if (out.path.empty()) {
        out.path = L"/";
    }
    out.scheme = uc.nScheme;
    out.port = uc.nPort;
    return out;
}

[[nodiscard]] std::expected<UniqueWinHttpHandle, std::string>
CreateConfiguredSession(std::string_view proxyUrl, std::chrono::milliseconds timeout) {
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
        return std::unexpected(std::format("Failed to initialize WinHTTP session: {}",
                                           FormatWinHttpError(::GetLastError())));
    }
    const int timeoutMs = static_cast<int>(std::clamp<std::int64_t>(timeout.count(), 100, 600000));
    ::WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    return session;
}

struct OpenedHttpGet {
    UniqueWinHttpHandle connect;
    UniqueWinHttpHandle request;
};

[[nodiscard]] std::expected<OpenedHttpGet, std::string> OpenAndSendHttpGet(HINTERNET session,
                                                                           std::string_view url,
                                                                           std::string_view acceptHeader,
                                                                           std::stop_token st = {}) {
    if (st.stop_requested()) {
        return std::unexpected(std::string("Cancelled"));
    }
    const std::string_view trimmedUrl = motrix::core::TrimAscii(url);
    if (trimmedUrl.empty()) {
        return std::unexpected(std::string("Empty HTTP URL"));
    }
    const auto cracked = CrackHttpUrl(trimmedUrl);
    if (!cracked) {
        return std::unexpected(cracked.error());
    }
    UniqueWinHttpHandle connect(::WinHttpConnect(session, cracked->host.c_str(), cracked->port, 0));
    if (!connect) {
        return std::unexpected(FormatUrlErr("Connect", trimmedUrl));
    }
    const DWORD reqFlags = (cracked->scheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0u;
    UniqueWinHttpHandle request(::WinHttpOpenRequest(connect.Get(),
                                                     L"GET",
                                                     cracked->path.c_str(),
                                                     nullptr,
                                                     WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     reqFlags));
    if (!request) {
        return std::unexpected(FormatUrlErr("OpenRequest", trimmedUrl));
    }
    if (!acceptHeader.empty()) {
        const std::wstring hdrW = motrix::core::Utf8ToWide(std::format("Accept: {}\r\n", acceptHeader));
        if (!hdrW.empty()) {
            ::WinHttpAddRequestHeaders(
                request.Get(), hdrW.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
        }
    }
    {
        CancelableWinHttp cancelReq(request);
        std::stop_callback onStop(st, [&] { cancelReq.Close(); });
        const HINTERNET hReq = cancelReq.Raw();
        const bool sent = ::WinHttpSendRequest(
                              hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                          && ::WinHttpReceiveResponse(hReq, nullptr);
        request.Reset(cancelReq.Detach());
        if (st.stop_requested()) {
            return std::unexpected(std::string("Cancelled"));
        }
        if (!sent || !request) {
            return std::unexpected(FormatUrlErr("HTTP request", trimmedUrl));
        }
    }
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!::WinHttpQueryHeaders(request.Get(),
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &statusCode,
                               &statusSize,
                               WINHTTP_NO_HEADER_INDEX)) {
        return std::unexpected(FormatUrlErr("QueryHeaders", trimmedUrl));
    }
    if (statusCode < 200u || statusCode >= 300u) {
        return std::unexpected(std::format("HTTP {} for '{}'", statusCode, trimmedUrl));
    }
    return OpenedHttpGet{std::move(connect), std::move(request)};
}

struct ReadBodyOptions {
    std::string_view url;
    std::size_t maxBytes = 0;
    bool failOnLimit = false;
    std::stop_token stopToken;
};

[[nodiscard]] std::expected<std::size_t, std::string>
StreamWinHttpBody(CancelableWinHttp& cancelReq,
                  const ReadBodyOptions& opt,
                  const std::function<bool(std::string_view)>& onChunk) {
    std::size_t totalBytes = 0;
    std::string chunk(kReadChunkBytes, '\0');
    for (;;) {
        if (opt.stopToken.stop_requested()) {
            return std::unexpected(std::string("Request cancelled"));
        }
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(cancelReq.Raw(), &avail)) {
            return std::unexpected(opt.stopToken.stop_requested()
                                       ? std::string("Request cancelled")
                                       : FormatUrlErr("QueryDataAvailable", opt.url));
        }
        if (avail == 0) {
            break;
        }
        if (totalBytes >= opt.maxBytes) {
            if (opt.failOnLimit) {
                return std::unexpected(std::format(
                    "Response exceeded maximum limit ({} bytes) for '{}'", opt.maxBytes, opt.url));
            }
            break;
        }
        const std::size_t rem = opt.failOnLimit ? chunk.size() : (opt.maxBytes - totalBytes);
        const DWORD toRead = static_cast<DWORD>(
            (std::min<std::size_t>)(avail, (std::min<std::size_t>)(rem, chunk.size())));
        DWORD bytesRead = 0;
        if (!::WinHttpReadData(cancelReq.Raw(), chunk.data(), toRead, &bytesRead)) {
            return std::unexpected(opt.stopToken.stop_requested() ? std::string("Request cancelled")
                                                                  : FormatUrlErr("ReadData", opt.url));
        }
        if (bytesRead == 0) {
            break;
        }
        totalBytes += bytesRead;
        if (opt.failOnLimit && totalBytes > opt.maxBytes) {
            return std::unexpected(std::format(
                "Response exceeded maximum limit ({} bytes) for '{}'", opt.maxBytes, opt.url));
        }
        if (!onChunk(std::string_view(chunk.data(), bytesRead))) {
            return std::unexpected(std::string("Failed to write response data"));
        }
    }
    return totalBytes;
}

[[nodiscard]] std::expected<std::string, std::string>
ReadWinHttpResponseBody(CancelableWinHttp& cancelReq, const ReadBodyOptions& opt) {
    std::string body;
    auto total = StreamWinHttpBody(cancelReq, opt, [&](std::string_view part) {
        body.append(part);
        return true;
    });
    if (!total) {
        return std::unexpected(total.error());
    }
    return body;
}

using motrix::core::ErrorCode;
using motrix::core::ErrorDomain;
using motrix::core::MakeError;

[[nodiscard]] auto MakeEngineError(ErrorCode code, std::string msg, std::string detail = {}) {
    return std::unexpected(MakeError(ErrorDomain::Engine, code, std::move(msg), std::move(detail)));
}

struct HttpSessionRequest {
    UniqueWinHttpHandle session;
    OpenedHttpGet opened;
};

[[nodiscard]] motrix::core::Result<HttpSessionRequest> OpenHttpSessionGet(const HttpRequestSpec& spec) {
    auto session = CreateConfiguredSession(spec.proxyUrl, spec.timeout);
    if (!session) {
        return MakeEngineError(ErrorCode::IoFailure, session.error());
    }
    auto opened = OpenAndSendHttpGet(session->Get(), spec.url, spec.acceptHeader, spec.stopToken);
    if (!opened) {
        return MakeEngineError(ErrorCode::ConnectionFailed, opened.error());
    }
    return HttpSessionRequest{std::move(*session), std::move(*opened)};
}

}  // namespace

motrix::core::Result<std::string> HttpGetUtf8OrBytes(const HttpRequestSpec& spec) {
    auto req = OpenHttpSessionGet(spec);
    if (!req) {
        return std::unexpected(req.error());
    }
    CancelableWinHttp cancelReq(req->opened.request);
    std::stop_callback onStop(spec.stopToken, [&] { cancelReq.Close(); });
    auto body = ReadWinHttpResponseBody(
        cancelReq,
        ReadBodyOptions{motrix::core::TrimAscii(spec.url), spec.maxBytes, true, spec.stopToken});
    if (!body) {
        return MakeEngineError(ErrorCode::IoFailure, body.error());
    }
    return *body;
}

motrix::core::Result<std::size_t> HttpDownloadToFile(const HttpRequestSpec& spec,
                                                     const std::filesystem::path& destPath) {
    auto req = OpenHttpSessionGet(spec);
    if (!req) {
        return std::unexpected(req.error());
    }
    std::error_code ec;
    if (destPath.has_parent_path()) {
        std::filesystem::create_directories(destPath.parent_path(), ec);
    }
    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return MakeEngineError(ErrorCode::IoFailure, "Failed to create staging download file");
    }
    CancelableWinHttp cancelReq(req->opened.request);
    std::stop_callback onStop(spec.stopToken, [&] { cancelReq.Close(); });
    auto totalBytes = StreamWinHttpBody(
        cancelReq,
        ReadBodyOptions{motrix::core::TrimAscii(spec.url), spec.maxBytes, true, spec.stopToken},
        [&](std::string_view part) {
            out.write(part.data(), static_cast<std::streamsize>(part.size()));
            return static_cast<bool>(out);
        });
    if (!totalBytes) {
        out.close();
        std::filesystem::remove(destPath, ec);
        const auto code =
            spec.stopToken.stop_requested() ? ErrorCode::Timeout : ErrorCode::ConnectionFailed;
        return MakeEngineError(code, totalBytes.error());
    }
    out.flush();
    if (!out) {
        out.close();
        std::filesystem::remove(destPath, ec);
        return MakeEngineError(ErrorCode::IoFailure, "Failed to flush downloaded staging file");
    }
    return *totalBytes;
}

motrix::core::Result<std::vector<HttpBatchItem>> HttpGetBatchUtf8(const HttpBatchRequestSpec& spec) {
    auto session = CreateConfiguredSession(spec.proxyUrl, spec.timeout);
    if (!session) {
        return MakeEngineError(
            ErrorCode::IoFailure, "Failed to initialize WinHTTP session", session.error());
    }
    std::vector<HttpBatchItem> items;
    items.reserve(spec.urls.size());
    for (const auto& url : spec.urls) {
        if (spec.stopToken.stop_requested()) {
            break;
        }
        auto opened = OpenAndSendHttpGet(session->Get(), url, {}, spec.stopToken);
        if (!opened) {
            items.push_back(HttpBatchItem{.error = opened.error()});
            continue;
        }
        CancelableWinHttp cancelReq(opened->request);
        std::stop_callback onStop(spec.stopToken, [&] { cancelReq.Close(); });
        auto body = ReadWinHttpResponseBody(
            cancelReq,
            ReadBodyOptions{
                motrix::core::TrimAscii(url), spec.maxBytes, spec.failOnLimit, spec.stopToken});
        if (body) {
            items.push_back(HttpBatchItem{.body = std::move(*body)});
        } else {
            items.push_back(HttpBatchItem{.error = body.error()});
        }
    }
    return items;
}

}  // namespace motrix::engine::detail
