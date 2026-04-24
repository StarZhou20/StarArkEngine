#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace ark {

/// Skybox: a cube map rendered as a fullscreen background.
///
/// Two ways to populate the cube map:
///   1. `SetFromFiles({+x,-x,+y,-y,+z,-z})` — 6 LDR PNG/JPG faces (sRGB).
///   2. `GenerateProceduralGradient(zenith, horizon, ground)` — runtime
///      procedural gradient so samples work without shipping assets. Called
///      automatically on Init() if no files are provided.
///
/// Rendered in linear HDR space (values > 1 allowed), so output goes into the
/// HDR FBO and picks up tone mapping / bloom from `PostProcess`.
class Skybox {
public:
    Skybox();
    ~Skybox();

    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    /// Allocate GL resources. Safe to call repeatedly (no-op after first).
    void Init();

    /// Load 6 face images from disk (sRGB). Faces must be square and
    /// same resolution. Order: +X -X +Y -Y +Z -Z.
    bool SetFromFiles(const std::array<std::string, 6>& faces);

    /// Fill the cube map with a vertical gradient from zenith → horizon →
    /// ground. Colors are in linear HDR (values > 1 permitted).
    void GenerateProceduralGradient(float zenithR, float zenithG, float zenithB,
                                    float horizonR, float horizonG, float horizonB,
                                    float groundR, float groundG, float groundB,
                                    int faceSize = 128);

    /// Draw the skybox. Must be bound inside the HDR scene FBO; writes
    /// depth = 1.0 fragments. Uses depth func LEQUAL and depth mask off.
    void Render(const glm::mat4& view, const glm::mat4& projection);

    void  SetEnabled(bool enabled) { enabled_ = enabled; }
    bool  IsEnabled() const { return enabled_; }
    void  SetIntensity(float i) { intensity_ = i; }
    float GetIntensity() const { return intensity_; }

    uint32_t GetCubeMap() const { return cubemap_; }

private:
    void CompileProgram();
    void AllocGeometry();
    void EnsureCubeMap();

    uint32_t cubemap_ = 0;
    uint32_t program_ = 0;
    uint32_t cubeVAO_ = 0;
    uint32_t cubeVBO_ = 0;

    bool  initialized_ = false;
    bool  hasData_     = false;
    bool  enabled_     = true;
    float intensity_   = 1.0f;
};

} // namespace ark
