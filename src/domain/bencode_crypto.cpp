module motrix.domain;

import std;

namespace motrix::domain {
namespace {

constexpr std::uint32_t RotLeft32(std::uint32_t v, int bits) noexcept {
    return (v << bits) | (v >> (32 - bits));
}

void Sha1ProcessBlock(const std::uint8_t* block, std::array<std::uint32_t, 5>& state) noexcept {
    std::array<std::uint32_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t off = i * 4;
        w[i] = (static_cast<std::uint32_t>(block[off]) << 24)
               | (static_cast<std::uint32_t>(block[off + 1]) << 16)
               | (static_cast<std::uint32_t>(block[off + 2]) << 8)
               | static_cast<std::uint32_t>(block[off + 3]);
    }
    for (std::size_t i = 16; i < 80; ++i) {
        w[i] = RotLeft32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];

    for (std::size_t i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        const std::uint32_t temp = RotLeft32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = RotLeft32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

}  // namespace

std::string Sha1Hex(std::span<const std::uint8_t> data) {
    std::array<std::uint32_t, 5> state{
        0x67452301u,
        0xEFCDAB89u,
        0x98BADCFEu,
        0x10325476u,
        0xC3D2E1F0u,
    };

    const std::size_t fullBlocks = data.size() / 64;
    for (std::size_t b = 0; b < fullBlocks; ++b) {
        Sha1ProcessBlock(data.data() + b * 64, state);
    }

    std::array<std::uint8_t, 128> tail{};
    const std::size_t rem = data.size() % 64;
    for (std::size_t i = 0; i < rem; ++i) {
        tail[i] = data[fullBlocks * 64 + i];
    }
    tail[rem] = 0x80u;

    const std::size_t totalTailBytes = (rem + 1 + 8 <= 64) ? 64 : 128;
    const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8u;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[totalTailBytes - 8 + i] = static_cast<std::uint8_t>((bitLen >> ((7 - i) * 8)) & 0xFFu);
    }

    Sha1ProcessBlock(tail.data(), state);
    if (totalTailBytes == 128) {
        Sha1ProcessBlock(tail.data() + 64, state);
    }

    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string hex;
    hex.resize(40);
    for (std::size_t i = 0; i < 5; ++i) {
        const std::uint32_t w = state[i];
        for (std::size_t j = 0; j < 8; ++j) {
            const int shift = static_cast<int>((7 - j) * 4);
            hex[i * 8 + j] = kHex[(w >> shift) & 0xFu];
        }
    }
    return hex;
}

std::string Base64Encode(std::span<const std::uint8_t> data) {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (data.empty()) {
        return {};
    }

    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16)
                                     | (static_cast<std::uint32_t>(data[i + 1]) << 8)
                                     | static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back(kAlphabet[triple & 0x3Fu]);
        i += 3;
    }

    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(v >> 12) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(v >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(v >> 6) & 0x3Fu]);
        out.push_back('=');
    }
    return out;
}

}  // namespace motrix::domain
