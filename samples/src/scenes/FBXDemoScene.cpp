#include "FBXDemoScene.h"
#include "../objects/CameraObject.h"
#include "../objects/LightObjects.h"
#include "../objects/GroundObject.h"
#include "../objects/ModelObject.h"
#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"
#include "engine/platform/Window.h"
#include "engine/rendering/LoadingScreen.h"
#include "engine/rendering/ModelLoader.h"
#include "engine/rendering/SceneSerializer.h"
#include "engine/rendering/ShaderManager.h"
#include <chrono>
#include <filesystem>
#include <future>

void FBXDemoScene::OnLoad() {
    SetSceneName("FBXDemoScene");
    ARK_LOG_INFO("Core", "FBXDemoScene::OnLoad — FBX Model Demo");

    // --- Resolve Bistro path ---
    std::filesystem::path repoRoot = ark::Paths::GameRoot().parent_path().parent_path();
    std::filesystem::path bistroFbx = repoRoot / "tmp" / "Bistro_v5_2" / "BistroExterior.fbx";
    if (!std::filesystem::exists(bistroFbx)) {
        ARK_LOG_FATAL("Core", std::string("BistroExterior.fbx not found: ") + bistroFbx.string());
    }
    std::string modelPath = bistroFbx.string();
    ARK_LOG_INFO("Core", std::string("FBXDemoScene using Bistro asset: ") + modelPath);

    auto& engine = ark::EngineBase::Get();
    auto* device = engine.GetRHIDevice();
    auto* window = engine.GetWindow();
    auto  shader = engine.GetRenderer()->GetShaderManager()->Get("pbr");
    if (!shader) {
        ARK_LOG_FATAL("RHI", "Failed to load PBR shader");
    }

    ark::LoadingScreen loadingScreen(window);

    // Render the overlay *immediately* so the user sees the progress bar
    // before any heavy work begins. Pump events twice so the window becomes
    // mapped/focused on Windows (first swap after glfwCreateWindow can be
    // hidden until the OS finishes showing the frame).
    loadingScreen.Render(-1.0f, "INITIALIZING");
    loadingScreen.Render(-1.0f, "INITIALIZING");

    // -------------------------------------------------------------------------
    // Phase 1: Parse on worker thread. Main thread animates indeterminate bar.
    // -------------------------------------------------------------------------
    auto parseFuture = ark::ModelLoader::ParseAsync(modelPath);

    using namespace std::chrono;
    while (parseFuture.wait_for(16ms) == std::future_status::timeout) {
        if (window->ShouldClose()) return;
        loadingScreen.Render(-1.0f, "PARSING FBX - " +
                             std::filesystem::path(modelPath).filename().string());
    }
    auto parsed = parseFuture.get();
    if (!parsed || !ark::ModelLoader::IsParsedValid(*parsed)) {
        ARK_LOG_FATAL("Core", "Bistro parse failed");
    }

    // -------------------------------------------------------------------------
    // Phase 2: GL upload on main thread. Throttle loading screen redraw to 30Hz
    // so texture uploads aren't drowned by the overlay.
    // -------------------------------------------------------------------------
    auto lastDraw = steady_clock::now();
    loadingScreen.Render(0.0f, "UPLOADING MESHES");

    auto nodes = ark::ModelLoader::Upload(device, shader, *parsed,
        [&](float progress, const std::string& label) {
            auto now = steady_clock::now();
            auto ms = duration_cast<milliseconds>(now - lastDraw).count();
            if (ms >= 33 || progress >= 1.0f) {
                loadingScreen.Render(progress, label);
                lastDraw = now;
            }
        });

    if (nodes.empty()) {
        ARK_LOG_FATAL("Core", "Bistro upload produced zero meshes");
    }

    loadingScreen.Render(1.0f, "FINALIZING");

    // -------------------------------------------------------------------------
    // Compute AABB from the parsed scene (native units, no scale) and derive a
    // camera position that frames the whole asset. This avoids guessing units —
    // works whether the FBX is in cm, m, or inches.
    // -------------------------------------------------------------------------
    glm::vec3 bmin(0.0f), bmax(0.0f);
    ark::ModelLoader::GetParsedBounds(*parsed, bmin, bmax);
    glm::vec3 center = (bmin + bmax) * 0.5f;
    glm::vec3 extents = (bmax - bmin) * 0.5f;
    float radius = glm::length(extents);
    ARK_LOG_INFO("Core", "Bistro AABB center=(" +
        std::to_string(center.x) + "," + std::to_string(center.y) + "," + std::to_string(center.z) +
        ") radius=" + std::to_string(radius));

    // -------------------------------------------------------------------------
    // Build the actual scene objects. Use model units 1:1 (no rescale) so the
    // world matches whatever the DCC tool exported.
    // -------------------------------------------------------------------------
    auto* cam = CreateObject<CameraObject>();
    // Sit the camera roughly 1.2× bounding-radius from the center, 30° above
    // horizon, looking back toward the centre.
    {
        glm::vec3 offset = glm::normalize(glm::vec3(1.0f, 0.5f, 1.0f)) * (radius * 1.2f);
        cam->initialPosition = center + offset;

        // Yaw toward center (XZ plane); pitch to look slightly down.
        glm::vec3 toCenter = center - cam->initialPosition;
        cam->initialYaw   = glm::degrees(atan2f(toCenter.z, toCenter.x));
        cam->initialPitch = glm::degrees(atan2f(toCenter.y,
                              sqrtf(toCenter.x * toCenter.x + toCenter.z * toCenter.z)));

        // Far plane must comfortably contain the scene.
        cam->farPlane  = std::max(500.0f, radius * 4.0f);
        // Move-speed scales with the scene so WASD feels natural regardless of units.
        cam->moveSpeed = std::max(5.0f, radius * 0.15f);
    }

    // Directional sun light only — Bistro has its own ground & geometry.
    CreateObject<LightObject>();

    // Model uses native units (1:1). Place at origin.
    auto* modelObj = CreateObject<ModelObject>();
    modelObj->ConfigureNodes(std::move(nodes), glm::vec3(0.0f), 1.0f);

    // Enable lighting.json hot-reload (LightingTuner edits this file).
    // First Tick auto-saves current defaults; subsequent external writes are
    // hot-reloaded into the live RenderSettings.
    ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));

    ARK_LOG_INFO("Core", "FBXDemoScene loaded");
}

void FBXDemoScene::OnUnload() {
    ARK_LOG_INFO("Core", "FBXDemoScene::OnUnload");
}
