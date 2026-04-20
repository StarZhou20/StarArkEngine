#include "Material.h"
#include <glm/gtc/type_ptr.hpp>

namespace ark {

void Material::Bind() const {
    if (!shader_) return;

    shader_->SetUniformVec4("uMaterial.color", glm::value_ptr(color_));
    shader_->SetUniformVec3("uMaterial.specular", glm::value_ptr(specular_));
    shader_->SetUniformFloat("uMaterial.shininess", shininess_);
    shader_->SetUniformInt("uMaterial.hasDiffuseTex", diffuseTex_ ? 1 : 0);
    shader_->SetUniformFloat("uMaterial.metallic", metallic_);
    shader_->SetUniformFloat("uMaterial.roughness", roughness_);
    shader_->SetUniformFloat("uMaterial.ao", ao_);

    // Bind diffuse texture to unit 0
    if (diffuseTex_) {
        diffuseTex_->Bind(0);
        shader_->SetUniformInt("uDiffuseTex", 0);
    }
}

} // namespace ark
