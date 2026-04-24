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

    void SetEmissive(const glm::vec3& e) { emissive_ = e; }
    const glm::vec3& GetEmissive() const { return emissive_; }

    void SetPBR(bool enabled) { pbrEnabled_ = enabled; }
    bool IsPBR() const { return pbrEnabled_; }

    // --- Textures ---
    // Unit 0: albedo/diffuse  (color, sRGB)
    // Unit 1: normal map      (linear, tangent-space)
    // Unit 2: metallic-roughness (linear; R=?, G=roughness, B=metallic — glTF convention)
    // Unit 3: ambient occlusion (linear)
    // Unit 4: emissive         (color, sRGB)
    void SetDiffuseTexture(std::shared_ptr<RHITexture> tex) { diffuseTex_ = std::move(tex); }
    RHITexture* GetDiffuseTexture() const { return diffuseTex_.get(); }

    void SetNormalTexture(std::shared_ptr<RHITexture> tex) { normalTex_ = std::move(tex); }
    RHITexture* GetNormalTexture() const { return normalTex_.get(); }

    void SetMetallicRoughnessTexture(std::shared_ptr<RHITexture> tex) { metallicRoughnessTex_ = std::move(tex); }
    RHITexture* GetMetallicRoughnessTexture() const { return metallicRoughnessTex_.get(); }

    void SetAOTexture(std::shared_ptr<RHITexture> tex) { aoTex_ = std::move(tex); }
    RHITexture* GetAOTexture() const { return aoTex_.get(); }

    void SetEmissiveTexture(std::shared_ptr<RHITexture> tex) { emissiveTex_ = std::move(tex); }
    RHITexture* GetEmissiveTexture() const { return emissiveTex_.get(); }

    // --- Apply per-material uniforms to shader ---
    void Bind() const;

private:
    std::shared_ptr<RHIShader> shader_;
    std::shared_ptr<RHITexture> diffuseTex_;
    std::shared_ptr<RHITexture> normalTex_;
    std::shared_ptr<RHITexture> metallicRoughnessTex_;
    std::shared_ptr<RHITexture> aoTex_;
    std::shared_ptr<RHITexture> emissiveTex_;

    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specular_{0.5f};
    float shininess_ = 32.0f;

    // PBR
    float metallic_ = 0.0f;
    float roughness_ = 0.5f;
    float ao_ = 1.0f;
    glm::vec3 emissive_{0.0f};
    bool pbrEnabled_ = false;
};

} // namespace ark
