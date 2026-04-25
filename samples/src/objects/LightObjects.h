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
        // Warm sunlight tint (~5200K). Lower green/blue pulls toward amber.
        light->SetColor(glm::vec3(1.0f, 0.88f, 0.72f));
        // Real sunlight at ground is several stops above IBL ambient. With
        // IBL.diffuseIntensity=0.35 this ratio gives proper "sun vs shadow".
        light->SetIntensity(7.0f);
        // IBL already provides ambient from the skybox; adding a constant
        // ambient term on top flattens the contrast (the "overcast look").
        light->SetAmbient(glm::vec3(0.0f));

        // Sun angle: tilt down AND rotate around Y so shadows fall
        // diagonally across the street for visible directional cues.
        glm::quat pitch = glm::angleAxis(glm::radians(-55.0f), glm::vec3(1, 0, 0));
        glm::quat yaw   = glm::angleAxis(glm::radians( 35.0f), glm::vec3(0, 1, 0));
        GetTransform().SetLocalRotation(yaw * pitch);

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
        // With physical 1/r^2 attenuation, point lights need higher intensity
        // to match perceived brightness. Think of this as "light unit power".
        light->SetIntensity(10.0f);
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
        light->SetIntensity(30.0f);
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
