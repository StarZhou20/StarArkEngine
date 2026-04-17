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

} // namespace ark
