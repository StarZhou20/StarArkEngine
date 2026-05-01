#include "Material.h"
#include <glm/gtc/type_ptr.hpp>

namespace ark {

void Material::Bind() const {
    BindToShader(shader_.get());
}

void Material::BindToShader(RHIShader* shader) const {
    if (!shader) return;

    shader->SetUniformVec4("uMaterial.color", glm::value_ptr(color_));
    shader->SetUniformVec3("uMaterial.specular", glm::value_ptr(specular_));
    shader->SetUniformFloat("uMaterial.shininess", shininess_);
    shader->SetUniformFloat("uMaterial.metallic", metallic_);
    shader->SetUniformFloat("uMaterial.roughness", roughness_);
    shader->SetUniformFloat("uMaterial.ao", ao_);
    shader->SetUniformVec3("uMaterial.emissive", glm::value_ptr(emissive_));

    // Texture presence flags (shader branches on these).
    shader->SetUniformInt("uMaterial.hasDiffuseTex",    diffuseTex_  ? 1 : 0);
    shader->SetUniformInt("uMaterial.hasNormalTex",     normalTex_   ? 1 : 0);
    shader->SetUniformInt("uMaterial.hasMetalRoughTex", metallicRoughnessTex_ ? 1 : 0);
    shader->SetUniformInt("uMaterial.hasAOTex",         aoTex_       ? 1 : 0);
    shader->SetUniformInt("uMaterial.hasEmissiveTex",   emissiveTex_ ? 1 : 0);

    // Bind texture slots. Always set sampler uniforms so unused samplers
    // reference a safe unit rather than undefined state.
    if (diffuseTex_) diffuseTex_->Bind(0);
    shader->SetUniformInt("uDiffuseTex", 0);

    if (normalTex_) normalTex_->Bind(1);
    shader->SetUniformInt("uNormalTex", 1);

    if (metallicRoughnessTex_) metallicRoughnessTex_->Bind(2);
    shader->SetUniformInt("uMetalRoughTex", 2);

    if (aoTex_) aoTex_->Bind(3);
    shader->SetUniformInt("uAOTex", 3);

    if (emissiveTex_) emissiveTex_->Bind(4);
    shader->SetUniformInt("uEmissiveTex", 4);
}

} // namespace ark
