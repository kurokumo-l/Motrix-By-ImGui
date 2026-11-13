export module motrix.engine:process_manager;

import std;
import motrix.core;
import :dialect;

namespace motrix::engine {

export struct EngineSpawnSpec {
    std::filesystem::path executablePath;
    EngineLaunchOptions launchOptions;
    std::vector<EngineOptionPair> initialOptions;
    EngineKind hintKind = EngineKind::Unknown;
};

export enum class EngineProcessState {
    Stopped = 0,
    Starting,
    Running,
    Stopping,
    Crashed,
};

export class IEngineProcessManager {
public:
    virtual ~IEngineProcessManager() = default;

    [[nodiscard]] virtual motrix::core::Result<void> Start(const EngineSpawnSpec& spec) = 0;
    [[nodiscard]] virtual motrix::core::Result<void> Stop(std::chrono::milliseconds gracefulTimeout) = 0;
    [[nodiscard]] virtual EngineProcessState State() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t ProcessId() const noexcept = 0;
};

}  // namespace motrix::engine
