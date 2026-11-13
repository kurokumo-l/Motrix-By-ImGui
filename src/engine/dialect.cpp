module motrix.engine;

import std;
import motrix.core;

namespace motrix::engine {

namespace {

[[nodiscard]] bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const std::string h = motrix::core::AsciiLower(haystack);
    const std::string n = motrix::core::AsciiLower(needle);
    return h.find(n) != std::string::npos;
}

[[nodiscard]] std::string NormalizeOptionKey(std::string_view key) {
    std::string out = motrix::core::AsciiLower(motrix::core::TrimAscii(key));
    if (out.starts_with("--")) {
        out.erase(0, 2);
    }
    return out;
}

[[nodiscard]] bool IsRetiredInAria2Next(std::string_view key) noexcept {
    constexpr std::string_view kRetired[] = {
        "rpc-secure",
        "rpc-certificate",
        "rpc-private-key",
        "async-dns",
        "async-dns-server",
        "disk-cache",
        "enable-mmap",
        "max-mmap-limit",
        "event-poll",
        "bt-detach-seed-only",
        "bt-enable-hook-after-hash-check",
        "save-session",
        "save-session-interval",
        "input-file",
        "force-save",
        "save-not-found",
        "optimize-concurrent-downloads",
        "deferred-input",
        "enable-http-pipelining",
        "ftp-user",
        "ftp-passwd",
        "ftp-type",
        "ftp-pasv",
        "ftp-reuse-connection",
        "follow-metalink",
        "metalink-base-uri",
        "metalink-enable-unique-protocol",
        "metalink-language",
        "metalink-location",
        "metalink-os",
        "metalink-version",
        "metalink-preferred-protocol",
    };
    for (const auto item : kRetired) {
        if (key == item) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsAria2NextOnlyOption(std::string_view key) noexcept {
    constexpr std::string_view kNextOnly[] = {
        "state-dir",
        "state-save-interval",
        "stop-with-process",
        "bt-port-mapping",
    };
    for (const auto item : kNextOnly) {
        if (key == item) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsOctet(std::string_view s) noexcept {
    if (s.empty() || s.size() > 3) {
        return false;
    }
    int val = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        val = val * 10 + (c - '0');
    }
    return val >= 0 && val <= 255;
}

[[nodiscard]] std::string ConvertWildcardIpv4ToCidr(std::string_view token) {
    if (token.find('*') == std::string_view::npos) {
        return std::string(token);
    }

    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= token.size()) {
        const std::size_t dot = token.find('.', start);
        if (dot == std::string_view::npos) {
            parts.push_back(token.substr(start));
            break;
        }
        parts.push_back(token.substr(start, dot - start));
        start = dot + 1;
    }

    if (parts.empty() || parts.size() > 4) {
        return std::string(token);
    }

    std::array<std::string_view, 4> octets{"0", "0", "0", "0"};
    int fixedCount = 0;
    bool sawWildcard = false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == "*") {
            sawWildcard = true;
            continue;
        }
        if (sawWildcard || !IsOctet(parts[i])) {
            return std::string(token);
        }
        octets[i] = parts[i];
        ++fixedCount;
    }
    if (!sawWildcard || fixedCount == 0 || fixedCount >= 4) {
        return std::string(token);
    }
    const int prefix = fixedCount * 8;
    return std::format("{}.{}.{}.{}/{}", octets[0], octets[1], octets[2], octets[3], prefix);
}

void AppendUniqueToken(std::vector<std::string>& dest, std::string value) {
    if (value.empty()) {
        return;
    }
    if (std::find(dest.begin(), dest.end(), value) == dest.end()) {
        dest.push_back(std::move(value));
    }
}

[[nodiscard]] std::string JoinTokens(const std::vector<std::string>& tokens) {
    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out.append(tokens[i]);
    }
    return out;
}

class ClassicAria2Dialect final : public IEngineDialect {
public:
    EngineKind Kind() const noexcept override { return EngineKind::ClassicAria2; }
    std::string_view Name() const noexcept override { return "ClassicAria2"; }
    bool SupportsProbe() const noexcept override { return false; }
    bool SupportsNativeUpnp() const noexcept override { return false; }
    bool UsesStateDirInsteadOfAria2Files() const noexcept override { return false; }

    std::vector<EngineOptionPair>
    FilterGlobalOptions(std::span<const EngineOptionPair> options) const override {
        std::vector<EngineOptionPair> out;
        out.reserve(options.size() + 1);
        bool hasAsyncDns = false;
        for (const auto& opt : options) {
            std::string key = NormalizeOptionKey(opt.key);
            if (key.empty() || IsAria2NextOnlyOption(key)) {
                continue;
            }
            if (key == "async-dns") {
                hasAsyncDns = true;
            }
            out.push_back({std::move(key), opt.value});
        }
        if (!hasAsyncDns) {
            out.push_back({"async-dns", "false"});
        }
        return out;
    }

    std::vector<EngineOptionPair>
    FilterTaskOptions(std::span<const EngineOptionPair> options) const override {
        std::vector<EngineOptionPair> out;
        out.reserve(options.size());
        for (const auto& opt : options) {
            std::string key = NormalizeOptionKey(opt.key);
            if (key.empty() || IsAria2NextOnlyOption(key)) {
                continue;
            }
            out.push_back({std::move(key), opt.value});
        }
        return out;
    }

    std::vector<std::string> BuildLaunchArgs(const EngineLaunchOptions& launch,
                                             std::span<const EngineOptionPair> options) const override {
        std::vector<std::string> args;
        args.push_back("--enable-rpc=true");
        args.push_back(std::format("--rpc-listen-port={}", launch.rpcPort));
        if (!launch.rpcSecret.empty()) {
            args.push_back(std::format("--rpc-secret={}", launch.rpcSecret));
        }
        if (!launch.confPath.empty()) {
            args.push_back(std::format("--conf-path={}", launch.confPath.string()));
        }
        if (!launch.stateOrSessionPath.empty()) {
            args.push_back(std::format("--save-session={}", launch.stateOrSessionPath.string()));
            args.push_back(std::format("--save-session-interval={}", launch.saveIntervalSeconds));
            if (launch.sessionFileExists) {
                args.push_back(std::format("--input-file={}", launch.stateOrSessionPath.string()));
            }
        }
        for (const auto& opt : FilterGlobalOptions(options)) {
            args.push_back(std::format("--{}={}", opt.key, opt.value));
        }
        return args;
    }

    std::string NormalizeNoProxy(std::string_view rawBypass) const override {
        std::vector<std::string> tokens;
        std::size_t pos = 0;
        while (pos <= rawBypass.size()) {
            const std::size_t sep = rawBypass.find_first_of(",;", pos);
            const std::string_view part = (sep == std::string_view::npos)
                                              ? rawBypass.substr(pos)
                                              : rawBypass.substr(pos, sep - pos);
            const std::string_view trimmed = motrix::core::TrimAscii(part);
            if (!trimmed.empty()) {
                AppendUniqueToken(tokens, std::string(trimmed));
            }
            if (sep == std::string_view::npos) {
                break;
            }
            pos = sep + 1;
        }
        return JoinTokens(tokens);
    }
};

class Aria2NextDialect final : public IEngineDialect {
public:
    EngineKind Kind() const noexcept override { return EngineKind::Aria2Next; }
    std::string_view Name() const noexcept override { return "Aria2Next"; }
    bool SupportsProbe() const noexcept override { return true; }
    bool SupportsNativeUpnp() const noexcept override { return true; }
    bool UsesStateDirInsteadOfAria2Files() const noexcept override { return true; }

    std::vector<EngineOptionPair>
    FilterGlobalOptions(std::span<const EngineOptionPair> options) const override {
        std::vector<EngineOptionPair> out;
        out.reserve(options.size());
        for (const auto& opt : options) {
            std::string key = NormalizeOptionKey(opt.key);
            if (key.empty()) {
                continue;
            }
            if (key == "auto-save-interval") {
                key = "state-save-interval";
            }
            if (IsRetiredInAria2Next(key)) {
                continue;
            }
            std::string val = (key == "no-proxy") ? NormalizeNoProxy(opt.value) : opt.value;
            out.push_back({std::move(key), std::move(val)});
        }
        return out;
    }

    std::vector<EngineOptionPair>
    FilterTaskOptions(std::span<const EngineOptionPair> options) const override {
        std::vector<EngineOptionPair> out;
        out.reserve(options.size());
        for (const auto& opt : options) {
            std::string key = NormalizeOptionKey(opt.key);
            if (key.empty() || IsRetiredInAria2Next(key)) {
                continue;
            }
            std::string val = (key == "no-proxy") ? NormalizeNoProxy(opt.value) : opt.value;
            out.push_back({std::move(key), std::move(val)});
        }
        return out;
    }

    std::vector<std::string> BuildLaunchArgs(const EngineLaunchOptions& launch,
                                             std::span<const EngineOptionPair> options) const override {
        std::vector<std::string> args;
        args.push_back("--enable-rpc=true");
        args.push_back(std::format("--rpc-listen-port={}", launch.rpcPort));
        if (!launch.rpcSecret.empty()) {
            args.push_back(std::format("--rpc-secret={}", launch.rpcSecret));
        }
        if (!launch.confPath.empty()) {
            args.push_back(std::format("--conf-path={}", launch.confPath.string()));
        }
        if (!launch.stateOrSessionPath.empty()) {
            args.push_back(std::format("--state-dir={}", launch.stateOrSessionPath.string()));
            args.push_back(std::format("--state-save-interval={}", launch.saveIntervalSeconds));
        }
        if (launch.hostPid != 0) {
            args.push_back(std::format("--stop-with-process={}", launch.hostPid));
        }
        for (const auto& opt : FilterGlobalOptions(options)) {
            args.push_back(std::format("--{}={}", opt.key, opt.value));
        }
        return args;
    }

    std::string NormalizeNoProxy(std::string_view rawBypass) const override {
        std::vector<std::string> tokens;
        std::size_t pos = 0;
        while (pos <= rawBypass.size()) {
            const std::size_t sep = rawBypass.find_first_of(",;", pos);
            const std::string_view part = (sep == std::string_view::npos)
                                              ? rawBypass.substr(pos)
                                              : rawBypass.substr(pos, sep - pos);
            const std::string_view trimmed = motrix::core::TrimAscii(part);
            if (!trimmed.empty()) {
                if (motrix::core::AsciiLower(trimmed) == "<local>") {
                    AppendUniqueToken(tokens, "localhost");
                    AppendUniqueToken(tokens, "127.0.0.1");
                    AppendUniqueToken(tokens, "::1");
                } else {
                    AppendUniqueToken(tokens, ConvertWildcardIpv4ToCidr(trimmed));
                }
            }
            if (sep == std::string_view::npos) {
                break;
            }
            pos = sep + 1;
        }
        return JoinTokens(tokens);
    }
};

}  // namespace

EngineKind DetectEngineKind(std::string_view product, std::string_view versionString) noexcept {
    if (ContainsIgnoreCase(product, "aria2-next") || ContainsIgnoreCase(versionString, "aria2-next")) {
        return EngineKind::Aria2Next;
    }
    if (!versionString.empty() || !product.empty()) {
        return EngineKind::ClassicAria2;
    }
    return EngineKind::Unknown;
}

std::unique_ptr<IEngineDialect> CreateDialect(EngineKind kind) {
    if (kind == EngineKind::Aria2Next) {
        return std::make_unique<Aria2NextDialect>();
    }
    return std::make_unique<ClassicAria2Dialect>();
}

}  // namespace motrix::engine
