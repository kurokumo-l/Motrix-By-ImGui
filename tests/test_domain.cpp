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

TEST_CASE("motrix.domain config schema defaults align with Motrix v1.8.19", "[domain][config]") {
    using namespace motrix::domain;
    const auto prefs = DefaultUserPreferences();
    CHECK(prefs.hideAppMenu);
    CHECK(prefs.newTaskShowDownloading);
    CHECK(prefs.enableUpnp);

    const auto sys = DefaultSystemConfig();
    CHECK(sys.rpcListenPort == 16800);
    CHECK(sys.maxConcurrentDownloads == 5);
    CHECK(sys.maxConnectionPerServer == 64);
    CHECK(sys.split == 64);
}
