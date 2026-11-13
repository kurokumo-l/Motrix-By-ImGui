#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;

using namespace motrix::domain;
using namespace motrix::engine;

TEST_CASE("UserPreferences and SystemConfig JSON round-trip cleanly", "[domain][config]") {
    UserPreferences prefs{};
    prefs.theme = ThemePreference::Dark;
    prefs.locale = motrix::core::Locale::ZhCN;
    prefs.hideAppMenu = true;
    prefs.newTaskShowDownloading = false;
    prefs.engineBinPath = "C:/Tools/aria2c.exe";

    const std::string userJson = SerializeUserPreferencesJson(prefs);
    const auto parsedPrefs = ParseUserPreferencesJson(userJson);
    REQUIRE(parsedPrefs.has_value());
    CHECK(parsedPrefs->theme == ThemePreference::Dark);
    CHECK(parsedPrefs->locale == motrix::core::Locale::ZhCN);
    CHECK(parsedPrefs->hideAppMenu == true);
    CHECK(parsedPrefs->newTaskShowDownloading == false);
    CHECK(parsedPrefs->engineBinPath == "C:/Tools/aria2c.exe");

    SystemConfig sys{};
    sys.dir = "D:/Downloads";
    sys.rpcListenPort = 16900;
    sys.rpcSecret = "secret-xyz";
    sys.maxConcurrentDownloads = 8;
    sys.split = 32;
    sys.continueDownloads = false;
    sys.followTorrent = false;

    const std::string sysJson = SerializeSystemConfigJson(sys);
    const auto parsedSys = ParseSystemConfigJson(sysJson);
    REQUIRE(parsedSys.has_value());
    CHECK(parsedSys->dir == "D:/Downloads");
    CHECK(parsedSys->rpcListenPort == 16900);
    CHECK(parsedSys->rpcSecret == "secret-xyz");
    CHECK(parsedSys->maxConcurrentDownloads == 8);
    CHECK(parsedSys->split == 32);
    CHECK(parsedSys->continueDownloads == false);
    CHECK(parsedSys->followTorrent == false);

    CHECK_FALSE(ParseUserPreferencesJson("{not json").has_value());
    CHECK_FALSE(ParseSystemConfigJson("[]").has_value());
}

TEST_CASE("CLI version banner parses both aria2-next and classic aria2", "[engine][probe]") {
    const std::string_view nextBanner =
        "aria2-next version 3.0.9\nCopyright (C) 2006\nEnabled Features: BitTorrent, HTTPS\n";
    const auto nextInfo = ParseCliVersionBanner(nextBanner);
    CHECK(nextInfo.kind == EngineKind::Aria2Next);
    CHECK(nextInfo.product == "aria2-next");
    CHECK(nextInfo.version == "3.0.9");
    REQUIRE(nextInfo.enabledFeatures.size() == 2);
    CHECK(nextInfo.enabledFeatures[0] == "BitTorrent");
    CHECK(nextInfo.enabledFeatures[1] == "HTTPS");

    const std::string_view classicBanner =
        "aria2 version 1.37.0\nEnabled Features: Async DNS, BitTorrent\n";
    const auto classicInfo = ParseCliVersionBanner(classicBanner);
    CHECK(classicInfo.kind == EngineKind::ClassicAria2);
    CHECK(classicInfo.product == "aria2");
    CHECK(classicInfo.version == "1.37.0");
}

TEST_CASE("JSON-RPC builders and frame parser handle requests, errors, and pushes", "[engine][rpc]") {
    const std::string req = BuildSimpleRpcRequest("req-1", "aria2.getVersion", "tok123");
    CHECK(req.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    CHECK(req.find("\"id\":\"req-1\"") != std::string::npos);
    CHECK(req.find("\"method\":\"aria2.getVersion\"") != std::string::npos);
    CHECK(req.find("\"token:tok123\"") != std::string::npos);

    const auto okFrame = ParseRpcFrame(
        R"({"id":"1","jsonrpc":"2.0","result":{"product":"aria2-next","version":"3.0.9"}})");
    REQUIRE(okFrame.has_value());
    CHECK_FALSE(okFrame->isNotification);
    CHECK_FALSE(okFrame->hasError);
    CHECK(okFrame->id == "1");

    const auto ver = ParseEngineVersionJson(okFrame->resultJson);
    REQUIRE(ver.has_value());
    CHECK(ver->kind == EngineKind::Aria2Next);
    CHECK(ver->version == "3.0.9");

    const auto classicVer = ParseEngineVersionJson(R"({"version":"1.37.0"})");
    REQUIRE(classicVer.has_value());
    CHECK(classicVer->kind == EngineKind::ClassicAria2);
    CHECK(classicVer->product == "aria2");

    const auto errFrame = ParseRpcFrame(R"({"id":"2","error":{"code":1,"message":"Unauthorized"}})");
    REQUIRE(errFrame.has_value());
    CHECK(errFrame->hasError);
    CHECK(errFrame->errorCode == 1);
    CHECK(errFrame->errorMessage == "Unauthorized");

    const auto pushFrame = ParseRpcFrame(
        R"({"jsonrpc":"2.0","method":"aria2.onDownloadComplete","params":[{"gid":"0001"}]})");
    REQUIRE(pushFrame.has_value());
    CHECK(pushFrame->isNotification);
    CHECK(pushFrame->notification.kind == EngineEventKind::DownloadComplete);
    CHECK(pushFrame->notification.gid == "0001");

    CHECK_FALSE(ParseRpcFrame("broken{json").has_value());
}

TEST_CASE("Task list parser parses HTTP/BT tasks and truncates files over 64 prioritizing selected",
          "[engine][json]") {
    std::string filesJson = "[";
    for (int i = 0; i < 80; ++i) {
        if (i > 0) {
            filesJson += ",";
        }
        const char* sel = (i == 75 || i == 76) ? "\"true\"" : "\"false\"";
        filesJson += std::format(
            R"({{"index":"{}","path":"C:/dl/f{}.bin","length":"1024","completedLength":"512","selected":{}}})",
            i + 1,
            i,
            sel);
    }
    filesJson += "]";

    const std::string payload = std::format(
        R"([{{
            "gid":"00000000000000aa",
            "status":"active",
            "totalLength":"100000",
            "completedLength":"25000",
            "downloadSpeed":"5000",
            "uploadSpeed":"1200",
            "connections":"16",
            "dir":"C:/dl",
            "infoHash":"0123456789abcdef",
            "numSeeders":"4",
            "bittorrent":{{"info":{{"name":"ubuntu.iso"}}}},
            "files":{}
        }}])",
        filesJson);

    const auto tasks = ParseTaskListJson(payload);
    REQUIRE(tasks.has_value());
    REQUIRE(tasks->size() == 1);
    const auto& t = (*tasks)[0];
    CHECK(t.gid == "00000000000000aa");
    CHECK(t.name == "ubuntu.iso");
    CHECK(t.taskType == TaskType::BitTorrent);
    CHECK(t.status == TaskStatus::Active);
    CHECK(t.totalLength == 100000);
    CHECK(t.completedLength == 25000);
    CHECK(t.filesTotal == 80);
    REQUIRE(t.files.size() == 64);
    CHECK(t.files[0].selected == true);
    CHECK(t.files[0].index == 76);
    CHECK(t.files[1].selected == true);
    CHECK(t.files[1].index == 77);
}

TEST_CASE("Live engine process spawn and WebSocket JSON-RPC smoke test", "[engine][live]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        SUCCEED("No local aria2c/aria2-next binary found; skipping live smoke test.");
        return;
    }

    const auto probe = ProbeEngineBinaryVersion(*exeOpt);
    REQUIRE(probe.has_value());
    INFO("Detected engine: " << probe->product << " " << probe->version);

    const auto tempDir = std::filesystem::temp_directory_path() / "motrix_native_m2_live_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    struct TempGuard {
        std::filesystem::path dir;
        ~TempGuard() {
            std::error_code e;
            std::filesystem::remove_all(dir, e);
        }
    } tempGuard{tempDir};

    motrix::core::Logger logger;
    auto proc = CreateEngineProcessManager(logger);
    REQUIRE(proc != nullptr);

    constexpr std::uint16_t kTestPort = 26890;
    const std::string kSecret = "m2_smoke_secret";
    const auto probeDialect = CreateDialect(probe->kind);
    EngineSpawnSpec spec{};
    spec.executablePath = *exeOpt;
    spec.hintKind = probe->kind;
    spec.launchOptions = EngineLaunchOptions{
        .rpcPort = kTestPort,
        .rpcSecret = kSecret,
        .stateOrSessionPath = probeDialect->UsesStateDirInsteadOfAria2Files()
                                  ? (tempDir / "state")
                                  : (tempDir / "engine.session"),
        .hostPid = CurrentHostProcessId(),
    };

    REQUIRE(proc->Start(spec).has_value());
    CHECK(proc->State() == EngineProcessState::Running);
    CHECK(proc->ProcessId() != 0);

    auto rpc = CreateWsRpcClient(logger);
    REQUIRE(rpc != nullptr);
    const RpcConnectionOptions connOpts{
        .host = "127.0.0.1",
        .port = kTestPort,
        .secret = kSecret,
    };
    bool connected = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (rpc->Connect(connOpts).has_value()) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    REQUIRE(connected);

    const auto ver = rpc->GetVersion();
    REQUIRE(ver.has_value());
    CHECK(ver->version == probe->version);

    const auto stat = rpc->GetGlobalStat();
    REQUIRE(stat.has_value());

    const std::array<EngineOptionPair, 1> gopts = {
        EngineOptionPair{"max-overall-download-limit", "1048576"}};
    CHECK(rpc->ChangeGlobalOption(gopts).has_value());

    AddUriRequest addReq{};
    addReq.uris = {"https://example.com/motrix-m2-smoke.bin"};
    addReq.options = {
        {"dir", motrix::core::PathToUtf8(tempDir)},
        {"pause", "true"},
        {"split", "4"},
    };
    const auto gidRes = rpc->AddUri(addReq);
    REQUIRE(gidRes.has_value());
    CHECK_FALSE(gidRes->empty());

    const auto sync1 = rpc->SyncAll();
    REQUIRE(sync1.has_value());
    bool foundWaiting = false;
    for (const auto& t : sync1->waiting) {
        if (t.gid == *gidRes) {
            foundWaiting = true;
            break;
        }
    }
    CHECK(foundWaiting);

    CHECK(rpc->ForceRemove(*gidRes).has_value());
    CHECK(rpc->RemoveDownloadResult(*gidRes).has_value());
    CHECK(rpc->PurgeDownloadResult().has_value());

    const auto sync2 = rpc->SyncAll();
    REQUIRE(sync2.has_value());
    CHECK(sync2->waiting.empty());
    CHECK(sync2->stopped.empty());

    if (!probeDialect->UsesStateDirInsteadOfAria2Files()) {
        CHECK(rpc->SaveSession().has_value());
    }
    (void)rpc->Shutdown(false);
    rpc->Disconnect();
    CHECK(proc->Stop(std::chrono::milliseconds(3000)).has_value());
    CHECK(proc->State() == EngineProcessState::Stopped);
}
