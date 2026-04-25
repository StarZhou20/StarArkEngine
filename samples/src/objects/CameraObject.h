#pragma once

#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/platform/Input.h"
#include "engine/rendering/Camera.h"
#include "engine/debug/DebugListenBus.h"
#include "../components/FlyCameraController.h"
#include <glm/glm.hpp>

class CameraObject : public ark::AObject {
public:
    // Optional pre-Init overrides. Set before the scene runs Init().
    glm::vec3 initialPosition{120.0f, 40.0f, 120.0f};
    float initialYaw   = -130.0f;
    float initialPitch = -10.0f;
    float moveSpeed    = 30.0f;
    float farPlane     = 2000.0f;

    void Init() override {
        SetName("MainCamera");
        auto* cam = AddComponent<ark::Camera>();
        cam->SetPerspective(60.0f, 0.1f, farPlane);
        cam->SetClearColor(glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));

        // 自由查看器：WASD 平移，Q/E 升降，右键拖拽看向。
        auto* fly = AddComponent<FlyCameraController>();
        fly->SetMoveSpeed(moveSpeed);
        fly->SetSprintMultiplier(6.0f);
        fly->SetLookSensitivity(0.10f);
        fly->SetYawPitch(initialYaw, initialPitch);

        GetTransform().SetLocalPosition(initialPosition);

        ARK_LOG_INFO("Core", "CameraObject initialized with FlyCameraController (WASD+QE)");
    }

    void Loop(float /*dt*/) override {
        if (ark::Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(
                ark::EngineBase::Get().GetWindow()->GetNativeHandle(), GLFW_TRUE);
        }
    }
};
