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

struct TempTestDir {
    std::filesystem::path root;
    explicit TempTestDir(std::string_view suffix)
        : root(std::filesystem::temp_directory_path() / suffix) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "app", ec);
        std::filesystem::create_directories(root / "dl", ec);
    }
    ~TempTestDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void WriteIsolatedConfig(const TempTestDir& td,
                         std::uint16_t rpcPort,
                         std::string_view secret = "dltest_secret") {
    UserPreferences prefs = DefaultUserPreferences();
    prefs.autoSyncTracker = false;
    prefs.enableUpnp = false;
    prefs.taskNotification = true;

    SystemConfig sys = DefaultSystemConfig();
    sys.rpcListenPort = rpcPort;
    sys.rpcSecret = std::string(secret);
    sys.dir = motrix::core::PathToUtf8(td.root / "dl");
    sys.split = 4;
    sys.maxConnectionPerServer = 4;
    sys.listenPort = PickFreeEphemeralPort();
    sys.dhtListenPort = PickFreeEphemeralPort();

    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "user.json", SerializeUserPreferencesJson(prefs))
            .has_value());
    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "system.json", SerializeSystemConfigJson(sys))
            .has_value());
}

[[nodiscard]] const DownloadTaskView* FindByGid(const AppStateSnapshot& st, std::string_view gid) {
    for (const auto* list : {&st.downloadingTasks, &st.waitingTasks, &st.stoppedTasks}) {
        for (const auto& t : *list) {
            if (t.gid == gid) {
                return &t;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const DownloadTaskView* FindStoppedByName(const AppStateSnapshot& st,
                                                        std::string_view substr) {
    for (const auto& t : st.stoppedTasks) {
        if (t.name.find(substr) != std::string::npos || t.uri.find(substr) != std::string::npos) {
            return &t;
        }
    }
    return nullptr;
}

[[nodiscard]] bool WaitConnected(AppCoordinator& coord) {
    return WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; });
}

[[nodiscard]] std::string MakeThunderUri(std::string_view innerUrl) {
    const std::string wrapped = std::format("AA{}ZZ", innerUrl);
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(wrapped.data()),
                                              wrapped.size());
    return std::format("thunder://{}", Base64Encode(bytes));
}

}  // namespace

TEST_CASE("Live HTTP downloads: exact, multi-split, headers, rename, thunder, redirect, chunked",
          "[downloads][live][http]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        SUCCEED("No local aria2c/aria2-next binary found; skipping.");
        return;
    }

    TempTestDir td("motrix_native_http_dl_suite");
    const std::uint16_t rpcPort = PickFreeEphemeralPort();
    WriteIsolatedConfig(td, rpcPort);

    LocalTestServer server;
    RecordingWindowHost host;
    motrix::core::Logger logger;

    CoordinatorOptions opts{};
    opts.appDataDir = td.root / "app";
    opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    opts.loadPersistedConfig = true;
    opts.enableOsIntegration = false;
    AppCoordinator coord(opts, logger, &host);
    REQUIRE(WaitConnected(coord));

    const auto smallExp = LocalTestServer::MakePatternBytes(64 * 1024, 0x11);
    const auto multiExp = LocalTestServer::MakePatternBytes(5 * 1024 * 1024, 0x22);
    const auto chunkExp = LocalTestServer::MakePatternBytes(64 * 1024, 0x55);

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/inspect/hdr.bin"},
        .outName = "custom_hdr.bin",
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 2,
        .userAgent = "MotrixSuiteUA/2.0",
        .authorization = "Bearer token999",
        .referer = "https://ref.local/dl",
        .cookie = "sid=xyz42; theme=dark",
    });

    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        const auto* t = FindStoppedByName(*st, "custom_hdr.bin");
        return t && t->status == TaskStatus::Complete;
    }));

    const auto hdrStats = server.Stats();
    CHECK(hdrStats.lastUserAgent == "MotrixSuiteUA/2.0");
    CHECK(hdrStats.lastReferer == "https://ref.local/dl");
    CHECK(hdrStats.lastAuthorization == "Bearer token999");
    CHECK(hdrStats.lastCookie == "sid=xyz42; theme=dark");
    CHECK(VerifyFileMatches(td.root / "dl" / "custom_hdr.bin", smallExp));
    CHECK_FALSE(host.Toasts().empty());

    server.ResetStats();
    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/exact/multi/big5mb.bin"},
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 4,
    });
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        const auto* t = FindStoppedByName(*st, "big5mb.bin");
        return t && t->status == TaskStatus::Complete;
    }));
    const auto splitStats = server.Stats();
    const bool isAria2Next = coord.Snapshot()->engineInfo.kind == EngineKind::Aria2Next;
    if (isAria2Next) {
        CHECK(splitStats.rangeRequests >= 2);
        CHECK(splitStats.maxConcurrent >= 2);
    } else {
        CHECK(splitStats.totalRequests >= 1);
    }
    CHECK(VerifyFileMatches(td.root / "dl" / "big5mb.bin", multiExp));

    coord.Dispatch(SubmitAddUriCmd{
        .uris =
            {
                MakeThunderUri(server.BaseUrl() + "/exact/small/th1.bin"),
                server.BaseUrl() + "/redirect/go.bin",
                server.BaseUrl() + "/chunked/stream.bin",
            },
        .outName = "should_not_collide.bin",
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 2,
    });
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        const auto* t1 = FindStoppedByName(*st, "th1.bin");
        const auto* t2 = FindStoppedByName(*st, "go.bin");
        const auto* t3 = FindStoppedByName(*st, "stream.bin");
        return t1 && t1->status == TaskStatus::Complete && t2 && t2->status == TaskStatus::Complete && t3
               && t3->status == TaskStatus::Complete;
    }));
    const bool redirMatched = VerifyFileMatches(td.root / "dl" / "redirected.bin", smallExp)
                              || VerifyFileMatches(td.root / "dl" / "go.bin", smallExp);
    CHECK(VerifyFileMatches(td.root / "dl" / "th1.bin", smallExp));
    CHECK(redirMatched);
    CHECK(VerifyFileMatches(td.root / "dl" / "stream.bin", chunkExp));
    CHECK_FALSE(std::filesystem::exists(td.root / "dl" / "should_not_collide.bin"));
}

TEST_CASE("Live controls: pause, resume, bulk pause/resume, restart error, delete files, port restart",
          "[downloads][live][controls]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        SUCCEED("No local aria2c/aria2-next binary found; skipping.");
        return;
    }

    TempTestDir td("motrix_native_controls_suite");
    const std::uint16_t rpcPort = PickFreeEphemeralPort();
    WriteIsolatedConfig(td, rpcPort);

    LocalTestServer server;
    RecordingWindowHost host;
    motrix::core::Logger logger;

    CoordinatorOptions opts{};
    opts.appDataDir = td.root / "app";
    opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    opts.loadPersistedConfig = true;
    opts.enableOsIntegration = false;
    AppCoordinator coord(opts, logger, &host);
    REQUIRE(WaitConnected(coord));

    const auto slowExp = LocalTestServer::MakePatternBytes(384 * 1024, 0x33);
    const auto toggleExp = LocalTestServer::MakePatternBytes(32 * 1024, 0x44);
    const auto smallExp = LocalTestServer::MakePatternBytes(64 * 1024, 0x11);

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/slow/slow_ctrl.bin"},
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 1,
    });
    std::string slowGid;
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        for (const auto& t : st->downloadingTasks) {
            if (t.name == "slow_ctrl.bin" && t.completedLength > 0) {
                slowGid = t.gid;
                return true;
            }
        }
        return false;
    }));
    CHECK(host.EverTrayActive());
    CHECK(host.EverNormalProgress());
    CHECK(host.EverSleepBlocked());

    coord.Dispatch(PauseTaskCmd{slowGid});
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        const auto* t = FindByGid(*st, slowGid);
        return t && t->status == TaskStatus::Paused;
    }));

    coord.Dispatch(ResumeTaskCmd{slowGid});
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        const auto* t = FindByGid(*st, slowGid);
        return t && t->status == TaskStatus::Complete;
    }));
    CHECK(VerifyFileMatches(td.root / "dl" / "slow_ctrl.bin", slowExp));

    coord.Dispatch(RemoveTaskCmd{slowGid, true});
    REQUIRE(WaitUntil([&] { return FindByGid(*coord.Snapshot(), slowGid) == nullptr; }));
    CHECK_FALSE(std::filesystem::exists(td.root / "dl" / "slow_ctrl.bin"));

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/toggle/dl?id=7"},
        .outName = "recovered.bin",
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 1,
    });
    std::string errGid;
    REQUIRE(WaitUntil(
        [&] {
            const auto st = coord.Snapshot();
            for (const auto& t : st->stoppedTasks) {
                if (t.status == TaskStatus::Error && t.name == "recovered.bin") {
                    errGid = t.gid;
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds(28)));

    server.SetToggleOk(true);
    coord.Dispatch(RestartTaskCmd{errGid});
    REQUIRE(WaitUntil([&] {
        const auto st = coord.Snapshot();
        if (FindByGid(*st, errGid) != nullptr) {
            return false;
        }
        const auto* t = FindStoppedByName(*st, "recovered.bin");
        return t && t->status == TaskStatus::Complete;
    }));
    CHECK(VerifyFileMatches(td.root / "dl" / "recovered.bin", toggleExp));

    auto stSnap = coord.Snapshot();
    auto newPrefs = stSnap->prefs;
    auto newSys = stSnap->sysConfig;
    newSys.rpcListenPort = PickFreeEphemeralPort();
    newSys.rpcSecret = "rotated_secret_99";
    newSys.maxOverallDownloadLimit = 256 * 1024;
    coord.Dispatch(SavePreferencesCmd{newPrefs, newSys, true});

    REQUIRE(WaitUntil([&] {
        const auto s = coord.Snapshot();
        return s->engineState == EngineConnectionState::Connected
               && s->sysConfig.rpcListenPort == newSys.rpcListenPort;
    }));

    coord.Dispatch(PurgeStoppedCmd{});
    REQUIRE(WaitUntil([&] { return coord.Snapshot()->stoppedTasks.empty(); }));

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/exact/small/after_restart.bin"},
        .dir = motrix::core::PathToUtf8(td.root / "dl"),
        .split = 2,
    });
    REQUIRE(WaitUntil([&] {
        const auto* t = FindStoppedByName(*coord.Snapshot(), "after_restart.bin");
        return t && t->status == TaskStatus::Complete;
    }));
    CHECK(VerifyFileMatches(td.root / "dl" / "after_restart.bin", smallExp));
}

TEST_CASE(
    "Session persistence saves unfinished task on shutdown, restores on relaunch, and resets cleanly",
    "[downloads][live][session]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        SUCCEED("No local aria2c/aria2-next binary found; skipping.");
        return;
    }

    TempTestDir td("motrix_native_session_suite");
    const std::uint16_t rpcPort = PickFreeEphemeralPort();
    WriteIsolatedConfig(td, rpcPort);

    LocalTestServer server;
    const auto slowExp = LocalTestServer::MakePatternBytes(384 * 1024, 0x33);
    std::string persistedGid;

    {
        motrix::core::Logger logger;
        CoordinatorOptions opts{};
        opts.appDataDir = td.root / "app";
        opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
        opts.loadPersistedConfig = true;
        AppCoordinator coord(opts, logger);
        REQUIRE(WaitConnected(coord));

        coord.Dispatch(SubmitAddUriCmd{
            .uris = {server.BaseUrl() + "/slow/persist_target.bin"},
            .dir = motrix::core::PathToUtf8(td.root / "dl"),
            .split = 1,
        });
        REQUIRE(WaitUntil([&] {
            for (const auto& t : coord.Snapshot()->downloadingTasks) {
                if (t.name == "persist_target.bin" && t.completedLength > 0) {
                    persistedGid = t.gid;
                    return true;
                }
            }
            return false;
        }));
        coord.Dispatch(PauseAllCmd{});
        REQUIRE(WaitUntil([&] {
            const auto* t = FindByGid(*coord.Snapshot(), persistedGid);
            return t && t->status == TaskStatus::Paused;
        }));
    }

    REQUIRE(std::filesystem::is_regular_file(td.root / "app" / "aria2.session"));

    {
        motrix::core::Logger logger;
        CoordinatorOptions opts{};
        opts.appDataDir = td.root / "app";
        opts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
        opts.loadPersistedConfig = true;
        AppCoordinator coord(opts, logger);
        REQUIRE(WaitConnected(coord));

        REQUIRE(WaitUntil([&] {
            const auto st = coord.Snapshot();
            return std::ranges::any_of(st->waitingTasks, [&](const DownloadTaskView& t) {
                return t.gid == persistedGid && t.status == TaskStatus::Paused;
            });
        }));

        coord.Dispatch(ResumeAllCmd{});
        REQUIRE(WaitUntil([&] {
            const auto* t = FindByGid(*coord.Snapshot(), persistedGid);
            return t && t->status == TaskStatus::Complete;
        }));
        CHECK(VerifyFileMatches(td.root / "dl" / "persist_target.bin", slowExp));

        coord.Dispatch(ResetSessionCmd{});
        REQUIRE(WaitUntil([&] { return coord.Snapshot()->stoppedTasks.empty(); }));
        CHECK_FALSE(std::filesystem::exists(td.root / "app" / "aria2.session"));
    }
}
