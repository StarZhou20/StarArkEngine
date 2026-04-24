#include "FBXDemoScene.h"
#include "../objects/CameraObject.h"
#include "../objects/LightObjects.h"
#include "../objects/GroundObject.h"
#include "../objects/ModelObject.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"
#include <filesystem>

void FBXDemoScene::OnLoad() {
    SetSceneName("FBXDemoScene");
    ARK_LOG_INFO("Core", "FBXDemoScene::OnLoad — FBX Model Demo");

    // Camera
    auto* cam = CreateObject<CameraObject>();
    (void)cam;

    // Directional sun light only — Bistro has its own ground & geometry.
    CreateObject<LightObject>();

    // Bistro (FBX) — 只加载 Bistro，不再回退 monkey。
    {
        std::filesystem::path repoRoot = ark::Paths::GameRoot().parent_path().parent_path();
        std::filesystem::path bistroFbx = repoRoot / "tmp" / "Bistro_v5_2" / "BistroExterior.fbx";

        if (!std::filesystem::exists(bistroFbx)) {
            ARK_LOG_FATAL("Core", std::string("BistroExterior.fbx not found: ") + bistroFbx.string());
        }
        std::string modelPath = bistroFbx.string();
        ARK_LOG_INFO("Core", std::string("FBXDemoScene using Bistro asset: ") + modelPath);

        auto* modelObj = CreateObject<ModelObject>();
        modelObj->Configure(modelPath, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }

    ARK_LOG_INFO("Core", "FBXDemoScene loaded");
}

void FBXDemoScene::OnUnload() {
    ARK_LOG_INFO("Core", "FBXDemoScene::OnUnload");
}
