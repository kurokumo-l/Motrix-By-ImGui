export module motrix.engine:dialect;

import std;

namespace motrix::engine {

export enum class EngineKind {
    Unknown = 0,
    ClassicAria2,
    Aria2Next,
};

export struct EngineVersionInfo {
    EngineKind kind = EngineKind::Unknown;
    std::string product;
    std::string version;
    std::vector<std::string> enabledFeatures;
    std::vector<std::string> mediaFeatures;
    std::vector<std::string> downloadFeatures;
};

export struct EngineOptionPair {
    std::string key;
    std::string value;
};

export struct EngineLaunchOptions {
    std::uint16_t rpcPort = 16800;
    std::string rpcSecret;
    std::filesystem::path stateOrSessionPath;
    bool sessionFileExists = false;
    std::filesystem::path confPath;
    std::uint32_t hostPid = 0;
    int saveIntervalSeconds = 10;
};

export class IEngineDialect {
public:
    virtual ~IEngineDialect() = default;

    [[nodiscard]] virtual EngineKind Kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
    [[nodiscard]] virtual bool SupportsProbe() const noexcept = 0;
    [[nodiscard]] virtual bool SupportsNativeUpnp() const noexcept = 0;
    [[nodiscard]] virtual bool UsesStateDirInsteadOfAria2Files() const noexcept = 0;

    [[nodiscard]] virtual std::vector<EngineOptionPair>
    FilterGlobalOptions(std::span<const EngineOptionPair> options) const = 0;
    [[nodiscard]] virtual std::vector<EngineOptionPair>
    FilterTaskOptions(std::span<const EngineOptionPair> options) const = 0;
    [[nodiscard]] virtual std::vector<std::string>
    BuildLaunchArgs(const EngineLaunchOptions& launch,
                    std::span<const EngineOptionPair> options) const = 0;
    [[nodiscard]] virtual std::string NormalizeNoProxy(std::string_view rawBypass) const = 0;
};

export [[nodiscard]] EngineKind DetectEngineKind(std::string_view product,
                                                 std::string_view versionString) noexcept;
export [[nodiscard]] std::unique_ptr<IEngineDialect> CreateDialect(EngineKind kind);

}  // namespace motrix::engine
