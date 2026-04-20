// TextureLoader.h — Load image files into RHITexture via stb_image
#pragma once

#include "engine/rhi/RHITexture.h"
#include "engine/rhi/RHIDevice.h"
#include <memory>
#include <string>

namespace ark {

class TextureLoader {
public:
    /// Load an image file (PNG/JPG/BMP/TGA) into a new RHITexture.
    /// Returns nullptr on failure.
    static std::shared_ptr<RHITexture> Load(RHIDevice* device, const std::string& filepath);
};

} // namespace ark
