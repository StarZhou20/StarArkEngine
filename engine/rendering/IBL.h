#pragma once

#include <cstdint>
#include <memory>

namespace ark {

class Skybox;
class RHIDevice;
class RHIRenderTarget;

/// Image-Based Lighting precompute + runtime textures (Phase 12).
///
/// Consumes an environment cube map (from `Skybox::GetCubeMap()`) and produces:
///  * `GetIrradianceMap()`  — diffuse irradiance cube map (small, e.g. 32²)
///  * `GetPrefilterMap()`   — GGX-prefiltered environment cube map with
///                            roughness baked into mip levels
///  * `GetBrdfLUT()`        — 2D look-up table of `(NdotV, roughness)` →
///                            `(scale, bias)` for split-sum specular.
///
/// PBR fragment shader samples these for the ambient term:
///     ambient = (kD * diffuse + specular) * ao
///
/// Must be bound inside the HDR FBO? No — precompute passes bind their own
/// FBOs and restore the default framebuffer when finished.
class IBL {
public:
    IBL();
    ~IBL();

    IBL(const IBL&) = delete;
    IBL& operator=(const IBL&) = delete;

    /// Optional: route per-bake render targets through the RHI. When
    /// unset the bake routines fall back to raw GL allocation (identical
    /// to the pre-RT behaviour). Cubemap face-attach bakes (irradiance /
    /// prefilter) currently always take the raw path because the RT
    /// abstraction does not yet expose `AttachColorCubeFace(face, mip)`.
    void SetDevice(RHIDevice* device) { device_ = device; }

    /// Precompute irradiance + prefilter + BRDF LUT from `envCubeMap`.
    /// Safe to call multiple times (e.g. after the skybox changes); each
    /// call frees and rebuilds the IBL textures. On failure the old
    /// textures are cleared and `IsValid()` returns false.
    ///
    /// `envCubeMap` is the GL name of the source (linear HDR recommended).
    void Bake(uint32_t envCubeMap,
              int irradianceSize = 32,
              int prefilterSize  = 128,
              int brdfLutSize    = 512);

    bool IsValid() const { return valid_; }

    uint32_t GetIrradianceMap()  const { return irradianceMap_; }
    uint32_t GetPrefilterMap()   const { return prefilterMap_; }
    uint32_t GetBrdfLUT()        const { return brdfLUT_; }
    int      GetPrefilterMipLevels() const { return prefilterMipLevels_; }

private:
    void EnsurePrograms();
    void ReleaseTextures();
    void RenderCubeToCubemap(uint32_t captureFBO, uint32_t captureRBO,
                             uint32_t targetCube, int size,
                             uint32_t program,
                             uint32_t srcCube,
                             int mipLevel);
    void BakeIrradiance(uint32_t envCube, int size);
    void BakePrefilter(uint32_t envCube, int size);
    void BakeBrdfLUT(int size);

    static uint32_t CompileProgram(const char* vs, const char* fs, const char* name);

    uint32_t irradianceMap_ = 0;
    uint32_t prefilterMap_  = 0;
    uint32_t brdfLUT_       = 0;
    int      prefilterMipLevels_ = 0;

    // Reusable cube VAO / quad VAO for precompute passes.
    uint32_t cubeVAO_ = 0;
    uint32_t cubeVBO_ = 0;
    uint32_t quadVAO_ = 0;
    uint32_t quadVBO_ = 0;

    // Programs.
    uint32_t progIrradiance_ = 0;
    uint32_t progPrefilter_  = 0;
    uint32_t progBrdfLut_    = 0;

    // Optional RHI route for the BrdfLUT 2D RT. Cubemap bakes stay raw.
    RHIDevice* device_ = nullptr;
    std::unique_ptr<RHIRenderTarget> rtBrdfLUT_;

    bool programsReady_ = false;
    bool valid_         = false;
};

} // namespace ark
