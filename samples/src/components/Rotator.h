#pragma once

#include "engine/core/AComponent.h"
#include "engine/core/AObject.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Rotator : public ark::AComponent {
public:
    void Loop(float dt) override {
        auto& transform = GetOwner()->GetTransform();
        angle_ += rotSpeed_ * dt;
        glm::quat rot = glm::angleAxis(angle_, glm::vec3(0, 1, 0))
                       * glm::angleAxis(0.3f, glm::vec3(1, 0, 0));
        transform.SetLocalRotation(rot);
    }

    void SetSpeed(float degreesPerSec) { rotSpeed_ = glm::radians(degreesPerSec); }

private:
    float rotSpeed_ = glm::radians(45.0f);
    float angle_ = 0.0f;
};
