// RHIRenderTarget.h — abstraction over framebuffers (Phase: deferred-pipeline prereq #3).
//
// A RenderTarget owns N color attachments + optionally 1 depth/stencil.
// Each attachment is created as a sampleable texture by default so it can
// be bound to a subsequent draw as input. Multisample attachments are
// resolve-on-blit and not directly sampleable; ShadowMap-style depth
// uses `shadowSampler = true` to enable hardware PCF.
//
// Migration target: replaces raw `glGenFramebuffers / glFramebufferTexture2D`
// in IBL / PostProcess / ShadowMap. Callers that still need the raw GL
// handle (e.g. binding the depth texture to a `sampler2DShadow` uniform)
// can fetch it via `GetColorTextureHandle(i)` / `GetDepthTextureHandle()`.
//
// IBL cubemap face / mip attachment is NOT covered by this abstraction
// yet — those sites stay raw-GL until a follow-up pass introduces
// `AttachToFace(face, mip)`. ShadowMap and the 9 PostProcess sites are
// the immediate users.
#pragma once

#include <cstdint>
#include <vector>

namespace ark {

/// Color attachment pixel format. Add entries here, the GL backend maps
/// them to internal/format/type triples in `GLRenderTarget.cpp`.
enum class RTColorFormat {
    None,         // attachment slot empty (used to terminate)
    RGBA8_UNorm,
    RGBA16F,
    RG16F,
    R16F,
    R32F,
    R8_UNorm,
};

/// Depth/stencil attachment format. `None` skips depth (e.g. bloom blur RT).
enum class RTDepthFormat {
    None,
    Depth24,
    Depth24Stencil8,
    Depth32F,
};

struct RTColorAttachmentDesc {
    RTColorFormat format = RTColorFormat::RGBA8_UNorm;
    int           samples = 1;     // 1 = no MSAA. >1 → MSAA texture (not sampleable).
};

struct RTDepthAttachmentDesc {
    RTDepthFormat format = RTDepthFormat::None;
    int           samples = 1;
    /// If true the depth texture is created as a renderbuffer (cheaper
    /// but cannot be sampled). Default `false` → sampleable depth texture.
    bool          renderbuffer = false;
    /// If true (depth-only sampleable mode), enable
    /// GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE so the depth
    /// texture can be bound to a `sampler2DShadow` for hardware PCF.
    bool          shadowSampler = false;
    /// If true and shadowSampler is true, set border color to 1.0
    /// (samples outside the map are fully lit). Default true.
    bool          clampToBorderWhite = true;
};

struct RenderTargetDesc {
    int width  = 0;
    int height = 0;
    /// 0..N color attachments. Empty vector = depth-only.
    std::vector<RTColorAttachmentDesc> colors;
    RTDepthAttachmentDesc depth;
};

class RHIRenderTarget {
public:
    virtual ~RHIRenderTarget() = default;

    /// Bind FBO + set viewport to (0,0,width,height). Saves the previous
    /// FBO + viewport so `Unbind()` restores them. Begin/End semantics
    /// match the existing ShadowMap helper.
    virtual void Bind() = 0;
    virtual void Unbind() = 0;

    /// Clear the bound attachments. Must be called between Bind/Unbind.
    /// `clearColor` only touches color attachments if any exist; same for
    /// depth/stencil. Use this in place of raw `glClear*` so future
    /// backends can implement load-ops correctly.
    virtual void Clear(bool clearColor, bool clearDepth,
                       float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f,
                       float depth = 1.0f) = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual int GetColorAttachmentCount() const = 0;
    virtual bool HasDepth() const = 0;

    /// Raw native handles — escape hatch during migration. Returns 0 if
    /// the requested attachment doesn't exist or is a renderbuffer.
    virtual uint32_t GetColorTextureHandle(int index) const = 0;
    virtual uint32_t GetDepthTextureHandle() const = 0;

    /// Underlying FBO handle (GL only). Used for blits during migration.
    virtual uint32_t GetNativeFramebufferHandle() const = 0;
};

} // namespace ark
