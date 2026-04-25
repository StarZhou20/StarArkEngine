#pragma once

#include <cstdint>

namespace ark {

/// PostProcess: owns an HDR offscreen framebuffer + bloom ping-pong chain, and
/// runs the final tone-mapping composite to the default framebuffer.
///
/// Lifecycle:
///   Init(w, h)           -> allocate GL resources
///   BeginScene(w, h)     -> bind HDR FBO (auto-resizes on size change)
///   ...scene draws here (cameras render into HDR FBO)...
///   EndScene()           -> unbind HDR FBO
///   ApplySSAO(...)       -> (optional) run SSAO on the HDR depth buffer
///   Apply(exposure, ...) -> bright-pass + blur + composite to FBO 0
///
/// Phase 10: HDR FBO + Bloom.
/// Phase 14: SSAO (depth-only, normal reconstructed from depth derivatives).
class PostProcess {
public:
    PostProcess();
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    /// Allocate initial GL resources at the given size. Safe to call once.
    void Init(int width, int height);

    /// Bind HDR FBO for scene rendering. Resizes internal targets if the
    /// window size changed since last frame.
    void BeginScene(int width, int height);

    /// Unbind HDR FBO (revert to default framebuffer). If MSAA is active the
    /// multisample buffer is resolved into the sampleable single-sample
    /// hdrColor_/hdrDepth_ textures used by all downstream passes.
    void EndScene();

    /// Configure MSAA sample count. 0/1 disables; 2/4/8 enables. Takes effect
    /// on the next frame (lazy reallocation in BeginScene).
    void SetMsaaSamples(int samples);

    /// Run SSAO pass + separable blur. Leaves an AO factor in the internal
    /// AO texture, which the composite pass multiplies the scene by.
    /// `projMat` is the primary camera's projection matrix (column-major 16).
    void ApplySSAO(const float* projMat,
                   float intensity, float radius, float bias, int samples);

    /// Run a short screen-space ray-march toward the main directional light
    /// and store a per-pixel contact-shadow factor in the internal texture.
    /// `viewLightDir` is the view-space direction **toward** the light
    /// (column-major 3 floats, must be normalized).
    void ApplyContactShadow(const float* projMat,
                            const float* viewLightDir,
                            int   steps,
                            float maxDistance,
                            float thickness,
                            float strength);

    /// Run screen-space reflections. Reconstructs view-space normals from
    /// depth derivatives, marches the reflection ray in screen space, and
    /// writes per-pixel reflected radiance to an internal texture. Composite
    /// blends this into the scene weighted by the upward-facing mask.
    void ApplySSR(const float* projMat,
                  const float* invProjMat,
                  float intensity,
                  float maxDistance,
                  int   steps,
                  float thickness,
                  float fadeEdge);

    /// Run bright-pass + gaussian blur ping-pong + composite to default FB.
    /// When `ssaoEnabled` is true, the composite samples the AO texture
    /// produced by a prior ApplySSAO() call for that frame.
    void Apply(int screenW, int screenH,
               float exposure,
               float bloomThreshold,
               float bloomStrength,
               int blurIterations,
               bool ssaoEnabled = false,
               bool contactShadowEnabled = false,
               bool fxaaEnabled = false,
               int  tonemapMode = 1,
               bool ssrEnabled = false,
               bool taaEnabled = false,
               float taaBlendNew = 0.10f,
               const float* prevViewProj16 = nullptr,
               const float* curInvViewProj16 = nullptr);

    /// Configure exponential height-fog applied during composite. Pass the
    /// **inverse view-projection** matrix (column-major 16) and the world-
    /// space camera position so the shader can reconstruct world positions
    /// from depth. Set `enabled=false` to disable fog for the next Apply().
    void SetFog(bool enabled,
                const float* invViewProj4x4,
                const float* cameraPos3,
                const float* color3,
                float density,
                float heightStart,
                float heightFalloff,
                float maxOpacity);

    // Bloom / exposure configuration (also settable at call time via Apply()).
    bool  IsBloomEnabled() const { return bloomEnabled_; }
    void  SetBloomEnabled(bool enabled) { bloomEnabled_ = enabled; }

private:
    void ResizeIfNeeded(int width, int height);
    void AllocTargets(int width, int height);
    void ReleaseTargets();

    void CompilePrograms();
    void ReleasePrograms();

    void EnsureSSAOResources();   // kernel + noise texture (one-time)

    static uint32_t CompileProgram(const char* vs, const char* fs, const char* name);

    // HDR scene framebuffer (full-res RGBA16F + depth texture).
    // When MSAA is enabled this holds the resolved single-sample target.
    uint32_t hdrFBO_   = 0;
    uint32_t hdrColor_ = 0;  // RGBA16F texture
    uint32_t hdrDepth_ = 0;  // GL_DEPTH_COMPONENT24 texture (sampleable for SSAO)

    // Optional multisample HDR target. The scene renders into this and
    // EndScene() blits it into hdrFBO_ to resolve.
    uint32_t hdrMsFBO_     = 0;
    uint32_t hdrMsColor_   = 0;  // renderbuffer, GL_RGBA16F multisample
    uint32_t hdrMsDepth_   = 0;  // renderbuffer, GL_DEPTH_COMPONENT24 multisample
    int      msaaSamples_  = 4;  // desired sample count (0/1 = off)
    int      msaaActive_   = 0;  // allocated sample count

    // Bloom ping-pong (half-res RGBA16F)
    uint32_t bloomFBO_[2]     = {0, 0};
    uint32_t bloomColor_[2]   = {0, 0};

    // SSAO (full-res R8; blurred ping-pong uses same resolution)
    uint32_t ssaoFBO_[2]   = {0, 0};
    uint32_t ssaoColor_[2] = {0, 0};   // [0]=raw AO, [1]=blurred AO
    uint32_t ssaoNoiseTex_ = 0;        // 4x4 random rotation noise (RG16F)

    // Contact shadow (full-res R8)
    uint32_t contactFBO_   = 0;
    uint32_t contactTex_   = 0;

    // SSR (half-res RGBA16F).
    uint32_t ssrFBO_   = 0;
    uint32_t ssrColor_ = 0;

    // TAA history (full-res sRGB8 — we run TAA in LDR after composite).
    uint32_t taaFBO_         = 0;
    uint32_t taaHistory_[2]  = {0, 0};
    int      taaWriteIndex_  = 0;     // ping-pong index for the next write
    bool     taaHistoryValid_ = false;

    // LDR intermediate for FXAA (full-res RGBA8, linear LDR after tonemap)
    uint32_t ldrFBO_   = 0;
    uint32_t ldrColor_ = 0;

    int width_  = 0;
    int height_ = 0;

    // Fullscreen triangle VAO/VBO
    uint32_t fsVAO_ = 0;
    uint32_t fsVBO_ = 0;

    // Shader programs
    uint32_t progBright_    = 0;
    uint32_t progBlur_      = 0;
    uint32_t progComposite_ = 0;
    uint32_t progSSAO_      = 0;
    uint32_t progSSAOBlur_  = 0;
    uint32_t progContact_   = 0;
    uint32_t progFXAA_      = 0;
    uint32_t progSSR_       = 0;
    uint32_t progTAA_       = 0;

    // Precomputed hemisphere kernel (flat xyz triplets, max 64 samples).
    float    ssaoKernel_[64 * 3] = {};
    int      ssaoKernelSize_     = 0;

    bool bloomEnabled_ = true;
    bool initialized_  = false;
    bool ssaoValidThisFrame_ = false;  // set true by ApplySSAO, read by Apply
    bool contactValidThisFrame_ = false; // set true by ApplyContactShadow
    bool ssrValidThisFrame_  = false;  // set true by ApplySSR

    // Fog state (set by SetFog, read by Apply).
    bool  fogEnabled_       = false;
    float fogInvViewProj_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float fogCameraPos_[3]  = {0,0,0};
    float fogColor_[3]      = {0.6f, 0.65f, 0.7f};
    float fogDensity_       = 0.006f;
    float fogHeightStart_   = 0.2f;
    float fogHeightFalloff_ = 0.18f;
    float fogMaxOpacity_    = 0.85f;
};

} // namespace ark
