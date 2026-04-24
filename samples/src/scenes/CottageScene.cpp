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

// 为每个 light.json 管理的光源准备一个命名对象。
ark::AObject* SpawnNamedLight(ark::AScene* scene, const std::string& name,
                              ark::Light::Type type, const glm::vec3& color,
                              float intensity) {
    auto* obj = scene->CreateObject<ark::AObject>();
    obj->SetName(name);
    auto* light = obj->AddComponent<ark::Light>();
    light->SetType(type);
    light->SetColor(color);
    light->SetIntensity(intensity);
    return obj;
}

std::shared_ptr<ark::Material> MakePbrMaterial(
    const std::shared_ptr<ark::RHIShader>& shader,
    const glm::vec4& albedo, float metallic, float roughness) {
    auto mat = std::make_shared<ark::Material>();
    mat->SetShader(shader);
    mat->SetColor(albedo);
    mat->SetMetallic(metallic);
    mat->SetRoughness(roughness);
    mat->SetAO(1.0f);
    return mat;
}

} // namespace

void CottageScene::OnLoad() {
    SetSceneName("CottageScene");
    ARK_LOG_INFO("Core", "CottageScene::OnLoad — v0.1 minimal demo");

    auto& engine   = ark::EngineBase::Get();
    auto* device   = engine.GetRHIDevice();
    auto* renderer = engine.GetRenderer();
    auto  pbr      = renderer->GetShaderManager()->Get("pbr");
    if (!pbr) { ARK_LOG_FATAL("RHI", "Failed to load PBR shader"); }

    // ---- 相机 (OrbitCamera) ----
    {
        auto* camObj = CreateObject<ark::AObject>();
        camObj->SetName("MainCamera");
        auto* cam = camObj->AddComponent<ark::Camera>();
        cam->SetPerspective(60.0f, 0.1f, 100.0f);
        cam->SetClearColor(glm::vec4(0.02f, 0.02f, 0.03f, 1.0f));

        auto* orbit = camObj->AddComponent<ark::OrbitCamera>();
        orbit->SetTarget(glm::vec3(0.0f, 0.8f, 0.0f));
        orbit->SetDistance(6.0f);
        orbit->SetYaw(30.0f);
        orbit->SetPitch(18.0f);

        camObj->AddComponent<EscapeToQuit>();
    }

    // ---- 光照：1 方向光（Sun） + 2 点光（Torch）----
    // 名称匹配 lighting.json，外部工具可实时调参。
    {
        auto* sun = SpawnNamedLight(this, "Sun", ark::Light::Type::Directional,
                                    glm::vec3(1.0f, 0.96f, 0.88f), 3.0f);
        // 倾斜方向：太阳高度 ~45° 偏东。
        glm::quat rot = glm::angleAxis(glm::radians(-50.0f), glm::vec3(1, 0, 0))
                      * glm::angleAxis(glm::radians( 30.0f), glm::vec3(0, 1, 0));
        sun->GetTransform().SetLocalRotation(rot);
        auto* sunLight = sun->GetComponent<ark::Light>();
        sunLight->SetAmbient(glm::vec3(0.12f));
    }
    {
        auto* torch = SpawnNamedLight(this, "Torch_L", ark::Light::Type::Point,
                                      glm::vec3(1.0f, 0.55f, 0.25f), 12.0f);
        torch->GetComponent<ark::Light>()->SetRange(8.0f);
        torch->GetTransform().SetLocalPosition(glm::vec3(-1.4f, 1.2f, 1.6f));
    }
    {
        auto* torch = SpawnNamedLight(this, "Torch_R", ark::Light::Type::Point,
                                      glm::vec3(1.0f, 0.55f, 0.25f), 12.0f);
        torch->GetComponent<ark::Light>()->SetRange(8.0f);
        torch->GetTransform().SetLocalPosition(glm::vec3(1.4f, 1.2f, 1.6f));
    }

    // ---- 地面（10×10 Plane）----
    {
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreatePlane(10.0f).release());
        mesh->Upload(device);
        auto mat = MakePbrMaterial(pbr, glm::vec4(0.55f, 0.50f, 0.45f, 1.0f),
                                   /*metal*/ 0.0f, /*rough*/ 0.85f);

        auto* obj = CreateObject<ark::AObject>();
        obj->SetName("Ground");
        auto* mr = obj->AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(mat);
        obj->GetTransform().SetLocalPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    }

    // ---- "小屋"主体（大立方体）----
    {
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateCube().release());
        mesh->Upload(device);
        auto mat = MakePbrMaterial(pbr, glm::vec4(0.78f, 0.72f, 0.62f, 1.0f),
                                   /*metal*/ 0.0f, /*rough*/ 0.7f);

        auto* obj = CreateObject<ark::AObject>();
        obj->SetName("CottageBody");
        auto* mr = obj->AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(mat);
        obj->GetTransform().SetLocalPosition(glm::vec3(0.0f, 0.75f, 0.0f));
        obj->GetTransform().SetLocalScale(glm::vec3(2.2f, 1.5f, 1.8f));
    }

    // ---- "屋顶"（扁立方体 + 暗红 PBR，略高）----
    {
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateCube().release());
        mesh->Upload(device);
        auto mat = MakePbrMaterial(pbr, glm::vec4(0.42f, 0.12f, 0.08f, 1.0f),
                                   /*metal*/ 0.0f, /*rough*/ 0.55f);

        auto* obj = CreateObject<ark::AObject>();
        obj->SetName("CottageRoof");
        auto* mr = obj->AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(mat);
        obj->GetTransform().SetLocalPosition(glm::vec3(0.0f, 1.65f, 0.0f));
        obj->GetTransform().SetLocalScale(glm::vec3(2.4f, 0.25f, 2.0f));
    }

    // ---- 右前方一颗金属球（展示 IBL + 粗糙度）----
    {
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateSphere(48, 24).release());
        mesh->Upload(device);
        auto mat = MakePbrMaterial(pbr, glm::vec4(0.95f, 0.85f, 0.55f, 1.0f),
                                   /*metal*/ 1.0f, /*rough*/ 0.25f);

        auto* obj = CreateObject<ark::AObject>();
        obj->SetName("MetalOrb");
        auto* mr = obj->AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(mat);
        obj->GetTransform().SetLocalPosition(glm::vec3(2.2f, 0.4f, 1.8f));
        obj->GetTransform().SetLocalScale(glm::vec3(0.4f));
    }

    // ---- 开启 lighting.json 外部热重载（首帧自动生成）----
    ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));

    ARK_LOG_INFO("Core",
        "CottageScene ready — drag right mouse to orbit, scroll to zoom, ESC to quit");
}
