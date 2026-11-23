#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;

namespace {

std::vector<std::uint8_t> ToBytes(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()),
            reinterpret_cast<const std::uint8_t*>(s.data() + s.size())};
}

std::string BencodeBytes(std::string_view s) {
    return std::format("{}:{}", s.size(), s);
}

std::string BencodeInt(std::int64_t v) {
    return std::format("i{}e", v);
}

}  // namespace

TEST_CASE("motrix.domain Sha1Hex matches RFC 3174 vectors", "[domain][bencode][crypto]") {
    using motrix::domain::Sha1Hex;
    CHECK(Sha1Hex(ToBytes("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(Sha1Hex(ToBytes("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(Sha1Hex(ToBytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))
          == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("motrix.domain Base64Encode matches RFC 4648 vectors", "[domain][bencode][crypto]") {
    using motrix::domain::Base64Encode;
    CHECK(Base64Encode(ToBytes("")).empty());
    CHECK(Base64Encode(ToBytes("f")) == "Zg==");
    CHECK(Base64Encode(ToBytes("fo")) == "Zm8=");
    CHECK(Base64Encode(ToBytes("foo")) == "Zm9v");
    CHECK(Base64Encode(ToBytes("foobar")) == "Zm9vYmFy");
}

TEST_CASE("motrix.domain ParseTorrentBytes single-file metadata and infoHash", "[domain][bencode]") {
    using namespace motrix::domain;
    const std::string fakePieceHash(20, '\x5A');
    const std::string infoDict = "d" + BencodeBytes("length") + BencodeInt(1048576)
                                 + BencodeBytes("name") + BencodeBytes("arch-x64.iso")
                                 + BencodeBytes("piece length") + BencodeInt(262144)
                                 + BencodeBytes("pieces") + BencodeBytes(fakePieceHash) + "e";
    const std::string expectedInfoHash = Sha1Hex(ToBytes(infoDict));

    const std::string torrent =
        "d" + BencodeBytes("announce") + BencodeBytes("https://tracker.example/announce")
        + BencodeBytes("comment") + BencodeBytes("Official ISO") + BencodeBytes("created by")
        + BencodeBytes("mktorrent 1.1") + BencodeBytes("creation date") + BencodeInt(1700000000)
        + BencodeBytes("info") + infoDict + "e";

    const auto parsed = ParseTorrentBytes(ToBytes(torrent));
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "arch-x64.iso");
    CHECK(parsed->infoHash == expectedInfoHash);
    CHECK(parsed->totalLength == 1048576ull);
    CHECK(parsed->pieceLength == 262144ull);
    CHECK(parsed->numPieces == 1u);
    CHECK(parsed->creationDate == 1700000000);
    CHECK(parsed->comment == "Official ISO");
    CHECK(parsed->createdBy == "mktorrent 1.1");
    CHECK_FALSE(parsed->isMultiFile);
    CHECK_FALSE(parsed->isPrivate);
    REQUIRE(parsed->trackers.size() == 1);
    CHECK(parsed->trackers[0] == "https://tracker.example/announce");
    REQUIRE(parsed->files.size() == 1);
    CHECK(parsed->files[0].index == 1);
    CHECK(parsed->files[0].path == "arch-x64.iso");
    CHECK(parsed->files[0].extension == ".iso");
    CHECK(parsed->files[0].length == 1048576ull);
    CHECK(parsed->files[0].selected);
}

TEST_CASE("motrix.domain ParseTorrentBytes multi-file UTF-8 paths and announce-list",
          "[domain][bencode]") {
    using namespace motrix::domain;
    const std::string pieces(40, '\x11');
    const std::string file1 = "d" + BencodeBytes("length") + BencodeInt(1024) + BencodeBytes("path")
                              + "l" + BencodeBytes("video") + BencodeBytes("clip.mkv") + "e"
                              + BencodeBytes("path.utf-8") + "l" + BencodeBytes("视频")
                              + BencodeBytes("演示.mp4") + "e" + "e";
    const std::string file2 = "d" + BencodeBytes("length") + BencodeInt(512) + BencodeBytes("path") + "l"
                              + BencodeBytes("cover.png") + "e" + "e";
    const std::string infoDict =
        "d" + BencodeBytes("files") + "l" + file1 + file2 + "e" + BencodeBytes("name")
        + BencodeBytes("PackFallback") + BencodeBytes("name.utf-8") + BencodeBytes("演示合集")
        + BencodeBytes("piece length") + BencodeInt(65536) + BencodeBytes("pieces")
        + BencodeBytes(pieces) + BencodeBytes("private") + BencodeInt(1) + "e";

    const std::string announceList = "l"
                                     "l"
                                     + BencodeBytes(" https://t1.example/announce ")
                                     + BencodeBytes("udp://t2.example:6969")
                                     + "e"
                                       "l"
                                     + BencodeBytes("http://t3.example/a")
                                     + "e"
                                       "e";

    const std::string torrent =
        "d" + BencodeBytes("announce") + BencodeBytes("https://t1.example/announce")
        + BencodeBytes("announce-list") + announceList + BencodeBytes("info") + infoDict + "e";

    const auto parsed = ParseTorrentBytes(ToBytes(torrent));
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "演示合集");
    CHECK(parsed->isMultiFile);
    CHECK(parsed->isPrivate);
    CHECK(parsed->totalLength == 1536ull);
    CHECK(parsed->numPieces == 2u);
    REQUIRE(parsed->trackers.size() == 3);
    CHECK(parsed->trackers[0] == "https://t1.example/announce");
    CHECK(parsed->trackers[1] == "udp://t2.example:6969");
    CHECK(parsed->trackers[2] == "http://t3.example/a");

    REQUIRE(parsed->files.size() == 2);
    CHECK(parsed->files[0].index == 1);
    CHECK(parsed->files[0].path == "视频/演示.mp4");
    CHECK(parsed->files[0].name == "演示.mp4");
    CHECK(parsed->files[0].extension == ".mp4");
    CHECK(parsed->files[0].length == 1024ull);
    CHECK(parsed->files[1].index == 2);
    CHECK(parsed->files[1].path == "cover.png");
    CHECK(parsed->files[1].extension == ".png");
    CHECK(parsed->files[1].length == 512ull);
}

TEST_CASE("motrix.domain ParseTorrentBytes rejects malformed and overly nested inputs",
          "[domain][bencode][error]") {
    using motrix::domain::ParseTorrentBytes;

    CHECK_FALSE(ParseTorrentBytes({}).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("i42e")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("l4:teste")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d8:announce1:ae")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d4:infod6:lengthi01eee")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d4:infod6:lengthi-0eee")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d4:info5:ab")).has_value());
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d4:info-1:ae")).has_value());
    CHECK_FALSE(
        ParseTorrentBytes(
            ToBytes("d4:infod6:lengthi1e4:name1:a12:piece lengthi1e6:pieces20:12345678901234567890eeX"))
            .has_value());

    const std::string deepLists = std::string(66, 'l') + std::string(66, 'e');
    const std::string deepTorrent = "d4:info" + deepLists + "e";
    CHECK_FALSE(ParseTorrentBytes(ToBytes(deepTorrent)).has_value());
}

TEST_CASE("motrix.domain ParseTorrentBytes enforces strict BEP-3 sorting, path safety, and bounds",
          "[domain][bencode][security]") {
    using namespace motrix::domain;
    const std::string fakePiece(20, '\x22');

    // Duplicate dictionary key in root and info must be rejected.
    const std::string validInfo = "d" + BencodeBytes("length") + BencodeInt(100) + BencodeBytes("name")
                                  + BencodeBytes("ok.bin") + BencodeBytes("piece length")
                                  + BencodeInt(16384) + BencodeBytes("pieces") + BencodeBytes(fakePiece)
                                  + "e";
    const std::string dupRoot =
        "d" + BencodeBytes("info") + validInfo + BencodeBytes("info") + validInfo + "e";
    CHECK_FALSE(ParseTorrentBytes(ToBytes(dupRoot)).has_value());

    const std::string dupInfo = "d" + BencodeBytes("length") + BencodeInt(100) + BencodeBytes("length")
                                + BencodeInt(200) + BencodeBytes("name") + BencodeBytes("ok.bin")
                                + BencodeBytes("piece length") + BencodeInt(16384)
                                + BencodeBytes("pieces") + BencodeBytes(fakePiece) + "e";
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d" + BencodeBytes("info") + dupInfo + "e")).has_value());

    // Unsorted dictionary keys must be rejected.
    const std::string unsortedInfo = "d" + BencodeBytes("name") + BencodeBytes("ok.bin")
                                     + BencodeBytes("length") + BencodeInt(100)
                                     + BencodeBytes("piece length") + BencodeInt(16384)
                                     + BencodeBytes("pieces") + BencodeBytes(fakePiece) + "e";
    CHECK_FALSE(ParseTorrentBytes(ToBytes("d" + BencodeBytes("info") + unsortedInfo + "e")).has_value());

    // Path traversal / unsafe names in single-file and multi-file torrents must be rejected.
    auto makeSingleWithName = [&](std::string_view name) {
        const std::string info = "d" + BencodeBytes("length") + BencodeInt(10) + BencodeBytes("name")
                                 + BencodeBytes(name) + BencodeBytes("piece length") + BencodeInt(16384)
                                 + BencodeBytes("pieces") + BencodeBytes(fakePiece) + "e";
        return "d" + BencodeBytes("info") + info + "e";
    };
    for (const std::string_view badName : std::array<std::string_view, 7>{
             "", ".", "..", "a/b", "a\\b", "C:evil", std::string_view("a\0b", 3)}) {
        CHECK_FALSE(ParseTorrentBytes(ToBytes(makeSingleWithName(badName))).has_value());
    }

    auto makeMultiWithSeg = [&](std::string_view seg) {
        const std::string f1 = "d" + BencodeBytes("length") + BencodeInt(10) + BencodeBytes("path") + "l"
                               + BencodeBytes(seg) + BencodeBytes("file.bin") + "e" + "e";
        const std::string info = "d" + BencodeBytes("files") + "l" + f1 + "e" + BencodeBytes("name")
                                 + BencodeBytes("pack") + BencodeBytes("piece length")
                                 + BencodeInt(16384) + BencodeBytes("pieces") + BencodeBytes(fakePiece)
                                 + "e";
        return "d" + BencodeBytes("info") + info + "e";
    };
    for (const std::string_view badSeg : std::array<std::string_view, 7>{
             "", ".", "..", "sub/dir", "sub\\dir", "D:", std::string_view("x\0y", 3)}) {
        CHECK_FALSE(ParseTorrentBytes(ToBytes(makeMultiWithSeg(badSeg))).has_value());
    }

    // Node count > 250000 must fail cleanly without OOM.
    std::string manyNodes = "d4:infol";
    manyNodes.reserve(250010u * 3u + 16u);
    for (int i = 0; i < 250005; ++i) {
        manyNodes += "i0e";
    }
    manyNodes += "ee";
    CHECK_FALSE(ParseTorrentBytes(ToBytes(manyNodes)).has_value());

    // Buffer > 32 MiB must fail immediately.
    std::vector<std::uint8_t> oversized(32u * 1024u * 1024u + 1u, 'd');
    CHECK_FALSE(ParseTorrentBytes(oversized).has_value());
}

TEST_CASE("motrix.domain BT UI helpers format select-file, peers, extensions, timestamps, bitfields",
          "[domain][bencode][helpers]") {
    using namespace motrix::domain;

    CHECK(FormatSelectFileOption({}).empty());
    const std::vector<int> nonPos{0, -3};
    CHECK(FormatSelectFileOption(nonPos).empty());
    const std::vector<int> mixed{3, 1, 3, 2, -1};
    CHECK(FormatSelectFileOption(mixed) == "1,2,3");

    CHECK(DecodePeerId("") == "Unknown");
    CHECK(DecodePeerId("-qB4620-123456789012") == "qBittorrent 4.6.2");
    CHECK(DecodePeerId("-TR4001-123456789012") == "Transmission 4.0.0.1");
    CHECK(DecodePeerId("%2DqB4600%2D123456789012") == "qBittorrent 4.6.0");
    CHECK(DecodePeerId("aria2/1.37.0-0000000") == "aria2");
    CHECK(DecodePeerId("A2-1-37-0-1234567890") == "aria2");

    CHECK(ExtractFileExtension("dir/movie.MKV") == ".mkv");
    CHECK(ExtractFileExtension(".gitignore").empty());
    CHECK(IsVideoExtension("dir/movie.MKV"));
    CHECK_FALSE(IsVideoExtension("song.flac"));
    CHECK(IsAudioExtension(".FLAC"));
    CHECK(IsAudioExtension("album/song.mp3"));
    CHECK(IsImageExtension("covers/poster.WebP"));
    CHECK_FALSE(IsImageExtension("archive.zip"));

    CHECK(FormatUnixTimestampUtc(0).empty());
    CHECK(FormatUnixTimestampUtc(-10).empty());
    CHECK(FormatUnixTimestampUtc(1700000000) == "2023-11-14 22:13:20");

    CHECK(BitfieldCompletion("", 16) == 0.0f);
    CHECK(BitfieldCompletion("zzzz", 16) == 0.0f);
    CHECK(std::abs(BitfieldCompletion("ff00", 16) - 0.5f) < 1e-5f);
    CHECK(std::abs(BitfieldCompletion("ff80", 9) - 1.0f) < 1e-5f);
}

TEST_CASE("motrix.engine BT RPC request builders and detail JSON parsers", "[engine][rpc][bencode]") {
    using namespace motrix::engine;

    AddTorrentRequest req{};
    req.torrentBase64 = "ZDE6YQ==";
    req.uris = {"https://mirror.example/extra"};
    req.options = {
        {"dir", "D:/Downloads"},
        {"select-file", "1,3"},
    };
    const std::string addReq = BuildAddTorrentRpcRequest("req1", "sec", req);
    CHECK(addReq.find("\"aria2.addTorrent\"") != std::string::npos);
    CHECK(addReq.find("\"token:sec\"") != std::string::npos);
    CHECK(addReq.find("\"ZDE6YQ==\"") != std::string::npos);
    CHECK(addReq.find("\"select-file\":\"1,3\"") != std::string::npos);

    const std::array<EngineOptionPair, 1> taskOpts{{{"select-file", "2,4"}}};
    const std::string chgReq = BuildChangeTaskOptionRpcRequest("req2", "sec", "gid123", taskOpts);
    CHECK(chgReq.find("\"aria2.changeOption\"") != std::string::npos);
    CHECK(chgReq.find("\"gid123\"") != std::string::npos);
    CHECK(chgReq.find("\"2,4\"") != std::string::npos);

    constexpr const char* kDetailJson = R"json({
        "gid": "bt001",
        "status": "active",
        "totalLength": "4000",
        "completedLength": "2000",
        "uploadLength": "500",
        "downloadSpeed": "1024",
        "uploadSpeed": "256",
        "connections": "8",
        "numSeeders": "3",
        "numPieces": "16",
        "pieceLength": "262144",
        "bitfield": "ff00",
        "dir": "D:/Downloads",
        "infoHash": "0123456789abcdef0123456789abcdef01234567",
        "bittorrent": {
            "info": {"name": "Fedora-Workstation"},
            "creationDate": 1700000000,
            "comment": "Fedora Live",
            "mode": "multi",
            "announceList": [
                ["https://t1.example/a", "udp://t2.example:6969"],
                ["https://t1.example/a"]
            ]
        },
        "files": [
            {"index": "1", "path": "D:/Downloads/1.iso", "length": "1000", "completedLength": "1000", "selected": "true"},
            {"index": "2", "path": "D:/Downloads/2.txt", "length": "1000", "completedLength": "1000", "selected": "true"},
            {"index": "3", "path": "D:/Downloads/3.sha", "length": "1000", "completedLength": "0", "selected": "false"},
            {"index": "4", "path": "D:/Downloads/4.sig", "length": "1000", "completedLength": "0", "selected": "false"}
        ]
    })json";

    const auto detailRes = ParseTaskDetailJson(kDetailJson);
    REQUIRE(detailRes.has_value());
    CHECK(detailRes->task.gid == "bt001");
    CHECK(detailRes->task.name == "Fedora-Workstation");
    CHECK(detailRes->task.files.size() == 4);
    CHECK(detailRes->task.filesTotal == 4);
    REQUIRE(detailRes->task.btInfo.has_value());
    CHECK(detailRes->task.btInfo->creationDate == "2023-11-14 22:13:20");
    REQUIRE(detailRes->trackers.size() == 2);
    CHECK(detailRes->trackers[0] == "https://t1.example/a");
    CHECK(detailRes->trackers[1] == "udp://t2.example:6969");

    constexpr const char* kFilesJson = R"json([
        {"index": "1", "path": "D:/Downloads/a.mkv", "length": "300", "completedLength": "150", "selected": "true"},
        {"index": "2", "path": "D:/Downloads/b.srt", "length": "100", "completedLength": "0", "selected": "false"}
    ])json";
    const auto filesRes = ParseTaskFilesJson(kFilesJson);
    REQUIRE(filesRes.has_value());
    REQUIRE(filesRes->size() == 2);
    CHECK((*filesRes)[0].index == 1);
    CHECK((*filesRes)[0].selected);
    CHECK((*filesRes)[1].index == 2);
    CHECK_FALSE((*filesRes)[1].selected);

    constexpr const char* kPeersJson = R"json([
        {"peerId": "-qB4620-000000000001", "ip": "1.2.3.4", "port": "51413", "bitfield": "ff00", "downloadSpeed": "2048", "uploadSpeed": "0", "amChoking": "false", "peerChoking": "false", "seeder": "false"},
        {"peerId": "-TR4001-000000000002", "ip": "5.6.7.8", "port": "6881", "bitfield": "ff00", "downloadSpeed": "4096", "uploadSpeed": "512", "amChoking": "false", "peerChoking": "false", "seeder": "true"}
    ])json";
    const auto peersRes = ParsePeersJson(kPeersJson, 16);
    REQUIRE(peersRes.has_value());
    REQUIRE(peersRes->size() == 2);
    CHECK((*peersRes)[0].client == "qBittorrent 4.6.2");
    CHECK((*peersRes)[0].port == 51413);
    CHECK_FALSE((*peersRes)[0].seeder);
    CHECK(std::abs((*peersRes)[0].progress - 0.5f) < 1e-5f);
    CHECK((*peersRes)[1].client == "Transmission 4.0.0.1");
    CHECK((*peersRes)[1].seeder);
    CHECK(std::abs((*peersRes)[1].progress - 1.0f) < 1e-5f);
}
