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

[[nodiscard]] std::string FindOptionValue(const std::vector<motrix::engine::EngineOptionPair>& opts,
                                          std::string_view key) {
    const auto it = std::find_if(opts.begin(), opts.end(), [&](const auto& o) { return o.key == key; });
    REQUIRE(it != opts.end());
    return it->value;
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
        {"bt-require-crypto", "true"},
        {"no-proxy", "<local>, 192.168.*, 10.*, 172.16.5.*"},
    };

    const auto filtered = dialect->FilterGlobalOptions(input);
    CHECK_FALSE(ContainsOptionKey(filtered, "rpc-secure"));
    CHECK_FALSE(ContainsOptionKey(filtered, "async-dns"));
    CHECK_FALSE(ContainsOptionKey(filtered, "disk-cache"));
    CHECK_FALSE(ContainsOptionKey(filtered, "enable-mmap"));
    CHECK_FALSE(ContainsOptionKey(filtered, "bt-detach-seed-only"));
    CHECK_FALSE(ContainsOptionKey(filtered, "bt-require-crypto"));
    CHECK(ContainsOptionKey(filtered, "bt-encryption"));
    CHECK(ContainsOptionKey(filtered, "save-session"));
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
    launch.sessionFilePath = "C:/Temp/aria2.session";
    launch.sessionFileExists = true;
    launch.hostPid = 4242;
    launch.saveIntervalSeconds = 10;

    const auto args = dialect->BuildLaunchArgs(launch, input);
    CHECK(ContainsArgPrefix(args, "--state-dir="));
    CHECK(ContainsArgPrefix(args, "--state-save-interval=10"));
    CHECK(ContainsArgPrefix(args, "--save-session=C:/Temp/aria2.session"));
    CHECK(ContainsArgPrefix(args, "--input-file=C:/Temp/aria2.session"));
    CHECK(ContainsArgPrefix(args, "--stop-with-process=4242"));
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

TEST_CASE("motrix.engine aria2.conf formatting, launch flags, and aria2-next option filtering",
          "[engine][dialect]") {
    using namespace motrix::engine;
    const std::vector<EngineOptionPair> rawConf = {
        {"", "ignored"},
        {"dir", "C:/dl\r\n"},
        {"bt-transport", "tcp"},
    };
    const std::string confText = FormatAria2Conf(rawConf);
    CHECK(confText.find("Auto-generated and synchronized") != std::string::npos);
    CHECK(confText.find("dir=C:/dl\n") != std::string::npos);
    CHECK(confText.find("bt-transport=tcp\n") != std::string::npos);
    CHECK(confText.find("=ignored") == std::string::npos);

    auto nextDialect = CreateDialect(EngineKind::Aria2Next);
    const std::vector<EngineOptionPair> nextInput = {
        {"bt-save-metadata", "true"},
        {"bt-min-crypto-level", "arc4"},
        {"bt-require-crypto", "true"},
        {"bt-transport", "tcp"},
        {"detach-share-only", "true"},
        {"media", "hls"},
        {"rpc-save-upload-metadata", "true"},
    };
    const auto filteredNext = nextDialect->FilterGlobalOptions(nextInput);
    CHECK_FALSE(ContainsOptionKey(filteredNext, "bt-save-metadata"));
    CHECK_FALSE(ContainsOptionKey(filteredNext, "bt-min-crypto-level"));
    CHECK(ContainsOptionKey(filteredNext, "bt-encryption"));
    CHECK(ContainsOptionKey(filteredNext, "bt-transport"));
    CHECK(ContainsOptionKey(filteredNext, "detach-share-only"));
    CHECK(ContainsOptionKey(filteredNext, "media"));
    CHECK(ContainsOptionKey(filteredNext, "rpc-save-upload-metadata"));

    EngineLaunchOptions withConf{};
    withConf.confPath = "C:/Temp/aria2.conf";
    withConf.logLevel = "debug";
    const auto argsConf = nextDialect->BuildLaunchArgs(withConf, filteredNext);
    CHECK(ContainsArgPrefix(argsConf, "--conf-path=C:/Temp/aria2.conf"));
    CHECK(ContainsArgPrefix(argsConf, "--log-level=debug"));
    CHECK(ContainsArgPrefix(argsConf, "--console-log-level=debug"));
    CHECK_FALSE(ContainsArgPrefix(argsConf, "--no-conf="));

    EngineLaunchOptions noConf{};
    const auto argsNoConf = nextDialect->BuildLaunchArgs(noConf, {});
    CHECK(ContainsArgPrefix(argsNoConf, "--no-conf=true"));

    auto classicDialect = CreateDialect(EngineKind::ClassicAria2);
    const auto filteredClassic = classicDialect->FilterGlobalOptions(nextInput);
    CHECK_FALSE(ContainsOptionKey(filteredClassic, "bt-transport"));
    CHECK_FALSE(ContainsOptionKey(filteredClassic, "detach-share-only"));
    CHECK_FALSE(ContainsOptionKey(filteredClassic, "media"));
    CHECK_FALSE(ContainsOptionKey(filteredClassic, "rpc-save-upload-metadata"));
    CHECK(ContainsOptionKey(filteredClassic, "bt-detach-seed-only"));
    CHECK(ContainsOptionKey(filteredClassic, "bt-save-metadata"));

    const std::vector<EngineOptionPair> reqInput = {
        EngineOptionPair{"bt-encryption", "required"},
        EngineOptionPair{"detach-share-only", "true"},
        EngineOptionPair{"rpc-save-upload-metadata", "true"},
        EngineOptionPair{"bt-transport", "utp"},
        EngineOptionPair{"media", "hls"},
    };
    const auto classicReq = classicDialect->FilterGlobalOptions(reqInput);
    CHECK_FALSE(ContainsOptionKey(classicReq, "bt-encryption"));
    CHECK_FALSE(ContainsOptionKey(classicReq, "bt-transport"));
    CHECK_FALSE(ContainsOptionKey(classicReq, "media"));
    CHECK(FindOptionValue(classicReq, "bt-require-crypto") == "true");
    CHECK(FindOptionValue(classicReq, "bt-min-crypto-level") == "arc4");
    CHECK(FindOptionValue(classicReq, "bt-detach-seed-only") == "true");
    CHECK(FindOptionValue(classicReq, "bt-save-metadata") == "true");

    const std::vector<EngineOptionPair> optInput = {
        EngineOptionPair{"bt-encryption", "optional"},
        EngineOptionPair{"max-connection-per-server", "64"},
    };
    const auto classicOpt = classicDialect->FilterGlobalOptions(optInput);
    CHECK(FindOptionValue(classicOpt, "bt-require-crypto") == "false");
    CHECK(FindOptionValue(classicOpt, "bt-min-crypto-level") == "plain");
    CHECK(FindOptionValue(classicOpt, "max-connection-per-server") == "16");

    const auto classicTaskOpts = classicDialect->FilterTaskOptions(optInput);
    CHECK(FindOptionValue(classicTaskOpts, "max-connection-per-server") == "16");

    const std::vector<EngineOptionPair> nextMaxConnInput = {
        EngineOptionPair{"max-connection-per-server", "128"},
    };
    const auto nextMaxConn = nextDialect->FilterTaskOptions(nextMaxConnInput);
    CHECK(FindOptionValue(nextMaxConn, "max-connection-per-server") == "128");
}

TEST_CASE("motrix.engine ClassicAria2Dialect normalizes no-proxy, preserves bt-max-peers, and "
          "FormatAria2Conf rejects invalid keys",
          "[engine][dialect]") {
    using namespace motrix::engine;
    auto classicDialect = CreateDialect(EngineKind::ClassicAria2);

    const std::vector<EngineOptionPair> globalInput = {
        {"no-proxy", " localhost ; 127.0.0.1 , localhost "},
        {"bt-max-peers", "64"},
        {"bt-max-connections", "128"},
    };
    const auto globalFiltered = classicDialect->FilterGlobalOptions(globalInput);
    CHECK(FindOptionValue(globalFiltered, "no-proxy") == "localhost,127.0.0.1");
    CHECK(FindOptionValue(globalFiltered, "bt-max-peers") == "64");
    CHECK_FALSE(ContainsOptionKey(globalFiltered, "bt-max-connections"));

    const std::vector<EngineOptionPair> emptyBypassInput = {
        {"no-proxy", " , ; "},
    };
    const auto emptyBypassFiltered = classicDialect->FilterGlobalOptions(emptyBypassInput);
    CHECK(FindOptionValue(emptyBypassFiltered, "no-proxy").empty());

    const std::vector<EngineOptionPair> taskInput = {
        {"no-proxy", " *.local ; 10.0.0.0/8 , *.local "},
        {"bt-max-peers", "32"},
        {"bt-max-connections", "64"},
    };
    const auto taskFiltered = classicDialect->FilterTaskOptions(taskInput);
    CHECK(FindOptionValue(taskFiltered, "no-proxy") == "*.local,10.0.0.0/8");
    CHECK(FindOptionValue(taskFiltered, "bt-max-peers") == "32");
    CHECK_FALSE(ContainsOptionKey(taskFiltered, "bt-max-connections"));

    const std::vector<EngineOptionPair> confEntries = {
        {"", "ignored_empty"},
        {"dir\nevil", "C:/evil"},
        {"split\r16", "ignored_cr"},
        {"a=b", "ignored_eq"},
        {"#comment", "ignored_hash"},
        {"bad key", "ignored_space"},
        {"tab\tkey", "ignored_tab"},
        {"slash/key", "ignored_slash"},
        {std::string("nul\0key", 7), "ignored_nul"},
        {"dir", "C:/dl"},
        {"bt-transport", "tcp"},
        {"user_agent_2", "Motrix/1.0"},
        {"RPC2", "enabled"},
    };
    const std::string confText = FormatAria2Conf(confEntries);
    CHECK(confText.find("\ndir=C:/dl\n") != std::string::npos);
    CHECK(confText.find("\nbt-transport=tcp\n") != std::string::npos);
    CHECK(confText.find("\nuser_agent_2=Motrix/1.0\n") != std::string::npos);
    CHECK(confText.find("\nRPC2=enabled\n") != std::string::npos);
    CHECK(confText.find("ignored_") == std::string::npos);
    CHECK(confText.find("C:/evil") == std::string::npos);
    CHECK(confText.find("a=b") == std::string::npos);
    CHECK(confText.find("#comment") == std::string::npos);
    CHECK(confText.find("bad key") == std::string::npos);
    CHECK(confText.find("tab\tkey") == std::string::npos);
    CHECK(confText.find("slash/key") == std::string::npos);
}
