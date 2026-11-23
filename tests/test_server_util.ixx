export module motrix.test_server_util;

import std;

namespace motrix::test {

export class WsaLease final {
public:
    WsaLease();
    ~WsaLease();

    WsaLease(const WsaLease&) = delete;
    WsaLease& operator=(const WsaLease&) = delete;
};

export [[nodiscard]] std::vector<std::uint8_t> HexToBytes(std::string_view hex);
export [[nodiscard]] std::string UrlDecodeRaw(std::string_view s);
export [[nodiscard]] std::unordered_map<std::string, std::string> ParseQueryRaw(std::string_view q);

export bool SendAll(std::uintptr_t sock, const char* data, std::size_t len);
export bool SendText(std::uintptr_t sock, std::string_view sv);

export void AppendBencodeStr(std::vector<std::uint8_t>& dst, std::string_view s);
export void AppendBencodeInt(std::vector<std::uint8_t>& dst, std::uint64_t v);

export struct ParsedRange {
    bool valid = false;
    std::size_t start = 0;
    std::size_t end = 0;
};

export [[nodiscard]] ParsedRange ParseByteRange(std::string_view hdr, std::size_t total);
export [[nodiscard]] std::uint16_t PickFreeLoopbackPort();
export [[nodiscard]] std::uint16_t PickFreeUdpLoopbackPort();
export [[nodiscard]] std::filesystem::path MakeUniqueTempDir(std::string_view prefix);
export [[nodiscard]] bool ShouldRequireLiveTests();

}  // namespace motrix::test
