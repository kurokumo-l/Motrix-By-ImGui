module motrix.engine;

import std;
import motrix.core;
import :tracker;
import :http_winhttp;

namespace motrix::engine {

namespace {

inline constexpr std::size_t kMaxTrackerPayloadBytes = 2u * 1024u * 1024u;

void AppendFetchedChunk(std::string& combined, std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    combined.append(chunk);
    if (combined.back() != '\n') {
        combined.push_back('\n');
    }
}

}  // namespace

motrix::core::Result<TrackerFetchResult> FetchTrackersFromUrls(std::span<const std::string> urls,
                                                               std::string_view proxyUrl,
                                                               std::chrono::milliseconds timeout,
                                                               std::stop_token stopToken) {
    TrackerFetchResult result{};
    if (urls.empty()) {
        return result;
    }

    auto batch = detail::HttpGetBatchUtf8(detail::HttpBatchRequestSpec{
        .urls = urls,
        .proxyUrl = proxyUrl,
        .timeout = timeout,
        .maxBytes = kMaxTrackerPayloadBytes,
        .failOnLimit = false,
        .stopToken = stopToken,
    });
    if (!batch) {
        return std::unexpected(batch.error());
    }

    for (auto& item : *batch) {
        if (item.body) {
            AppendFetchedChunk(result.rawCombinedText, *item.body);
            ++result.succeededSources;
        } else {
            result.errors.push_back(std::move(item.error));
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
