export module motrix.engine:http_winhttp;

import std;
import motrix.core;

namespace motrix::engine::detail {

export struct HttpRequestSpec {
    std::string url;
    std::string proxyUrl;
    std::string acceptHeader;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(15000);
    std::size_t maxBytes = 8u * 1024u * 1024u;
    std::stop_token stopToken = {};
};

export struct HttpBatchRequestSpec {
    std::span<const std::string> urls;
    std::string_view proxyUrl;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(15000);
    std::size_t maxBytes = 2u * 1024u * 1024u;
    bool failOnLimit = false;
    std::stop_token stopToken = {};
};

export struct HttpBatchItem {
    std::optional<std::string> body;
    std::string error;
};

export [[nodiscard]] motrix::core::Result<std::string> HttpGetUtf8OrBytes(const HttpRequestSpec& spec);

export [[nodiscard]] motrix::core::Result<std::size_t>
HttpDownloadToFile(const HttpRequestSpec& spec, const std::filesystem::path& destPath);

export [[nodiscard]] motrix::core::Result<std::vector<HttpBatchItem>>
HttpGetBatchUtf8(const HttpBatchRequestSpec& spec);

}  // namespace motrix::engine::detail
