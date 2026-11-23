module;

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

module motrix.engine;

import std;
import motrix.core;
import motrix.domain;

namespace motrix::engine {

namespace {

class NetSystemLease {
public:
    NetSystemLease() {
        std::scoped_lock lock(mutex_);
        if (refCount_++ == 0) {
            ix::initNetSystem();
        }
    }
    ~NetSystemLease() {
        std::scoped_lock lock(mutex_);
        if (--refCount_ == 0) {
            ix::uninitNetSystem();
        }
    }
    NetSystemLease(const NetSystemLease&) = delete;
    NetSystemLease& operator=(const NetSystemLease&) = delete;

private:
    static inline std::mutex mutex_;
    static inline int refCount_ = 0;
};

struct PendingCall {
    std::promise<motrix::core::Result<std::string>> promise;
    std::atomic_flag settled = ATOMIC_FLAG_INIT;

    void Complete(motrix::core::Result<std::string> res) {
        if (!settled.test_and_set(std::memory_order_acq_rel)) {
            promise.set_value(std::move(res));
        }
    }
};

struct RpcSessionSnapshot {
    bool connected = false;
    std::uint64_t gen = 0;
    std::string secret;
};

[[nodiscard]] motrix::core::AppError MakeNotConnectedError() {
    return motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                   motrix::core::ErrorCode::ConnectionFailed,
                                   "WebSocket RPC is not connected");
}

class WsRpcClient final : public IEngineRpcClient {
public:
    explicit WsRpcClient(motrix::core::Logger& logger) : logger_(logger) {}

    ~WsRpcClient() override {
        Disconnect();
        if (notifyThread_.joinable()) {
            notifyThread_.request_stop();
            notifyCv_.notify_all();
            if (std::this_thread::get_id() != notifyThread_.get_id()) {
                notifyThread_.join();
            }
        }
    }

    void SetNotificationCallback(NotificationCallback cb) override {
        std::scoped_lock lock(cbMutex_);
        notificationCb_ = std::move(cb);
    }

    motrix::core::Result<void> Connect(const RpcConnectionOptions& options) override {
        std::scoped_lock lifecycleLock(lifecycleMutex_);
        DisconnectLocked();

        std::uint64_t gen = 0;
        {
            std::scoped_lock lock(stateMutex_);
            options_ = options;
            gen = ++connectionGen_;
            activeConnectGen_ = gen;
            connectAttemptDone_ = false;
            connectError_.clear();
            connected_.store(false, std::memory_order_release);
            disconnecting_.store(false, std::memory_order_release);
        }

        StartNotifyWorkerLocked();

        const bool isBareIpv6 =
            (options.host.find(':') != std::string::npos && !options.host.starts_with('['));
        const std::string hostFormatted = isBareIpv6 ? std::format("[{}]", options.host) : options.host;
        const std::string url = std::format("ws://{}:{}/jsonrpc", hostFormatted, options.port);
        ws_.setUrl(url);
        ws_.disableAutomaticReconnection();
        ws_.disablePerMessageDeflate();
        ws_.setHandshakeTimeout(3);
        ws_.setOnMessageCallback(
            [this, gen](const ix::WebSocketMessagePtr& msg) { HandleWsMessage(gen, msg); });
        ws_.start();

        std::unique_lock lock(stateMutex_);
        const bool finished = connectCv_.wait_for(
            lock, std::chrono::milliseconds(2500), [&] { return connectAttemptDone_; });
        const bool ok = finished && connected_.load(std::memory_order_acquire);
        if (!ok) {
            activeConnectGen_ = 0;
            connected_.store(false, std::memory_order_release);
            const std::string err =
                connectError_.empty() ? "Timed out waiting for WS handshake" : connectError_;
            lock.unlock();
            DisconnectLocked();
            return std::unexpected(motrix::core::MakeError(
                motrix::core::ErrorDomain::Engine, motrix::core::ErrorCode::ConnectionFailed, err));
        }
        return {};
    }

    void Disconnect() override {
        disconnecting_.store(true, std::memory_order_release);
        connected_.store(false, std::memory_order_release);
        {
            std::scoped_lock stateLock(stateMutex_);
            activeConnectGen_ = 0;
            if (connectError_.empty()) {
                connectError_ = "WebSocket RPC disconnected";
            }
            connectAttemptDone_ = true;
        }
        connectCv_.notify_all();

        std::scoped_lock lifecycleLock(lifecycleMutex_);
        DisconnectLocked();
    }

    bool IsConnected() const noexcept override { return connected_.load(std::memory_order_acquire); }

    motrix::core::Result<EngineVersionInfo> GetVersion() override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildSimpleRpcRequest(id, "aria2.getVersion", snap.secret),
                        ParseEngineVersionJson);
    }

    motrix::core::Result<EngineGlobalStat> GetGlobalStat() override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildSimpleRpcRequest(id, "aria2.getGlobalStat", snap.secret),
                        ParseGlobalStatJson);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellActive() override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(
            id, snap.gen, BuildSimpleRpcRequest(id, "aria2.tellActive", snap.secret), ParseTaskListJson);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellWaiting(int offset,
                                                                                    int num) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildTellSliceRpcRequest(id, snap.secret, {"aria2.tellWaiting", offset, num}),
                        ParseTaskListJson);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellStopped(int offset,
                                                                                    int num) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildTellSliceRpcRequest(id, snap.secret, {"aria2.tellStopped", offset, num}),
                        ParseTaskListJson);
    }

    motrix::core::Result<EngineSyncSnapshot> SyncAll(int waitingLimit, int stoppedLimit) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildMulticallSyncRequest(id, snap.secret, waitingLimit, stoppedLimit),
                        ParseMulticallSyncJson);
    }

    motrix::core::Result<TaskDetailResult> TellStatus(std::string_view gid) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildGidRpcRequest(id, "aria2.tellStatus", snap.secret, gid),
                        ParseTaskDetailJson);
    }

    motrix::core::Result<std::vector<motrix::domain::TaskFileView>>
    GetFiles(std::string_view gid) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildGidRpcRequest(id, "aria2.getFiles", snap.secret, gid),
                        ParseTaskFilesJson);
    }

    motrix::core::Result<std::vector<motrix::domain::TaskPeerView>>
    GetPeers(std::string_view gid) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(id,
                        snap.gen,
                        BuildGidRpcRequest(id, "aria2.getPeers", snap.secret, gid),
                        [](std::string_view json) { return ParsePeersJson(json, 0); });
    }

    motrix::core::Result<std::string> AddUri(const AddUriRequest& request) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(
            id, snap.gen, BuildAddUriRpcRequest(id, snap.secret, request), ParseStringResultJson);
    }

    motrix::core::Result<std::string> AddTorrent(const AddTorrentRequest& request) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        return CallJson(
            id, snap.gen, BuildAddTorrentRpcRequest(id, snap.secret, request), ParseStringResultJson);
    }

    motrix::core::Result<void> ChangeTaskOption(std::string_view gid,
                                                std::span<const EngineOptionPair> options) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        auto raw = CallRaw(id, snap.gen, BuildChangeTaskOptionRpcRequest(id, snap.secret, gid, options));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<void> Pause(std::string_view gid) override {
        return CallVoidGid("aria2.pause", gid);
    }

    motrix::core::Result<void> Unpause(std::string_view gid) override {
        return CallVoidGid("aria2.unpause", gid);
    }

    motrix::core::Result<void> ForceRemove(std::string_view gid) override {
        return CallVoidGid("aria2.forceRemove", gid);
    }

    motrix::core::Result<void> Remove(std::string_view gid) override {
        return CallVoidGid("aria2.remove", gid);
    }

    motrix::core::Result<void> RemoveDownloadResult(std::string_view gid) override {
        return CallVoidGid("aria2.removeDownloadResult", gid);
    }

    motrix::core::Result<void> PauseAll() override { return CallVoidSimple("aria2.pauseAll"); }

    motrix::core::Result<void> UnpauseAll() override { return CallVoidSimple("aria2.unpauseAll"); }

    motrix::core::Result<void> PurgeDownloadResult() override {
        return CallVoidSimple("aria2.purgeDownloadResult");
    }

    motrix::core::Result<void> ChangeGlobalOption(std::span<const EngineOptionPair> options) override {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        auto raw = CallRaw(id, snap.gen, BuildChangeGlobalOptionRpcRequest(id, snap.secret, options));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<void> SaveSession() override { return CallVoidSimple("aria2.saveSession"); }

    motrix::core::Result<void> Shutdown(bool force) override {
        return CallVoidSimple(force ? "aria2.forceShutdown" : "aria2.shutdown");
    }

private:
    [[nodiscard]] std::string NextId() {
        return std::format("mn-{}", nextId_.fetch_add(1, std::memory_order_relaxed));
    }

    [[nodiscard]] RpcSessionSnapshot ReadSessionSnapshot() {
        std::scoped_lock lock(stateMutex_);
        return RpcSessionSnapshot{
            .connected = connected_.load(std::memory_order_acquire),
            .gen = activeConnectGen_,
            .secret = options_.secret,
        };
    }

    void DisconnectLocked() {
        disconnecting_.store(true, std::memory_order_release);
        connected_.store(false, std::memory_order_release);
        {
            std::scoped_lock stateLock(stateMutex_);
            activeConnectGen_ = 0;
            connectAttemptDone_ = true;
        }
        connectCv_.notify_all();
        ws_.stop();
        ws_.setOnMessageCallback({});
        FailAllPending(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                               motrix::core::ErrorCode::Cancelled,
                                               "WebSocket RPC disconnected"));
        ResetNotifyQueueLocked();
        disconnecting_.store(false, std::memory_order_release);
    }

    void StartNotifyWorkerLocked() {
        if (!notifyThread_.joinable()) {
            notifyThread_ = std::jthread([this](std::stop_token st) { NotifyLoop(st); });
        }
    }

    void ResetNotifyQueueLocked() {
        std::unique_lock lock(notifyQueueMutex_);
        ++notifyGen_;
        notifyQueue_.clear();
        if (notifyThread_.joinable() && std::this_thread::get_id() != notifyThread_.get_id()) {
            notifyIdleCv_.wait(lock, [&] { return !notifyDispatching_; });
        }
    }

    void NotifyLoop(std::stop_token st) {
        while (!st.stop_requested()) {
            std::deque<std::pair<std::uint64_t, EngineNotification>> batch;
            {
                std::unique_lock lock(notifyQueueMutex_);
                notifyCv_.wait(lock, st, [&] { return st.stop_requested() || !notifyQueue_.empty(); });
                if (st.stop_requested()) {
                    break;
                }
                batch.swap(notifyQueue_);
                notifyDispatching_ = true;
            }
            for (const auto& [gen, ev] : batch) {
                if (st.stop_requested()) {
                    break;
                }
                {
                    std::scoped_lock lock(notifyQueueMutex_);
                    if (gen != notifyGen_) {
                        continue;
                    }
                }
                DispatchNotification(ev);
            }
            {
                std::scoped_lock lock(notifyQueueMutex_);
                notifyDispatching_ = false;
            }
            notifyIdleCv_.notify_all();
        }
    }

    template <typename Fn>
    auto
    CallJson(const std::string& id, std::uint64_t expectedGen, const std::string& payload, Fn&& parser)
        -> decltype(parser(std::string_view{})) {
        auto raw = CallRaw(id, expectedGen, payload);
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return parser(*raw);
    }

    motrix::core::Result<void> CallVoidSimple(std::string_view method) {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        auto raw = CallRaw(id, snap.gen, BuildSimpleRpcRequest(id, method, snap.secret));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<void> CallVoidGid(std::string_view method, std::string_view gid) {
        const auto snap = ReadSessionSnapshot();
        if (!snap.connected || snap.gen == 0) {
            return std::unexpected(MakeNotConnectedError());
        }
        const std::string id = NextId();
        auto raw = CallRaw(id, snap.gen, BuildGidRpcRequest(id, method, snap.secret, gid));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<std::string>
    CallRaw(const std::string& id,
            std::uint64_t expectedGen,
            const std::string& payload,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        auto pending = std::make_shared<PendingCall>();
        auto future = pending->promise.get_future();
        {
            std::scoped_lock stateLock(stateMutex_);
            if (!connected_.load(std::memory_order_acquire) || activeConnectGen_ != expectedGen
                || disconnecting_.load(std::memory_order_acquire)) {
                return std::unexpected(MakeNotConnectedError());
            }
            std::scoped_lock pendingLock(pendingMutex_);
            pending_.emplace(id, pending);
        }

        const auto sendInfo = ws_.sendUtf8Text(payload);
        if (!sendInfo.success) {
            std::scoped_lock lock(pendingMutex_);
            pending_.erase(id);
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::ConnectionFailed,
                                                           "Failed to write WebSocket RPC frame"));
        }

        if (future.wait_for(timeout) != std::future_status::ready) {
            std::scoped_lock lock(pendingMutex_);
            pending_.erase(id);
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::Timeout,
                                                           "JSON-RPC call timed out"));
        }
        return future.get();
    }

    void FailAllPending(const motrix::core::AppError& err) {
        std::unordered_map<std::string, std::shared_ptr<PendingCall>> copy;
        {
            std::scoped_lock lock(pendingMutex_);
            copy.swap(pending_);
        }
        for (auto& [_, call] : copy) {
            call->Complete(std::unexpected(err));
        }
    }

    void EnqueueNotification(const EngineNotification& ev) {
        {
            std::scoped_lock lock(notifyQueueMutex_);
            notifyQueue_.emplace_back(notifyGen_, ev);
        }
        notifyCv_.notify_one();
    }

    void DispatchNotification(const EngineNotification& ev) {
        NotificationCallback cb;
        {
            std::scoped_lock lock(cbMutex_);
            cb = notificationCb_;
        }
        if (cb) {
            cb(ev);
        }
    }

    void HandleWsMessage(std::uint64_t gen, const ix::WebSocketMessagePtr& msg) {
        if (!msg || disconnecting_.load(std::memory_order_acquire)) {
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Open) {
            bool accepted = false;
            {
                std::scoped_lock lock(stateMutex_);
                if (!disconnecting_.load(std::memory_order_acquire) && activeConnectGen_ == gen) {
                    connected_.store(true, std::memory_order_release);
                    connectAttemptDone_ = true;
                    accepted = true;
                }
            }
            if (accepted) {
                connectCv_.notify_all();
                EnqueueNotification(EngineNotification{EngineEventKind::Connected, {}});
            }
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Close
            || msg->type == ix::WebSocketMessageType::Error) {
            bool wasConnected = false;
            bool notifyAttempt = false;
            const std::string reason = (msg->type == ix::WebSocketMessageType::Error)
                                           ? msg->errorInfo.reason
                                           : msg->closeInfo.reason;
            {
                std::scoped_lock lock(stateMutex_);
                if (activeConnectGen_ == gen) {
                    wasConnected = connected_.exchange(false, std::memory_order_acq_rel);
                    activeConnectGen_ = 0;
                    connectError_ = reason;
                    connectAttemptDone_ = true;
                    notifyAttempt = true;
                }
            }
            if (notifyAttempt) {
                connectCv_.notify_all();
                FailAllPending(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ConnectionFailed,
                                                       reason.empty() ? "WebSocket closed" : reason));
            }
            if (wasConnected) {
                EnqueueNotification(EngineNotification{EngineEventKind::Disconnected, {}});
            }
            return;
        }
        if (msg->type != ix::WebSocketMessageType::Message) {
            return;
        }

        const auto parsed = ParseRpcFrame(msg->str);
        if (!parsed) {
            logger_.Warn("rpc", "{}", parsed.error().message);
            return;
        }
        if (parsed->isNotification) {
            EnqueueNotification(parsed->notification);
            return;
        }

        std::shared_ptr<PendingCall> call;
        {
            std::scoped_lock lock(pendingMutex_);
            if (const auto it = pending_.find(parsed->id); it != pending_.end()) {
                call = it->second;
                pending_.erase(it);
            }
        }
        if (!call) {
            return;
        }
        if (parsed->hasError) {
            call->Complete(std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                                   motrix::core::ErrorCode::RpcError,
                                                                   parsed->errorMessage,
                                                                   std::to_string(parsed->errorCode))));
        } else {
            call->Complete(parsed->resultJson);
        }
    }

    NetSystemLease netLease_;
    motrix::core::Logger& logger_;
    ix::WebSocket ws_;
    RpcConnectionOptions options_;
    std::atomic<bool> disconnecting_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> nextId_{1};

    std::mutex lifecycleMutex_;
    std::mutex stateMutex_;
    std::condition_variable connectCv_;
    std::uint64_t connectionGen_ = 0;
    std::uint64_t activeConnectGen_ = 0;
    bool connectAttemptDone_ = false;
    std::string connectError_;

    std::mutex pendingMutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingCall>> pending_;

    std::mutex cbMutex_;
    NotificationCallback notificationCb_;

    std::mutex notifyQueueMutex_;
    std::condition_variable_any notifyCv_;
    std::condition_variable notifyIdleCv_;
    std::uint64_t notifyGen_ = 1;
    bool notifyDispatching_ = false;
    std::deque<std::pair<std::uint64_t, EngineNotification>> notifyQueue_;
    std::jthread notifyThread_;
};

}  // namespace

std::unique_ptr<IEngineRpcClient> CreateWsRpcClient(motrix::core::Logger& logger) {
    return std::make_unique<WsRpcClient>(logger);
}

}  // namespace motrix::engine
