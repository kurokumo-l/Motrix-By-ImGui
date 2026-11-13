#include <catch2/catch_test_macros.hpp>

import std;
import motrix.engine;

namespace {

[[nodiscard]] bool ContainsArgPrefix(const std::vector<std::string>& args, std::string_view prefix) {
    return std::any_of(
        args.begin(), args.end(), [&](const std::string& s) { return s.starts_with(prefix); });
}

[[nodiscard]] bool ContainsOptionKey(const std::vector<motrix::engine::EngineOptionPair>& opts,
                                     std::string_view key) {
    return std::any_of(opts.begin(), opts.end(), [&](const auto& o) { return o.key == key; });
}

}  // namespace

TEST_CASE("motrix.engine detects classic aria2 vs aria2-next", "[engine][dialect]") {
    using namespace motrix::engine;
    CHECK(DetectEngineKind("aria2-next", "3.1.0") == EngineKind::Aria2Next);
    CHECK(DetectEngineKind("", "aria2-next 3.1.0") == EngineKind::Aria2Next);
    CHECK(DetectEngineKind("", "1.37.0") == EngineKind::ClassicAria2);
    CHECK(DetectEngineKind("", "") == EngineKind::Unknown);
}

TEST_CASE("motrix.engine Aria2NextDialect filters retired options and adapts state-dir",
          "[engine][dialect]") {
    using namespace motrix::engine;
    auto dialect = CreateDialect(EngineKind::Aria2Next);
    REQUIRE(dialect->Kind() == EngineKind::Aria2Next);
    CHECK(dialect->SupportsProbe());
    CHECK(dialect->SupportsNativeUpnp());
    CHECK(dialect->UsesStateDirInsteadOfAria2Files());

    const std::vector<EngineOptionPair> input = {
        {"rpc-secure", "true"},
        {"async-dns", "true"},
        {"disk-cache", "64M"},
        {"enable-mmap", "true"},
        {"bt-detach-seed-only", "true"},
        {"save-session", "old.session"},
        {"auto-save-interval", "15"},
        {"split", "64"},
        {"no-proxy", "<local>, 192.168.*, 10.*, 172.16.5.*"},
    };

    const auto filtered = dialect->FilterGlobalOptions(input);
    CHECK_FALSE(ContainsOptionKey(filtered, "rpc-secure"));
    CHECK_FALSE(ContainsOptionKey(filtered, "async-dns"));
    CHECK_FALSE(ContainsOptionKey(filtered, "disk-cache"));
    CHECK_FALSE(ContainsOptionKey(filtered, "enable-mmap"));
    CHECK_FALSE(ContainsOptionKey(filtered, "bt-detach-seed-only"));
    CHECK_FALSE(ContainsOptionKey(filtered, "save-session"));
    CHECK(ContainsOptionKey(filtered, "state-save-interval"));
    CHECK(ContainsOptionKey(filtered, "split"));

    const auto noProxyIt = std::find_if(
        filtered.begin(), filtered.end(), [](const auto& o) { return o.key == "no-proxy"; });
    REQUIRE(noProxyIt != filtered.end());
    CHECK(noProxyIt->value == "localhost,127.0.0.1,::1,192.168.0.0/16,10.0.0.0/8,172.16.5.0/24");

    EngineLaunchOptions launch{};
    launch.rpcPort = 16800;
    launch.rpcSecret = "motrix-secret";
    launch.stateOrSessionPath = "C:/Temp/motrix_state";
    launch.hostPid = 4242;
    launch.saveIntervalSeconds = 10;

    const auto args = dialect->BuildLaunchArgs(launch, input);
    CHECK(ContainsArgPrefix(args, "--state-dir="));
    CHECK(ContainsArgPrefix(args, "--state-save-interval=10"));
    CHECK(ContainsArgPrefix(args, "--stop-with-process=4242"));
    CHECK_FALSE(ContainsArgPrefix(args, "--save-session="));
    CHECK_FALSE(ContainsArgPrefix(args, "--async-dns="));
}

TEST_CASE("motrix.engine ClassicAria2Dialect preserves session and prevents IPv6 async-dns hang",
          "[engine][dialect]") {
    using namespace motrix::engine;
    auto dialect = CreateDialect(EngineKind::ClassicAria2);
    REQUIRE(dialect->Kind() == EngineKind::ClassicAria2);
    CHECK_FALSE(dialect->SupportsProbe());
    CHECK_FALSE(dialect->SupportsNativeUpnp());
    CHECK_FALSE(dialect->UsesStateDirInsteadOfAria2Files());

    const std::vector<EngineOptionPair> input = {
        {"state-dir", "C:/Temp/state"},
        {"stop-with-process", "999"},
        {"split", "32"},
    };
    const auto filtered = dialect->FilterGlobalOptions(input);
    CHECK_FALSE(ContainsOptionKey(filtered, "state-dir"));
    CHECK_FALSE(ContainsOptionKey(filtered, "stop-with-process"));
    CHECK(ContainsOptionKey(filtered, "split"));
    CHECK(ContainsOptionKey(filtered, "async-dns"));

    EngineLaunchOptions launch{};
    launch.stateOrSessionPath = "C:/Temp/aria2.session";
    launch.sessionFileExists = true;
    const auto args = dialect->BuildLaunchArgs(launch, input);
    CHECK(ContainsArgPrefix(args, "--save-session="));
    CHECK(ContainsArgPrefix(args, "--input-file="));
    CHECK(ContainsArgPrefix(args, "--async-dns=false"));
    CHECK_FALSE(ContainsArgPrefix(args, "--state-dir="));
}
