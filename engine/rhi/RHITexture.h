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

/// GPU-side typed format for empty/render-target textures (UploadEmpty).
/// Mirrors `RTColorFormat` plus depth variants; kept separate so RHITexture
/// can be used standalone (e.g. as a G-buffer texture sampled later by a
/// fullscreen pass) without dragging in the render-target abstraction.
enum class TextureColorFormat {
    RGBA8_UNorm,
    RGBA8_sRGB,
    RGBA16F,
    RG16F,
    R16F,
    R32F,
    R8_UNorm,
    Depth24,
    Depth32F,
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

    /// Allocate immutable storage with no source data — for textures that
    /// will be written by a render pass (G-buffer slots, history buffers,
    /// etc). Filtering defaults to LINEAR/CLAMP. Replaces any previously
    /// uploaded data; safe to call repeatedly to resize.
    virtual void UploadEmpty(int width, int height, TextureColorFormat format) = 0;

    virtual void Bind(int unit = 0) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace ark
