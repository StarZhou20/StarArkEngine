#pragma once

#include "engine/rhi/RHIShader.h"
#include "engine/rhi/RHITexture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace ark {

/// Render pass slots a material can supply distinct shaders for. The
/// renderer queries `GetShaderForPass(pass)` and falls back to `Forward`
/// when the requested slot is empty (e.g. a material without a custom
/// gbuffer shader still draws via deferred by sharing its forward shader,
/// provided the forward shader writes the gbuffer-compatible outputs).
enum class MaterialPass {
    Forward,      // Default lit pass (HDR forward output).
    GBuffer,      // Deferred geometry pass writes to MRT G-buffer.
    Shadow,       // Depth-only pass for shadow maps (no fragment work).
    Transparent,  // Alpha-blended forward pass after deferred resolve.
    Count
};

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
    /// Legacy single-shader setter — fills the `Forward` slot. Existing
    /// callers (cottage / Bistro / hellomod) keep working unchanged.
    void SetShader(std::shared_ptr<RHIShader> shader) {
        shader_ = shader;
        passShaders_[static_cast<int>(MaterialPass::Forward)] = std::move(shader);
    }
    RHIShader* GetShader() const { return shader_.get(); }

    /// Per-pass shader slot. Pass `nullptr` to clear.
    void SetShaderForPass(MaterialPass pass, std::shared_ptr<RHIShader> shader) {
        passShaders_[static_cast<int>(pass)] = std::move(shader);
        if (pass == MaterialPass::Forward) shader_ = passShaders_[0];
    }

    /// Resolve a shader for the requested pass, falling back to Forward when
    /// the slot is empty. Returns nullptr only if Forward is itself empty.
    RHIShader* GetShaderForPass(MaterialPass pass) const {
        const int idx = static_cast<int>(pass);
        if (passShaders_[idx]) return passShaders_[idx].get();
        return passShaders_[static_cast<int>(MaterialPass::Forward)].get();
    }

    /// Whether a non-fallback shader is registered for the requested pass.
    /// Used by the deferred renderer to decide "draw in gbuffer pass" vs
    /// "draw in transparent forward pass".
    bool HasShaderForPass(MaterialPass pass) const {
        return passShaders_[static_cast<int>(pass)] != nullptr;
    }

    // --- Render queue ---
    void SetTransparent(bool t) { transparent_ = t; }
    bool IsTransparent() const { return transparent_; }

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
    /// Bind material params + textures to the material's own (Forward) shader.
    void Bind() const;
    /// Bind material params + textures to an explicitly supplied shader.
    /// Used by DeferredRenderer to feed the same Material into the shared
    /// `gbuffer` shader without overwriting `shader_`. Texture units used
    /// (0..4) match `Bind()` exactly.
    void BindToShader(RHIShader* shader) const;

private:
    std::shared_ptr<RHIShader> shader_;
    std::shared_ptr<RHIShader> passShaders_[static_cast<int>(MaterialPass::Count)] = {};
    bool transparent_ = false;
    std::shared_ptr<RHITexture> diffuseTex_;
    std::shared_ptr<RHITexture> normalTex_;
    std::shared_ptr<RHITexture> metallicRoughnessTex_;
    std::shared_ptr<RHITexture> aoTex_;
    std::shared_ptr<RHITexture> emissiveTex_;

    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specular_{0.5f};
    float shininess_ = 32.0f;

    // PBR
    // Scalars apply only when the corresponding texture is absent. When the
    // texture *is* present the shader uses the texture channel directly.
    float metallic_ = 0.0f;
    float roughness_ = 0.8f;
    float ao_ = 1.0f;
    glm::vec3 emissive_{0.0f};
    bool pbrEnabled_ = false;
};

} // namespace ark
