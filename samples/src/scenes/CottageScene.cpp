// CottageScene.cpp — 参见 CottageScene.h 的头部注释。
#include "CottageScene.h"

#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Input.h"
#include "engine/platform/Paths.h"
#include "engine/platform/Window.h"
#include "engine/rendering/Camera.h"
#include "engine/rendering/ForwardRenderer.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/MeshRenderer.h"
#include "engine/rendering/OrbitCamera.h"
#include "engine/rendering/SceneSerializer.h"
#include "engine/rendering/ShaderManager.h"
#include "engine/serialization/SceneDoc.h"

#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace {

// 小型"退出"组件：ESC 关窗。只为保持场景自洽（公共 Input API 足够）。
class EscapeToQuit : public ark::AComponent {
public:
    void Loop(float /*dt*/) override {
        if (ark::Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            auto* w = ark::EngineBase::Get().GetWindow();
            if (w) glfwSetWindowShouldClose(w->GetNativeHandle(), GLFW_TRUE);
        }
    }
};

// v0.3 — game mod id selected from CLI (--game=<id>). Defaults to "vanilla"
// so launching by alias `StarArkSamples.exe cottage` keeps working.
std::string& ActiveGameModRef() {
    static std::string id = "vanilla";
    return id;
}

} // namespace

void CottageScene::SetActiveGameMod(std::string id) {
    ActiveGameModRef() = std::move(id);
}
const std::string& CottageScene::GetActiveGameMod() {
    return ActiveGameModRef();
}

void CottageScene::OnLoad() {
    const std::string& modId = ActiveGameModRef();

    SetSceneName(std::string("GameMod:") + modId);
    ARK_LOG_INFO("Core",
        std::string("CottageScene::OnLoad — loading game mod '") + modId + "'");

    auto& engine   = ark::EngineBase::Get();
    auto* device   = engine.GetRHIDevice();
    auto* renderer = engine.GetRenderer();

    // ---- v0.3 — load scene from mods/<id>/scenes/main.toml under mod context.
    // Falls back to the legacy content/scenes/cottage.toml only when the
    // mod-side file is missing (eases mid-migration debugging).
    const auto modScenePath = ark::Paths::Mods() / modId / "scenes" / "main.toml";
    std::filesystem::path tomlPath = modScenePath;
    bool useMod = std::filesystem::exists(modScenePath);
    if (!useMod) {
        tomlPath = ark::Paths::ResolveContent("scenes/cottage.toml");
        ARK_LOG_WARN("Core",
            std::string("game mod '") + modId
            + "' has no scenes/main.toml — falling back to content/scenes/cottage.toml");
    }

    if (useMod) {
        ark::Paths::ModContextScope scope(modId);
        if (!ark::SceneDoc::Load(tomlPath, this)) {
            ARK_LOG_FATAL("Core", std::string("failed to load scene TOML: ") + tomlPath.string());
        }
    } else {
        if (!ark::SceneDoc::Load(tomlPath, this)) {
            ARK_LOG_FATAL("Core", std::string("failed to load scene TOML: ") + tomlPath.string());
        }
    }

    // v0.3 — MeshRenderer::OnReflectionLoaded() now wires up Mesh+Material
    // automatically as part of SceneDoc::Load, so the manual `resolveAll`
    // walk is gone. Resources are realized before AddComponentRaw, before
    // OnAttach, so any code reading GetMesh()/GetMaterial() in OnAttach
    // sees a fully-built object.
    (void)engine; (void)device; (void)renderer;

    // ---- 给 MainCamera 补上 OrbitCamera + EscapeToQuit（尚未反射）----
    auto findByName = [&](const std::string& name) -> ark::AObject* {
        auto search = [&](std::vector<std::unique_ptr<ark::AObject>>& list) -> ark::AObject* {
            for (auto& up : list) {
                if (up && up->GetName() == name) return up.get();
            }
            return nullptr;
        };
        if (auto* o = search(GetObjectList())) return o;
        return search(GetPendingList());
    };

    if (auto* camObj = findByName("MainCamera")) {
        auto* orbit = camObj->AddComponent<ark::OrbitCamera>();
        orbit->SetTarget(glm::vec3(0.0f, 0.8f, 0.0f));
        orbit->SetDistance(6.0f);
        orbit->SetYaw(30.0f);
        orbit->SetPitch(18.0f);
        camObj->AddComponent<EscapeToQuit>();
    } else {
        ARK_LOG_WARN("Core", "MainCamera not found in scene TOML");
    }

    // ---- 开启 lighting.json 外部热重载（首帧自动生成）----
    // SceneSerializer 按 AObject 名字匹配 Light（Sun/Torch_L/Torch_R）。
    ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));

    ARK_LOG_INFO("Core",
        "CottageScene ready — drag right mouse to orbit, scroll to zoom, ESC to quit");
}
