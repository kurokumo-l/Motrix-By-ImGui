export module motrix.ipc;

import std;
import motrix.core;

namespace motrix::ipc {

export inline constexpr std::string_view kDefaultPipeName = R"(\\.\pipe\MotrixNativeIPC)";

export struct StartupPayload {
    std::vector<std::string> uris;
    std::vector<std::filesystem::path> torrentFiles;
    bool activateWindow = true;
};

export class ISingleInstanceChannel {
public:
    using PayloadHandler = std::function<void(const StartupPayload&)>;

    virtual ~ISingleInstanceChannel() = default;

    [[nodiscard]] virtual motrix::core::Result<bool>
    TryAcquirePrimary(PayloadHandler onRemoteStartup) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> ForwardToPrimary(const StartupPayload& payload) = 0;
    virtual void Close() = 0;
};

}  // namespace motrix::ipc
