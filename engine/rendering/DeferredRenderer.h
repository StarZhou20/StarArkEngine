#pragma once

#include "engine/rhi/RHIPipeline.h"
#include "engine/rhi/RHIRenderTarget.h"
#include "engine/rhi/RHIShader.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace ark {

class Camera;
class RHIDevice;
class RHICommandBuffer;
class ShaderManager;
class ShadowMap;
class IBL;
struct RenderSettings;

/// Deferred renderer (Roadmap #9). Owns its own G-buffer + lit HDR target,
/// uses the shared `gbuffer.{vert,frag}` shader to write surface data into
/// the MRT G-buffer, then (eventually) a fullscreen `lighting.frag` pass
/// to resolve lighting into `lit_`. Integration into EngineBase is gated
/// on `RenderSettings.pipeline == Deferred`.
///
/// G-buffer layout (see `gbuffer.frag`):
///   RT0  RGBA8_UNorm   .rgb=albedo  .a=metallic
///   RT1  RGBA16F       .xyz=worldNormal  .w=roughness
///   RT2  RGBA16F       .rgb=emissive  .a=motion-scratch (TBD)
///   RT3  RGBA8_UNorm   .r=ao  .gba=flags-scratch (TBD)
///   D    Depth32F texture (sampled by lighting pass for worldPos)
///
/// Stage B (current): geometry pass + blit RT0 (albedo) → backbuffer for
/// visual confirmation that MRT plumbing is correct. Lighting pass +
/// PostProcess plumbing land in stage C.
class DeferredRenderer {
public:
    DeferredRenderer(RHIDevice* device, ShaderManager* shaderManager);
    ~DeferredRenderer();

    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;

    /// Allocate the G-buffer + lit HDR target. Safe to call multiple times
    /// (will tear down and re-allocate when the size changes).
    bool Initialize(int width, int height);

    /// Tear down and re-allocate render targets at the new size. No-op when
    /// the size is unchanged. Returns false on allocation failure.
    bool Resize(int width, int height);

    /// Run the deferred frame for `camera`:
    ///   1. (lazy) ensure RTs sized to (backBufferW, backBufferH)
    ///   2. gbuffer geometry pass  → 4 RTs + Depth32F
    ///   3. blit RT0 (albedo) → backbuffer (stage B placeholder)
    /// Returns false if the gbuffer shader is missing or RT alloc failed
    /// (caller should fall back to forward).
    bool Render(Camera* camera, int backBufferWidth, int backBufferHeight);

    /// Per-frame: caller (ForwardRenderer) provides shadow map + IBL +
    /// settings. Pointers may be null (shadow/IBL disabled). Lifetime is
    /// managed by the caller; DeferredRenderer only reads through them.
    void SetFrameContext(const RenderSettings* settings,
                         ShadowMap* shadowMap, bool shadowEnabledThisFrame,
                         IBL* ibl, bool iblBaked);

    /// Stage F (deferred): blit the G-buffer's depth into `lit_`'s depth
    /// renderbuffer so a subsequent forward transparent overlay can
    /// depth-test against the opaque scene. Called by ForwardRenderer
    /// between Render() and the transparent overlay.
    void BlitGBufferDepthToLit();

    bool IsInitialized() const { return initialized_; }
    int  GetWidth()  const { return width_; }
    int  GetHeight() const { return height_; }

    RHIRenderTarget* GetGBuffer()   const { return gbuffer_.get(); }
    RHIRenderTarget* GetLitTarget() const { return lit_.get(); }

private:
    void ReleaseTargets();
    bool AllocateTargets(int width, int height);

    /// Look up or create a cached pipeline for the given desc (separate
    /// cache from ForwardRenderer's so deferred PSOs can carry the
    /// gbuffer color-attachment list without polluting forward).
    RHIPipeline* GetOrCreatePipeline(const PipelineDesc& desc);
    static uint64_t HashPipelineDesc(const PipelineDesc& desc);

    RHIDevice*     device_ = nullptr;
    ShaderManager* shaderManager_ = nullptr;
    std::unique_ptr<RHICommandBuffer> cmdBuffer_;

    std::unique_ptr<RHIRenderTarget> gbuffer_;  // 4 colors + Depth32F
    std::unique_ptr<RHIRenderTarget> lit_;      // single RGBA16F HDR

    /// Shared GLSL program for all gbuffer draws. Resolved lazily through
    /// `shaderManager_->Get("gbuffer")` on the first Render call.
    std::shared_ptr<RHIShader> gbufferShader_;
    /// Fullscreen lighting program (samples G-buffer + depth, writes lit_).
    std::shared_ptr<RHIShader> lightingShader_;
    /// Empty VAO for the fullscreen triangle (gl_VertexID-driven; no VBO).
    /// Created on first lighting pass; destroyed in ~DeferredRenderer.
    uint32_t fullscreenVAO_ = 0;

    // Per-frame context populated by ForwardRenderer before each Render().
    const RenderSettings* settings_ = nullptr;
    ShadowMap* shadowMap_ = nullptr;
    IBL*       ibl_       = nullptr;
    bool       shadowEnabledThisFrame_ = false;
    bool       iblBaked_  = false;

    struct PipelineCacheEntry {
        std::unique_ptr<RHIPipeline> pipeline;
    };
    std::unordered_map<uint64_t, PipelineCacheEntry> pipelineCache_;

    int  width_  = 0;
    int  height_ = 0;
    bool initialized_ = false;

    // Cached single-attachment FBO used by BlitGBufferDepthToLit. Lazily
    // created on first use; reattaching the depth texture each frame is
    // free vs. the per-frame glGen/glDelete this used to do.
    uint32_t blitTmpFBO_ = 0;
};

} // namespace ark
