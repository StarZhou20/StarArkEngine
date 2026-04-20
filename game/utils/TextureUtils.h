#pragma once

#include "engine/rhi/RHITexture.h"
#include "engine/rhi/RHIDevice.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace utils {

inline std::shared_ptr<ark::RHITexture> CreateCheckerTexture(ark::RHIDevice* device, int size = 256, int tiles = 8) {
    std::vector<uint8_t> pixels(size * size * 3);
    int tileSize = size / tiles;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool white = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            uint8_t c = white ? 200 : 50;
            int idx = (y * size + x) * 3;
            pixels[idx] = c;
            pixels[idx + 1] = c;
            pixels[idx + 2] = c;
        }
    }
    auto tex = std::shared_ptr<ark::RHITexture>(device->CreateTexture().release());
    tex->Upload(size, size, 3, pixels.data());
    return tex;
}

} // namespace utils
