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

class WsRpcClient final : public IEngineRpcClient {
public:
    explicit WsRpcClient(motrix::core::Logger& logger) : logger_(logger) {}

    ~WsRpcClient() override { Disconnect(); }

    void SetNotificationCallback(NotificationCallback cb) override {
        std::scoped_lock lock(cbMutex_);
        notificationCb_ = std::move(cb);
    }

    motrix::core::Result<void> Connect(const RpcConnectionOptions& options) override {
        Disconnect();

        {
            std::scoped_lock lock(stateMutex_);
            options_ = options;
            connectAttemptDone_ = false;
            connectError_.clear();
            connected_.store(false, std::memory_order_release);
        }

        const std::string url = std::format("ws://{}:{}/jsonrpc", options.host, options.port);
        ws_.setUrl(url);
        ws_.disableAutomaticReconnection();
        ws_.disablePerMessageDeflate();
        ws_.setHandshakeTimeout(3);
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { HandleWsMessage(msg); });
        ws_.start();

        std::unique_lock lock(stateMutex_);
        const bool finished = connectCv_.wait_for(
            lock, std::chrono::milliseconds(2500), [&] { return connectAttemptDone_; });
        if (!finished || !connected_.load(std::memory_order_acquire)) {
            const std::string err =
                connectError_.empty() ? "Timed out waiting for WS handshake" : connectError_;
            lock.unlock();
            Disconnect();
            return std::unexpected(motrix::core::MakeError(
                motrix::core::ErrorDomain::Engine, motrix::core::ErrorCode::ConnectionFailed, err));
        }
        return {};
    }

    void Disconnect() override {
        disconnecting_.store(true, std::memory_order_release);
        connected_.store(false, std::memory_order_release);
        ws_.stop();
        ws_.setOnMessageCallback({});
        FailAllPending(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                               motrix::core::ErrorCode::Cancelled,
                                               "WebSocket RPC disconnected"));
        disconnecting_.store(false, std::memory_order_release);
    }

    bool IsConnected() const noexcept override { return connected_.load(std::memory_order_acquire); }

    motrix::core::Result<EngineVersionInfo> GetVersion() override {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildSimpleRpcRequest(id, "aria2.getVersion", options_.secret));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseEngineVersionJson(*raw);
    }

    motrix::core::Result<EngineGlobalStat> GetGlobalStat() override {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildSimpleRpcRequest(id, "aria2.getGlobalStat", options_.secret));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseGlobalStatJson(*raw);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellActive() override {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildSimpleRpcRequest(id, "aria2.tellActive", options_.secret));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseTaskListJson(*raw);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellWaiting(int offset,
                                                                                    int num) override {
        const std::string id = NextId();
        auto raw =
            CallRaw(id, BuildTellSliceRpcRequest(id, "aria2.tellWaiting", options_.secret, offset, num));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseTaskListJson(*raw);
    }

    motrix::core::Result<std::vector<motrix::domain::DownloadTaskView>> TellStopped(int offset,
                                                                                    int num) override {
        const std::string id = NextId();
        auto raw =
            CallRaw(id, BuildTellSliceRpcRequest(id, "aria2.tellStopped", options_.secret, offset, num));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseTaskListJson(*raw);
    }

    motrix::core::Result<EngineSyncSnapshot> SyncAll(int waitingLimit, int stoppedLimit) override {
        const std::string id = NextId();
        auto raw =
            CallRaw(id, BuildMulticallSyncRequest(id, options_.secret, waitingLimit, stoppedLimit));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseMulticallSyncJson(*raw);
    }

    motrix::core::Result<std::string> AddUri(const AddUriRequest& request) override {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildAddUriRpcRequest(id, options_.secret, request));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return ParseStringResultJson(*raw);
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
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildChangeGlobalOptionRpcRequest(id, options_.secret, options));
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

    motrix::core::Result<void> CallVoidSimple(std::string_view method) {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildSimpleRpcRequest(id, method, options_.secret));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<void> CallVoidGid(std::string_view method, std::string_view gid) {
        const std::string id = NextId();
        auto raw = CallRaw(id, BuildGidRpcRequest(id, method, options_.secret, gid));
        if (!raw) {
            return std::unexpected(raw.error());
        }
        return {};
    }

    motrix::core::Result<std::string>
    CallRaw(const std::string& id,
            const std::string& payload,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        if (!IsConnected()) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::ConnectionFailed,
                                                           "WebSocket RPC is not connected"));
        }

        auto pending = std::make_shared<PendingCall>();
        auto future = pending->promise.get_future();
        {
            std::scoped_lock lock(pendingMutex_);
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

    void EmitNotification(const EngineNotification& ev) {
        NotificationCallback cb;
        {
            std::scoped_lock lock(cbMutex_);
            cb = notificationCb_;
        }
        if (cb) {
            cb(ev);
        }
    }

    void HandleWsMessage(const ix::WebSocketMessagePtr& msg) {
        if (!msg || disconnecting_.load(std::memory_order_acquire)) {
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Open) {
            {
                std::scoped_lock lock(stateMutex_);
                connected_.store(true, std::memory_order_release);
                connectAttemptDone_ = true;
            }
            connectCv_.notify_all();
            EmitNotification(EngineNotification{EngineEventKind::Connected, {}});
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Close
            || msg->type == ix::WebSocketMessageType::Error) {
            const bool wasConnected = connected_.exchange(false, std::memory_order_acq_rel);
            const std::string reason = (msg->type == ix::WebSocketMessageType::Error)
                                           ? msg->errorInfo.reason
                                           : msg->closeInfo.reason;
            {
                std::scoped_lock lock(stateMutex_);
                connectError_ = reason;
                connectAttemptDone_ = true;
            }
            connectCv_.notify_all();
            FailAllPending(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                   motrix::core::ErrorCode::ConnectionFailed,
                                                   reason.empty() ? "WebSocket closed" : reason));
            if (wasConnected) {
                EmitNotification(EngineNotification{EngineEventKind::Disconnected, {}});
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
            EmitNotification(parsed->notification);
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

    std::mutex stateMutex_;
    std::condition_variable connectCv_;
    bool connectAttemptDone_ = false;
    std::string connectError_;

    std::mutex pendingMutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingCall>> pending_;

    std::mutex cbMutex_;
    NotificationCallback notificationCb_;
};

}  // namespace

std::unique_ptr<IEngineRpcClient> CreateWsRpcClient(motrix::core::Logger& logger) {
    return std::make_unique<WsRpcClient>(logger);
}

}  // namespace motrix::engine
