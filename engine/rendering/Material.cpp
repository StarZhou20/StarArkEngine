#include "Material.h"
#include <glm/gtc/type_ptr.hpp>

namespace ark {

void Material::Bind() const {
    if (!shader_) return;

    shader_->SetUniformVec4("uMaterial.color", glm::value_ptr(color_));
    shader_->SetUniformVec3("uMaterial.specular", glm::value_ptr(specular_));
    shader_->SetUniformFloat("uMaterial.shininess", shininess_);
    shader_->SetUniformInt("uMaterial.hasDiffuseTex", diffuseTex_ ? 1 : 0);
}

} // namespace ark
