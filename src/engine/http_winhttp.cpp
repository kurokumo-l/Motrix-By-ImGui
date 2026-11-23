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

struct AsyncHttpContext {
    motrix::core::UniqueHandle opEvent;
    motrix::core::UniqueHandle cancelEvent;
    motrix::core::UniqueHandle closedEvent;
    std::atomic<DWORD> opError{ERROR_SUCCESS};
    std::atomic<DWORD> availBytes{0};
    std::atomic<DWORD> bytesRead{0};
    std::array<char, kReadChunkBytes> readBuffer{};
};

void CALLBACK AsyncWinHttpStatusCallback(HINTERNET,
                                         DWORD_PTR dwContext,
                                         DWORD dwInternetStatus,
                                         LPVOID lpvStatusInformation,
                                         DWORD dwStatusInformationLength) {
    if (dwContext == 0) {
        return;
    }
    auto* ctx = reinterpret_cast<AsyncHttpContext*>(dwContext);
    switch (dwInternetStatus) {
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING: ::SetEvent(ctx->closedEvent.Get()); return;
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            ctx->opError.store(ERROR_SUCCESS, std::memory_order_release);
            ::SetEvent(ctx->opEvent.Get());
            return;
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: {
            const DWORD avail = (lpvStatusInformation && dwStatusInformationLength >= sizeof(DWORD))
                                    ? *static_cast<const DWORD*>(lpvStatusInformation)
                                    : 0u;
            ctx->availBytes.store(avail, std::memory_order_release);
            ctx->opError.store(ERROR_SUCCESS, std::memory_order_release);
            ::SetEvent(ctx->opEvent.Get());
            return;
        }
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            ctx->bytesRead.store(dwStatusInformationLength, std::memory_order_release);
            ctx->opError.store(ERROR_SUCCESS, std::memory_order_release);
            ::SetEvent(ctx->opEvent.Get());
            return;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
            const auto* res = static_cast<const WINHTTP_ASYNC_RESULT*>(lpvStatusInformation);
            const DWORD err = res ? res->dwError : ERROR_WINHTTP_OPERATION_CANCELLED;
            ctx->opError.store(err, std::memory_order_release);
            ::SetEvent(ctx->opEvent.Get());
            return;
        }
        default: return;
    }
}

class AsyncHttpRequest {
public:
    AsyncHttpRequest() = default;
    ~AsyncHttpRequest() {
        Cancel();
        if (callbackBound_ && ctx_ && ctx_->closedEvent.IsValid()) {
            ::WaitForSingleObject(ctx_->closedEvent.Get(), INFINITE);
        }
    }

    AsyncHttpRequest(const AsyncHttpRequest&) = delete;
    AsyncHttpRequest& operator=(const AsyncHttpRequest&) = delete;

    [[nodiscard]] bool Init(UniqueWinHttpHandle& rawReq) {
        auto ctx = std::make_unique<AsyncHttpContext>();
        ctx->opEvent.Reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        ctx->cancelEvent.Reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        ctx->closedEvent.Reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!ctx->opEvent.IsValid() || !ctx->cancelEvent.IsValid() || !ctx->closedEvent.IsValid()) {
            return false;
        }

        DWORD_PTR ctxVal = reinterpret_cast<DWORD_PTR>(ctx.get());
        if (!::WinHttpSetOption(rawReq.Get(), WINHTTP_OPTION_CONTEXT_VALUE, &ctxVal, sizeof(ctxVal))) {
            return false;
        }
        const WINHTTP_STATUS_CALLBACK prev = ::WinHttpSetStatusCallback(
            rawReq.Get(),
            &AsyncWinHttpStatusCallback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
            0);
        if (prev == WINHTTP_INVALID_STATUS_CALLBACK) {
            return false;
        }
        ctx_ = std::move(ctx);
        callbackBound_ = true;
        handle_ = rawReq.Release();
        return true;
    }

    HINTERNET BeginApiCall() noexcept {
        std::scoped_lock lk(mu_);
        if (closing_ || handle_ == nullptr) {
            return nullptr;
        }
        ++inFlightCalls_;
        return handle_;
    }

    void EndApiCall() noexcept {
        std::scoped_lock lk(mu_);
        if (--inFlightCalls_ == 0 && closing_) {
            cv_.notify_all();
        }
    }

    void Cancel() noexcept {
        HINTERNET toClose = nullptr;
        {
            std::unique_lock lk(mu_);
            closing_ = true;
            if (ctx_ && ctx_->cancelEvent.IsValid()) {
                ::SetEvent(ctx_->cancelEvent.Get());
            }
            cv_.wait(lk, [&] { return inFlightCalls_ == 0; });
            toClose = std::exchange(handle_, nullptr);
        }
        if (toClose != nullptr) {
            ::WinHttpCloseHandle(toClose);
        }
    }

    [[nodiscard]] AsyncHttpContext* Context() const noexcept { return ctx_.get(); }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::unique_ptr<AsyncHttpContext> ctx_;
    HINTERNET handle_ = nullptr;
    int inFlightCalls_ = 0;
    bool closing_ = false;
    bool callbackBound_ = false;
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

[[nodiscard]] std::expected<std::string, std::string> NormalizeWinHttpProxy(std::string_view proxyUrl) {
    std::string_view trimmed = motrix::core::TrimAscii(proxyUrl);
    if (trimmed.empty()) {
        return std::string{};
    }
    for (const char ch : trimmed) {
        if (static_cast<unsigned char>(ch) < 0x20u || ch == 0x7Fu) {
            return std::unexpected(std::string("Proxy URL contains invalid control characters"));
        }
    }
    const std::string lower = motrix::core::AsciiLower(trimmed);
    if (lower.starts_with("socks")) {
        return std::unexpected(std::string("SOCKS proxy is not supported by WinHTTP"));
    }
    if (lower.starts_with("http://")) {
        trimmed.remove_prefix(7);
    } else if (lower.starts_with("https://")) {
        trimmed.remove_prefix(8);
    }
    if (trimmed.find('@') != std::string_view::npos) {
        return std::unexpected(std::string("Proxy credentials in URL are not supported by WinHTTP"));
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

[[nodiscard]] std::string FormatUrlErr(std::string_view op, std::string_view url, DWORD err) {
    return std::format("{} failed for '{}': {}", op, url, FormatWinHttpError(err));
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
        return std::unexpected(FormatUrlErr("Invalid URL", trimmedUrl, ::GetLastError()));
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
    const auto normalizedProxy = NormalizeWinHttpProxy(proxyUrl);
    if (!normalizedProxy) {
        return std::unexpected(normalizedProxy.error());
    }
    const std::wstring proxyW = motrix::core::Utf8ToWide(*normalizedProxy);
    const DWORD accessType =
        proxyW.empty() ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY;

    UniqueWinHttpHandle session(::WinHttpOpen(L"MotrixNative/1.0",
                                              accessType,
                                              proxyW.empty() ? WINHTTP_NO_PROXY_NAME : proxyW.c_str(),
                                              WINHTTP_NO_PROXY_BYPASS,
                                              WINHTTP_FLAG_ASYNC));
    if (!session) {
        return std::unexpected(std::format("Failed to initialize WinHTTP session: {}",
                                           FormatWinHttpError(::GetLastError())));
    }
    const int timeoutMs = static_cast<int>(std::clamp<std::int64_t>(timeout.count(), 100, 600000));
    ::WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(
        session.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    return session;
}

template <typename Fn>
[[nodiscard]] DWORD ExecuteAsyncWinHttpOp(AsyncHttpRequest& req, std::stop_token st, Fn&& issueCall) {
    if (st.stop_requested()) {
        return ERROR_WINHTTP_OPERATION_CANCELLED;
    }
    AsyncHttpContext* ctx = req.Context();
    ::ResetEvent(ctx->opEvent.Get());
    HINTERNET h = req.BeginApiCall();
    if (!h) {
        return ERROR_WINHTTP_OPERATION_CANCELLED;
    }
    const BOOL queued = issueCall(h);
    const DWORD issueErr = queued ? ERROR_SUCCESS : ::GetLastError();
    req.EndApiCall();
    if (!queued) {
        return issueErr;
    }

    HANDLE waits[2] = {ctx->opEvent.Get(), ctx->cancelEvent.Get()};
    const DWORD wr = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (wr != WAIT_OBJECT_0 || st.stop_requested()) {
        return ERROR_WINHTTP_OPERATION_CANCELLED;
    }
    return ctx->opError.load(std::memory_order_acquire);
}

struct OpenedHttpGet {
    UniqueWinHttpHandle connect;
    std::unique_ptr<AsyncHttpRequest> request;
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
        return std::unexpected(FormatUrlErr("Connect", trimmedUrl, ::GetLastError()));
    }
    const DWORD reqFlags = (cracked->scheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0u;
    UniqueWinHttpHandle rawReq(::WinHttpOpenRequest(connect.Get(),
                                                    L"GET",
                                                    cracked->path.c_str(),
                                                    nullptr,
                                                    WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    reqFlags));
    if (!rawReq) {
        return std::unexpected(FormatUrlErr("OpenRequest", trimmedUrl, ::GetLastError()));
    }
    if (!acceptHeader.empty()) {
        const std::wstring hdrW = motrix::core::Utf8ToWide(std::format("Accept: {}\r\n", acceptHeader));
        if (!hdrW.empty()) {
            ::WinHttpAddRequestHeaders(
                rawReq.Get(), hdrW.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
        }
    }

    auto asyncReq = std::make_unique<AsyncHttpRequest>();
    if (!asyncReq->Init(rawReq)) {
        return std::unexpected(FormatUrlErr("AsyncInit", trimmedUrl, ::GetLastError()));
    }
    std::stop_callback onStop(st, [&] { asyncReq->Cancel(); });

    const DWORD sendErr = ExecuteAsyncWinHttpOp(*asyncReq, st, [&](HINTERNET h) {
        return ::WinHttpSendRequest(h,
                                    WINHTTP_NO_ADDITIONAL_HEADERS,
                                    0,
                                    WINHTTP_NO_REQUEST_DATA,
                                    0,
                                    0,
                                    reinterpret_cast<DWORD_PTR>(asyncReq->Context()));
    });
    if (sendErr != ERROR_SUCCESS) {
        return std::unexpected(st.stop_requested() ? std::string("Cancelled")
                                                   : FormatUrlErr("HTTP send", trimmedUrl, sendErr));
    }

    const DWORD recvErr = ExecuteAsyncWinHttpOp(
        *asyncReq, st, [](HINTERNET h) { return ::WinHttpReceiveResponse(h, nullptr); });
    if (recvErr != ERROR_SUCCESS) {
        return std::unexpected(st.stop_requested() ? std::string("Cancelled")
                                                   : FormatUrlErr("HTTP receive", trimmedUrl, recvErr));
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    HINTERNET h = asyncReq->BeginApiCall();
    const BOOL queryOk = h != nullptr
                         && ::WinHttpQueryHeaders(h,
                                                  WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                  WINHTTP_HEADER_NAME_BY_INDEX,
                                                  &statusCode,
                                                  &statusSize,
                                                  WINHTTP_NO_HEADER_INDEX);
    const DWORD queryErr = queryOk ? ERROR_SUCCESS : ::GetLastError();
    if (h != nullptr) {
        asyncReq->EndApiCall();
    }
    if (!queryOk) {
        return std::unexpected(st.stop_requested() ? std::string("Cancelled")
                                                   : FormatUrlErr("QueryHeaders", trimmedUrl, queryErr));
    }
    if (statusCode < 200u || statusCode >= 300u) {
        return std::unexpected(std::format("HTTP {} for '{}'", statusCode, trimmedUrl));
    }
    return OpenedHttpGet{std::move(connect), std::move(asyncReq)};
}

struct ReadBodyOptions {
    std::string_view url;
    std::size_t maxBytes = 0;
    bool failOnLimit = false;
    std::stop_token stopToken;
};

[[nodiscard]] std::expected<std::size_t, std::string>
StreamWinHttpBody(AsyncHttpRequest& req,
                  const ReadBodyOptions& opt,
                  const std::function<bool(std::string_view)>& onChunk) {
    std::size_t totalBytes = 0;
    AsyncHttpContext* ctx = req.Context();
    for (;;) {
        if (opt.stopToken.stop_requested()) {
            return std::unexpected(std::string("Request cancelled"));
        }
        const DWORD availErr = ExecuteAsyncWinHttpOp(
            req, opt.stopToken, [](HINTERNET h) { return ::WinHttpQueryDataAvailable(h, nullptr); });
        if (availErr != ERROR_SUCCESS) {
            return std::unexpected(opt.stopToken.stop_requested()
                                       ? std::string("Request cancelled")
                                       : FormatUrlErr("QueryDataAvailable", opt.url, availErr));
        }
        const DWORD avail = ctx->availBytes.load(std::memory_order_acquire);
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
        const std::size_t rem = opt.failOnLimit ? ctx->readBuffer.size() : (opt.maxBytes - totalBytes);
        const DWORD toRead = static_cast<DWORD>(
            (std::min<std::size_t>)(avail, (std::min<std::size_t>)(rem, ctx->readBuffer.size())));
        const DWORD readErr = ExecuteAsyncWinHttpOp(req, opt.stopToken, [&](HINTERNET h) {
            return ::WinHttpReadData(h, ctx->readBuffer.data(), toRead, nullptr);
        });
        if (readErr != ERROR_SUCCESS) {
            return std::unexpected(opt.stopToken.stop_requested()
                                       ? std::string("Request cancelled")
                                       : FormatUrlErr("ReadData", opt.url, readErr));
        }
        const DWORD bytesRead = ctx->bytesRead.load(std::memory_order_acquire);
        if (bytesRead == 0) {
            break;
        }
        totalBytes += bytesRead;
        if (opt.failOnLimit && totalBytes > opt.maxBytes) {
            return std::unexpected(std::format(
                "Response exceeded maximum limit ({} bytes) for '{}'", opt.maxBytes, opt.url));
        }
        if (!onChunk(std::string_view(ctx->readBuffer.data(), bytesRead))) {
            return std::unexpected(std::string("Failed to write response data"));
        }
    }
    return totalBytes;
}

[[nodiscard]] std::expected<std::string, std::string>
ReadWinHttpResponseBody(AsyncHttpRequest& req, const ReadBodyOptions& opt) {
    std::string body;
    auto total = StreamWinHttpBody(req, opt, [&](std::string_view part) {
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
        return MakeEngineError(spec.stopToken.stop_requested() ? ErrorCode::Cancelled
                                                               : ErrorCode::InvalidArgument,
                               session.error());
    }
    auto opened = OpenAndSendHttpGet(session->Get(), spec.url, spec.acceptHeader, spec.stopToken);
    if (!opened) {
        const auto code =
            spec.stopToken.stop_requested() ? ErrorCode::Cancelled : ErrorCode::ConnectionFailed;
        return MakeEngineError(code, opened.error());
    }
    return HttpSessionRequest{std::move(*session), std::move(*opened)};
}

}  // namespace

motrix::core::Result<std::string> HttpGetUtf8OrBytes(const HttpRequestSpec& spec) {
    auto req = OpenHttpSessionGet(spec);
    if (!req) {
        return std::unexpected(req.error());
    }
    std::stop_callback onStop(spec.stopToken, [&] { req->opened.request->Cancel(); });
    auto body = ReadWinHttpResponseBody(
        *req->opened.request,
        ReadBodyOptions{motrix::core::TrimAscii(spec.url), spec.maxBytes, true, spec.stopToken});
    if (!body) {
        const auto code = spec.stopToken.stop_requested() ? ErrorCode::Cancelled : ErrorCode::IoFailure;
        return MakeEngineError(code, body.error());
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
        if (ec) {
            return MakeEngineError(
                ErrorCode::IoFailure, "Failed to create staging directory", ec.message());
        }
    }
    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return MakeEngineError(ErrorCode::IoFailure, "Failed to create staging download file");
    }
    std::stop_callback onStop(spec.stopToken, [&] { req->opened.request->Cancel(); });
    auto totalBytes = StreamWinHttpBody(
        *req->opened.request,
        ReadBodyOptions{motrix::core::TrimAscii(spec.url), spec.maxBytes, true, spec.stopToken},
        [&](std::string_view part) {
            out.write(part.data(), static_cast<std::streamsize>(part.size()));
            return static_cast<bool>(out);
        });
    if (!totalBytes) {
        out.close();
        std::filesystem::remove(destPath, ec);
        const auto code =
            spec.stopToken.stop_requested() ? ErrorCode::Cancelled : ErrorCode::ConnectionFailed;
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
            return MakeEngineError(ErrorCode::Cancelled, "HTTP batch request cancelled");
        }
        auto opened = OpenAndSendHttpGet(session->Get(), url, {}, spec.stopToken);
        if (!opened) {
            if (spec.stopToken.stop_requested()) {
                return MakeEngineError(ErrorCode::Cancelled, "HTTP batch request cancelled");
            }
            items.push_back(HttpBatchItem{.error = opened.error()});
            continue;
        }
        std::stop_callback onStop(spec.stopToken, [&] { opened->request->Cancel(); });
        auto body = ReadWinHttpResponseBody(
            *opened->request,
            ReadBodyOptions{
                motrix::core::TrimAscii(url), spec.maxBytes, spec.failOnLimit, spec.stopToken});
        if (body) {
            items.push_back(HttpBatchItem{.body = std::move(*body)});
        } else {
            if (spec.stopToken.stop_requested()) {
                return MakeEngineError(ErrorCode::Cancelled, "HTTP batch request cancelled");
            }
            items.push_back(HttpBatchItem{.error = body.error()});
        }
    }
    return items;
}

}  // namespace motrix::engine::detail
