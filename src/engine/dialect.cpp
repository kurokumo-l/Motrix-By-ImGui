module motrix.engine;

import std;
import motrix.core;

namespace motrix::engine {

namespace {

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
        "optimize-concurrent-downloads",
        "deferred-input",
        "enable-http-pipelining",
        "ftp-user",
        "ftp-passwd",
        "ftp-type",
        "ftp-pasv",
        "ftp-reuse-connection",
        "metalink-base-uri",
        "metalink-enable-unique-protocol",
        "metalink-language",
        "metalink-location",
        "metalink-os",
        "metalink-version",
        "metalink-preferred-protocol",
        "bt-min-crypto-level",
        "bt-save-metadata",
    };
    return std::ranges::find(kRetired, key) != std::end(kRetired);
}

[[nodiscard]] bool IsAria2NextOnlyOption(std::string_view key) noexcept {
    constexpr std::string_view kNextOnly[] = {
        "state-dir",
        "state-save-interval",
        "stop-with-process",
        "bt-port-mapping",
        "bt-encryption",
        "bt-transport",
        "bt-anonymous-mode",
        "bt-max-connections",
        "bt-first-last-piece-first",
        "detach-share-only",
        "media",
        "media-format",
        "rpc-save-upload-metadata",
    };
    return std::ranges::find(kNextOnly, key) != std::end(kNextOnly);
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

[[nodiscard]] bool HasOptionKey(std::span<const EngineOptionPair> opts, std::string_view key) noexcept {
    return std::any_of(opts.begin(), opts.end(), [&](const auto& o) { return o.key == key; });
}

[[nodiscard]] std::string ClampClassicMaxConnPerServer(std::string_view value) {
    if (const auto parsed = motrix::core::ParseStrictInt(motrix::core::TrimAscii(value))) {
        return std::to_string(std::clamp(*parsed, 1, 16));
    }
    return std::string(value);
}

void AppendCommonLaunchFlags(std::vector<std::string>& args,
                             const EngineLaunchOptions& launch,
                             std::span<const EngineOptionPair> filtered) {
    if (!launch.confPath.empty()) {
        args.push_back(std::format("--conf-path={}", motrix::core::PathToUtf8(launch.confPath)));
    } else {
        args.push_back("--no-conf=true");
    }
    if (!HasOptionKey(filtered, "rpc-allow-origin-all")) {
        args.push_back("--rpc-allow-origin-all=true");
    }
    if (!HasOptionKey(filtered, "console-log-level")) {
        const std::string lvl = launch.logLevel.empty() ? "warn" : launch.logLevel;
        args.push_back(std::format("--console-log-level={}", lvl));
    }
    if (!HasOptionKey(filtered, "log-level") && !launch.logLevel.empty()) {
        args.push_back(std::format("--log-level={}", launch.logLevel));
    }
    if (!HasOptionKey(filtered, "summary-interval")) {
        args.push_back("--summary-interval=0");
    }
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
        out.reserve(options.size() + 2);
        bool hasAsyncDns = false;
        for (const auto& opt : options) {
            std::string key = NormalizeOptionKey(opt.key);
            if (key.empty()) {
                continue;
            }
            if (key == "bt-encryption") {
                const bool req = (opt.value == "required");
                out.push_back({"bt-require-crypto", req ? "true" : "false"});
                if (opt.value != "disabled") {
                    out.push_back({"bt-min-crypto-level", req ? "arc4" : "plain"});
                }
                continue;
            }
            if (key == "detach-share-only") {
                out.push_back({"bt-detach-seed-only", opt.value});
                continue;
            }
            if (key == "rpc-save-upload-metadata") {
                out.push_back({"bt-save-metadata", opt.value});
                continue;
            }
            if (IsAria2NextOnlyOption(key)) {
                continue;
            }
            if (key == "async-dns") {
                hasAsyncDns = true;
            }
            if (key == "max-connection-per-server") {
                out.push_back({std::move(key), ClampClassicMaxConnPerServer(opt.value)});
                continue;
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
            std::string val = (key == "max-connection-per-server")
                                  ? ClampClassicMaxConnPerServer(opt.value)
                                  : opt.value;
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
        const auto filtered = FilterGlobalOptions(options);
        AppendCommonLaunchFlags(args, launch, filtered);
        if (!launch.stateOrSessionPath.empty()) {
            const std::string sessionPath = motrix::core::PathToUtf8(launch.stateOrSessionPath);
            args.push_back(std::format("--save-session={}", sessionPath));
            args.push_back(std::format("--save-session-interval={}", launch.saveIntervalSeconds));
            if (launch.sessionFileExists) {
                args.push_back(std::format("--input-file={}", sessionPath));
            }
        }
        for (const auto& opt : filtered) {
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
            std::string val = opt.value;
            if (key == "bt-require-crypto") {
                key = "bt-encryption";
                val = (opt.value == "true") ? "required" : "preferred";
            } else if (key == "no-proxy") {
                val = NormalizeNoProxy(opt.value);
            }
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
        const auto filtered = FilterGlobalOptions(options);
        AppendCommonLaunchFlags(args, launch, filtered);
        if (!launch.stateOrSessionPath.empty()) {
            args.push_back(
                std::format("--state-dir={}", motrix::core::PathToUtf8(launch.stateOrSessionPath)));
            args.push_back(std::format("--state-save-interval={}", launch.saveIntervalSeconds));
        }
        if (!launch.sessionFilePath.empty()) {
            const std::string sessPath = motrix::core::PathToUtf8(launch.sessionFilePath);
            args.push_back(std::format("--save-session={}", sessPath));
            args.push_back(std::format("--save-session-interval={}", launch.saveIntervalSeconds));
            if (launch.sessionFileExists) {
                args.push_back(std::format("--input-file={}", sessPath));
            }
        }
        if (launch.hostPid != 0) {
            args.push_back(std::format("--stop-with-process={}", launch.hostPid));
        }
        for (const auto& opt : filtered) {
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
                if (motrix::core::EqualsAsciiInsensitive(trimmed, "<local>")) {
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

std::unique_ptr<IEngineDialect> CreateDialect(EngineKind kind) {
    if (kind == EngineKind::Aria2Next) {
        return std::make_unique<Aria2NextDialect>();
    }
    return std::make_unique<ClassicAria2Dialect>();
}

namespace {

void AppendCsvFeatures(std::string_view rest, std::vector<std::string>& out) {
    std::size_t fp = 0;
    while (fp <= rest.size()) {
        const std::size_t comma = rest.find(',', fp);
        const std::string_view slice =
            (comma == std::string_view::npos) ? rest.substr(fp) : rest.substr(fp, comma - fp);
        const std::string_view feat = motrix::core::TrimAscii(slice);
        if (!feat.empty()) {
            out.emplace_back(feat);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        fp = comma + 1;
    }
}

void ParseCliHeaderLine(std::string_view line, EngineVersionInfo& info) {
    constexpr std::string_view kNextPrefix = "aria2-next version ";
    constexpr std::string_view kClassicPrefix = "aria2 version ";
    if (motrix::core::StartsWithAsciiInsensitive(line, kNextPrefix)) {
        info.product = "aria2-next";
        info.version = std::string(motrix::core::TrimAscii(line.substr(kNextPrefix.size())));
        info.kind = EngineKind::Aria2Next;
    } else if (motrix::core::StartsWithAsciiInsensitive(line, kClassicPrefix)) {
        info.product = "aria2";
        info.version = std::string(motrix::core::TrimAscii(line.substr(kClassicPrefix.size())));
        info.kind = EngineKind::ClassicAria2;
    } else {
        info.version = std::string(line);
        info.kind = DetectEngineKind("", line);
    }
}

[[nodiscard]] std::string SanitizeConfValue(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char ch : raw) {
        if (ch != '\r' && ch != '\n') {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

EngineKind DetectEngineKind(std::string_view product, std::string_view versionString) noexcept {
    if (motrix::core::ContainsAsciiInsensitive(product, "aria2-next")
        || motrix::core::ContainsAsciiInsensitive(versionString, "aria2-next")) {
        return EngineKind::Aria2Next;
    }
    if (!versionString.empty() || !product.empty()) {
        return EngineKind::ClassicAria2;
    }
    return EngineKind::Unknown;
}

EngineVersionInfo ParseCliVersionBanner(std::string_view stdoutText) {
    EngineVersionInfo info{};
    std::size_t pos = 0;
    bool firstLineDone = false;
    while (pos < stdoutText.size()) {
        const std::size_t nl = stdoutText.find('\n', pos);
        const std::size_t end = (nl == std::string_view::npos) ? stdoutText.size() : nl;
        const std::string_view line = motrix::core::TrimAscii(stdoutText.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) {
            continue;
        }
        if (!firstLineDone) {
            firstLineDone = true;
            ParseCliHeaderLine(line, info);
            continue;
        }
        constexpr std::string_view kFeatPrefix = "Enabled Features:";
        if (line.starts_with(kFeatPrefix)) {
            AppendCsvFeatures(line.substr(kFeatPrefix.size()), info.enabledFeatures);
        }
    }
    return info;
}

std::string FormatAria2Conf(std::span<const EngineOptionPair> options) {
    std::string out = "# Auto-generated and synchronized by Motrix Native for aria2-next.\n"
                      "# Modify settings from Preferences inside Motrix Native.\n\n";
    for (const auto& opt : options) {
        if (opt.key.empty()) {
            continue;
        }
        out += std::format("{}={}\n", opt.key, SanitizeConfValue(opt.value));
    }
    return out;
}

}  // namespace motrix::engine
