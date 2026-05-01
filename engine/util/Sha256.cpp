// engine/util/Sha256.cpp — FIPS 180-4 reference implementation.
// Independent of platform endianness; bytes are interpreted big-endian.

#include "engine/util/Sha256.h"

#include <cstring>

namespace ark {

namespace {

constexpr std::uint32_t kInit[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t Rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); }

} // namespace

Sha256::Sha256() {
    std::memcpy(state_, kInit, sizeof(state_));
    bitLen_ = 0;
    bufLen_ = 0;
}

void Sha256::Compress(const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[i * 4 + 0]) << 24)
             | (std::uint32_t(block[i * 4 + 1]) << 16)
             | (std::uint32_t(block[i * 4 + 2]) <<  8)
             | (std::uint32_t(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = Rotr(w[i-15], 7) ^ Rotr(w[i-15], 18) ^ (w[i-15] >>  3);
        std::uint32_t s1 = Rotr(w[i-2], 17) ^ Rotr(w[i-2],  19) ^ (w[i-2]  >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        std::uint32_t ch = (e & f) ^ (~e & g);
        std::uint32_t t1 = h + S1 + ch + kRound[i] + w[i];
        std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::Update(const void* data, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    bitLen_ += static_cast<std::uint64_t>(len) * 8u;
    while (len > 0) {
        std::size_t take = (64 - bufLen_ < len) ? (64 - bufLen_) : len;
        std::memcpy(buf_ + bufLen_, p, take);
        bufLen_ += take;
        p       += take;
        len     -= take;
        if (bufLen_ == 64) {
            Compress(buf_);
            bufLen_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::Finalize() {
    // Append 0x80, then zeros until len%64 == 56, then 8-byte big-endian bit length.
    std::uint64_t bits = bitLen_;
    buf_[bufLen_++] = 0x80;
    if (bufLen_ > 56) {
        while (bufLen_ < 64) buf_[bufLen_++] = 0;
        Compress(buf_);
        bufLen_ = 0;
    }
    while (bufLen_ < 56) buf_[bufLen_++] = 0;
    for (int i = 7; i >= 0; --i) buf_[bufLen_++] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xff);
    Compress(buf_);

    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >>  8) & 0xff);
        out[i * 4 + 3] = static_cast<std::uint8_t>((state_[i]      ) & 0xff);
    }
    return out;
}

std::string Sha256::HashHex(std::string_view s) {
    Sha256 h;
    h.Update(s);
    auto bytes = h.Finalize();
    static const char kHex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[i * 2 + 0] = kHex[(bytes[i] >> 4) & 0xf];
        out[i * 2 + 1] = kHex[(bytes[i]     ) & 0xf];
    }
    return out;
}

std::string Sha256::HashHex16(std::string_view s) {
    return HashHex(s).substr(0, 16);
}

} // namespace ark
