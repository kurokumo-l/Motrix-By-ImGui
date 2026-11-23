module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#include <nlohmann/json.hpp>
// clang-format on

module motrix.ipc;

import std;
import motrix.core;

namespace motrix::ipc {
namespace {

inline constexpr std::size_t kMaxIpcPayloadBytes = 1024u * 1024u;
inline constexpr DWORD kPipeConnectWaitMs = 1500u;
inline constexpr DWORD kPipeReadTimeoutMs = 2000u;
inline constexpr DWORD kPipeWriteTimeoutMs = 2000u;
inline constexpr DWORD kPipeBufferBytes = 65536u;

[[nodiscard]] motrix::core::AppError IpcError(motrix::core::ErrorCode code, std::string msg) {
    return motrix::core::MakeError(motrix::core::ErrorDomain::Ipc, code, std::move(msg));
}

[[nodiscard]] bool ReadProcessTokenInfo(TOKEN_INFORMATION_CLASS infoClass,
                                        std::vector<std::uint8_t>& buf) {
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    const motrix::core::UniqueHandle token(rawToken);
    DWORD needed = 0;
    (void)::GetTokenInformation(token.Get(), infoClass, nullptr, 0, &needed);
    if (needed == 0) {
        return false;
    }
    buf.resize(needed);
    return ::GetTokenInformation(token.Get(), infoClass, buf.data(), needed, &needed) != 0;
}

[[nodiscard]] DWORD QueryCurrentIntegrityRid() {
    std::vector<std::uint8_t> buf;
    if (!ReadProcessTokenInfo(TokenIntegrityLevel, buf) || buf.size() < sizeof(TOKEN_MANDATORY_LABEL)) {
        return 0;
    }
    const auto* tml = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buf.data());
    if (!tml->Label.Sid || !::IsValidSid(tml->Label.Sid)) {
        return 0;
    }
    const PUCHAR subCountPtr = ::GetSidSubAuthorityCount(tml->Label.Sid);
    if (!subCountPtr || *subCountPtr == 0) {
        return 0;
    }
    return *::GetSidSubAuthority(tml->Label.Sid, static_cast<DWORD>(*subCountPtr - 1));
}

[[nodiscard]] std::wstring QueryCurrentUserSidWide() {
    std::vector<std::uint8_t> buf;
    if (!ReadProcessTokenInfo(TokenUser, buf) || buf.size() < sizeof(TOKEN_USER)) {
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buf.data());
    LPWSTR rawSid = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &rawSid) || !rawSid) {
        return {};
    }
    const motrix::core::UniqueLocalPtr<wchar_t> sidGuard(rawSid);
    return std::wstring(sidGuard.get());
}

[[nodiscard]] std::string QueryCurrentUserSidString() {
    std::vector<std::uint8_t> buf;
    if (!ReadProcessTokenInfo(TokenUser, buf) || buf.size() < sizeof(TOKEN_USER)) {
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buf.data());
    LPSTR rawSid = nullptr;
    if (!::ConvertSidToStringSidA(user->User.Sid, &rawSid) || !rawSid) {
        return {};
    }
    const motrix::core::UniqueLocalPtr<char> sidGuard(rawSid);
    return std::string(sidGuard.get());
}

[[nodiscard]] motrix::core::UniqueLocalPtr<> BuildCurrentUserPipeSd(std::wstring_view sidWide) {
    if (sidWide.empty()) {
        return nullptr;
    }
    std::wstring sddl = L"D:P(A;;GA;;;";
    sddl += sidWide;
    sddl += L")";
    if (QueryCurrentIntegrityRid() >= SECURITY_MANDATORY_MEDIUM_RID) {
        sddl += L"S:(ML;;NW;;;ME)";
    }
    PSECURITY_DESCRIPTOR rawSd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &rawSd, nullptr)
        || !rawSd) {
        return nullptr;
    }
    return motrix::core::UniqueLocalPtr<>(rawSd);
}

[[nodiscard]] HANDLE
CreateOwnedNamedPipeHandle(std::wstring_view pipeName, DWORD& outErr, std::string& outErrCtx) {
    outErr = 0;
    outErrCtx.clear();
    const std::wstring sidWide = QueryCurrentUserSidWide();
    const motrix::core::UniqueLocalPtr<> sd = BuildCurrentUserPipeSd(sidWide);
    if (!sd) {
        outErrCtx = "Failed to build pipe owner DACL";
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd.get();
    sa.bInheritHandle = FALSE;

    const std::wstring wideName(pipeName);
    HANDLE raw =
        ::CreateNamedPipeW(wideName.c_str(),
                           PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                           1,
                           0,
                           kPipeBufferBytes,
                           0,
                           &sa);
    if (raw == INVALID_HANDLE_VALUE) {
        outErr = ::GetLastError();
    }
    return raw;
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
        if (motrix::core::StartsWithAsciiInsensitive(text, scheme)) {
            return true;
        }
    }
    return false;
}

void ReadStringArray(const nlohmann::json& j, const char* key, std::vector<std::string>& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string()) {
                out.push_back(item.get_ref<const std::string&>());
            }
        }
    }
}

[[nodiscard]] bool ReadChunkOverlapped(HANDLE pipe,
                                       HANDLE stopEvent,
                                       HANDLE ovEvent,
                                       void* dest,
                                       DWORD chunk,
                                       DWORD waitMs,
                                       DWORD& outRead) noexcept {
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;
    ::ResetEvent(ovEvent);

    outRead = 0;
    if (::ReadFile(pipe, dest, chunk, &outRead, &ov)) {
        return outRead > 0;
    }
    const DWORD err = ::GetLastError();
    if (err == ERROR_MORE_DATA) {
        return outRead > 0;
    }
    if (err != ERROR_IO_PENDING) {
        return false;
    }
    HANDLE waitHandles[2] = {stopEvent, ovEvent};
    if (::WaitForMultipleObjects(2, waitHandles, FALSE, waitMs) != WAIT_OBJECT_0 + 1) {
        ::CancelIoEx(pipe, &ov);
        DWORD dummy = 0;
        (void)::GetOverlappedResult(pipe, &ov, &dummy, TRUE);
        return false;
    }
    if (!::GetOverlappedResult(pipe, &ov, &outRead, FALSE) && ::GetLastError() != ERROR_MORE_DATA) {
        return false;
    }
    return outRead > 0;
}

[[nodiscard]] bool ReadExactOverlapped(HANDLE pipe,
                                       HANDLE stopEvent,
                                       HANDLE ovEvent,
                                       void* dest,
                                       std::size_t bytesNeeded,
                                       std::chrono::steady_clock::time_point deadline) noexcept {
    std::size_t offset = 0;
    auto* out = static_cast<std::uint8_t*>(dest);
    while (offset < bytesNeeded) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const DWORD waitMs = static_cast<DWORD>(std::max<std::int64_t>(1, remMs));
        const DWORD chunk = static_cast<DWORD>(bytesNeeded - offset);
        DWORD bytesRead = 0;
        if (!ReadChunkOverlapped(pipe, stopEvent, ovEvent, out + offset, chunk, waitMs, bytesRead)) {
            return false;
        }
        offset += bytesRead;
    }
    return true;
}

[[nodiscard]] motrix::core::Result<void>
WriteExactOverlapped(HANDLE pipe,
                     HANDLE ovEvent,
                     std::span<const std::uint8_t> data,
                     std::chrono::steady_clock::time_point deadline) {
    std::size_t writtenTotal = 0;
    while (writtenTotal < data.size()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::Timeout, "Timed out writing to primary pipe"));
        }
        const auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const DWORD waitMs = static_cast<DWORD>(std::max<std::int64_t>(1, remMs));

        OVERLAPPED ov{};
        ov.hEvent = ovEvent;
        ::ResetEvent(ovEvent);

        DWORD written = 0;
        const DWORD toWrite = static_cast<DWORD>(data.size() - writtenTotal);
        if (!::WriteFile(pipe, data.data() + writtenTotal, toWrite, &written, &ov)) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return std::unexpected(
                    IpcError(motrix::core::ErrorCode::IoFailure,
                             std::format("WriteFile to primary pipe failed (code {})", err)));
            }
            if (::WaitForSingleObject(ovEvent, waitMs) != WAIT_OBJECT_0) {
                ::CancelIoEx(pipe, &ov);
                DWORD dummy = 0;
                (void)::GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                return std::unexpected(
                    IpcError(motrix::core::ErrorCode::Timeout, "Timed out writing to primary pipe"));
            }
            if (!::GetOverlappedResult(pipe, &ov, &written, FALSE)) {
                return std::unexpected(
                    IpcError(motrix::core::ErrorCode::IoFailure,
                             std::format("Overlapped WriteFile to primary pipe failed (code {})",
                                         ::GetLastError())));
            }
        }
        if (written == 0) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::IoFailure, "WriteFile to primary pipe wrote 0 bytes"));
        }
        writtenTotal += written;
    }
    return {};
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

        motrix::core::UniqueHandle stopEv(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!stopEv.IsValid()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::IoFailure, "Failed to create IPC stop event"));
        }

        const std::wstring pipeW = motrix::core::Utf8ToWide(pipeName_);
        DWORD err = 0;
        std::string errCtx;
        motrix::core::UniqueHandle pipe(CreateOwnedNamedPipeHandle(pipeW, err, errCtx));
        if (!pipe.IsValid()) {
            if (!errCtx.empty()) {
                return std::unexpected(IpcError(motrix::core::ErrorCode::IoFailure, std::move(errCtx)));
            }
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
        motrix::core::UniqueHandle client;
        DWORD lastErr = ERROR_SUCCESS;
        for (int attempt = 0; attempt < 3; ++attempt) {
            HANDLE raw =
                ::CreateFileW(pipeW.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                              nullptr);
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

        const motrix::core::UniqueHandle writeEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!writeEvent.IsValid()) {
            return std::unexpected(
                IpcError(motrix::core::ErrorCode::IoFailure, "Failed to create IPC write event"));
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kPipeWriteTimeoutMs);
        auto writeRes = WriteExactOverlapped(client.Get(), writeEvent.Get(), frame, deadline);
        client.Reset();
        return writeRes;
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
        if (err == ERROR_PIPE_CONNECTED || err == ERROR_NO_DATA) {
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
            const DWORD ovErr = ::GetLastError();
            if (ovErr == ERROR_PIPE_CONNECTED || ovErr == ERROR_NO_DATA) {
                return true;
            }
            shouldBreak = (ovErr == ERROR_OPERATION_ABORTED || st.stop_requested());
            if (!shouldBreak) {
                (void)::DisconnectNamedPipe(pipe_.Get());
            }
            return false;
        }
        return true;
    }

    void ProcessClientPayload(HANDLE ovEvent) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kPipeReadTimeoutMs);
        std::uint32_t payloadLen = 0;
        const bool gotLen = ReadExactOverlapped(
            pipe_.Get(), stopEvent_.Get(), ovEvent, &payloadLen, sizeof(payloadLen), deadline);
        if (!gotLen || payloadLen == 0 || payloadLen > kMaxIpcPayloadBytes) {
            return;
        }
        std::string jsonBuf(payloadLen, '\0');
        if (!ReadExactOverlapped(
                pipe_.Get(), stopEvent_.Get(), ovEvent, jsonBuf.data(), payloadLen, deadline)) {
            return;
        }
        auto parsed = DeserializeStartupPayload(jsonBuf);
        if (parsed && handler_) {
            handler_(*parsed);
        } else if (!parsed) {
            logger_.Warn("ipc", "Invalid IPC StartupPayload: {}", parsed.error().message);
        }
    }

    void ListenLoop(std::stop_token st) {
        const motrix::core::UniqueHandle ovEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
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
            ProcessClientPayload(ovEvent.Get());
            (void)::DisconnectNamedPipe(pipe_.Get());
        }
    }

    motrix::core::Logger& logger_;
    std::string pipeName_;
    PayloadHandler handler_;
    motrix::core::UniqueHandle stopEvent_;
    motrix::core::UniqueHandle pipe_;
    std::jthread listenerThread_;
};

}  // namespace

std::string SerializeStartupPayload(const StartupPayload& payload) {
    std::vector<std::string> torrentUtf8;
    torrentUtf8.reserve(payload.torrentFiles.size());
    for (const auto& p : payload.torrentFiles) {
        torrentUtf8.push_back(motrix::core::PathToUtf8(p));
    }

    const nlohmann::json j = {
        {"uris", payload.uris},
        {"torrentFiles", torrentUtf8},
        {"deepLinks", payload.deepLinks},
        {"activateWindow", payload.activateWindow},
    };
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

motrix::core::Result<StartupPayload> DeserializeStartupPayload(std::string_view jsonText) {
    const auto parsed = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Ipc,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid StartupPayload JSON"));
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
        payload.activateWindow = it->get_ref<const bool&>();
    }
    return payload;
}

StartupPayload BuildStartupPayloadFromArgs(std::span<const std::string> args) {
    StartupPayload payload{};
    payload.activateWindow = true;
    for (const auto& rawArg : args) {
        const std::string_view cleaned = motrix::core::TrimQuotedArg(rawArg);
        if (cleaned.empty() || cleaned.starts_with('-')) {
            continue;
        }
        if (motrix::core::StartsWithAsciiInsensitive(cleaned, "motrix://")
            || motrix::core::StartsWithAsciiInsensitive(cleaned, "mo://")) {
            payload.deepLinks.emplace_back(cleaned);
        } else if (IsSupportedUriScheme(cleaned)) {
            payload.uris.emplace_back(cleaned);
        } else if (motrix::core::EndsWithAsciiInsensitive(cleaned, ".torrent")) {
            payload.torrentFiles.push_back(motrix::core::PathFromUtf8(cleaned));
        }
    }
    return payload;
}

std::string DefaultIpcPipeName() {
    const std::string sid = QueryCurrentUserSidString();
    DWORD sessionId = 0;
    (void)::ProcessIdToSessionId(::GetCurrentProcessId(), &sessionId);
    if (sid.empty()) {
        return std::format("{}_s{}", kDefaultPipeName, sessionId);
    }
    return std::format("{}_{}_s{}", kDefaultPipeName, sid, sessionId);
}

std::unique_ptr<ISingleInstanceChannel> CreateNamedPipeChannel(motrix::core::Logger& logger,
                                                               std::string pipeName) {
    if (pipeName.empty()) {
        pipeName = DefaultIpcPipeName();
    }
    return std::make_unique<NamedPipeSingleInstanceChannel>(logger, std::move(pipeName));
}

}  // namespace motrix::ipc
