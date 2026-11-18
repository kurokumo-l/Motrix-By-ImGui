module motrix.test_server;

import std;
import motrix.domain;
import motrix.test_server_util;

namespace motrix::test {

TorrentBuildResult BuildTestTorrent(std::string_view torrentName,
                                    std::string_view trackerUrl,
                                    std::span<const TorrentFileSpec> files,
                                    bool singleFileLayout) {
    std::vector<std::uint8_t> concatenated;
    for (const auto& f : files) {
        concatenated.insert(concatenated.end(), f.data.begin(), f.data.end());
    }
    constexpr std::size_t kPieceLen = 16384;
    std::vector<std::uint8_t> pieces;
    for (std::size_t off = 0; off < concatenated.size(); off += kPieceLen) {
        const std::size_t len = std::min(kPieceLen, concatenated.size() - off);
        const std::string hex = motrix::domain::Sha1Hex({concatenated.data() + off, len});
        const auto raw = HexToBytes(hex);
        pieces.insert(pieces.end(), raw.begin(), raw.end());
    }

    std::vector<std::uint8_t> info;
    info.push_back('d');
    if (singleFileLayout && files.size() == 1) {
        AppendBencodeStr(info, "length");
        AppendBencodeInt(info, files[0].data.size());
    } else {
        AppendBencodeStr(info, "files");
        info.push_back('l');
        for (const auto& f : files) {
            info.push_back('d');
            AppendBencodeStr(info, "length");
            AppendBencodeInt(info, f.data.size());
            AppendBencodeStr(info, "path");
            info.push_back('l');
            for (const auto& seg : f.pathParts) {
                AppendBencodeStr(info, seg);
            }
            info.push_back('e');
            info.push_back('e');
        }
        info.push_back('e');
    }
    AppendBencodeStr(info, "name");
    AppendBencodeStr(info, torrentName);
    AppendBencodeStr(info, "piece length");
    AppendBencodeInt(info, kPieceLen);
    AppendBencodeStr(info, "pieces");
    AppendBencodeStr(info,
                     std::string_view(reinterpret_cast<const char*>(pieces.data()), pieces.size()));
    info.push_back('e');

    std::vector<std::uint8_t> torrent;
    torrent.push_back('d');
    AppendBencodeStr(torrent, "announce");
    AppendBencodeStr(torrent, trackerUrl);
    AppendBencodeStr(torrent, "comment");
    AppendBencodeStr(torrent, "Motrix Integration Test Torrent");
    AppendBencodeStr(torrent, "creation date");
    AppendBencodeInt(torrent, 1700000000);
    AppendBencodeStr(torrent, "info");
    torrent.insert(torrent.end(), info.begin(), info.end());
    torrent.push_back('e');

    TorrentBuildResult res{};
    res.infoHashHex = motrix::domain::Sha1Hex(info);
    res.base64 = motrix::domain::Base64Encode(torrent);
    for (const auto& f : files) {
        res.filePayloads.push_back(f.data);
    }
    return res;
}

bool VerifyFileMatches(const std::filesystem::path& path, std::span<const std::uint8_t> expected) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return false;
    }
    const auto sz = ifs.tellg();
    if (sz < 0 || static_cast<std::size_t>(sz) != expected.size()) {
        return false;
    }
    std::vector<std::uint8_t> actual(expected.size());
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()))) {
        return false;
    }
    return actual == std::vector<std::uint8_t>(expected.begin(), expected.end());
}

std::uint16_t PickFreeEphemeralPort() {
    return PickFreeLoopbackPort();
}

}  // namespace motrix::test
