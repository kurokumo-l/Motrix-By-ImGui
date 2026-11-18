#include <catch2/catch_test_macros.hpp>

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
// clang-format on

#pragma comment(lib, "bcrypt.lib")

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

struct UpdaterTempDir {
    std::filesystem::path root;
    explicit UpdaterTempDir(std::string_view suffix)
        : root(std::filesystem::temp_directory_path() / suffix) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "app", ec);
        std::filesystem::create_directories(root / "dl", ec);
    }
    ~UpdaterTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

[[nodiscard]] std::vector<std::uint8_t> MakeSyntheticPeBytes() {
    std::vector<std::uint8_t> buf(4096, 0);
    buf[0] = 'M';
    buf[1] = 'Z';
    buf[0x3C] = 0x80;
    buf[0x80] = 'P';
    buf[0x81] = 'E';
    buf[0x84] = 0x64;
    buf[0x85] = 0x86;
    buf[0x86] = 0x01;
    buf[0x94] = 0xF0;
    buf[0x96] = 0x22;
    buf[0x98] = 0x0B;
    buf[0x99] = 0x02;
    return buf;
}

[[nodiscard]] std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return {};
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<std::uint8_t> out(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    }
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> ToBytes(std::string_view sv) {
    return {reinterpret_cast<const std::uint8_t*>(sv.data()),
            reinterpret_cast<const std::uint8_t*>(sv.data()) + sv.size()};
}

[[nodiscard]] std::string ComputeBytesSha256Hex(const std::vector<std::uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    REQUIRE(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0);
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<std::uint8_t, 32> digest{};
    const auto cleanup = [&]() {
        if (hash) {
            ::BCryptDestroyHash(hash);
        }
        if (alg) {
            ::BCryptCloseAlgorithmProvider(alg, 0);
        }
    };
    if (::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        cleanup();
        FAIL("BCryptCreateHash failed");
    }
    if (!bytes.empty()
        && ::BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0)
               != 0) {
        cleanup();
        FAIL("BCryptHashData failed");
    }
    if (::BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        cleanup();
        FAIL("BCryptFinishHash failed");
    }
    cleanup();
    std::string hex;
    hex.reserve(64);
    for (std::uint8_t b : digest) {
        hex += std::format("{:02x}", b);
    }
    return hex;
}

void WriteUpdaterConfig(const UpdaterTempDir& td, std::uint16_t rpcPort) {
    UserPreferences prefs = DefaultUserPreferences();
    prefs.autoSyncTracker = false;
    prefs.enableUpnp = false;
    prefs.traySpeedometer = true;

    SystemConfig sys = DefaultSystemConfig();
    sys.rpcListenPort = rpcPort;
    sys.rpcSecret = "upd_secret_1";
    sys.dir = motrix::core::PathToUtf8(td.root / "dl");
    sys.maxOverallDownloadLimit = 64 * 1024;
    sys.listenPort = PickFreeEphemeralPort();
    sys.dhtListenPort = PickFreeEphemeralPort();

    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "user.json", SerializeUserPreferencesJson(prefs))
            .has_value());
    REQUIRE(
        motrix::core::WriteTextAtomic(td.root / "app" / "system.json", SerializeSystemConfigJson(sys))
            .has_value());
}

}  // namespace

TEST_CASE("aria2-next release JSON parser, mirror prefix, and PE validation", "[engine][updater]") {
    CHECK(NormalizeVersionTag("v3.0.9") == "3.0.9");
    CHECK(NormalizeVersionTag("V3.1.0-rc1") == "3.1.0-rc1");
    CHECK(NormalizeVersionTag("3.0.9") == "3.0.9");

    constexpr std::string_view kGhUrl =
        "https://github.com/AnInsomniacy/aria2-next/releases/download/v3.0.9/"
        "aria2-next-3.0.9-windows-x86_64.exe";
    CHECK(ApplyGitHubMirrorPrefix(kGhUrl, "https://ghproxy.net")
          == std::format("https://ghproxy.net/{}", kGhUrl));
    CHECK(ApplyGitHubMirrorPrefix(kGhUrl, "https://ghproxy.net/")
          == std::format("https://ghproxy.net/{}", kGhUrl));
    CHECK(ApplyGitHubMirrorPrefix("http://127.0.0.1:9000/a.exe", "https://ghproxy.net/")
          == "http://127.0.0.1:9000/a.exe");

    constexpr std::string_view kMockReleaseJson = R"({
        "tag_name": "v3.1.0",
        "assets": [
            {"name": "aria2-next-3.1.0-linux-x86_64.tar.gz",
             "browser_download_url": "https://github.com/AnInsomniacy/aria2-next/releases/download/v3.1.0/a.tar.gz"},
            {"name": "aria2-next-3.1.0-windows-arm64.exe",
             "browser_download_url": "https://github.com/AnInsomniacy/aria2-next/releases/download/v3.1.0/arm64.exe"},
            {"name": "aria2-next-3.1.0-windows-x86_64.zip",
             "browser_download_url": "https://github.com/AnInsomniacy/aria2-next/releases/download/v3.1.0/x64.zip"},
            {"name": "aria2-next-3.1.0-windows-x86_64.exe",
             "browser_download_url": "https://github.com/AnInsomniacy/aria2-next/releases/download/v3.1.0/x64.exe"}
        ]
    })";
    const auto parsed = ParseAria2NextReleaseJson(kMockReleaseJson, "https://ghproxy.net");
    REQUIRE(parsed.has_value());
    CHECK(parsed->tagName == "v3.1.0");
    CHECK(parsed->normalizedVersion == "3.1.0");
    CHECK(parsed->assetName == "aria2-next-3.1.0-windows-x86_64.exe");
    CHECK(parsed->downloadUrl
          == "https://ghproxy.net/https://github.com/AnInsomniacy/aria2-next/releases/download/v3.1.0/"
             "x64.exe");
    CHECK(parsed->expectedSha256.empty());

    const std::string validHex(64, 'A');
    const auto withDigest = ParseAria2NextReleaseJson(
        std::format(R"({{"tag_name":"v3.1.1","assets":[{{"name":"aria2-next-3.1.1-windows-x86_64.exe",)"
                    R"("browser_download_url":"https://example.com/a.exe","digest":"sha256:{}"}}]}})",
                    validHex),
        {});
    REQUIRE(withDigest.has_value());
    CHECK(withDigest->expectedSha256 == std::string(64, 'a'));

    const auto badDigest = ParseAria2NextReleaseJson(
        R"({"tag_name":"v3.1.1","assets":[{"name":"aria2-next-3.1.1-windows-x86_64.exe",)"
        R"("browser_download_url":"https://example.com/a.exe","digest":"sha256:short"}]})",
        {});
    REQUIRE(badDigest.has_value());
    CHECK(badDigest->expectedSha256.empty());

    CHECK_FALSE(ParseAria2NextReleaseJson("{invalid", {}).has_value());
    CHECK_FALSE(ParseAria2NextReleaseJson(R"({"tag_name":"v1","assets":[]})", {}).has_value());

    UpdaterTempDir td("motrix_native_pe_check");
    const auto pePath = td.root / "synthetic.exe";
    const auto junkPath = td.root / "junk.exe";
    {
        const auto pe = MakeSyntheticPeBytes();
        std::ofstream out(pePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pe.data()), static_cast<std::streamsize>(pe.size()));
    }
    REQUIRE(motrix::core::WriteTextAtomic(junkPath, "not-an-exe").has_value());
    CHECK(LooksLikePeExecutableHeader(pePath));
    CHECK_FALSE(LooksLikePeExecutableHeader(junkPath));
}

TEST_CASE("StageAria2NextUpdate rejects corrupt binaries and verifies valid aria2-next offline",
          "[engine][updater][live]") {
    UpdaterTempDir td("motrix_native_stage_test");
    LocalTestServer server;
    const auto targetExe = ManagedAria2NextExecutablePath(td.root / "app");

    Aria2NextUpdateSpec missingSpec{
        .updateUrl = server.BaseUrl() + "/aria2-next/missing",
        .targetExePath = targetExe,
    };
    CHECK_FALSE(StageAria2NextUpdate(missingSpec).has_value());

    server.SetCustomRoute("/aria2-next/corrupt.exe", ToBytes("not-pe-payload"));
    server.SetCustomRoute(
        "/aria2-next/corrupt.json",
        ToBytes(std::format(R"({{"tag_name":"v9.9.9","assets":[{{"name":"aria2-next-win64.exe",)"
                            R"("browser_download_url":"{}/aria2-next/corrupt.exe"}}]}})",
                            server.BaseUrl())),
        "application/json");
    Aria2NextUpdateSpec corruptSpec{
        .updateUrl = server.BaseUrl() + "/aria2-next/corrupt.json",
        .targetExePath = targetExe,
    };
    CHECK_FALSE(StageAria2NextUpdate(corruptSpec).has_value());

    std::vector<std::uint8_t> badMzPayload(4096, 0);
    badMzPayload[0] = 'M';
    badMzPayload[1] = 'Z';
    server.SetCustomRoute("/aria2-next/fake_pe.exe", std::move(badMzPayload));
    server.SetCustomRoute(
        "/aria2-next/fake_pe.json",
        ToBytes(std::format(R"({{"tag_name":"v9.9.9","assets":[{{"name":"aria2-next-win64.exe",)"
                            R"("browser_download_url":"{}/aria2-next/fake_pe.exe"}}]}})",
                            server.BaseUrl())),
        "application/json");
    corruptSpec.updateUrl = server.BaseUrl() + "/aria2-next/fake_pe.json";
    CHECK_FALSE(StageAria2NextUpdate(corruptSpec).has_value());
    CHECK_FALSE(std::filesystem::exists(targetExe));

    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        SUCCEED("No local aria2c/aria2-next binary found; skipping full PE stage/swap.");
        return;
    }
    const auto probe = ProbeEngineBinaryVersion(*exeOpt);
    if (!probe || probe->kind != EngineKind::Aria2Next) {
        SUCCEED("Local engine is not aria2-next; skipping aria2-next binary swap check.");
        return;
    }

    auto binBytes = ReadFileBytes(*exeOpt);
    REQUIRE(!binBytes.empty());
    const std::string binSha256 = ComputeBytesSha256Hex(binBytes);
    server.SetCustomRoute("/aria2-next/download/aria2-next-9.9.9-windows-x86_64.exe",
                          std::move(binBytes));

    server.SetCustomRoute(
        "/aria2-next/releases/latest",
        ToBytes(std::format(
            R"({{"tag_name":"v9.9.9","assets":[{{"name":"aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("browser_download_url":"{}/aria2-next/download/aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("digest":"sha256:{}"}}]}})",
            server.BaseUrl(),
            binSha256)),
        "application/json");

    server.SetCustomRoute(
        "/aria2-next/releases/bad_sha",
        ToBytes(std::format(
            R"({{"tag_name":"v9.9.9","assets":[{{"name":"aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("browser_download_url":"{}/aria2-next/download/aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("digest":"sha256:{}"}}]}})",
            server.BaseUrl(),
            std::string(64, '0'))),
        "application/json");
    Aria2NextUpdateSpec badShaSpec{
        .updateUrl = server.BaseUrl() + "/aria2-next/releases/bad_sha",
        .targetExePath = targetExe,
        .currentVersion = "3.0.0",
        .currentKind = EngineKind::Aria2Next,
    };
    CHECK_FALSE(StageAria2NextUpdate(badShaSpec).has_value());

    Aria2NextUpdateSpec okSpec{
        .updateUrl = server.BaseUrl() + "/aria2-next/releases/latest",
        .targetExePath = targetExe,
        .currentVersion = "3.0.0",
        .currentKind = EngineKind::Aria2Next,
    };
    const auto stage = StageAria2NextUpdate(okSpec);
    REQUIRE(stage.has_value());
    CHECK_FALSE(stage->alreadyUpToDate);
    CHECK(stage->detectedKind == EngineKind::Aria2Next);
    REQUIRE(InstallStagedAria2Next(stage->stagedExePath, targetExe).has_value());
    CHECK(std::filesystem::exists(targetExe));
}

TEST_CASE("AppCoordinator parity: initialDetailTab, initialUri, multi-select, and batch delete",
          "[app][parity]") {
    motrix::core::Logger logger;
    CoordinatorOptions opts{};
    opts.enableEngine = false;
    opts.loadPersistedConfig = false;
    opts.populatePreviewTasks = true;
    opts.initialDetailTab = 2;
    opts.initialUri = "https://example.com/archive.zip";

    AppCoordinator mockCoord(opts, logger);
    mockCoord.Dispatch(SubmitAddUriCmd{
        .uris = {"https://example.com/one.bin", "https://example.com/two.bin"},
        .dir = "C:/Temp",
    });
    auto snap = mockCoord.Snapshot();
    CHECK(snap->activeDetailTab == 2);
    CHECK(snap->pendingPrefillUri == "https://example.com/archive.zip");
    CHECK(snap->prefillUriSeq == 1);
    REQUIRE(snap->downloadingTasks.size() >= 3u);

    const std::string g0 = snap->downloadingTasks[0].gid;
    const std::string g1 = snap->downloadingTasks[1].gid;
    const std::string g2 = snap->downloadingTasks[2].gid;

    mockCoord.Dispatch(SelectAllTasksCmd{});
    CHECK(mockCoord.Snapshot()->selectedTaskGids.size() == snap->downloadingTasks.size());

    mockCoord.Dispatch(SelectTaskCmd{g0, false, false, false});
    CHECK(mockCoord.Snapshot()->selectedTaskGids == std::vector<std::string>{g0});
    mockCoord.Dispatch(SelectTaskCmd{g2, false, false, true});
    CHECK(mockCoord.Snapshot()->selectedTaskGids == std::vector<std::string>{g0, g1, g2});
    mockCoord.Dispatch(SelectTaskCmd{g1, false, true, false});
    CHECK(mockCoord.Snapshot()->selectedTaskGids == std::vector<std::string>{g0, g2});

    mockCoord.Dispatch(RemoveTasksCmd{{g0, g2}, false});
    const auto afterDel = mockCoord.Snapshot();
    CHECK(afterDel->downloadingTasks.size() == snap->downloadingTasks.size() - 2u);
    CHECK(afterDel->selectedTaskGids.empty());
}

TEST_CASE("AppCoordinator parity: speedometer toggle, tray speed, and live aria2-next swap",
          "[app][parity][live]") {
    const auto exeOpt = DiscoverEngineExecutable("");
    if (!exeOpt.has_value()) {
        return;
    }

    motrix::core::Logger logger;
    UpdaterTempDir td("motrix_native_parity_live");
    WriteUpdaterConfig(td, PickFreeEphemeralPort());
    LocalTestServer server;
    RecordingWindowHost host;

    CoordinatorOptions liveOpts{};
    liveOpts.appDataDir = td.root / "app";
    liveOpts.defaultDownloadDir = motrix::core::PathToUtf8(td.root / "dl");
    liveOpts.loadPersistedConfig = true;
    AppCoordinator coord(liveOpts, logger, &host);
    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; }));

    coord.Dispatch(SubmitAddUriCmd{
        .uris = {server.BaseUrl() + "/slow/tray_speed_test.bin"},
        .dir = liveOpts.defaultDownloadDir,
        .split = 1,
    });
    REQUIRE(WaitUntil([&] { return host.LastTrayTooltip().find("Motrix") != std::string::npos; }));

    const auto confPath = td.root / "app" / "aria2.conf";
    coord.Dispatch(ToggleSpeedLimitCmd{});
    REQUIRE(WaitUntil([&] {
        const auto txt = motrix::core::ReadFileUtf8(confPath);
        return coord.Snapshot()->speedLimitPaused && txt
               && txt->find("max-overall-download-limit=0") != std::string::npos;
    }));
    coord.Dispatch(ToggleSpeedLimitCmd{});
    REQUIRE(WaitUntil([&] {
        const auto txt = motrix::core::ReadFileUtf8(confPath);
        return !coord.Snapshot()->speedLimitPaused && txt
               && txt->find("max-overall-download-limit=65536") != std::string::npos;
    }));

    const auto probe = ProbeEngineBinaryVersion(*exeOpt);
    if (!probe || probe->kind != EngineKind::Aria2Next) {
        return;
    }
    auto binBytes = ReadFileBytes(*exeOpt);
    REQUIRE(!binBytes.empty());
    const std::string binSha256 = ComputeBytesSha256Hex(binBytes);
    server.SetCustomRoute("/aria2-next/download/aria2-next-9.9.9-windows-x86_64.exe",
                          std::move(binBytes));

    server.SetCustomRoute(
        "/aria2-next/releases/latest",
        ToBytes(std::format(
            R"({{"tag_name":"v9.9.9","assets":[{{"name":"aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("browser_download_url":"{}/aria2-next/download/aria2-next-9.9.9-windows-x86_64.exe",)"
            R"("digest":"sha256:{}"}}]}})",
            server.BaseUrl(),
            binSha256)),
        "application/json");
    auto sSnap = coord.Snapshot();
    auto p = sSnap->prefs;
    p.aria2NextUpdateUrl = server.BaseUrl() + "/aria2-next/releases/latest";
    coord.Dispatch(SavePreferencesCmd{std::move(p), sSnap->sysConfig, false});
    coord.Dispatch(StartAria2NextUpdateCmd{true});

    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineUpdateStatus == EngineUpdateStatus::Succeeded; },
                  std::chrono::seconds(25)));
    REQUIRE(
        WaitUntil([&] { return coord.Snapshot()->engineState == EngineConnectionState::Connected; }));

    const auto afterUpd = coord.Snapshot();
    const auto managedExe = ManagedAria2NextExecutablePath(td.root / "app");
    CHECK(std::filesystem::exists(managedExe));
    CHECK(afterUpd->prefs.engineBinPath == motrix::core::PathToUtf8(managedExe));
}
