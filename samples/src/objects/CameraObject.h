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
    void Init() override {
        SetName("MainCamera");
        auto* cam = AddComponent<ark::Camera>();
        cam->SetPerspective(60.0f, 0.1f, 5000.0f);
        cam->SetClearColor(glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));

        // 自由查看器：WASD 平移，Q/E 升降，右键拖拽看向。
        auto* fly = AddComponent<FlyCameraController>();
        fly->SetMoveSpeed(20.0f);
        fly->SetSprintMultiplier(6.0f);
        fly->SetLookSensitivity(0.10f);
        fly->SetYawPitch(-90.0f, -5.0f);

        GetTransform().SetLocalPosition(glm::vec3(0.0f, 5.0f, 30.0f));

        ARK_LOG_INFO("Core", "CameraObject initialized with FlyCameraController (WASD+QE)");
    }

    void Loop(float /*dt*/) override {
        if (ark::Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(
                ark::EngineBase::Get().GetWindow()->GetNativeHandle(), GLFW_TRUE);
        }
    }
};
