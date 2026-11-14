module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {
namespace {

constexpr int kMaxBencodeDepth = 64;

struct BencodeValue {
    enum class Kind {
        Bytes,
        Integer,
        List,
        Dict,
    };

    Kind kind = Kind::Bytes;
    std::string bytes;
    std::int64_t integer = 0;
    std::vector<BencodeValue> list;
    std::vector<std::pair<std::string, BencodeValue>> dict;
    std::size_t rawStart = 0;
    std::size_t rawEnd = 0;

    [[nodiscard]] const BencodeValue* FindKey(std::string_view key) const noexcept {
        if (kind != Kind::Dict) {
            return nullptr;
        }
        for (const auto& [k, v] : dict) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }
};

struct BencodeCursor {
    std::span<const std::uint8_t> data;
    std::size_t pos = 0;

    [[nodiscard]] bool AtEnd() const noexcept { return pos >= data.size(); }

    [[nodiscard]] std::uint8_t Peek() const noexcept { return data[pos]; }
};

motrix::core::AppError MakeParseError(std::string msg) {
    return motrix::core::MakeError(
        motrix::core::ErrorDomain::Domain, motrix::core::ErrorCode::ParseFailure, std::move(msg));
}

motrix::core::Result<std::string> ParseByteString(BencodeCursor& cur) {
    if (cur.AtEnd()) {
        return std::unexpected(MakeParseError("Unexpected EOF parsing bencode string length"));
    }
    const std::size_t digitsStart = cur.pos;
    std::uint64_t len = 0;
    while (!cur.AtEnd()) {
        const std::uint8_t ch = cur.Peek();
        if (ch == ':') {
            break;
        }
        if (ch < '0' || ch > '9') {
            return std::unexpected(MakeParseError("Invalid character in bencode string length"));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (len > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return std::unexpected(MakeParseError("Bencode string length overflow"));
        }
        len = len * 10u + digit;
        ++cur.pos;
    }

    if (cur.AtEnd() || cur.pos == digitsStart) {
        return std::unexpected(MakeParseError("Missing ':' in bencode byte string"));
    }
    if (cur.pos - digitsStart > 1 && cur.data[digitsStart] == '0') {
        return std::unexpected(MakeParseError("Leading zero in bencode byte string length"));
    }

    ++cur.pos;  // consume ':'
    const std::size_t remaining = cur.data.size() - cur.pos;
    if (len > static_cast<std::uint64_t>(remaining)) {
        return std::unexpected(MakeParseError("Truncated bencode byte string payload"));
    }

    const std::size_t byteLen = static_cast<std::size_t>(len);
    std::string out(reinterpret_cast<const char*>(cur.data.data() + cur.pos), byteLen);
    cur.pos += byteLen;
    return out;
}

motrix::core::Result<std::int64_t> ParseInteger(BencodeCursor& cur) {
    ++cur.pos;  // consume 'i'
    const std::size_t start = cur.pos;
    bool negative = false;
    if (!cur.AtEnd() && cur.Peek() == '-') {
        negative = true;
        ++cur.pos;
    }

    const std::size_t digitsStart = cur.pos;
    std::uint64_t mag = 0;
    while (!cur.AtEnd()) {
        const std::uint8_t ch = cur.Peek();
        if (ch == 'e') {
            break;
        }
        if (ch < '0' || ch > '9') {
            return std::unexpected(MakeParseError("Invalid digit in bencode integer"));
        }
        const std::uint64_t d = static_cast<std::uint64_t>(ch - '0');
        const std::uint64_t limit =
            negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u
                     : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (mag > (limit - d) / 10u) {
            return std::unexpected(MakeParseError("Bencode integer overflow"));
        }
        mag = mag * 10u + d;
        ++cur.pos;
    }

    if (cur.AtEnd() || cur.pos == digitsStart) {
        return std::unexpected(MakeParseError("Unterminated or empty bencode integer"));
    }
    if (negative && mag == 0) {
        return std::unexpected(MakeParseError("Negative zero in bencode integer"));
    }
    if (cur.pos - digitsStart > 1 && cur.data[digitsStart] == '0') {
        return std::unexpected(MakeParseError("Leading zero in bencode integer"));
    }
    (void)start;
    ++cur.pos;  // consume 'e'

    if (negative) {
        if (mag == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(mag);
    }
    return static_cast<std::int64_t>(mag);
}

motrix::core::Result<BencodeValue> ParseValue(BencodeCursor& cur, int depth) {
    if (depth > kMaxBencodeDepth) {
        return std::unexpected(MakeParseError("Bencode nesting depth exceeds safety limit"));
    }
    if (cur.AtEnd()) {
        return std::unexpected(MakeParseError("Unexpected EOF parsing bencode value"));
    }

    BencodeValue val;
    val.rawStart = cur.pos;

    const std::uint8_t tag = cur.Peek();
    if (tag == 'i') {
        auto iv = ParseInteger(cur);
        if (!iv) {
            return std::unexpected(iv.error());
        }
        val.kind = BencodeValue::Kind::Integer;
        val.integer = *iv;
    } else if (tag == 'l') {
        ++cur.pos;
        val.kind = BencodeValue::Kind::List;
        while (!cur.AtEnd() && cur.Peek() != 'e') {
            auto elem = ParseValue(cur, depth + 1);
            if (!elem) {
                return std::unexpected(elem.error());
            }
            val.list.push_back(std::move(*elem));
        }
        if (cur.AtEnd()) {
            return std::unexpected(MakeParseError("Unterminated bencode list"));
        }
        ++cur.pos;
    } else if (tag == 'd') {
        ++cur.pos;
        val.kind = BencodeValue::Kind::Dict;
        while (!cur.AtEnd() && cur.Peek() != 'e') {
            if (cur.Peek() < '0' || cur.Peek() > '9') {
                return std::unexpected(MakeParseError("Bencode dictionary key must be byte string"));
            }
            auto key = ParseByteString(cur);
            if (!key) {
                return std::unexpected(key.error());
            }
            auto entryVal = ParseValue(cur, depth + 1);
            if (!entryVal) {
                return std::unexpected(entryVal.error());
            }
            val.dict.emplace_back(std::move(*key), std::move(*entryVal));
        }
        if (cur.AtEnd()) {
            return std::unexpected(MakeParseError("Unterminated bencode dictionary"));
        }
        ++cur.pos;
    } else if (tag >= '0' && tag <= '9') {
        auto bs = ParseByteString(cur);
        if (!bs) {
            return std::unexpected(bs.error());
        }
        val.kind = BencodeValue::Kind::Bytes;
        val.bytes = std::move(*bs);
    } else {
        return std::unexpected(MakeParseError("Unknown bencode type tag"));
    }

    val.rawEnd = cur.pos;
    return val;
}

std::string BasenameOf(std::string_view path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) {
        return std::string(path);
    }
    return std::string(path.substr(pos + 1));
}

void AppendUniqueTracker(std::vector<std::string>& out, std::string_view tracker) {
    while (!tracker.empty()
           && (tracker.front() == ' ' || tracker.front() == '\t' || tracker.front() == '\r'
               || tracker.front() == '\n')) {
        tracker.remove_prefix(1);
    }
    while (!tracker.empty()
           && (tracker.back() == ' ' || tracker.back() == '\t' || tracker.back() == '\r'
               || tracker.back() == '\n')) {
        tracker.remove_suffix(1);
    }
    if (tracker.empty()) {
        return;
    }
    for (const auto& existing : out) {
        if (existing == tracker) {
            return;
        }
    }
    out.emplace_back(tracker);
}

std::string PreferredUtf8Field(const BencodeValue& dict, std::string_view key) {
    const std::string utf8Key = std::string(key) + ".utf-8";
    if (const auto* u = dict.FindKey(utf8Key);
        u != nullptr && u->kind == BencodeValue::Kind::Bytes && !u->bytes.empty()) {
        return u->bytes;
    }
    if (const auto* v = dict.FindKey(key); v != nullptr && v->kind == BencodeValue::Kind::Bytes) {
        return v->bytes;
    }
    return {};
}

void ExtractAnnounceTrackers(const BencodeValue& root, TorrentMetadata& meta) {
    if (const auto* ann = root.FindKey("announce");
        ann != nullptr && ann->kind == BencodeValue::Kind::Bytes) {
        AppendUniqueTracker(meta.trackers, ann->bytes);
    }
    const auto* annList = root.FindKey("announce-list");
    if (annList == nullptr || annList->kind != BencodeValue::Kind::List) {
        return;
    }
    for (const auto& tier : annList->list) {
        if (tier.kind == BencodeValue::Kind::Bytes) {
            AppendUniqueTracker(meta.trackers, tier.bytes);
            continue;
        }
        if (tier.kind != BencodeValue::Kind::List) {
            continue;
        }
        for (const auto& url : tier.list) {
            if (url.kind == BencodeValue::Kind::Bytes) {
                AppendUniqueTracker(meta.trackers, url.bytes);
            }
        }
    }
}

[[nodiscard]] motrix::core::Result<void> ParseMultiFileEntries(const BencodeValue& filesNode,
                                                               TorrentMetadata& meta) {
    if (filesNode.kind != BencodeValue::Kind::List || filesNode.list.empty()) {
        return std::unexpected(MakeParseError("Torrent 'files' must be a non-empty list"));
    }
    meta.isMultiFile = true;
    int idx = 1;
    for (const auto& f : filesNode.list) {
        if (f.kind != BencodeValue::Kind::Dict) {
            return std::unexpected(MakeParseError("Torrent file entry must be a dictionary"));
        }
        const auto* lenNode = f.FindKey("length");
        if (lenNode == nullptr || lenNode->kind != BencodeValue::Kind::Integer || lenNode->integer < 0) {
            return std::unexpected(MakeParseError("Torrent file entry missing valid 'length'"));
        }
        const BencodeValue* pathList = f.FindKey("path.utf-8");
        if (pathList == nullptr || pathList->kind != BencodeValue::Kind::List
            || pathList->list.empty()) {
            pathList = f.FindKey("path");
        }
        if (pathList == nullptr || pathList->kind != BencodeValue::Kind::List
            || pathList->list.empty()) {
            return std::unexpected(MakeParseError("Torrent file entry missing valid 'path' list"));
        }

        std::string relPath;
        for (const auto& seg : pathList->list) {
            if (seg.kind != BencodeValue::Kind::Bytes || seg.bytes.empty()) {
                return std::unexpected(MakeParseError("Invalid torrent path segment"));
            }
            if (!relPath.empty()) {
                relPath.push_back('/');
            }
            relPath += seg.bytes;
        }

        const std::uint64_t fileLen = static_cast<std::uint64_t>(lenNode->integer);
        if (meta.totalLength > std::numeric_limits<std::uint64_t>::max() - fileLen) {
            return std::unexpected(MakeParseError("Torrent total length overflow"));
        }
        meta.totalLength += fileLen;

        TorrentFileEntry entry{};
        entry.index = idx++;
        entry.path = relPath;
        entry.name = BasenameOf(relPath);
        entry.extension = ExtractFileExtension(entry.name);
        entry.length = fileLen;
        entry.selected = true;
        meta.files.push_back(std::move(entry));
    }
    return {};
}

}  // namespace

motrix::core::Result<TorrentMetadata> ParseTorrentBytes(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::unexpected(MakeParseError("Empty torrent buffer"));
    }

    BencodeCursor cur{bytes, 0};
    auto root = ParseValue(cur, 0);
    if (!root) {
        return std::unexpected(root.error());
    }
    if (!cur.AtEnd()) {
        return std::unexpected(MakeParseError("Trailing bytes after root bencode dictionary"));
    }
    if (root->kind != BencodeValue::Kind::Dict) {
        return std::unexpected(MakeParseError("Torrent root must be a bencode dictionary"));
    }

    const BencodeValue* info = root->FindKey("info");
    if (info == nullptr || info->kind != BencodeValue::Kind::Dict) {
        return std::unexpected(MakeParseError("Torrent missing 'info' dictionary"));
    }

    TorrentMetadata meta{};
    meta.infoHash = Sha1Hex(bytes.subspan(info->rawStart, info->rawEnd - info->rawStart));
    meta.name = PreferredUtf8Field(*info, "name");
    meta.comment = PreferredUtf8Field(*root, "comment");
    if (const auto* cb = root->FindKey("created by");
        cb != nullptr && cb->kind == BencodeValue::Kind::Bytes) {
        meta.createdBy = cb->bytes;
    }
    if (const auto* cd = root->FindKey("creation date");
        cd != nullptr && cd->kind == BencodeValue::Kind::Integer && cd->integer > 0) {
        meta.creationDate = cd->integer;
    }
    ExtractAnnounceTrackers(*root, meta);

    if (const auto* priv = info->FindKey("private");
        priv != nullptr && priv->kind == BencodeValue::Kind::Integer && priv->integer == 1) {
        meta.isPrivate = true;
    }
    if (const auto* pl = info->FindKey("piece length");
        pl != nullptr && pl->kind == BencodeValue::Kind::Integer && pl->integer > 0) {
        meta.pieceLength = static_cast<std::uint64_t>(pl->integer);
    }
    if (const auto* pieces = info->FindKey("pieces");
        pieces != nullptr && pieces->kind == BencodeValue::Kind::Bytes) {
        if (pieces->bytes.size() % 20 != 0) {
            return std::unexpected(MakeParseError("Invalid 'pieces' byte length (not multiple of 20)"));
        }
        meta.numPieces = static_cast<std::uint32_t>(pieces->bytes.size() / 20);
    }

    if (const auto* filesNode = info->FindKey("files"); filesNode != nullptr) {
        if (auto res = ParseMultiFileEntries(*filesNode, meta); !res) {
            return std::unexpected(res.error());
        }
        return meta;
    }
    const auto* lenNode = info->FindKey("length");
    if (lenNode == nullptr || lenNode->kind != BencodeValue::Kind::Integer || lenNode->integer < 0) {
        return std::unexpected(MakeParseError("Torrent 'info' missing both 'files' and 'length'"));
    }
    meta.isMultiFile = false;
    meta.totalLength = static_cast<std::uint64_t>(lenNode->integer);
    TorrentFileEntry single{};
    single.index = 1;
    single.path = meta.name;
    single.name = BasenameOf(meta.name);
    single.extension = ExtractFileExtension(single.name);
    single.length = meta.totalLength;
    single.selected = true;
    meta.files.push_back(std::move(single));
    return meta;
}

}  // namespace motrix::domain
