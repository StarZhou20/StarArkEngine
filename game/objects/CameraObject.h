#pragma once

#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/platform/Input.h"
#include "engine/rendering/Camera.h"
#include "engine/rendering/OrbitCamera.h"
#include "engine/debug/DebugListenBus.h"
#include <glm/glm.hpp>

class CameraObject : public ark::AObject {
public:
    void Init() override {
        SetName("MainCamera");
        auto* cam = AddComponent<ark::Camera>();
        cam->SetPerspective(60.0f, 0.1f, 100.0f);
        cam->SetClearColor(glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));

        auto* orbit = AddComponent<ark::OrbitCamera>();
        orbit->SetTarget(glm::vec3(0.0f, 0.3f, 0.0f));
        orbit->SetDistance(5.0f);
        orbit->SetYaw(0.0f);
        orbit->SetPitch(20.0f);

        ARK_LOG_INFO("Core", "CameraObject initialized with OrbitCamera");
    }

    void Loop(float /*dt*/) override {
        if (ark::Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(
                ark::EngineBase::Get().GetWindow()->GetNativeHandle(), GLFW_TRUE);
        }
    }
};
