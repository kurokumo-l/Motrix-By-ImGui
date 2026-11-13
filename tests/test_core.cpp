#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;

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
    const auto tempRoot = std::filesystem::temp_directory_path() / "motrix_native_test_fs";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

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

    // Missing file returns NotFound
    auto missing = ReadFileUtf8(tempRoot / "does_not_exist.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);

    std::filesystem::remove_all(tempRoot, ec);
}

TEST_CASE("motrix.core i18n lookup and async logger", "[core][i18n][logger]") {
    using namespace motrix::core;
    Translator tr(Locale::EnUS);
    CHECK(tr.Tr("task.active") == "Downloading");
    tr.SetLocale(Locale::ZhCN);
    CHECK(tr.Tr("task.active") == "下载中");
    CHECK(tr.Tr("unknown.key") == "unknown.key");

    const auto logPath = std::filesystem::temp_directory_path() / "motrix_native_test_logs" / "test.log";
    std::error_code ec;
    std::filesystem::remove_all(logPath.parent_path(), ec);
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
    std::filesystem::remove_all(logPath.parent_path(), ec);
}
