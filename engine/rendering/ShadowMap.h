// ShadowMap.h — Directional-light shadow map (Phase 13).
//
// Owns a GL_DEPTH_COMPONENT24 depth texture + FBO. The caller
// (`ForwardRenderer`) is responsible for computing `lightSpaceMatrix`
// from the light direction and rendering all shadow-casting meshes
// with a depth-only pipeline while `Begin()` is active.
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace ark {

class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    /// Lazily create/recreate the FBO + depth texture at `resolution`.
    /// Returns true if ready to render into.
    bool Init(int resolution);

    /// Bind the shadow FBO, set viewport to map size, clear depth.
    /// Caches the previous viewport + FBO to restore in End().
    void Begin();

    /// Restore previous FBO and viewport.
    void End();

    uint32_t        GetDepthTexture() const { return depthTex_; }
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
                      float farPlane);

    bool IsValid() const { return fbo_ != 0 && depthTex_ != 0; }

private:
    uint32_t fbo_       = 0;
    uint32_t depthTex_  = 0;
    int      resolution_ = 0;

    int prevViewport_[4] = {0, 0, 0, 0};
    int prevFbo_         = 0;

    glm::mat4 lightSpaceMatrix_{1.0f};
};

} // namespace ark
