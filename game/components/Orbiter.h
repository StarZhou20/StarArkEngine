#pragma once

#include "engine/core/AComponent.h"
#include "engine/core/AObject.h"
#include <glm/glm.hpp>
#include <cmath>

class Orbiter : public ark::AComponent {
public:
    void Loop(float dt) override {
        angle_ += speed_ * dt;
        float x = radius_ * std::cos(angle_);
        float z = radius_ * std::sin(angle_);
        GetOwner()->GetTransform().SetLocalPosition(glm::vec3(x, height_, z));
    }

    void SetOrbit(float radius, float height, float degreesPerSec) {
        radius_ = radius;
        height_ = height;
        speed_ = glm::radians(degreesPerSec);
    }

private:
    float radius_ = 2.0f;
    float height_ = 1.5f;
    float speed_ = glm::radians(60.0f);
    float angle_ = 0.0f;
};
