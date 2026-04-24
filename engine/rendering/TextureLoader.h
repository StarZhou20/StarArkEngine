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
    /// `isSRGB = true` (default) marks color/albedo/diffuse/emissive maps so the
    /// GPU performs sRGB→linear on sample. Set `false` for normal, roughness,
    /// metallic, AO, MRA and any other data (non-color) maps.
    /// Returns nullptr on failure.
    static std::shared_ptr<RHITexture> Load(RHIDevice* device,
                                            const std::string& filepath,
                                            bool isSRGB = true);
};

} // namespace ark
