// OrbitCamera.cpp — Orbit camera controller implementation
#include "OrbitCamera.h"
#include "engine/core/AObject.h"
#include "engine/platform/Input.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace ark {

void OrbitCamera::Loop(float /*dt*/) {
    // Right mouse button: rotate
    if (Input::GetMouseButton(GLFW_MOUSE_BUTTON_RIGHT)) {
        float dx = Input::GetMouseDeltaX();
        float dy = Input::GetMouseDeltaY();
        yaw_   -= dx * sensitivity_;
        pitch_ -= dy * sensitivity_;
        pitch_ = std::clamp(pitch_, minPitch_, maxPitch_);
    }

    // Middle mouse button: pan
    if (Input::GetMouseButton(GLFW_MOUSE_BUTTON_MIDDLE)) {
        float dx = Input::GetMouseDeltaX();
        float dy = Input::GetMouseDeltaY();

        // Compute camera right and up vectors from current orientation
        float yawRad = glm::radians(yaw_);
        float pitchRad = glm::radians(pitch_);

        glm::vec3 forward;
        forward.x = std::cos(pitchRad) * std::sin(yawRad);
        forward.y = std::sin(pitchRad);
        forward.z = std::cos(pitchRad) * std::cos(yawRad);

        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        target_ -= right * dx * panSpeed_ * distance_;
        target_ += up * dy * panSpeed_ * distance_;
    }

    // Scroll: zoom
    float scroll = Input::GetScrollDelta();
    if (scroll != 0.0f) {
        distance_ -= scroll * zoomSpeed_;
        distance_ = std::clamp(distance_, minDist_, maxDist_);
    }

    UpdateTransform();
}

void OrbitCamera::UpdateTransform() {
    float yawRad = glm::radians(yaw_);
    float pitchRad = glm::radians(pitch_);

    // Spherical to Cartesian offset from target
    glm::vec3 offset;
    offset.x = distance_ * std::cos(pitchRad) * std::sin(yawRad);
    offset.y = distance_ * std::sin(pitchRad);
    offset.z = distance_ * std::cos(pitchRad) * std::cos(yawRad);

    glm::vec3 eyePos = target_ + offset;

    GetOwner()->GetTransform().SetLocalPosition(eyePos);

    // Look at target
    glm::mat4 lookMat = glm::lookAt(eyePos, target_, glm::vec3(0, 1, 0));
    GetOwner()->GetTransform().SetLocalRotation(glm::conjugate(glm::quat_cast(lookMat)));
}

} // namespace ark
