#pragma once

#include "engine/core/AComponent.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include "engine/platform/Input.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

class FlyCameraController : public ark::AComponent {
public:
    void Init() override {
        // Yaw=-90, Pitch=0 对应默认朝向 -Z。
        ApplyRotation();
    }

    void Loop(float dt) override {
        auto* owner = GetOwner();
        if (!owner) return;

        // 右键按住时用鼠标改变朝向。
        if (ark::Input::GetMouseButton(GLFW_MOUSE_BUTTON_RIGHT)) {
            yawDeg_ += ark::Input::GetMouseDeltaX() * lookSensitivity_;
            pitchDeg_ -= ark::Input::GetMouseDeltaY() * lookSensitivity_;
            pitchDeg_ = std::clamp(pitchDeg_, -89.0f, 89.0f);
            ApplyRotation();
        }

        glm::vec3 forward = GetForward();
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 move(0.0f);

        if (ark::Input::GetKey(GLFW_KEY_W)) move += forward;
        if (ark::Input::GetKey(GLFW_KEY_S)) move -= forward;
        if (ark::Input::GetKey(GLFW_KEY_D)) move += right;
        if (ark::Input::GetKey(GLFW_KEY_A)) move -= right;
        if (ark::Input::GetKey(GLFW_KEY_Q)) move += glm::vec3(0.0f, 1.0f, 0.0f);
        if (ark::Input::GetKey(GLFW_KEY_E)) move -= glm::vec3(0.0f, 1.0f, 0.0f);

        if (glm::length(move) > 0.0f) {
            move = glm::normalize(move);
            float speed = moveSpeed_;
            if (ark::Input::GetKey(GLFW_KEY_LEFT_SHIFT)) speed *= sprintMultiplier_;

            auto& tr = owner->GetTransform();
            tr.SetLocalPosition(tr.GetLocalPosition() + move * speed * dt);
        }
    }

    void SetMoveSpeed(float v) { moveSpeed_ = v; }
    void SetLookSensitivity(float v) { lookSensitivity_ = v; }
    void SetSprintMultiplier(float v) { sprintMultiplier_ = v; }
    void SetYawPitch(float yawDeg, float pitchDeg) {
        yawDeg_ = yawDeg;
        pitchDeg_ = std::clamp(pitchDeg, -89.0f, 89.0f);
        ApplyRotation();
    }

private:
    glm::vec3 GetForward() const {
        const float yaw = glm::radians(yawDeg_);
        const float pitch = glm::radians(pitchDeg_);
        glm::vec3 f;
        f.x = std::cos(pitch) * std::cos(yaw);
        f.y = std::sin(pitch);
        f.z = std::cos(pitch) * std::sin(yaw);
        return glm::normalize(f);
    }

    void ApplyRotation() {
        auto* owner = GetOwner();
        if (!owner) return;

        glm::vec3 forward = GetForward();
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f), forward, worldUp);
        glm::mat4 invView = glm::inverse(view);
        glm::quat rot = glm::quat_cast(invView);
        owner->GetTransform().SetLocalRotation(rot);
    }

private:
    float moveSpeed_ = 10.0f;
    float sprintMultiplier_ = 3.0f;
    float lookSensitivity_ = 0.12f;
    float yawDeg_ = -90.0f;
    float pitchDeg_ = 0.0f;
};
