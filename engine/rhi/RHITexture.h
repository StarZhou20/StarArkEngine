#pragma once

#include <cstdint>

namespace ark {

/// How to interpret pixel data on upload.
/// - sRGB_Auto: 3 or 4 channel data is treated as sRGB-encoded (stored with
///   GL_SRGB8_ALPHA8); 1 or 2 channel data is linear. Correct default for
///   color / albedo / diffuse / emissive maps.
/// - Linear: all channels linear (for normal maps, roughness, metallic, MRA,
///   data masks, height, etc).
enum class TextureFormat {
    sRGB_Auto,
    Linear,
};

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual void Upload(int width, int height, int channels, const uint8_t* data,
                        TextureFormat format = TextureFormat::sRGB_Auto) = 0;
    virtual void Bind(int unit = 0) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace ark
