#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import motrix.app;
import motrix.test_server;

namespace {

using namespace motrix::app;
using namespace motrix::domain;
using namespace motrix::engine;
using namespace motrix::test;

struct BtTempDir {
    std::filesystem::path root;
    explicit BtTempDir(std::string_view prefix) : root(MakeUniqueTempDir(prefix)) {
        std::error_code ec;
        std::filesystem::create_directories(root / "seed", ec);
        std::filesystem::create_directories(root / "seed_state", ec);
        std::filesystem::create_directories(root / "app", ec);
        std::filesystem::create_directories(root / "dl", ec);
    }
    ~BtTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void WriteSeedFile(const std::filesystem::path& p, std::span<const std::uint8_t> data) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    REQUIRE(ofs.is_open());
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    REQUIRE(ofs.good());
}

[[nodiscard]] std::string UrlEncodeComponent(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
            || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out += std::format("%{:02X}", c);
        }
    }
    return out;
}

void WriteAppConfig(const BtTempDir& td,
                    std::uint16_t rpcPort,
                    std::uint16_t btPort,
                    std::uint16_t dhtPort) {
    UserPreferences prefs = DefaultUserPreferences();
    prefs.autoSyncTracker = false;
    prefs.enableUpnp = false;
    prefs.taskNotification = true;

    SystemConfig sys = DefaultSystemConfig();
    sys.rpcListenPort = rpcPort;
    sys.rpcSecret = "bt_leech_sec";
    sys.dir = motrix::core::PathToUtf8(td.root / "dl");
    sys.listenPort = static_cast<int>(btPort);
    sys.dhtListenPort = static_cast<int>(dhtPort);
    sys.enableDht = false;
    sys.btEnableLpd = true;
    sys.seedTime = 5;
    sys.seedRatio = 10.0;

    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "user.json", SerializeUserPreferencesJson(prefs))
            .has_value());
    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "system.json", SerializeSystemConfigJson(sys))
            .has_value());
}

struct LocalSeeder {
    std::unique_ptr<IEngineProcessManager> proc;
    std::unique_ptr<IEngineRpcClient> rpc;
    std::uint16_t btPort = 0;

    void Start(const std::filesystem::path& exePath,
               EngineKind kind,
               const BtTempDir& td,
               motrix::core::Logger& logger) {
        const std::uint16_t rpcPort = PickFreeEphemeralPort();
        btPort = PickFreeEphemeralPort();
        const std::uint16_t dhtPort = PickFreeUdpLoopbackPort();
        const std::string secret = "bt_seed_sec";

        proc = CreateEngineProcessManager(logger);
        REQUIRE(proc != nullptr);
        EngineSpawnSpec spec{};
        spec.executablePath = exePath;
        spec.hintKind = kind;
        spec.launchOptions = EngineLaunchOptions{
            .rpcPort = rpcPort,
            .rpcSecret = secret,
            .stateOrSessionPath = td.root / "seed_state",
            .sessionFilePath = td.root / "seed.session",
            .hostPid = CurrentHostProcessId(),
        };
        spec.initialOptions = {
            {"dir", motrix::core::PathToUtf8(td.root / "seed")},
            {"listen-port", std::to_string(btPort)},
            {"dht-listen-port", std::to_string(dhtPort)},
            {"enable-dht", "false"},
            {"bt-port-mapping", "false"},
            {"bt-seed-unverified", "true"},
            {"seed-time", "60"},
            {"seed-ratio", "100"},
        };
        REQUIRE(proc->Start(spec).has_value());

        rpc = CreateWsRpcClient(logger);
        REQUIRE(WaitUntil([&] { return rpc->Connect({"127.0.0.1", rpcPort, secret}).has_value(); }));
    }

    ~LocalSeeder() {
        if (rpc && rpc->IsConnected()) {
            (void)rpc->Shutdown(false);
            rpc->Disconnect();
        }
        if (proc) {
            (void)proc->Stop(std::chrono::milliseconds(3000));
        }
    }
};

[[nodiscard]] const DownloadTaskView* FindBtByName(const AppStateSnapshot& st, std::string_view name) {
    for (const auto* list : {&st.downloadingTasks, &st.waitingTasks, &st.stoppedTasks}) {
        for (const auto& t : *list) {
            if (t.name == name) {
                return &t;
            }
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Live offline BitTorrent P2P: multi-file torrent, select-file update, drawer detail, "
          "StopSeeding, magnet",
          "[downloads][live][bt]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        if (ShouldRequireLiveTests()) {
            FAIL("No local aria2c/aria2-next binary found while MOTRIX_REQUIRE_LIVE_TESTS=1.");
        }
        SKIP("No local aria2c/aria2-next binary found; skipping.");
    }
    const auto probe = ProbeEngineBinaryVersion(*exeOpt);
    REQUIRE(probe.has_value());

    BtTempDir td("motrix_native_bt_p2p_suite");
    LocalTestServer server;
    motrix::core::Logger logger;

    const std::string trackerUrl = server.BaseUrl() + "/announce";
    const std::vector<TorrentFileSpec> multiFiles = {
        {{"alpha.bin"}, LocalTestServer::MakePatternBytes(32768, 0x61)},
        {{"nested", "beta.bin"}, LocalTestServer::MakePatternBytes(49152, 0x62)},
        {{"gamma.txt"}, LocalTestServer::MakePatternBytes(16384, 0x63)},
    };
    WriteSeedFile(td.root / "seed" / "pack_demo" / "alpha.bin", multiFiles[0].data);
    WriteSeedFile(td.root / "seed" / "pack_demo" / "nested" / "beta.bin", multiFiles[1].data);
    WriteSeedFile(td.root / "seed" / "pack_demo" / "gamma.txt", multiFiles[2].data);

    const auto multiTor = BuildTestTorrent("pack_demo", trackerUrl, multiFiles, false);

    const std::vector<TorrentFileSpec> magFiles = {
        {{"mag_payload.bin"}, LocalTestServer::MakePatternBytes(32768, 0x71)},
    };
    WriteSeedFile(td.root / "seed" / "mag_payload.bin", magFiles[0].data);
    const auto magTor = BuildTestTorrent("mag_payload.bin", trackerUrl, magFiles, true);

    LocalSeeder seeder;
    seeder.Start(*exeOpt, probe->kind, td, logger);
    REQUIRE(seeder.rpc
                ->AddTorrent({multiTor.base64,
                              {},
                              {{"dir", motrix::core::PathToUtf8(td.root / "seed")},
                               {"bt-seed-unverified", "true"}}})
                .has_value());
    REQUIRE(seeder.rpc
                ->AddTorrent({magTor.base64,
                              {},
                              {{"dir", motrix::core::PathToUtf8(td.root / "seed")},
                               {"bt-seed-unverified", "true"}}})
                .has_value());

    const std::uint16_t leechRpc = PickFreeEphemeralPort();
    WriteAppConfig(td, leechRpc, PickFreeEphemeralPort(), PickFreeUdpLoopbackPort());
    RecordingWindowHost host;
    CoordinatorOptions opts{};
    opts.appDataDir = td.root / "app";
    opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    opts.loadPersistedConfig = true;
    AppCoordinator coord(opts, logger, &host);
    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; }));

    coord.Dispatch(SubmitAddTorrentCmd{
        .torrentBase64 = multiTor.base64,
        .selectedIndices = {1, 3},
        .totalFiles = 3,
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
    });

    std::string btGid;
    REQUIRE(WaitUntil(
        [&] {
            const auto* t = FindBtByName(*coord.Snapshot(), "pack_demo");
            if (!t || !t->isSeeding || t->files.size() != 3) {
                return false;
            }
            btGid = t->gid;
            return t->files[0].selected && !t->files[1].selected && t->files[2].selected;
        },
        std::chrono::seconds(20)));
    CHECK(VerifyFileMatches(td.root / "dl" / "pack_demo" / "alpha.bin", multiFiles[0].data));
    CHECK(VerifyFileMatches(td.root / "dl" / "pack_demo" / "gamma.txt", multiFiles[2].data));

    coord.Dispatch(SelectTaskCmd{btGid, true});
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        return st->detailedTask && st->detailedTask->gid == btGid && !st->detailedTrackers.empty();
    }));

    coord.Dispatch(ApplyTaskFileSelectionCmd{btGid, {1, 2, 3}});
    REQUIRE(WaitUntil(
        [&] {
            const auto st = coord.Snapshot();
            return st->detailedTask && st->detailedTask->files.size() == 3
                   && st->detailedTask->files[1].selected
                   && st->detailedTask->files[1].completedLength == multiFiles[1].data.size();
        },
        std::chrono::seconds(8)));
    CHECK(VerifyFileMatches(td.root / "dl" / "pack_demo" / "nested" / "beta.bin", multiFiles[1].data));

    coord.Dispatch(CopyTaskLinkCmd{btGid});
    CHECK(host.LastClipboard().find(multiTor.infoHashHex) != std::string::npos);

    coord.Dispatch(PauseTaskCmd{btGid});
    REQUIRE(WaitUntil([&] {
        const auto* t = FindBtByName(*coord.Snapshot(), "pack_demo");
        return t && t->status == TaskStatus::Paused;
    }));
    coord.Dispatch(RemoveTaskCmd{btGid, false});
    REQUIRE(WaitUntil([&] { return FindBtByName(*coord.Snapshot(), "pack_demo") == nullptr; }));

    const std::string magUri = std::format("magnet:?xt=urn:btih:{}&dn=mag_payload.bin&tr={}",
                                           magTor.infoHashHex,
                                           UrlEncodeComponent(trackerUrl));
    coord.Dispatch(SubmitAddUriCmd{
        .uris = {magUri},
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
    });
    REQUIRE(WaitUntil(
        [&] {
            const auto* t = FindBtByName(*coord.Snapshot(), "mag_payload.bin");
            return t && (t->isSeeding || t->status == TaskStatus::Complete);
        },
        std::chrono::seconds(20)));
    CHECK(VerifyFileMatches(td.root / "dl" / "mag_payload.bin", magFiles[0].data));
}

TEST_CASE("Live RemoveTaskCmd deleteFiles removes sidecar and partial HTTP download",
          "[downloads][live][delete]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        if (ShouldRequireLiveTests()) {
            FAIL("No local aria2c/aria2-next binary found while MOTRIX_REQUIRE_LIVE_TESTS=1.");
        }
        SKIP("No local aria2c/aria2-next binary found; skipping.");
    }

    BtTempDir td("motrix_native_delete_http_suite");
    LocalTestServer server;
    WriteAppConfig(td, PickFreeEphemeralPort(), PickFreeEphemeralPort(), PickFreeUdpLoopbackPort());

    motrix::core::Logger logger;
    CoordinatorOptions opts{};
    opts.loadPersistedConfig = true;
    opts.appDataDir = td.root / "app";
    opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    AppCoordinator coord(opts, logger);
    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; }));

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/slow/sidecar_victim.bin"},
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 1,
    });
    std::string httpGid;
    REQUIRE(WaitUntil([&] {
        for (const auto& t : coord.Snapshot()->downloadingTasks) {
            if (t.name == "sidecar_victim.bin" && t.completedLength > 0) {
                httpGid = t.gid;
                return true;
            }
        }
        return false;
    }));
    coord.Dispatch(PauseTaskCmd{httpGid});
    REQUIRE(WaitUntil([&] {
        for (const auto& t : coord.Snapshot()->waitingTasks) {
            if (t.gid == httpGid && t.status == TaskStatus::Paused) {
                return true;
            }
        }
        return false;
    }));

    const auto victimFile = td.root / "dl" / "sidecar_victim.bin";
    const auto sidecarFile = td.root / "dl" / "sidecar_victim.bin.aria2";
    WriteSeedFile(sidecarFile, std::array<std::uint8_t, 4>{1, 2, 3, 4});
    coord.Dispatch(RemoveTaskCmd{httpGid, true});
    REQUIRE(WaitUntil(
        [&] { return !std::filesystem::exists(victimFile) && !std::filesystem::exists(sidecarFile); }));
}

TEST_CASE("Live RemoveTaskCmd deleteFiles removes multi-file BT tree and prunes empty subdirs",
          "[downloads][live][delete]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        if (ShouldRequireLiveTests()) {
            FAIL("No local aria2c/aria2-next binary found while MOTRIX_REQUIRE_LIVE_TESTS=1.");
        }
        SKIP("No local aria2c/aria2-next binary found; skipping.");
    }
    const auto probe = ProbeEngineBinaryVersion(*exeOpt);
    REQUIRE(probe.has_value());

    BtTempDir td("motrix_native_delete_bt_suite");
    LocalTestServer server;
    const std::vector<TorrentFileSpec> treeFiles = {
        {{"alpha.bin"}, LocalTestServer::MakePatternBytes(32768, 0x41)},
        {{"nested", "beta.bin"}, LocalTestServer::MakePatternBytes(32768, 0x42)},
    };
    WriteSeedFile(td.root / "seed" / "del_pack" / "alpha.bin", treeFiles[0].data);
    WriteSeedFile(td.root / "seed" / "del_pack" / "nested" / "beta.bin", treeFiles[1].data);
    const auto tor = BuildTestTorrent("del_pack", server.BaseUrl() + "/announce", treeFiles, false);

    motrix::core::Logger logger;
    LocalSeeder seeder;
    seeder.Start(*exeOpt, probe->kind, td, logger);
    REQUIRE(seeder.rpc
                ->AddTorrent({tor.base64,
                              {},
                              {{"dir", motrix::core::PathToUtf8(td.root / "seed")},
                               {"bt-seed-unverified", "true"}}})
                .has_value());

    WriteAppConfig(td, PickFreeEphemeralPort(), PickFreeEphemeralPort(), PickFreeUdpLoopbackPort());
    CoordinatorOptions opts{};
    opts.loadPersistedConfig = true;
    opts.appDataDir = td.root / "app";
    opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    AppCoordinator coord(opts, logger);
    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; }));

    coord.Dispatch(SubmitAddTorrentCmd{
        .torrentBase64 = tor.base64,
        .selectedIndices = {1, 2},
        .totalFiles = 2,
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
    });
    std::string btGid;
    REQUIRE(WaitUntil(
        [&] {
            const auto* t = FindBtByName(*coord.Snapshot(), "del_pack");
            if (!t || !t->isSeeding || t->files.size() != 2) {
                return false;
            }
            btGid = t->gid;
            return true;
        },
        std::chrono::seconds(20)));
    REQUIRE(std::filesystem::exists(td.root / "dl" / "del_pack" / "nested" / "beta.bin"));

    coord.Dispatch(RemoveTaskCmd{btGid, true});
    REQUIRE(WaitUntil([&] {
        return FindBtByName(*coord.Snapshot(), "del_pack") == nullptr
               && !std::filesystem::exists(td.root / "dl" / "del_pack");
    }));
    CHECK(std::filesystem::is_directory(td.root / "dl"));
}
