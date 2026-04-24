#include "DemoScene.h"
#include "../objects/CameraObject.h"
#include "../objects/LightObjects.h"
#include "../objects/GroundObject.h"
#include "../objects/PBRSphere.h"
#include "../objects/ModelObject.h"
#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"
#include "engine/rendering/SceneSerializer.h"
#include <string>

void DemoScene::OnLoad() {
    SetSceneName("DemoScene");
    ARK_LOG_INFO("Core", "DemoScene::OnLoad — PBR Rendering");
    CreateObject<CameraObject>();
    CreateObject<LightObject>();
    CreateObject<PointLightObject>();
    CreateObject<SpotLightObject>();
    CreateObject<GroundObject>();

    // Row of spheres: varying roughness
    // Top row: metallic = 1.0 (gold tint)
    // Bottom row: metallic = 0.0 (dielectric, red)
    const int count = 5;
    const float spacing = 1.2f;
    const float startX = -(count - 1) * spacing * 0.5f;

    for (int i = 0; i < count; ++i) {
        float roughness = static_cast<float>(i) / (count - 1);
        float x = startX + i * spacing;

        // Top row: metallic gold
        {
            PBRSphere::Params p;
            p.position = glm::vec3(x, 0.5f, 0.0f);
            p.albedo = glm::vec4(1.0f, 0.86f, 0.57f, 1.0f);
            p.metallic = 1.0f;
            p.roughness = roughness;
            p.name = "Metal_R" + std::to_string(i);
            auto obj = CreateObject<PBRSphere>();
            obj->Configure(p);
        }

        // Bottom row: dielectric red
        {
            PBRSphere::Params p;
            p.position = glm::vec3(x, -0.5f + 1.2f, -1.5f);
            p.albedo = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
            p.metallic = 0.0f;
            p.roughness = roughness;
            p.name = "Diel_R" + std::to_string(i);
            auto obj = CreateObject<PBRSphere>();
            obj->Configure(p);
        }
    }

    ARK_LOG_INFO("Core", "Created " + std::to_string(count * 2) + " PBR spheres");

    // Load external model via Assimp
    {
        auto obj = CreateObject<ModelObject>();
        obj->Configure(ark::Paths::ResolveContent("models/icosahedron.obj").string(),
                       glm::vec3(0.0f, 1.8f, 0.0f), 0.8f);
    }

    // --- Phase M10 (mini): persist render settings + lights to JSON, watch for edits ---
    // Initial save/load happens on the first SceneSerializer::Tick() after
    // object components have been Init()'d.
    ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));
}

void DemoScene::OnUnload() {
    ARK_LOG_INFO("Core", "DemoScene::OnUnload");
}
