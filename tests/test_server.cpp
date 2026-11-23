module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on

module motrix.test_server;

import std;
import motrix.core;
import motrix.domain;
import motrix.test_server_util;

namespace motrix::test {

struct LocalTestServer::Impl {
    WsaLease wsa;
    SOCKET listenSock = INVALID_SOCKET;
    std::uint16_t port = 0;
    std::atomic<bool> stopping{false};

    struct CustomRoute {
        std::vector<std::uint8_t> body;
        std::string contentType;
    };
    mutable std::mutex mu;
    ServerStats stats;
    int activeConns = 0;
    bool toggleOk = false;
    std::unordered_map<std::string, std::vector<std::pair<std::uint16_t, std::string>>> peersByHash;
    std::unordered_map<std::string, CustomRoute> customRoutes;

    std::vector<std::uint8_t> smallData = LocalTestServer::MakePatternBytes(64 * 1024, 0x11);
    std::vector<std::uint8_t> multiData = LocalTestServer::MakePatternBytes(5 * 1024 * 1024, 0x22);
    std::vector<std::uint8_t> slowData = LocalTestServer::MakePatternBytes(384 * 1024, 0x33);
    std::vector<std::uint8_t> toggleData = LocalTestServer::MakePatternBytes(32 * 1024, 0x44);
    std::vector<std::uint8_t> chunkedData = LocalTestServer::MakePatternBytes(64 * 1024, 0x55);

    std::jthread acceptThread;
    std::mutex sockMu;
    std::unordered_set<SOCKET> activeClientSocks;
    std::mutex workerMu;
    std::vector<std::jthread> workers;

    void Start() {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            throw std::runtime_error("LocalTestServer: socket() failed");
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        socklen_t len = sizeof(addr);
        if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0
            || ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0
            || ::listen(s, SOMAXCONN) != 0) {
            ::closesocket(s);
            throw std::runtime_error("LocalTestServer: bind/getsockname/listen failed");
        }
        {
            std::scoped_lock lock(sockMu);
            listenSock = s;
        }
        port = ::ntohs(addr.sin_port);
        acceptThread = std::jthread([this](std::stop_token) { AcceptLoop(); });
    }

    void Stop() {
        stopping.store(true, std::memory_order_release);
        SOCKET ls = INVALID_SOCKET;
        std::vector<SOCKET> clientCopy;
        {
            std::scoped_lock lock(sockMu);
            ls = std::exchange(listenSock, INVALID_SOCKET);
            clientCopy.assign(activeClientSocks.begin(), activeClientSocks.end());
        }
        if (ls != INVALID_SOCKET) {
            ::closesocket(ls);
        }
        for (SOCKET c : clientCopy) {
            ::shutdown(c, SD_BOTH);
        }
        if (acceptThread.joinable()) {
            acceptThread.join();
        }
        std::vector<std::jthread> wCopy;
        {
            std::scoped_lock lock(workerMu);
            wCopy.swap(workers);
        }
        for (auto& w : wCopy) {
            if (w.joinable()) {
                w.join();
            }
        }
        std::unordered_set<SOCKET> leftover;
        {
            std::scoped_lock lock(sockMu);
            leftover.swap(activeClientSocks);
        }
        for (SOCKET c : leftover) {
            ::shutdown(c, SD_BOTH);
            ::closesocket(c);
        }
    }

    void AcceptLoop() {
        while (!stopping.load(std::memory_order_acquire)) {
            SOCKET ls = INVALID_SOCKET;
            {
                std::scoped_lock lock(sockMu);
                ls = listenSock;
            }
            if (ls == INVALID_SOCKET) {
                break;
            }
            sockaddr_in cli{};
            socklen_t clen = sizeof(cli);
            SOCKET c = ::accept(ls, reinterpret_cast<sockaddr*>(&cli), &clen);
            if (c == INVALID_SOCKET) {
                break;
            }
            if (stopping.load(std::memory_order_acquire)) {
                ::shutdown(c, SD_BOTH);
                ::closesocket(c);
                break;
            }
            {
                std::scoped_lock lock(sockMu);
                activeClientSocks.insert(c);
            }
            std::scoped_lock lock(workerMu);
            workers.emplace_back([this, c](std::stop_token) { HandleClient(c); });
        }
    }

    void HandleClient(SOCKET s) {
        const DWORD rcvTimeoutMs = 5000;
        ::setsockopt(s,
                     SOL_SOCKET,
                     SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&rcvTimeoutMs),
                     sizeof(rcvTimeoutMs));
        struct ConnGuard {
            Impl& impl;
            SOCKET sock;
            ConnGuard(Impl& i, SOCKET s_) : impl(i), sock(s_) {
                std::scoped_lock lock(impl.mu);
                if (++impl.activeConns > impl.stats.maxConcurrent) {
                    impl.stats.maxConcurrent = impl.activeConns;
                }
            }
            ~ConnGuard() {
                {
                    std::scoped_lock lock(impl.mu);
                    --impl.activeConns;
                }
                bool shouldClose = false;
                {
                    std::scoped_lock lock(impl.sockMu);
                    shouldClose = (impl.activeClientSocks.erase(sock) > 0);
                }
                if (shouldClose) {
                    ::shutdown(sock, SD_BOTH);
                    ::closesocket(sock);
                }
            }
        } guard(*this, s);

        std::string reqBuf;
        reqBuf.reserve(4096);
        char tmp[2048]{};
        while (reqBuf.find("\r\n\r\n") == std::string::npos) {
            const int n = ::recv(s, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                return;
            }
            reqBuf.append(tmp, static_cast<std::size_t>(n));
            if (reqBuf.size() > 65536) {
                return;
            }
        }

        const auto lineEnd = reqBuf.find("\r\n");
        const std::string_view reqLine(reqBuf.data(), lineEnd);
        const auto sp1 = reqLine.find(' ');
        const auto sp2 =
            (sp1 == std::string_view::npos) ? std::string_view::npos : reqLine.find(' ', sp1 + 1);
        if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
            return;
        }
        const std::string_view method = reqLine.substr(0, sp1);
        const std::string_view target = reqLine.substr(sp1 + 1, sp2 - sp1 - 1);
        const bool isHead = (method == "HEAD");

        std::unordered_map<std::string, std::string> hdrs;
        std::size_t cur = lineEnd + 2;
        const std::size_t hdrEnd = reqBuf.find("\r\n\r\n");
        while (cur < hdrEnd) {
            const std::size_t next = reqBuf.find("\r\n", cur);
            const std::string_view line = (next == std::string_view::npos)
                                              ? std::string_view{}
                                              : std::string_view(reqBuf.data() + cur, next - cur);
            if (const auto colon = line.find(':'); colon != std::string_view::npos) {
                hdrs[motrix::core::AsciiLower(motrix::core::TrimAscii(line.substr(0, colon)))] =
                    std::string(motrix::core::TrimAscii(line.substr(colon + 1)));
            }
            cur = next + 2;
        }

        {
            std::scoped_lock lock(mu);
            ++stats.totalRequests;
            if (hdrs.contains("range")) {
                ++stats.rangeRequests;
            }
        }

        const auto qPos = target.find('?');
        const std::string_view path = (qPos == std::string_view::npos) ? target : target.substr(0, qPos);
        const std::string_view query =
            (qPos == std::string_view::npos) ? std::string_view{} : target.substr(qPos + 1);

        if (path == "/announce") {
            HandleAnnounce(s, query);
            return;
        }
        if (path.starts_with("/inspect/")) {
            {
                std::scoped_lock lock(mu);
                stats.lastUserAgent = hdrs["user-agent"];
                stats.lastReferer = hdrs["referer"];
                stats.lastCookie = hdrs["cookie"];
                stats.lastAuthorization = hdrs["authorization"];
            }
            ServeBytes(s, smallData, hdrs["range"], isHead, 0);
            return;
        }
        if (path.starts_with("/exact/small/")) {
            ServeBytes(s, smallData, hdrs["range"], isHead, 0);
            return;
        }
        if (path.starts_with("/exact/multi/")) {
            ServeBytes(s, multiData, hdrs["range"], isHead, 65536, std::chrono::milliseconds(15));
            return;
        }
        if (path.starts_with("/slow/")) {
            ServeBytes(s, slowData, hdrs["range"], isHead, 8192, std::chrono::milliseconds(30));
            return;
        }
        if (path.starts_with("/toggle/")) {
            bool ok = false;
            {
                std::scoped_lock lock(mu);
                ok = toggleOk;
            }
            if (!ok) {
                SendText(
                    s, "HTTP/1.1 503 Unavailable\r\nContent-Length: 4\r\nConnection: close\r\n\r\nwait");
                return;
            }
            ServeBytes(s, toggleData, hdrs["range"], isHead, 0);
            return;
        }
        if (path.starts_with("/redirect/")) {
            SendText(s,
                     "HTTP/1.1 302 Found\r\nLocation: /exact/small/redirected.bin\r\n"
                     "Content-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        if (path.starts_with("/chunked/")) {
            SendText(s,
                     "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                     "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
            if (!isHead) {
                constexpr std::size_t kChunkBytes = 4096;
                for (std::size_t off = 0; off < chunkedData.size(); off += kChunkBytes) {
                    const std::size_t n = std::min(kChunkBytes, chunkedData.size() - off);
                    if (!SendText(s, std::format("{:x}\r\n", n))
                        || !SendAll(s, reinterpret_cast<const char*>(chunkedData.data() + off), n)
                        || !SendText(s, "\r\n")) {
                        return;
                    }
                }
                (void)SendText(s, "0\r\n\r\n");
            }
            return;
        }
        if (ServeCustomRoute(s, path, isHead)) {
            return;
        }
        SendText(s, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found");
    }

    bool ServeCustomRoute(SOCKET s, std::string_view path, bool isHead) {
        std::optional<CustomRoute> route;
        {
            std::scoped_lock lock(mu);
            if (auto it = customRoutes.find(std::string(path)); it != customRoutes.end()) {
                route = it->second;
            }
        }
        if (!route) {
            return false;
        }
        SendText(s,
                 std::format("HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: "
                             "{}\r\nConnection: close\r\n\r\n",
                             route->contentType,
                             route->body.size()));
        if (!isHead && !route->body.empty()) {
            SendAll(s, reinterpret_cast<const char*>(route->body.data()), route->body.size());
        }
        return true;
    }

    void ServeBytes(SOCKET s,
                    const std::vector<std::uint8_t>& data,
                    const std::string& rangeHdr,
                    bool isHead,
                    std::size_t throttleChunkBytes = 0,
                    std::chrono::milliseconds throttleDelay = {}) {
        std::size_t off = 0;
        std::size_t len = data.size();
        if (!rangeHdr.empty()) {
            const auto pr = ParseByteRange(rangeHdr, data.size());
            if (!pr.valid) {
                SendText(
                    s,
                    std::format("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{}\r\n"
                                "Content-Length: 0\r\nConnection: close\r\n\r\n",
                                data.size()));
                return;
            }
            off = pr.start;
            len = pr.end - pr.start + 1;
            SendText(s,
                     std::format("HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n"
                                 "Content-Range: bytes {}-{}/{}\r\nContent-Length: {}\r\n"
                                 "Connection: close\r\n\r\n",
                                 pr.start,
                                 pr.end,
                                 data.size(),
                                 len));
        } else {
            SendText(s,
                     std::format("HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Length: {}\r\n"
                                 "Connection: close\r\n\r\n",
                                 len));
        }
        if (isHead) {
            return;
        }
        if (throttleChunkBytes == 0) {
            SendAll(s, reinterpret_cast<const char*>(data.data() + off), len);
            return;
        }
        std::size_t pos = 0;
        while (pos < len && !stopping.load(std::memory_order_acquire)) {
            const std::size_t n = std::min(throttleChunkBytes, len - pos);
            if (!SendAll(s, reinterpret_cast<const char*>(data.data() + off + pos), n)) {
                return;
            }
            pos += n;
            if (throttleDelay.count() > 0) {
                std::this_thread::sleep_for(throttleDelay);
            }
        }
    }

    void HandleAnnounce(SOCKET s, std::string_view query) {
        const auto params = ParseQueryRaw(query);
        const std::string infoHash =
            params.contains("info_hash") ? params.at("info_hash") : std::string{};
        const std::string event = params.contains("event") ? params.at("event") : std::string{};
        const std::string peerId =
            params.contains("peer_id") ? params.at("peer_id") : std::string(20, '\0');
        const bool compact = params.contains("compact") && params.at("compact") == "1";
        std::uint16_t peerPort = 0;
        if (const auto it = params.find("port"); it != params.end()) {
            int pVal = 0;
            std::from_chars(it->second.data(), it->second.data() + it->second.size(), pVal);
            if (pVal > 0 && pVal <= 65535) {
                peerPort = static_cast<std::uint16_t>(pVal);
            }
        }

        std::vector<std::pair<std::uint16_t, std::string>> others;
        {
            std::scoped_lock lock(mu);
            auto& plist = peersByHash[infoHash];
            std::erase_if(plist, [&](const auto& pr) { return pr.first == peerPort; });
            if (event != "stopped" && peerPort != 0) {
                plist.emplace_back(peerPort, peerId);
            }
            for (const auto& pr : plist) {
                if (pr.first != peerPort) {
                    others.push_back(pr);
                }
            }
        }

        std::vector<std::uint8_t> body;
        body.push_back('d');
        AppendBencodeStr(body, "complete");
        AppendBencodeInt(body, others.size());
        AppendBencodeStr(body, "interval");
        AppendBencodeInt(body, 2);
        AppendBencodeStr(body, "peers");
        if (compact) {
            std::string blob;
            blob.reserve(others.size() * 6);
            for (const auto& pr : others) {
                blob.push_back(127);
                blob.push_back(0);
                blob.push_back(0);
                blob.push_back(1);
                blob.push_back(static_cast<char>((pr.first >> 8) & 0xFF));
                blob.push_back(static_cast<char>(pr.first & 0xFF));
            }
            AppendBencodeStr(body, blob);
        } else {
            body.push_back('l');
            for (const auto& pr : others) {
                body.push_back('d');
                AppendBencodeStr(body, "ip");
                AppendBencodeStr(body, "127.0.0.1");
                AppendBencodeStr(body, "peer id");
                std::string pid = pr.second;
                pid.resize(20, '\0');
                AppendBencodeStr(body, pid);
                AppendBencodeStr(body, "port");
                AppendBencodeInt(body, pr.first);
                body.push_back('e');
            }
            body.push_back('e');
        }
        body.push_back('e');

        const std::string hdr =
            std::format("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                        "{}\r\nConnection: close\r\n\r\n",
                        body.size());
        SendText(s, hdr);
        SendAll(s, reinterpret_cast<const char*>(body.data()), body.size());
    }
};

LocalTestServer::LocalTestServer() : impl_(std::make_unique<Impl>()) {
    impl_->Start();
}

LocalTestServer::~LocalTestServer() {
    if (impl_) {
        impl_->Stop();
    }
}

std::uint16_t LocalTestServer::Port() const noexcept {
    return impl_->port;
}

std::string LocalTestServer::BaseUrl() const {
    return std::format("http://127.0.0.1:{}", impl_->port);
}

ServerStats LocalTestServer::Stats() const {
    std::scoped_lock lock(impl_->mu);
    return impl_->stats;
}

void LocalTestServer::ResetStats() {
    std::scoped_lock lock(impl_->mu);
    impl_->stats = ServerStats{};
}

void LocalTestServer::SetToggleOk(bool ok) {
    std::scoped_lock lock(impl_->mu);
    impl_->toggleOk = ok;
}

void LocalTestServer::SetCustomRoute(std::string path,
                                     std::vector<std::uint8_t> body,
                                     std::string contentType) {
    std::scoped_lock lock(impl_->mu);
    impl_->customRoutes[std::move(path)] = Impl::CustomRoute{
        .body = std::move(body),
        .contentType = std::move(contentType),
    };
}

std::vector<std::uint8_t> LocalTestServer::MakePatternBytes(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>((i * 31u + seed) & 0xFFu);
    }
    return out;
}

}  // namespace motrix::test
