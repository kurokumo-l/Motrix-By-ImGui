#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;

TEST_CASE("motrix.domain task status category and progress calculation", "[domain][task]") {
    using namespace motrix::domain;
    CHECK(CategoryOf(TaskStatus::Active) == TaskCategory::Downloading);
    CHECK(CategoryOf(TaskStatus::Waiting) == TaskCategory::Waiting);
    CHECK(CategoryOf(TaskStatus::Paused) == TaskCategory::Waiting);
    CHECK(CategoryOf(TaskStatus::Complete) == TaskCategory::Stopped);
    CHECK(CategoryOf(TaskStatus::Error) == TaskCategory::Stopped);
    CHECK(CategoryOf(TaskStatus::Removed) == TaskCategory::Stopped);

    DownloadTaskView task{};
    CHECK(ProgressOf(task) == 0.0f);
    CHECK_FALSE(EtaSecondsOf(task).has_value());

    task.status = TaskStatus::Active;
    task.totalLength = 851443712ull;
    task.completedLength = 371510476ull;
    task.downloadSpeed = 19922944ull;
    CHECK(ProgressOf(task) > 0.43f);
    CHECK(ProgressOf(task) < 0.44f);
    REQUIRE(EtaSecondsOf(task).has_value());
    CHECK(*EtaSecondsOf(task) == 25u);
}

TEST_CASE("motrix.domain byte, speed, and ETA formatting matches Motrix", "[domain][formatters]") {
    using namespace motrix::domain;
    CHECK(FormatBytes(0) == "0 B");
    CHECK(FormatBytes(512) == "512 B");
    CHECK(FormatBytes(371510476ull) == "354.3 MB");
    CHECK(FormatBytes(851443712ull) == "812.0 MB");
    CHECK(FormatSpeed(0) == "0 KB/s");
    CHECK(FormatSpeed(19922944ull) == "19.0 MB/s");
    CHECK(FormatRemainingEta(479933236ull, 19922944ull) == "Remaining 25s");
    CHECK(FormatRemainingEta(0, 1000) == "");
    CHECK(FormatRemainingEta(1000, 0) == "");
}

TEST_CASE("motrix.domain thunder URI decoding and magnet parsing", "[domain][uri]") {
    using namespace motrix::domain;
    // "AAhttps://example.com/file.zipZZ" base64 = QUFodHRwczovL2V4YW1wbGUuY29tL2ZpbGUuemlwWlo=
    auto decoded = DecodeThunderUri("thunder://QUFodHRwczovL2V4YW1wbGUuY29tL2ZpbGUuemlwWlo=");
    REQUIRE(decoded.has_value());
    CHECK(*decoded == "https://example.com/file.zip");

    auto invalid = DecodeThunderUri("thunder://not-valid-aa-zz!!!");
    CHECK_FALSE(invalid.has_value());

    const auto uris = SplitAndNormalizeUris(
        "  https://a.example/1.iso \r\n\n thunder://QUFodHRwczovL2V4YW1wbGUuY29tL2ZpbGUuemlwWlo= \n");
    REQUIRE(uris.size() == 2);
    CHECK(uris[0] == "https://a.example/1.iso");
    CHECK(uris[1] == "https://example.com/file.zip");

    constexpr std::string_view kMagnet = "magnet:?xt=urn:btih:0123456789abcdef&dn=Ubuntu+24.04%20LTS";
    CHECK(IsMagnetUri(kMagnet));
    CHECK(ExtractMagnetDisplayName(kMagnet) == "Ubuntu 24.04 LTS");
    CHECK_FALSE(IsMagnetUri("https://example.com"));
}

TEST_CASE("motrix.domain config schema defaults align with personal preferences", "[domain][config]") {
    using namespace motrix::domain;
    const auto prefs = DefaultUserPreferences();
    CHECK(prefs.theme == ThemePreference::Auto);
    CHECK(prefs.fontFamily == "Microsoft YaHei UI");
    CHECK(prefs.monoFontFamily.empty());
    CHECK(prefs.traySpeedometer);
    CHECK(prefs.showTooltips);
    CHECK(prefs.aria2NextUpdateUrl == kDefaultAria2NextUpdateUrl);
    CHECK(prefs.newTaskShowDownloading);
    CHECK(prefs.enableUpnp);
    CHECK_FALSE(prefs.protocolMagnet);
    CHECK_FALSE(prefs.protocolThunder);
    CHECK_FALSE(prefs.protocolMotrix);
    CHECK_FALSE(prefs.protocolTorrent);

    const auto sys = DefaultSystemConfig();
    CHECK(sys.rpcListenPort == 16800);
    CHECK(sys.maxConcurrentDownloads == 5);
    CHECK(sys.maxConnectionPerServer == 16);
    CHECK(sys.split == 16);
    CHECK(sys.fileAllocation == "none");
    CHECK(sys.btEnableLpd);
    CHECK(sys.btMaxPeers == 64);
}

TEST_CASE("motrix.domain aria2-next config roundtrip, effective options, and legacy migration",
          "[domain][config]") {
    using namespace motrix::domain;
    UserPreferences prefs = DefaultUserPreferences();
    prefs.theme = ThemePreference::Auto;
    prefs.traySpeedometer = false;
    prefs.showTooltips = false;
    prefs.aria2NextUpdateUrl = "https://example.com/releases/latest";
    prefs.aria2NextMirrorPrefix = "https://ghproxy.net/";
    prefs.deleteFilesWhenDeleteTask = true;
    prefs.logLevel = "debug";
    prefs.keepSeeding = true;
    prefs.useProxy = false;
    prefs.fontFamily = "MiSans";
    prefs.monoFontFamily = "Consolas";

    const auto parsedPrefs = ParseUserPreferencesJson(SerializeUserPreferencesJson(prefs));
    REQUIRE(parsedPrefs.has_value());
    CHECK(parsedPrefs->theme == ThemePreference::Auto);
    CHECK_FALSE(parsedPrefs->traySpeedometer);
    CHECK_FALSE(parsedPrefs->showTooltips);
    CHECK(parsedPrefs->aria2NextUpdateUrl == "https://example.com/releases/latest");
    CHECK(parsedPrefs->aria2NextMirrorPrefix == "https://ghproxy.net/");
    CHECK(parsedPrefs->deleteFilesWhenDeleteTask);
    CHECK(parsedPrefs->logLevel == "debug");
    CHECK(parsedPrefs->keepSeeding);
    CHECK_FALSE(parsedPrefs->useProxy);
    CHECK(parsedPrefs->fontFamily == "MiSans");
    CHECK(parsedPrefs->monoFontFamily == "Consolas");

    SystemConfig sys = DefaultSystemConfig();
    sys.btEncryption = "required";
    sys.btTransport = "utp";
    sys.fileAllocation = "falloc";
    sys.btMaxPeers = 250;
    sys.mediaMode = "hls";
    sys.mediaFormat = "mkv";
    sys.btFirstLastPieceFirst = true;
    sys.detachShareOnly = true;
    sys.rpcSaveUploadMetadata = false;
    sys.seedRatio = 2.5;
    sys.seedTime = 120;
    sys.allProxy = "http://127.0.0.1:8080";
    sys.noProxy = "localhost";

    const auto parsedSys = ParseSystemConfigJson(SerializeSystemConfigJson(sys));
    REQUIRE(parsedSys.has_value());
    CHECK(parsedSys->btEncryption == "required");
    CHECK(parsedSys->btForceEncryption);
    CHECK(parsedSys->btTransport == "utp");
    CHECK(parsedSys->fileAllocation == "falloc");
    CHECK(parsedSys->btMaxPeers == 250);
    CHECK(parsedSys->mediaMode == "hls");
    CHECK(parsedSys->mediaFormat == "mkv");
    CHECK(parsedSys->btFirstLastPieceFirst);
    CHECK(parsedSys->detachShareOnly);
    CHECK_FALSE(parsedSys->rpcSaveUploadMetadata);
    CHECK_FALSE(parsedSys->btSaveMetadata);

    const auto eff = BuildEffectiveSystemConfig(sys, prefs);
    CHECK(eff.seedRatio == 0.0);
    CHECK(eff.seedTime == 0);
    CHECK(eff.allProxy.empty());
    CHECK(eff.noProxy.empty());
    CHECK(eff.btForceEncryption);

    const auto pairs = SystemConfigToOptionPairs(eff);
    auto hasPair = [&](std::string_view k, std::string_view v) {
        return std::ranges::any_of(pairs, [&](const auto& p) { return p.first == k && p.second == v; });
    };
    CHECK(hasPair("seed-ratio", "0.00"));
    CHECK(hasPair("seed-time", "0"));
    CHECK(hasPair("bt-encryption", "required"));
    CHECK(hasPair("bt-transport", "utp"));
    CHECK(hasPair("file-allocation", "falloc"));
    CHECK(hasPair("media", "hls"));
    CHECK(hasPair("bt-first-last-piece-first", "true"));
    CHECK(hasPair("detach-share-only", "true"));
    CHECK_FALSE(std::ranges::any_of(pairs, [](const auto& p) { return p.first == "all-proxy"; }));
}

TEST_CASE("motrix.domain ParseSystemConfigJson migrates legacy aria2 keys", "[domain][config]") {
    using namespace motrix::domain;
    const auto migrated =
        ParseSystemConfigJson(R"({"bt-save-metadata": false, "bt-force-encryption": true})");
    REQUIRE(migrated.has_value());
    CHECK_FALSE(migrated->rpcSaveUploadMetadata);
    CHECK(migrated->btEncryption == "required");
    CHECK(migrated->btForceEncryption);
}
