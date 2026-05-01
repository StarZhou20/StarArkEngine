#pragma once

#include "engine/rhi/RHIDevice.h"
#include "engine/rhi/RHICommandBuffer.h"
#include "engine/rhi/RHIPipeline.h"
#include "engine/rendering/ShaderManager.h"
#include "engine/rendering/PostProcess.h"
#include "engine/rendering/Skybox.h"
#include "engine/rendering/IBL.h"
#include "engine/rendering/ShadowMap.h"
#include "engine/rendering/DeferredRenderer.h"
#include "engine/rendering/RenderSettings.h"
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace ark {

class Camera;
class Light;
class MeshRenderer;
class Window;

/// ForwardRenderer: iterates over cameras → collects visible MeshRenderers + lights → draws.
/// Called by EngineBase in main loop step 11.
class ForwardRenderer {
public:
    explicit ForwardRenderer(RHIDevice* device);
    ~ForwardRenderer() = default;

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    /// Render all cameras. Called once per frame.
    void RenderFrame(Window* window);

    // --- Render settings (aggregate; serialized in Phase M10) --------------
    RenderSettings&       GetRenderSettings()       { return settings_; }
    const RenderSettings& GetRenderSettings() const { return settings_; }

    // Exposure (convenience pass-through to settings_.exposure).
    void  SetExposure(float exposure) { settings_.exposure = exposure; }
    float GetExposure() const { return settings_.exposure; }

    /// Shader registry (file-backed, with optional hot reload). Samples and
    /// user code should request shaders from here rather than compiling
    /// GLSL strings manually, so that hot reload works uniformly.
    ShaderManager* GetShaderManager() { return shaderManager_.get(); }

    /// HDR + Bloom post-process stack (Phase 10). Owned by the renderer.
    PostProcess* GetPostProcess() { return postProcess_.get(); }

    /// Skybox (Phase 11). Lazily initialized on first access; defaults to
    /// a procedural sky gradient until populated via `SetFromFiles`.
    Skybox* GetSkybox() { return skybox_.get(); }

    /// Image-based lighting (Phase 12). Baked once from the skybox's cube map;
    /// `RebakeIBL()` re-runs the convolution (call after changing the skybox).
    IBL* GetIBL() { return ibl_.get(); }
    void RebakeIBL();

    /// Directional shadow map (Phase 13). Owned by the renderer;
    /// updated each frame from the first directional light.
    ShadowMap* GetShadowMap() { return shadowMap_.get(); }

    // Bloom knobs (pass-through to settings_.bloom).
    void  SetBloomEnabled(bool enabled)   { settings_.bloom.enabled    = enabled; }
    bool  IsBloomEnabled() const          { return settings_.bloom.enabled; }
    void  SetBloomThreshold(float t)      { settings_.bloom.threshold  = t; }
    float GetBloomThreshold() const       { return settings_.bloom.threshold; }
    void  SetBloomStrength(float s)       { settings_.bloom.strength   = s; }
    float GetBloomStrength() const        { return settings_.bloom.strength; }
    void  SetBloomIterations(int n)       { settings_.bloom.iterations = n; }
    int   GetBloomIterations() const      { return settings_.bloom.iterations; }

    /// Stage F (deferred): render transparent renderers (back-to-front)
    /// into the supplied render target using the standard forward `pbr`
    /// shading path (full shadow + IBL light setup). Caller is expected
    /// to have copied the opaque scene depth into `targetRT` so depth
    /// testing works correctly. Used by the deferred dispatch after the
    /// G-buffer/lighting pass and before PostProcess composite.
    void RenderTransparentOverlay(Camera* camera, RHIRenderTarget* targetRT);

private:
    void RenderCamera(Camera* camera, Window* window);
    void SetLightUniforms(RHIShader* shader, Camera* camera);
    void DrawMeshRenderer(MeshRenderer* renderer, Camera* camera);

    /// Render all shadow-casting meshes into the shadow depth map.
    /// Returns true if the shadow pass produced a usable depth buffer.
    bool RenderShadowPass();

    /// Look up or create a cached pipeline for the given desc.
    RHIPipeline* GetOrCreatePipeline(const PipelineDesc& desc);

    RHIDevice* device_;
    std::unique_ptr<RHICommandBuffer> cmdBuffer_;
    std::unique_ptr<ShaderManager> shaderManager_;
    std::unique_ptr<PostProcess>   postProcess_;
    std::unique_ptr<Skybox>        skybox_;
    std::unique_ptr<IBL>           ibl_;
    std::unique_ptr<ShadowMap>     shadowMap_;
    /// Deferred sub-renderer (Roadmap #9). Lazily created on the first
    /// frame `settings_.pipeline == Deferred`. Owned here so that
    /// EngineBase keeps its single `renderer_` handle.
    std::unique_ptr<DeferredRenderer> deferredRenderer_;

    RenderSettings settings_;
    bool iblBaked_ = false;
    bool shadowThisFrame_ = false;

    // Cached world-space direction of the main directional light (the one
    // used for shadow map + contact shadows). Zero vector if none exists.
    float mainLightDirWorld_[3] = {0.0f, -1.0f, 0.0f};
    bool  hasMainLight_         = false;

    // TAA: previous-frame view-projection of the primary camera.
    float prevViewProj_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool  hasPrevViewProj_  = false;

    // Pipeline cache: keyed by hash of PipelineDesc fields
    struct PipelineCacheEntry {
        std::unique_ptr<RHIPipeline> pipeline;
    };
    std::unordered_map<uint64_t, PipelineCacheEntry> pipelineCache_;

    static uint64_t HashPipelineDesc(const PipelineDesc& desc);
};

} // namespace ark
