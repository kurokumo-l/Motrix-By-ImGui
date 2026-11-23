// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format on

#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;

namespace {

std::atomic<std::uint64_t> g_coreTestDirSeq{0};

struct TempDirGuard {
    std::filesystem::path path;
    explicit TempDirGuard(std::string_view tag) {
        const auto seq = g_coreTestDirSeq.fetch_add(1, std::memory_order_relaxed);
        path = std::filesystem::temp_directory_path()
               / std::format("motrix_test_core_{}_{}_{}", ::GetCurrentProcessId(), seq, tag);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("motrix.core error result propagation", "[core][error]") {
    using namespace motrix::core;
    const auto okFn = []() -> Result<int> { return 42; };
    const auto errFn = []() -> Result<int> {
        return std::unexpected(
            MakeError(ErrorDomain::Domain, ErrorCode::ParseFailure, "bad input", "offset 7"));
    };

    const auto r1 = okFn();
    REQUIRE(r1.has_value());
    REQUIRE(*r1 == 42);

    const auto r2 = errFn();
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().domain == ErrorDomain::Domain);
    CHECK(r2.error().code == ErrorCode::ParseFailure);
    CHECK(ErrorDomainName(r2.error().domain) == "Domain");
}

TEST_CASE("motrix.core UTF-8 <-> Wide roundtrip", "[core][encoding]") {
    using namespace motrix::core;
    const std::string original = "Motrix_下载测试_Ubuntu-24.04.iso";
    const std::wstring wide = Utf8ToWide(original);
    REQUIRE_FALSE(wide.empty());
    CHECK(WideToUtf8(wide) == original);
    CHECK(Utf8ToWide("").empty());
    CHECK(WideToUtf8(L"").empty());
    CHECK(AsciiLower("ArIa2-NeXt") == "aria2-next");
    CHECK(TrimAscii(" \t motrix \r\n ") == "motrix");
}

TEST_CASE("motrix.core atomic filesystem write, overwrite, and BOM strip", "[core][atomic_fs]") {
    using namespace motrix::core;
    TempDirGuard tempDir("atomic_fs");
    const auto& tempRoot = tempDir.path;

    const auto filePath = tempRoot / "nested" / "config.json";
    REQUIRE(WriteTextAtomic(filePath, "{\"port\":16800}").has_value());
    auto read1 = ReadFileUtf8(filePath);
    REQUIRE(read1.has_value());
    CHECK(*read1 == "{\"port\":16800}");

    // Overwrite existing file cleanly
    REQUIRE(WriteTextAtomic(filePath, "{\"port\":16801}").has_value());
    auto read2 = ReadFileUtf8(filePath);
    REQUIRE(read2.has_value());
    CHECK(*read2 == "{\"port\":16801}");

    // UTF-8 BOM stripping
    const std::vector<std::uint8_t> bomBytes = {0xEF, 0xBB, 0xBF, 'o', 'k'};
    REQUIRE(WriteFileAtomic(filePath, bomBytes).has_value());
    auto readBom = ReadFileUtf8(filePath);
    REQUIRE(readBom.has_value());
    CHECK(*readBom == "ok");

    // No stray .tmp files left behind after overwrites
    std::size_t fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(filePath.parent_path())) {
        (void)entry;
        ++fileCount;
    }
    CHECK(fileCount == 1);

    // Empty target path rejected
    auto emptyRes = WriteTextAtomic({}, "data");
    REQUIRE_FALSE(emptyRes.has_value());
    CHECK(emptyRes.error().code == ErrorCode::InvalidArgument);

    // Overwriting an existing directory fails cleanly, preserves Win32 detail, and cleans temp file
    const auto dirTarget = tempRoot / "existing_dir";
    std::error_code ec;
    std::filesystem::create_directories(dirTarget, ec);
    auto dirRes = WriteTextAtomic(dirTarget, "payload");
    REQUIRE_FALSE(dirRes.has_value());
    CHECK(dirRes.error().code == ErrorCode::IoFailure);
    CHECK(dirRes.error().detail.find("win32 error") != std::string::npos);

    std::size_t rootEntries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(tempRoot)) {
        CHECK(entry.path().extension() != ".tmp");
        ++rootEntries;
    }
    CHECK(rootEntries == 2);

    // Missing file returns NotFound
    auto missing = ReadFileUtf8(tempRoot / "does_not_exist.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("motrix.core i18n lookup and async logger", "[core][i18n][logger]") {
    using namespace motrix::core;
    Translator tr(Locale::EnUS);
    CHECK(tr.Tr("task.active") == "Downloading");
    tr.SetLocale(Locale::ZhCN);
    CHECK(tr.Tr("task.active") == "下载中");
    CHECK(tr.Tr("unknown.key") == "unknown.key");

    TempDirGuard tempDir("logger_basic");
    const auto logPath = tempDir.path / "test.log";
    {
        Logger logger(LoggerOptions{
            .logFilePath = logPath,
            .minLevel = LogLevel::Info,
        });
        logger.Debug("test", "filtered out {}", 1);
        logger.Info("test", "hello logger {}", 16800);
        logger.Flush();
    }
    auto logged = ReadFileUtf8(logPath);
    REQUIRE(logged.has_value());
    CHECK(logged->find("hello logger 16800") != std::string::npos);
    CHECK(logged->find("filtered out") == std::string::npos);
}

TEST_CASE("motrix.core ParseLogLevel maps known strings case-insensitively", "[core][logger]") {
    using namespace motrix::core;
    CHECK(ParseLogLevel("debug") == LogLevel::Debug);
    CHECK(ParseLogLevel("DEBUG") == LogLevel::Debug);
    CHECK(ParseLogLevel("info") == LogLevel::Info);
    CHECK(ParseLogLevel("warn") == LogLevel::Warn);
    CHECK(ParseLogLevel("NOTICE") == LogLevel::Warn);
    CHECK(ParseLogLevel("error") == LogLevel::Error);
    CHECK(ParseLogLevel("Error") == LogLevel::Error);
    CHECK(ParseLogLevel("") == LogLevel::Info);
    CHECK(ParseLogLevel("unknown") == LogLevel::Info);
}

TEST_CASE("motrix.core ParseStrictInt validates integer strings strictly", "[core][encoding]") {
    using namespace motrix::core;
    CHECK(ParseStrictInt("16800") == 16800);
    CHECK(ParseStrictInt("  42 \t") == 42);
    CHECK(ParseStrictInt("0") == 0);
    CHECK(ParseStrictInt("-7") == -7);
    CHECK_FALSE(ParseStrictInt("").has_value());
    CHECK_FALSE(ParseStrictInt("   ").has_value());
    CHECK_FALSE(ParseStrictInt("abc").has_value());
    CHECK_FALSE(ParseStrictInt("123abc").has_value());
    CHECK_FALSE(ParseStrictInt("999999999999999").has_value());
}

TEST_CASE("motrix.core shared ASCII and OS path/handle helpers behave consistently", "[core][os]") {
    using namespace motrix::core;
    CHECK(EqualsAsciiInsensitive("Aria2-Next", "aria2-next"));
    CHECK_FALSE(EqualsAsciiInsensitive("aria2", "aria2-next"));
    CHECK(StartsWithAsciiInsensitive("MOTRIX://task-list", "motrix://"));
    CHECK_FALSE(StartsWithAsciiInsensitive("http://a", "https://"));
    CHECK(EndsWithAsciiInsensitive("Ubuntu.TORRENT", ".torrent"));
    CHECK_FALSE(EndsWithAsciiInsensitive("a.iso", ".torrent"));
    CHECK(ContainsAsciiInsensitive("aria2-next-Windows-x86_64.exe", "windows"));
    CHECK(ContainsAsciiInsensitive("Aria2-Next 3.1.0", "ARIA2-NEXT"));
    CHECK_FALSE(ContainsAsciiInsensitive("aria2 1.37", "aria2-next"));
    CHECK_FALSE(ContainsAsciiInsensitive("abc", ""));
    CHECK(TrimQuotedArg("  \"'C:\\Downloads\\a.torrent'\"  ") == "C:\\Downloads\\a.torrent");

    const auto selfExe = CurrentExecutablePath();
    REQUIRE_FALSE(selfExe.empty());
    CHECK(std::filesystem::exists(selfExe));

    UniqueHandle invalid;
    CHECK_FALSE(invalid.IsValid());
}

TEST_CASE("motrix.core logger rotates cleanly when maxBackups is zero", "[core][logger]") {
    using namespace motrix::core;
    TempDirGuard tempDir("logger_zero");
    const auto logPath = tempDir.path / "zero.log";
    {
        Logger logger(LoggerOptions{
            .logFilePath = logPath,
            .minLevel = LogLevel::Info,
            .maxFileBytes = 64,
            .maxBackups = 0,
        });
        logger.Info("rot", "first-long-log-line-exceeding-sixty-four-bytes-1234567890");
        logger.Info("rot", "second-entry");
        logger.Flush();
    }
    auto logged = ReadFileUtf8(logPath);
    REQUIRE(logged.has_value());
    CHECK(logged->find("second-entry") != std::string::npos);
    CHECK(logged->find("first-long-log-line") == std::string::npos);
}

TEST_CASE("motrix.core logger multi-backup rotation and locked rotation backoff", "[core][logger]") {
    using namespace motrix::core;
    TempDirGuard tempDir("logger_multi");
    const auto logPath = tempDir.path / "app.log";

    {
        Logger logger(LoggerOptions{
            .logFilePath = logPath,
            .minLevel = LogLevel::Info,
            .maxFileBytes = 64,
            .maxBackups = 2,
        });
        logger.Info("rot", "batch-one-line-padding-to-exceed-64-bytes-123456789012345");
        logger.Info("rot", "batch-two-line-padding-to-exceed-64-bytes-123456789012345");
        logger.Info("rot", "batch-three-current");
        logger.Flush();
    }

    CHECK(std::filesystem::exists(logPath));
    CHECK(std::filesystem::exists(tempDir.path / "app.log.1"));
    CHECK(std::filesystem::exists(tempDir.path / "app.log.2"));
    CHECK_FALSE(std::filesystem::exists(tempDir.path / "app.log.3"));

    // Hold app.log.1 open without FILE_SHARE_DELETE to simulate external lock during rotation.
    const auto lockedBackup = tempDir.path / "app.log.1";
    UniqueHandle lockHandle(::CreateFileW(lockedBackup.c_str(),
                                          GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr,
                                          OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL,
                                          nullptr));
    REQUIRE(lockHandle.IsValid());

    {
        Logger logger(LoggerOptions{
            .logFilePath = logPath,
            .minLevel = LogLevel::Info,
            .maxFileBytes = 64,
            .maxBackups = 2,
        });
        logger.Info("lock", "locked-rotation-trigger-line-exceeding-sixty-four-bytes-12345");
        logger.Info("lock", "post-backoff-continued-line");
        logger.Flush();
    }

    auto afterLocked = ReadFileUtf8(logPath);
    REQUIRE(afterLocked.has_value());
    CHECK(afterLocked->find("post-backoff-continued-line") != std::string::npos);
}
