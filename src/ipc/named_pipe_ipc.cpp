module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <nlohmann/json.hpp>
// clang-format on

module motrix.ipc;

import std;
import motrix.core;

namespace motrix::ipc {

namespace {

inline constexpr std::size_t kMaxIpcPayloadBytes = 1024u * 1024u;
inline constexpr DWORD kPipeBufferBytes = 65536u;
inline constexpr DWORD kPipeConnectWaitMs = 1500u;
inline constexpr DWORD kPipeReadTimeoutMs = 2000u;

[[nodiscard]] char LowerChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool StartsWithAsciiInsensitive(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (LowerChar(text[i]) != LowerChar(prefix[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool EndsWithAsciiInsensitive(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size()
           && StartsWithAsciiInsensitive(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] std::string_view TrimStartupArg(std::string_view text) noexcept {
    text = motrix::core::TrimAscii(text);
    while (text.size() >= 2
           && ((text.front() == '"' && text.back() == '"')
               || (text.front() == '\'' && text.back() == '\''))) {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = motrix::core::TrimAscii(text);
    }
    while (!text.empty() && text.front() == '"') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    return motrix::core::TrimAscii(text);
}

[[nodiscard]] bool IsSupportedUriScheme(std::string_view text) noexcept {
    constexpr std::array<std::string_view, 6> kSchemes = {
        "http://",
        "https://",
        "ftp://",
        "sftp://",
        "magnet:",
        "thunder://",
    };
    for (const auto scheme : kSchemes) {
        if (StartsWithAsciiInsensitive(text, scheme)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] motrix::core::AppError IpcError(motrix::core::ErrorCode code, std::string msg) {
    return motrix::core::MakeError(motrix::core::ErrorDomain::Ipc, code, std::move(msg));
}

void ReadStringArray(const nlohmann::json& j, const char* key, std::vector<std::string>& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
}

class UniquePipeHandle {
public:
    UniquePipeHandle() noexcept = default;
    explicit UniquePipeHandle(HANDLE h) noexcept : handle_(h) {}
    ~UniquePipeHandle() { Reset(); }

    UniquePipeHandle(const UniquePipeHandle&) = delete;
    UniquePipeHandle& operator=(const UniquePipeHandle&) = delete;

    UniquePipeHandle(UniquePipeHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    UniquePipeHandle& operator=(UniquePipeHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] bool IsValid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    void Reset(HANDLE next = nullptr) noexcept {
        if (IsValid()) {
            ::CloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] bool ReadExactOverlapped(
    HANDLE pipe, HANDLE stopEvent, HANDLE ovEvent, void* dest, std::size_t bytesNeeded) noexcept {
    std::size_t offset = 0;
    auto* out = static_cast<std::uint8_t*>(dest);
    while (offset < bytesNeeded) {
        OVERLAPPED ov{};
        ov.hEvent = ovEvent;
        ::ResetEvent(ovEvent);

        DWORD bytesRead = 0;
        const DWORD chunk = static_cast<DWORD>(bytesNeeded - offset);
        if (!::ReadFile(pipe, out + offset, chunk, &bytesRead, &ov)) {
            if (::GetLastError() != ERROR_IO_PENDING) {
                return false;
            }
            HANDLE waitHandles[2] = {stopEvent, ovEvent};
            if (::WaitForMultipleObjects(2, waitHandles, FALSE, kPipeReadTimeoutMs)
                != WAIT_OBJECT_0 + 1) {
                ::CancelIoEx(pipe, &ov);
                DWORD dummy = 0;
                (void)::GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                return false;
            }
            if (!::GetOverlappedResult(pipe, &ov, &bytesRead, FALSE)) {
                return false;
            }
        }
        if (bytesRead == 0) {
            return false;
        }
        offset += bytesRead;
    }
    return true;
}

class NamedPipeSingleInstanceChannel final : public ISingleInstanceChannel {
public:
    NamedPipeSingleInstanceChannel(motrix::core::Logger& logger, std::string pipeName)
        : logger_(logger), pipeName_(std::move(pipeName)) {}

    ~NamedPipeSingleInstanceChannel() override { Close(); }

    motrix::core::Result<bool> TryAcquirePrimary(PayloadHandler onRemoteStartup) override {
        Close();
        if (pipeName_.empty()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::InvalidArgument, "Pipe name cannot be empty"));
        }

        UniquePipeHandle stopEv(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!stopEv.IsValid()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::IoFailure, "Failed to create IPC stop event"));
        }

        const std::wstring pipeW = motrix::core::Utf8ToWide(pipeName_);
        UniquePipeHandle pipe(::CreateNamedPipeW(pipeW.c_str(),
                                                 PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED
                                                     | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                                 1,
                                                 0,
                                                 kPipeBufferBytes,
                                                 0,
                                                 nullptr));
        if (!pipe.IsValid()) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_ACCESS_DENIED || err == ERROR_PIPE_BUSY) {
                return false;
            }
            return std::unexpected(IpcError(motrix::core::ErrorCode::IoFailure,
                                            std::format("CreateNamedPipeW failed (code {})", err)));
        }

        handler_ = std::move(onRemoteStartup);
        stopEvent_ = std::move(stopEv);
        pipe_ = std::move(pipe);
        listenerThread_ = std::jthread([this](std::stop_token st) { ListenLoop(st); });
        return true;
    }

    motrix::core::Result<void> ForwardToPrimary(const StartupPayload& payload) override {
        if (pipeName_.empty()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::InvalidArgument, "Pipe name cannot be empty"));
        }

        const std::string json = SerializeStartupPayload(payload);
        if (json.size() > kMaxIpcPayloadBytes) {
            return std::unexpected(IpcError(motrix::core::ErrorCode::InvalidArgument,
                                            "StartupPayload exceeds 1 MiB limit"));
        }

        const std::uint32_t len = static_cast<std::uint32_t>(json.size());
        std::vector<std::uint8_t> frame(sizeof(len) + json.size());
        std::memcpy(frame.data(), &len, sizeof(len));
        std::memcpy(frame.data() + sizeof(len), json.data(), json.size());

        const std::wstring pipeW = motrix::core::Utf8ToWide(pipeName_);
        UniquePipeHandle client;
        DWORD lastErr = ERROR_SUCCESS;
        for (int attempt = 0; attempt < 3; ++attempt) {
            HANDLE raw =
                ::CreateFileW(pipeW.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (raw != INVALID_HANDLE_VALUE) {
                client.Reset(raw);
                break;
            }
            lastErr = ::GetLastError();
            if (lastErr == ERROR_PIPE_BUSY) {
                ::WaitNamedPipeW(pipeW.c_str(), kPipeConnectWaitMs);
                continue;
            }
            break;
        }
        if (!client.IsValid()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::ConnectionFailed,
                         std::format("Failed to connect to primary pipe (code {})", lastErr)));
        }

        std::size_t writtenTotal = 0;
        while (writtenTotal < frame.size()) {
            DWORD written = 0;
            const DWORD toWrite = static_cast<DWORD>(frame.size() - writtenTotal);
            if (!::WriteFile(client.Get(), frame.data() + writtenTotal, toWrite, &written, nullptr)) {
                return std::unexpected(IpcError(
                    motrix::core::ErrorCode::IoFailure,
                    std::format("WriteFile to primary pipe failed (code {})", ::GetLastError())));
            }
            if (written == 0) {
                return std::unexpected(IpcError(motrix::core::ErrorCode::IoFailure,
                                                "WriteFile to primary pipe wrote 0 bytes"));
            }
            writtenTotal += written;
        }

        (void)::FlushFileBuffers(client.Get());
        client.Reset();
        return {};
    }

    void Close() override {
        if (stopEvent_.IsValid()) {
            ::SetEvent(stopEvent_.Get());
        }
        if (listenerThread_.joinable()) {
            listenerThread_.request_stop();
            listenerThread_.join();
        }
        pipe_.Reset();
        stopEvent_.Reset();
        handler_ = nullptr;
    }

private:
    bool AcceptClient(HANDLE ovEvent, std::stop_token st, bool& shouldBreak) {
        shouldBreak = false;
        OVERLAPPED ov{};
        ov.hEvent = ovEvent;
        ::ResetEvent(ovEvent);

        if (::ConnectNamedPipe(pipe_.Get(), &ov) != 0) {
            return true;
        }
        const DWORD err = ::GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            return true;
        }
        if (err != ERROR_IO_PENDING) {
            shouldBreak = st.stop_requested();
            if (!shouldBreak) {
                logger_.Warn("ipc", "ConnectNamedPipe failed (code {})", err);
                (void)::DisconnectNamedPipe(pipe_.Get());
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        }

        HANDLE waitHandles[2] = {stopEvent_.Get(), ovEvent};
        if (::WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
            ::CancelIoEx(pipe_.Get(), &ov);
            DWORD dummy = 0;
            (void)::GetOverlappedResult(pipe_.Get(), &ov, &dummy, TRUE);
            shouldBreak = true;
            return false;
        }

        DWORD dummy = 0;
        if (!::GetOverlappedResult(pipe_.Get(), &ov, &dummy, FALSE)) {
            shouldBreak = (::GetLastError() == ERROR_OPERATION_ABORTED || st.stop_requested());
            if (!shouldBreak) {
                (void)::DisconnectNamedPipe(pipe_.Get());
            }
            return false;
        }
        return true;
    }

    void ListenLoop(std::stop_token st) {
        UniquePipeHandle ovEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!ovEvent.IsValid()) {
            logger_.Error("ipc", "Failed to create overlapped pipe event");
            return;
        }

        while (!st.stop_requested()) {
            bool shouldBreak = false;
            if (!AcceptClient(ovEvent.Get(), st, shouldBreak)) {
                if (shouldBreak) {
                    break;
                }
                continue;
            }

            std::uint32_t payloadLen = 0;
            if (ReadExactOverlapped(
                    pipe_.Get(), stopEvent_.Get(), ovEvent.Get(), &payloadLen, sizeof(payloadLen))
                && payloadLen > 0 && payloadLen <= kMaxIpcPayloadBytes) {
                std::string jsonBuf(payloadLen, '\0');
                if (ReadExactOverlapped(
                        pipe_.Get(), stopEvent_.Get(), ovEvent.Get(), jsonBuf.data(), payloadLen)) {
                    auto parsed = DeserializeStartupPayload(jsonBuf);
                    if (parsed && handler_) {
                        handler_(*parsed);
                    } else if (!parsed) {
                        logger_.Warn("ipc", "Invalid IPC StartupPayload: {}", parsed.error().message);
                    }
                }
            }

            (void)::DisconnectNamedPipe(pipe_.Get());
        }
    }

    motrix::core::Logger& logger_;
    std::string pipeName_;
    PayloadHandler handler_;
    UniquePipeHandle stopEvent_;
    UniquePipeHandle pipe_;
    std::jthread listenerThread_;
};

}  // namespace

std::string SerializeStartupPayload(const StartupPayload& payload) {
    std::vector<std::string> torrentUtf8;
    torrentUtf8.reserve(payload.torrentFiles.size());
    for (const auto& p : payload.torrentFiles) {
        torrentUtf8.push_back(motrix::core::PathToUtf8(p));
    }

    nlohmann::json j = {
        {"uris", payload.uris},
        {"torrentFiles", torrentUtf8},
        {"deepLinks", payload.deepLinks},
        {"activateWindow", payload.activateWindow},
    };
    return j.dump();
}

motrix::core::Result<StartupPayload> DeserializeStartupPayload(std::string_view jsonText) {
    const auto parsed = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::unexpected(
            IpcError(motrix::core::ErrorCode::ParseFailure, "Invalid StartupPayload JSON"));
    }

    StartupPayload payload{};
    ReadStringArray(parsed, "uris", payload.uris);
    std::vector<std::string> rawTorrents;
    ReadStringArray(parsed, "torrentFiles", rawTorrents);
    payload.torrentFiles.reserve(rawTorrents.size());
    for (const auto& s : rawTorrents) {
        payload.torrentFiles.push_back(motrix::core::PathFromUtf8(s));
    }
    ReadStringArray(parsed, "deepLinks", payload.deepLinks);
    if (const auto it = parsed.find("activateWindow"); it != parsed.end() && it->is_boolean()) {
        payload.activateWindow = it->get<bool>();
    }
    return payload;
}

StartupPayload BuildStartupPayloadFromArgs(std::span<const std::string> args) {
    StartupPayload payload{};
    payload.activateWindow = true;
    for (const auto& rawArg : args) {
        const std::string_view cleaned = TrimStartupArg(rawArg);
        if (cleaned.empty() || cleaned.starts_with('-')) {
            continue;
        }
        if (StartsWithAsciiInsensitive(cleaned, "motrix://")
            || StartsWithAsciiInsensitive(cleaned, "mo://")) {
            payload.deepLinks.emplace_back(cleaned);
        } else if (IsSupportedUriScheme(cleaned)) {
            payload.uris.emplace_back(cleaned);
        } else if (EndsWithAsciiInsensitive(cleaned, ".torrent")) {
            payload.torrentFiles.push_back(motrix::core::PathFromUtf8(cleaned));
        }
    }
    return payload;
}

std::unique_ptr<ISingleInstanceChannel> CreateNamedPipeChannel(motrix::core::Logger& logger,
                                                               std::string pipeName) {
    return std::make_unique<NamedPipeSingleInstanceChannel>(logger, std::move(pipeName));
}

}  // namespace motrix::ipc
