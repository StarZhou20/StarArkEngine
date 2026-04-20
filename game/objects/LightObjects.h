#pragma once

#include "engine/core/AObject.h"
#include "engine/rendering/Light.h"
#include "engine/debug/DebugListenBus.h"
#include "../components/Orbiter.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Directional light
class LightObject : public ark::AObject {
public:
    void Init() override {
        SetName("DirectionalLight");
        auto* light = AddComponent<ark::Light>();
        light->SetType(ark::Light::Type::Directional);
        light->SetColor(glm::vec3(1.0f, 0.95f, 0.9f));
        light->SetIntensity(1.0f);
        light->SetAmbient(glm::vec3(0.15f));

        GetTransform().SetLocalRotation(
            glm::angleAxis(glm::radians(-45.0f), glm::vec3(1, 0, 0)));

        ARK_LOG_INFO("Core", "LightObject initialized");
    }
};

// Orbiting point light
class PointLightObject : public ark::AObject {
public:
    void Init() override {
        SetName("PointLight");
        auto* light = AddComponent<ark::Light>();
        light->SetType(ark::Light::Type::Point);
        light->SetColor(glm::vec3(0.4f, 0.7f, 1.0f));
        light->SetIntensity(1.5f);
        light->SetRange(15.0f);

        auto* orbiter = AddComponent<Orbiter>();
        orbiter->SetOrbit(2.5f, 1.5f, 60.0f);

        ARK_LOG_INFO("Core", "PointLightObject initialized (orbiting)");
    }
};

// Spot light aimed at origin
class SpotLightObject : public ark::AObject {
public:
    void Init() override {
        SetName("SpotLight");
        auto* light = AddComponent<ark::Light>();
        light->SetType(ark::Light::Type::Spot);
        light->SetColor(glm::vec3(0.2f, 1.0f, 0.3f));
        light->SetIntensity(2.0f);
        light->SetRange(20.0f);
        light->SetSpotAngles(15.0f, 25.0f);

        GetTransform().SetLocalPosition(glm::vec3(3.0f, 3.0f, 0.0f));
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::vec3 pos = GetTransform().GetLocalPosition();
        glm::mat4 lookMat = glm::lookAt(pos, target, glm::vec3(0, 1, 0));
        GetTransform().SetLocalRotation(glm::conjugate(glm::quat_cast(lookMat)));

        ARK_LOG_INFO("Core", "SpotLightObject initialized");
    }
};
