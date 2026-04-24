#include "Light.h"
#include <algorithm>

namespace ark {

std::vector<Light*> Light::allLights_;

void Light::OnAttach() {
    allLights_.push_back(this);
}

void Light::OnDetach() {
    allLights_.erase(
        std::remove(allLights_.begin(), allLights_.end(), this),
        allLights_.end());
}

const std::vector<Light*>& Light::GetAllLights() {
    return allLights_;
}

// -----------------------------------------------------------------------------
// 反射注册 (v0.2 15.A)
// 字段顺序按"渲染语义分组"排列，方便 Inspector 自动出的 UI 读起来顺眼。
// -----------------------------------------------------------------------------
ARK_REFLECT_COMPONENT(Light)
    ARK_FIELD(type_,            "light_type",        EnumInt)
    ARK_FIELD(color_,           "color",             Color3)
    ARK_FIELD(intensity_,       "intensity",         Float)
    ARK_FIELD(range_,           "range",             Float)
    ARK_FIELD(constant_,        "attenuation_const", Float)
    ARK_FIELD(linear_,          "attenuation_lin",   Float)
    ARK_FIELD(quadratic_,       "attenuation_quad",  Float)
    ARK_FIELD(spotInnerAngle_,  "spot_inner_deg",    Float)
    ARK_FIELD(spotOuterAngle_,  "spot_outer_deg",    Float)
    ARK_FIELD(ambient_,         "ambient",           Color3)
ARK_END_REFLECT(Light)

} // namespace ark
