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

} // namespace

void CottageScene::OnLoad() {
    // SceneDoc 会改写 scene name，但先给个 fallback。
    SetSceneName("CottageScene");
    ARK_LOG_INFO("Core", "CottageScene::OnLoad — v0.2 TOML-driven");

    auto& engine   = ark::EngineBase::Get();
    auto* device   = engine.GetRHIDevice();
    auto* renderer = engine.GetRenderer();

    // ---- 从 TOML 加载对象 / 组件 ----
    auto tomlPath = ark::Paths::ResolveContent("scenes/cottage.toml");
    if (!ark::SceneDoc::Load(tomlPath, this)) {
        ARK_LOG_FATAL("Core", std::string("failed to load scene TOML: ") + tomlPath.string());
    }

    // ---- 为 MeshRenderer 构造 runtime 资源（mesh_ + material_）----
    // SceneDoc::Load 只填 spec；primitive/模型的实际构造在这里做。
    // 注意：CreateObject 放进 pendingList_，这一帧末才会进入 objectList_。
    auto resolveAll = [&](std::vector<std::unique_ptr<ark::AObject>>& list) {
        for (auto& up : list) {
            if (!up) continue;
            for (auto& compUp : up->GetComponents()) {
                if (auto* mr = dynamic_cast<ark::MeshRenderer*>(compUp.get())) {
                    mr->ResolveResources(device, renderer->GetShaderManager());
                }
            }
        }
    };
    resolveAll(GetObjectList());
    resolveAll(GetPendingList());

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
