// OrbitCamera.h — Orbit camera controller component
// Right-drag to rotate, scroll to zoom, middle-drag to pan
#pragma once

#include "engine/core/AComponent.h"
#include <glm/glm.hpp>

namespace ark {

class OrbitCamera : public AComponent {
public:
    void OnAttach() override {}
    void OnDetach() override {}
    void Loop(float dt) override;

    // --- Configuration ---
    void SetTarget(const glm::vec3& target) { target_ = target; }
    const glm::vec3& GetTarget() const { return target_; }

    void SetDistance(float dist) { distance_ = dist; }
    float GetDistance() const { return distance_; }

    void SetYaw(float deg) { yaw_ = deg; }
    void SetPitch(float deg) { pitch_ = deg; }

    void SetSensitivity(float s) { sensitivity_ = s; }
    void SetZoomSpeed(float s) { zoomSpeed_ = s; }
    void SetPanSpeed(float s) { panSpeed_ = s; }
    void SetMinDistance(float d) { minDist_ = d; }
    void SetMaxDistance(float d) { maxDist_ = d; }
    void SetMinPitch(float deg) { minPitch_ = deg; }
    void SetMaxPitch(float deg) { maxPitch_ = deg; }

private:
    void UpdateTransform();

    glm::vec3 target_{0.0f, 0.0f, 0.0f};
    float distance_ = 5.0f;
    float yaw_ = 0.0f;      // degrees, around Y axis
    float pitch_ = 20.0f;   // degrees, above horizon

    float sensitivity_ = 0.3f;
    float zoomSpeed_ = 1.0f;
    float panSpeed_ = 0.005f;
    float minDist_ = 0.5f;
    float maxDist_ = 50.0f;
    float minPitch_ = -89.0f;
    float maxPitch_ = 89.0f;
};

} // namespace ark
