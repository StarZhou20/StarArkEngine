#include "engine/core/Guid.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>

namespace ark {

namespace {

std::mt19937_64& ThreadLocalRng() {
    thread_local std::mt19937_64 rng = []() {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return rng;
}

} // anonymous namespace

std::string Guid::NewGuid() {
    auto& rng = ThreadLocalRng();

    // 16 随机字节
    std::array<std::uint8_t, 16> b{};
    std::uint64_t r0 = rng();
    std::uint64_t r1 = rng();
    for (int i = 0; i < 8; ++i) {
        b[i]     = static_cast<std::uint8_t>((r0 >> (i * 8)) & 0xFF);
        b[i + 8] = static_cast<std::uint8_t>((r1 >> (i * 8)) & 0xFF);
    }

    // RFC 4122: version 4（b[6] 高 4 位 = 0100），variant（b[8] 高 2 位 = 10）
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);

    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);

    return std::string(buf, 36);
}

bool Guid::IsValid(std::string_view s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else {
            bool hex = (c >= '0' && c <= '9') ||
                       (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
            if (!hex) return false;
        }
    }
    return true;
}

} // namespace ark
