#pragma once

#include <cstdint>
#include <memory>
#include <glm/glm.hpp>

namespace ark {

class RHIDevice;
class RHIRenderTarget;

class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    /// Lazily create/recreate the RT at `resolution`. Requires a valid
    /// `RHIDevice` (uses `CreateRenderTarget` for a depth-only sampleable
    /// PCF-enabled depth texture). Returns true on success.
    bool Init(RHIDevice* device, int resolution);

    /// Bind the shadow RT, set viewport, clear depth.
    void Begin();

    /// Restore previous FBO/viewport.
    void End();

    uint32_t        GetDepthTexture() const;
    int             GetResolution() const { return resolution_; }
    const glm::mat4& GetLightSpaceMatrix() const { return lightSpaceMatrix_; }
    void            SetLightSpaceMatrix(const glm::mat4& m) { lightSpaceMatrix_ = m; }

    /// Compute an orthographic light view-projection that encloses a box
    /// around `focusPoint` along the (normalized) light direction.
    /// Result is stored internally and returned by `GetLightSpaceMatrix()`.
    void UpdateMatrix(const glm::vec3& lightDirWorld,
                      const glm::vec3& focusPoint,
                      float orthoHalfSize,
                      float nearPlane,
                      float farPlane,
                      int   resolution = 0);   // 0 = no texel snap

    bool IsValid() const;

private:
    std::unique_ptr<RHIRenderTarget> rt_;
    int      resolution_ = 0;

    glm::mat4 lightSpaceMatrix_{1.0f};
};

} // namespace ark

