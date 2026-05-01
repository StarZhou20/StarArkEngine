// engine/util/Sha256.h — minimal SHA-256 (FIPS 180-4) for schema_hash and
// asset content addressing. Header-light, single-file, no third-party deps.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ark {

class Sha256 {
public:
    Sha256();

    void Update(const void* data, std::size_t len);
    void Update(std::string_view s) { Update(s.data(), s.size()); }

    // Finalize and write 32 raw bytes. Calling Update after Finalize is UB.
    std::array<std::uint8_t, 32> Finalize();

    // Convenience: one-shot hex digest.
    static std::string HashHex(std::string_view s);

    // 16-char prefix per ModSpec §6.2 schema_hash convention.
    static std::string HashHex16(std::string_view s);

private:
    void Compress(const std::uint8_t block[64]);

    std::uint32_t state_[8];
    std::uint64_t bitLen_;
    std::uint8_t  buf_[64];
    std::size_t   bufLen_;
};

} // namespace ark
