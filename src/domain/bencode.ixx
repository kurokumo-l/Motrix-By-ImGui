export module motrix.domain:bencode;

import std;
import motrix.core;

namespace motrix::domain {

export struct TorrentFileEntry {
    int index = 1;
    std::string path;
    std::string name;
    std::string extension;
    std::uint64_t length = 0;
    bool selected = true;
};

export struct TorrentMetadata {
    std::string name;
    std::string infoHash;
    std::uint64_t totalLength = 0;
    std::uint64_t pieceLength = 0;
    std::uint32_t numPieces = 0;
    std::int64_t creationDate = 0;
    std::string comment;
    std::string createdBy;
    bool isMultiFile = false;
    bool isPrivate = false;
    std::vector<std::string> trackers;
    std::vector<TorrentFileEntry> files;
};

export [[nodiscard]] std::string Sha1Hex(std::span<const std::uint8_t> data);

export [[nodiscard]] std::string Base64Encode(std::span<const std::uint8_t> data);

export [[nodiscard]] motrix::core::Result<TorrentMetadata>
ParseTorrentBytes(std::span<const std::uint8_t> bytes);

export [[nodiscard]] std::string FormatSelectFileOption(std::span<const int> selectedIndices);

export [[nodiscard]] std::string DecodePeerId(std::string_view rawOrPercentEncoded);

export [[nodiscard]] std::string ExtractFileExtension(std::string_view path);

export [[nodiscard]] bool IsVideoExtension(std::string_view extOrPath) noexcept;
export [[nodiscard]] bool IsAudioExtension(std::string_view extOrPath) noexcept;
export [[nodiscard]] bool IsImageExtension(std::string_view extOrPath) noexcept;

export [[nodiscard]] std::string FormatUnixTimestampUtc(std::int64_t unixSeconds);
export [[nodiscard]] std::string FormatUnixTimestampLocal(std::int64_t unixSeconds, bool zhCn = false);

export [[nodiscard]] float BitfieldCompletion(std::string_view bitfieldHex,
                                              std::uint32_t numPieces = 0) noexcept;

}  // namespace motrix::domain
