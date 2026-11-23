#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.ipc;
import motrix.engine;
import motrix.platform;
import motrix.app;

using namespace motrix::domain;
using namespace motrix::ipc;

TEST_CASE("motrix.domain UserPreferences M4 fields JSON roundtrip and nested protocols",
          "[domain][config][m4]") {
    UserPreferences prefs = DefaultUserPreferences();
    REQUIRE(prefs.trackerSource.size() == 2u);
    CHECK(prefs.trackerSource == DefaultTrackerSources());

    prefs.trackerSource = {
        "https://fastly.jsdelivr.net/gh/ngosang/trackerslist/trackers_all_ip.txt",
        "https://fastly.jsdelivr.net/gh/XIU2/TrackersListCollection/best.txt",
    };
    prefs.lastSyncTrackerTime = 1760000123ll;
    prefs.protocolMagnet = false;
    prefs.protocolThunder = true;
    prefs.protocolMotrix = false;
    prefs.protocolTorrent = true;
    prefs.windowX = -120;
    prefs.windowY = 48;
    prefs.windowWidth = 1280;
    prefs.windowHeight = 800;
    prefs.windowMaximized = true;

    const std::string json = SerializeUserPreferencesJson(prefs);
    CHECK(json.find("\"protocols\"") != std::string::npos);
    CHECK(json.find("\"windowState\"") != std::string::npos);

    const auto parsed = ParseUserPreferencesJson(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->trackerSource == prefs.trackerSource);
    CHECK(parsed->lastSyncTrackerTime == 1760000123ll);
    CHECK(parsed->protocolMagnet == false);
    CHECK(parsed->protocolThunder == true);
    CHECK(parsed->protocolMotrix == false);
    CHECK(parsed->protocolTorrent == true);
    REQUIRE(parsed->windowX.has_value());
    REQUIRE(parsed->windowY.has_value());
    CHECK(*parsed->windowX == -120);
    CHECK(*parsed->windowY == 48);
    CHECK(parsed->windowWidth == 1280);
    CHECK(parsed->windowHeight == 800);
    CHECK(parsed->windowMaximized == true);

    UserPreferences noPos = DefaultUserPreferences();
    noPos.windowX.reset();
    noPos.windowY.reset();
    const auto parsedNoPos = ParseUserPreferencesJson(SerializeUserPreferencesJson(noPos));
    REQUIRE(parsedNoPos.has_value());
    CHECK_FALSE(parsedNoPos->windowX.has_value());
    CHECK_FALSE(parsedNoPos->windowY.has_value());

    const auto parsedMinimal = ParseUserPreferencesJson(R"({"theme":"dark"})");
    REQUIRE(parsedMinimal.has_value());
    CHECK(parsedMinimal->trackerSource == DefaultTrackerSources());

    const auto parsedEmptyTrackers = ParseUserPreferencesJson(R"({"trackerSource":[]})");
    REQUIRE(parsedEmptyTrackers.has_value());
    CHECK(parsedEmptyTrackers->trackerSource.empty());

    const auto parsedFlatProtocols = ParseUserPreferencesJson(
        R"({"protocolMagnet":false,"protocolThunder":false,"protocolMotrix":true,"protocolTorrent":true})");
    REQUIRE(parsedFlatProtocols.has_value());
    CHECK(parsedFlatProtocols->protocolMagnet == false);
    CHECK(parsedFlatProtocols->protocolThunder == false);
    CHECK(parsedFlatProtocols->protocolMotrix == true);
    CHECK(parsedFlatProtocols->protocolTorrent == true);

    const auto parsedNestedProtocols = ParseUserPreferencesJson(
        R"({"protocols":{"magnet":false,"thunder":true,"motrix":false,"torrent":true}})");
    REQUIRE(parsedNestedProtocols.has_value());
    CHECK(parsedNestedProtocols->protocolMagnet == false);
    CHECK(parsedNestedProtocols->protocolThunder == true);
    CHECK(parsedNestedProtocols->protocolMotrix == false);
    CHECK(parsedNestedProtocols->protocolTorrent == true);
}

TEST_CASE("motrix.domain tracker presets, normalization, boundary reduction, and auto-sync",
          "[domain][tracker][m4]") {
    const auto options = AllTrackerSourceOptions();
    REQUIRE(options.size() == 14u);
    CHECK(options[0].key == "ngosang_best_ip_cdn");
    CHECK(options[1].key == "ngosang_best_cdn");
    CHECK(options[13].key == "xiu2_http");

    const auto defaults = DefaultTrackerSources();
    REQUIRE(defaults.size() == 2u);
    CHECK(defaults[0] == options[0].url);
    CHECK(defaults[1] == options[1].url);

    constexpr std::string_view kRaw =
        "# Header comment, with comma\r\n"
        "\r\n"
        "  udp://tracker.one:80/announce  \n"
        "udp://tracker.two:6969/announce, udp://tracker.one:80/announce \r\n"
        "# trailing comment\n"
        "  https://tracker.three/announce , \n";

    const std::string lines = ConvertTrackerDataToLine(kRaw);
    CHECK(lines
          == "udp://tracker.one:80/announce\n"
             "udp://tracker.two:6969/announce\n"
             "https://tracker.three/announce");

    const std::string comma = ConvertTrackerDataToComma(kRaw, kMaxBtTrackerLength);
    CHECK(comma
          == "udp://tracker.one:80/announce,"
             "udp://tracker.two:6969/announce,"
             "https://tracker.three/announce");

    // Build > 6144 bytes of trackers and verify ReduceTrackerString never cuts a URL in half.
    std::string manyRaw;
    for (int i = 0; i < 200; ++i) {
        manyRaw += std::format("udp://tracker{:03d}.example.org:6969/announce\n", i);
    }
    const std::string reduced = ConvertTrackerDataToComma(manyRaw, kMaxBtTrackerLength);
    CHECK(reduced.size() <= kMaxBtTrackerLength);
    CHECK(reduced.size() > 6000u);
    CHECK(reduced.back() != ',');
    const std::size_t lastSlash = reduced.rfind('/');
    REQUIRE(lastSlash != std::string::npos);
    CHECK(reduced.substr(lastSlash) == "/announce");

    CHECK(ReduceTrackerString("udp://a,udp://b,udp://c", 7) == "udp://a");
    CHECK(ReduceTrackerString("udp://a,udp://b,udp://c", 9) == "udp://a");
    CHECK(ReduceTrackerString("udp://a,udp://b,udp://c", 15) == "udp://a,udp://b");

    UserPreferences syncPrefs = DefaultUserPreferences();
    syncPrefs.autoSyncTracker = true;
    syncPrefs.lastSyncTrackerTime = 0;
    CHECK(ShouldAutoSyncTrackers(syncPrefs, 1000));

    syncPrefs.lastSyncTrackerTime = 100000;
    CHECK_FALSE(ShouldAutoSyncTrackers(syncPrefs, 100000 + kAutoSyncTrackerIntervalSec - 1));
    CHECK(ShouldAutoSyncTrackers(syncPrefs, 100000 + kAutoSyncTrackerIntervalSec));

    syncPrefs.autoSyncTracker = false;
    CHECK_FALSE(ShouldAutoSyncTrackers(syncPrefs, 100000 + kAutoSyncTrackerIntervalSec));

    syncPrefs.autoSyncTracker = true;
    syncPrefs.trackerSource.clear();
    CHECK_FALSE(ShouldAutoSyncTrackers(syncPrefs, 100000 + kAutoSyncTrackerIntervalSec));
}

TEST_CASE("motrix.domain deep links, supported URIs, and startup arg classification",
          "[domain][uri][m4]") {
    CHECK(IsSupportedDownloadUri("HTTPS://example.com/a.zip"));
    CHECK(IsSupportedDownloadUri("  http://example.com/a.zip  "));
    CHECK(IsSupportedDownloadUri("ftp://files.example.com/a.bin"));
    CHECK(IsSupportedDownloadUri("sftp://host/file"));
    CHECK(IsSupportedDownloadUri("MAGNET:?xt=urn:btih:1234"));
    CHECK(IsSupportedDownloadUri("THUNDER://QUFodHRwczovL2V4YW1wbGUuY29tL2ZpbGUuemlwWlo="));
    CHECK_FALSE(IsSupportedDownloadUri("--hidden"));
    CHECK_FALSE(IsSupportedDownloadUri("C:\\Downloads\\ubuntu.torrent"));

    const auto dlNew = ParseMotrixDeepLink("motrix://new-task?uri=https%3A%2F%2Fexample.com%2Fa.zip");
    CHECK(dlNew.action == DeepLinkAction::NewTask);
    CHECK(dlNew.uriParam == "https://example.com/a.zip");

    const auto dlPlus =
        ParseMotrixDeepLink("motrix://new-task?uri=https%3A%2F%2Fexample.com%2Fv1+2%2B3.tar.gz");
    CHECK(dlPlus.action == DeepLinkAction::NewTask);
    CHECK(dlPlus.uriParam == "https://example.com/v1+2+3.tar.gz");

    CHECK(ParseMotrixDeepLink("MO://PREFERENCES").action == DeepLinkAction::Preferences);
    CHECK(ParseMotrixDeepLink("mo://prefs").action == DeepLinkAction::Preferences);
    CHECK(ParseMotrixDeepLink("mo://about").action == DeepLinkAction::About);
    CHECK(ParseMotrixDeepLink("mo://task-list").action == DeepLinkAction::TaskList);
    CHECK(ParseMotrixDeepLink("motrix://pause-all-task").action == DeepLinkAction::PauseAll);
    CHECK(ParseMotrixDeepLink("motrix://pause-all").action == DeepLinkAction::PauseAll);
    CHECK(ParseMotrixDeepLink("motrix://resume-all-task").action == DeepLinkAction::ResumeAll);
    CHECK(ParseMotrixDeepLink("motrix://resume-all").action == DeepLinkAction::ResumeAll);
    CHECK(ParseMotrixDeepLink("motrix://new-bt-task").action == DeepLinkAction::NewBtTask);
    CHECK(ParseMotrixDeepLink("https://example.com").action == DeepLinkAction::None);

    const auto argFlag = ClassifyStartupArg("  --minimized  ");
    CHECK(argFlag.kind == StartupArgKind::None);
    CHECK(ClassifyStartupArg("-v").kind == StartupArgKind::None);

    const auto argTorrent = ClassifyStartupArg(R"(  "C:\Downloads\Ubuntu 24.04.TORRENT"  )");
    CHECK(argTorrent.kind == StartupArgKind::TorrentFile);
    CHECK(motrix::core::PathToUtf8(argTorrent.filePath) == R"(C:\Downloads\Ubuntu 24.04.TORRENT)");

    const auto argRemoteTorrent = ClassifyStartupArg("https://example.com/ubuntu.torrent");
    CHECK(argRemoteTorrent.kind == StartupArgKind::ResourceUri);
    CHECK(argRemoteTorrent.uri == "https://example.com/ubuntu.torrent");

    const auto argThunder = ClassifyStartupArg("thunder://QUFodHRwczovL2V4YW1wbGUuY29tL2ZpbGUuemlwWlo=");
    CHECK(argThunder.kind == StartupArgKind::ResourceUri);
    CHECK(argThunder.uri == "https://example.com/file.zip");

    const auto argBadThunder = ClassifyStartupArg("thunder://not-valid-base64!!!");
    CHECK(argBadThunder.kind == StartupArgKind::None);
    CHECK(argBadThunder.uri.empty());

    const auto argMagnet = ClassifyStartupArg("magnet:?xt=urn:btih:0123456789abcdef");
    CHECK(argMagnet.kind == StartupArgKind::ResourceUri);
    CHECK(argMagnet.uri == "magnet:?xt=urn:btih:0123456789abcdef");

    const auto argDeep = ClassifyStartupArg("motrix://new-task?uri=https%3A%2F%2Fexample.com%2Fa.zip");
    CHECK(argDeep.kind == StartupArgKind::DeepLink);
    CHECK(argDeep.deepLink.action == DeepLinkAction::NewTask);
    CHECK(argDeep.deepLink.uriParam == "https://example.com/a.zip");
}

TEST_CASE("motrix.ipc StartupPayload serialization and arg builder", "[ipc][m4]") {
    CHECK_FALSE(motrix::platform::CurrentExecutablePath().empty());

    StartupPayload original{};
    original.uris = {"https://example.com/a.zip", "magnet:?xt=urn:btih:abcdef"};
    original.torrentFiles = {motrix::core::PathFromUtf8(R"(D:\测试\ubuntu.torrent)")};
    original.deepLinks = {"motrix://new-task?uri=https%3A%2F%2Fexample.com%2Fb.zip"};
    original.activateWindow = false;

    const std::string json = SerializeStartupPayload(original);
    const auto decoded = DeserializeStartupPayload(json);
    REQUIRE(decoded.has_value());
    CHECK(decoded->uris == original.uris);
    REQUIRE(decoded->torrentFiles.size() == 1u);
    CHECK(motrix::core::PathToUtf8(decoded->torrentFiles[0]) == R"(D:\测试\ubuntu.torrent)");
    CHECK(decoded->deepLinks == original.deepLinks);
    CHECK(decoded->activateWindow == false);

    CHECK_FALSE(DeserializeStartupPayload("{broken").has_value());

    const std::vector<std::string> args = {
        "--minimized",
        "-x",
        R"("https://example.com/1.iso")",
        R"(C:\dl\arch.torrent)",
        "motrix://preferences",
        "ignored-bare-word",
    };
    const StartupPayload built = BuildStartupPayloadFromArgs(args);
    CHECK(built.activateWindow == true);
    REQUIRE(built.uris.size() == 1u);
    CHECK(built.uris[0] == "https://example.com/1.iso");
    REQUIRE(built.torrentFiles.size() == 1u);
    CHECK(motrix::core::PathToUtf8(built.torrentFiles[0]) == R"(C:\dl\arch.torrent)");
    REQUIRE(built.deepLinks.size() == 1u);
    CHECK(built.deepLinks[0] == "motrix://preferences");
}

TEST_CASE("motrix.ipc NamedPipe single-instance acquire, forward, and teardown", "[ipc][pipe][m4]") {
    motrix::core::Logger logger;
    static std::atomic<std::uint64_t> m4PipeSeq{0};
    const std::string pipeName = std::string(R"(\\.\pipe\MotrixNativeTest_)")
                                 + std::to_string(motrix::engine::CurrentHostProcessId()) + "_"
                                 + std::to_string(m4PipeSeq.fetch_add(1, std::memory_order_relaxed));

    auto primary = CreateNamedPipeChannel(logger, pipeName);
    auto secondary = CreateNamedPipeChannel(logger, pipeName);
    REQUIRE(primary != nullptr);
    REQUIRE(secondary != nullptr);

    std::mutex mu;
    std::condition_variable cv;
    bool receivedFlag = false;
    StartupPayload receivedPayload{};

    const auto acq1 = primary->TryAcquirePrimary([&](const StartupPayload& payload) {
        {
            std::lock_guard lock(mu);
            receivedPayload = payload;
            receivedFlag = true;
        }
        cv.notify_one();
    });
    REQUIRE(acq1.has_value());
    CHECK(*acq1 == true);

    const auto acq2 = secondary->TryAcquirePrimary([](const StartupPayload&) {});
    REQUIRE(acq2.has_value());
    CHECK(*acq2 == false);

    StartupPayload outgoing{};
    outgoing.uris = {"https://example.com/release.iso"};
    outgoing.torrentFiles = {motrix::core::PathFromUtf8(R"(C:\temp\distro.torrent)")};
    outgoing.deepLinks = {"motrix://new-task?uri=https%3A%2F%2Fexample.com%2Fb.bin"};
    outgoing.activateWindow = true;

    const auto fwd = secondary->ForwardToPrimary(outgoing);
    REQUIRE(fwd.has_value());

    {
        std::unique_lock lock(mu);
        const bool ok = cv.wait_for(lock, std::chrono::milliseconds(3000), [&] { return receivedFlag; });
        REQUIRE(ok);
        CHECK(receivedPayload.uris == outgoing.uris);
        REQUIRE(receivedPayload.torrentFiles.size() == 1u);
        CHECK(motrix::core::PathToUtf8(receivedPayload.torrentFiles[0]) == R"(C:\temp\distro.torrent)");
        CHECK(receivedPayload.deepLinks == outgoing.deepLinks);
        CHECK(receivedPayload.activateWindow == true);
    }

    secondary->Close();
    primary->Close();

    auto reacquired = CreateNamedPipeChannel(logger, pipeName);
    const auto acq3 = reacquired->TryAcquirePrimary([](const StartupPayload&) {});
    REQUIRE(acq3.has_value());
    CHECK(*acq3 == true);
    reacquired->Close();
}

TEST_CASE("motrix.platform Motrix M tray and window icon pixel rendering", "[platform][icon][m4]") {
    using motrix::platform::MotrixIconKind;
    using motrix::platform::RenderMotrixIconBgra;
    using motrix::platform::ResolveMotrixIconResource;

    CHECK(ResolveMotrixIconResource(MotrixIconKind::AppBadge).resourceId == 101);
    CHECK(std::wstring_view(ResolveMotrixIconResource(MotrixIconKind::AppBadge).fileName) == L"app.ico");
    CHECK(ResolveMotrixIconResource(MotrixIconKind::TrayIdle).resourceId == 102);
    CHECK(std::wstring_view(ResolveMotrixIconResource(MotrixIconKind::TrayIdle).fileName)
          == L"tray-normal.ico");
    CHECK(ResolveMotrixIconResource(MotrixIconKind::TrayActive).resourceId == 103);
    CHECK(std::wstring_view(ResolveMotrixIconResource(MotrixIconKind::TrayActive).fileName)
          == L"tray-active.ico");

    const auto at = [](const std::vector<std::uint32_t>& buf, int size, int x, int y) {
        return buf[static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                   + static_cast<std::size_t>(x)];
    };
    const auto alpha = [](std::uint32_t bgra) -> std::uint32_t { return (bgra >> 24u) & 0xFFu; };
    const auto red = [](std::uint32_t bgra) -> std::uint32_t { return (bgra >> 16u) & 0xFFu; };
    const auto green = [](std::uint32_t bgra) -> std::uint32_t { return (bgra >> 8u) & 0xFFu; };
    const auto blue = [](std::uint32_t bgra) -> std::uint32_t { return bgra & 0xFFu; };

    const auto darkTray = RenderMotrixIconBgra(MotrixIconKind::TrayIdle, 32, true);
    const auto lightTray = RenderMotrixIconBgra(MotrixIconKind::TrayIdle, 32, false);
    REQUIRE(darkTray.size() == 32u * 32u);
    REQUIRE(lightTray.size() == 32u * 32u);
    CHECK(alpha(at(darkTray, 32, 0, 0)) == 0u);
    const std::uint32_t framePx = at(darkTray, 32, 6, 16);
    CHECK(alpha(framePx) > 240u);
    CHECK(blue(framePx) > green(framePx));
    CHECK(blue(framePx) > red(framePx));
    const std::uint32_t facePx = at(darkTray, 32, 16, 16);
    CHECK(alpha(facePx) > 240u);
    CHECK(red(facePx) > 220u);
    CHECK(green(facePx) > 220u);
    CHECK(blue(facePx) > 220u);
    const std::uint32_t visorPx = at(lightTray, 32, 16, 10);
    CHECK(alpha(visorPx) > 240u);
    CHECK(red(visorPx) < 60u);
    CHECK(green(visorPx) < 60u);
    CHECK(blue(visorPx) < 60u);

    const auto activeTray = RenderMotrixIconBgra(MotrixIconKind::TrayActive, 32, true);
    REQUIRE(activeTray.size() == 32u * 32u);
    const std::uint32_t dotPx = at(activeTray, 32, 26, 5);
    CHECK(alpha(dotPx) > 220u);
    CHECK(green(dotPx) > red(dotPx));
    CHECK(green(dotPx) > blue(dotPx));

    const auto badge = RenderMotrixIconBgra(MotrixIconKind::AppBadge, 32, true);
    REQUIRE(badge.size() == 32u * 32u);
    CHECK(alpha(at(badge, 32, 0, 0)) == 0u);
    const std::uint32_t bgPx = at(badge, 32, 6, 16);
    CHECK(alpha(bgPx) > 240u);
    CHECK(blue(bgPx) > green(bgPx));
    const std::uint32_t glyphPx = at(badge, 32, 16, 16);
    CHECK(red(glyphPx) > 220u);
    CHECK(green(glyphPx) > 220u);
    CHECK(blue(glyphPx) > 220u);
}

TEST_CASE(
    "motrix.app HandleStartupPayload preserves both URIs and torrents and keeps pause/resume quiet",
    "[app][ipc][m4]") {
    motrix::core::Logger logger;
    motrix::app::CoordinatorOptions opts{};
    opts.enableEngine = false;
    opts.loadPersistedConfig = false;

    motrix::app::AppCoordinator coord(opts, logger);
    coord.Dispatch(motrix::app::HandleStartupPayloadCmd{
        .uris = {"https://example.com/archive.tar.zst"},
        .torrentFiles = {R"(C:\Downloads\ubuntu.torrent)"},
        .deepLinks = {},
        .activateWindow = true,
    });
    const auto afterAdd = coord.Snapshot();
    CHECK(afterAdd->activeModal == motrix::app::ActiveModal::AddTask);
    CHECK(afterAdd->pendingPrefillUri == "https://example.com/archive.tar.zst");
    CHECK(afterAdd->pendingDroppedTorrentPath == R"(C:\Downloads\ubuntu.torrent)");

    coord.Dispatch(motrix::app::CloseModalCmd{});
    coord.Dispatch(motrix::app::HandleStartupPayloadCmd{
        .uris = {},
        .torrentFiles = {},
        .deepLinks = {"motrix://pause-all-task"},
        .activateWindow = false,
    });
    const auto afterPause = coord.Snapshot();
    CHECK(afterPause->activeModal == motrix::app::ActiveModal::None);
}

TEST_CASE("motrix.app FactoryResetCmd and ResetSessionCmd reset session and regenerate secret",
          "[app][parity][m4]") {
    motrix::core::Logger logger;
    motrix::app::CoordinatorOptions opts{};
    opts.enableEngine = false;
    opts.loadPersistedConfig = false;
    opts.populatePreviewTasks = true;

    motrix::app::AppCoordinator coord(opts, logger);
    const auto initial = coord.Snapshot();
    REQUIRE(!initial->downloadingTasks.empty());

    auto customPrefs = initial->prefs;
    customPrefs.theme = ThemePreference::Dark;
    auto customSys = initial->sysConfig;
    customSys.maxConcurrentDownloads = 16;
    customSys.rpcSecret = "old_custom_secret";
    coord.Dispatch(motrix::app::SavePreferencesCmd{std::move(customPrefs), std::move(customSys), false});

    const auto modified = coord.Snapshot();
    CHECK(modified->prefs.theme == ThemePreference::Dark);
    CHECK(modified->sysConfig.maxConcurrentDownloads == 16);
    CHECK(modified->sysConfig.rpcSecret == "old_custom_secret");

    coord.Dispatch(motrix::app::SelectTaskCmd{initial->downloadingTasks.front().gid, true});
    REQUIRE(!coord.Snapshot()->detailedTaskGid.empty());

    coord.Dispatch(motrix::app::ResetSessionCmd{});
    const auto afterReset = coord.Snapshot();
    CHECK(afterReset->downloadingTasks.empty());
    CHECK(afterReset->waitingTasks.empty());
    CHECK(afterReset->stoppedTasks.empty());
    CHECK(afterReset->selectedTaskGids.empty());
    CHECK(afterReset->detailedTaskGid.empty());

    coord.Dispatch(motrix::app::FactoryResetCmd{});
    const auto afterFactory = coord.Snapshot();
    CHECK(afterFactory->prefs.theme == DefaultUserPreferences().theme);
    CHECK(afterFactory->sysConfig.maxConcurrentDownloads
          == DefaultSystemConfig().maxConcurrentDownloads);
    CHECK_FALSE(afterFactory->sysConfig.rpcSecret.empty());
    CHECK(afterFactory->sysConfig.rpcSecret != "old_custom_secret");
}
