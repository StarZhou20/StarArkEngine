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

/// GPU-side compressed texture format for UploadCompressed().
/// Matches the block-compression formats commonly found in DDS files.
enum class CompressedFormat {
    BC1_RGB,       // DXT1 opaque, linear
    BC1_RGB_sRGB,  // DXT1 opaque, sRGB
    BC3_RGBA,      // DXT5, linear
    BC3_RGBA_sRGB, // DXT5, sRGB (albedo with alpha)
    BC5_RG,        // ATI2 / 3Dc — 2-channel normal map (X,Y)
    BC7_RGBA,      // BPTC, linear (Bistro uses this heavily)
    BC7_RGBA_sRGB, // BPTC, sRGB (Bistro base-color)
};

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual void Upload(int width, int height, int channels, const uint8_t* data,
                        TextureFormat format = TextureFormat::sRGB_Auto) = 0;

    /// Upload GPU-compressed texture data. `data` must contain `mipCount` mip
    /// levels concatenated in memory, mip 0 first, each at its natural size.
    /// Returns false if the format is unsupported.
    virtual bool UploadCompressed(int width, int height, CompressedFormat format,
                                  const uint8_t* data, size_t dataSize,
                                  int mipCount) = 0;

    virtual void Bind(int unit = 0) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace ark
