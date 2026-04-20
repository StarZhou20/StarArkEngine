#include "FBXDemoScene.h"
#include "../objects/CameraObject.h"
#include "../objects/LightObjects.h"
#include "../objects/GroundObject.h"
#include "../objects/ModelObject.h"
#include "engine/debug/DebugListenBus.h"

void FBXDemoScene::OnLoad() {
    SetSceneName("FBXDemoScene");
    ARK_LOG_INFO("Core", "FBXDemoScene::OnLoad — FBX Model Demo");

    // Camera
    auto* cam = CreateObject<CameraObject>();
    (void)cam;

    // Lights
    CreateObject<LightObject>();
    CreateObject<PointLightObject>();
    CreateObject<SpotLightObject>();

    // Ground plane
    CreateObject<GroundObject>();

    // Monkey (FBX)
    {
        auto* monkey = CreateObject<ModelObject>();
        monkey->Configure("fbx/monkey.fbx", glm::vec3(0.0f, 0.5f, 0.0f), 0.01f);
    }

    ARK_LOG_INFO("Core", "FBXDemoScene loaded");
}

void FBXDemoScene::OnUnload() {
    ARK_LOG_INFO("Core", "FBXDemoScene::OnUnload");
}
