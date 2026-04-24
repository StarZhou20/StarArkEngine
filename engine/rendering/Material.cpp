#include "Material.h"
#include <glm/gtc/type_ptr.hpp>

namespace ark {

void Material::Bind() const {
    if (!shader_) return;

    shader_->SetUniformVec4("uMaterial.color", glm::value_ptr(color_));
    shader_->SetUniformVec3("uMaterial.specular", glm::value_ptr(specular_));
    shader_->SetUniformFloat("uMaterial.shininess", shininess_);
    shader_->SetUniformFloat("uMaterial.metallic", metallic_);
    shader_->SetUniformFloat("uMaterial.roughness", roughness_);
    shader_->SetUniformFloat("uMaterial.ao", ao_);
    shader_->SetUniformVec3("uMaterial.emissive", glm::value_ptr(emissive_));

    // Texture presence flags (shader branches on these).
    shader_->SetUniformInt("uMaterial.hasDiffuseTex",    diffuseTex_  ? 1 : 0);
    shader_->SetUniformInt("uMaterial.hasNormalTex",     normalTex_   ? 1 : 0);
    shader_->SetUniformInt("uMaterial.hasMetalRoughTex", metallicRoughnessTex_ ? 1 : 0);
    shader_->SetUniformInt("uMaterial.hasAOTex",         aoTex_       ? 1 : 0);
    shader_->SetUniformInt("uMaterial.hasEmissiveTex",   emissiveTex_ ? 1 : 0);

    // Bind texture slots. Always set sampler uniforms so unused samplers
    // reference a safe unit rather than undefined state.
    if (diffuseTex_) diffuseTex_->Bind(0);
    shader_->SetUniformInt("uDiffuseTex", 0);

    if (normalTex_) normalTex_->Bind(1);
    shader_->SetUniformInt("uNormalTex", 1);

    if (metallicRoughnessTex_) metallicRoughnessTex_->Bind(2);
    shader_->SetUniformInt("uMetalRoughTex", 2);

    if (aoTex_) aoTex_->Bind(3);
    shader_->SetUniformInt("uAOTex", 3);

    if (emissiveTex_) emissiveTex_->Bind(4);
    shader_->SetUniformInt("uEmissiveTex", 4);
}

} // namespace ark
