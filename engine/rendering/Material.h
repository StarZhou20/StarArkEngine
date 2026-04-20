#pragma once

#include "engine/rhi/RHIShader.h"
#include "engine/rhi/RHITexture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace ark {

/// Material holds a shader reference and per-material parameters.
/// The ForwardRenderer sets per-frame uniforms (lights, camera);
/// Material::Bind() sets per-material uniforms.
class Material {
public:
    Material() = default;
    ~Material() = default;

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    // --- Shader ---
    void SetShader(std::shared_ptr<RHIShader> shader) { shader_ = std::move(shader); }
    RHIShader* GetShader() const { return shader_.get(); }

    // --- Color properties ---
    void SetColor(const glm::vec4& color) { color_ = color; }
    const glm::vec4& GetColor() const { return color_; }

    void SetSpecular(const glm::vec3& spec) { specular_ = spec; }
    const glm::vec3& GetSpecular() const { return specular_; }

    void SetShininess(float s) { shininess_ = s; }
    float GetShininess() const { return shininess_; }

    // --- PBR properties ---
    void SetMetallic(float m) { metallic_ = m; }
    float GetMetallic() const { return metallic_; }

    void SetRoughness(float r) { roughness_ = r; }
    float GetRoughness() const { return roughness_; }

    void SetAO(float ao) { ao_ = ao; }
    float GetAO() const { return ao_; }

    void SetPBR(bool enabled) { pbrEnabled_ = enabled; }
    bool IsPBR() const { return pbrEnabled_; }

    // --- Texture ---
    void SetDiffuseTexture(std::shared_ptr<RHITexture> tex) { diffuseTex_ = std::move(tex); }
    RHITexture* GetDiffuseTexture() const { return diffuseTex_.get(); }

    // --- Apply per-material uniforms to shader ---
    void Bind() const;

private:
    std::shared_ptr<RHIShader> shader_;
    std::shared_ptr<RHITexture> diffuseTex_;

    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specular_{0.5f};
    float shininess_ = 32.0f;

    // PBR
    float metallic_ = 0.0f;
    float roughness_ = 0.5f;
    float ao_ = 1.0f;
    bool pbrEnabled_ = false;
};

} // namespace ark
