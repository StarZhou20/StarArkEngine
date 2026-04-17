// main.cpp — Phase 4 verification: 3D Rendering Pipeline
// Renders a rotating lit cube with Camera + Light + MeshRenderer + ForwardRenderer
#include "engine/core/EngineBase.h"
#include "engine/core/AScene.h"
#include "engine/core/AObject.h"
#include "engine/core/AComponent.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/debug/FileDebugListener.h"
#include "engine/debug/ConsoleDebugListener.h"
#include "engine/platform/Input.h"
#include "engine/platform/Time.h"
#include "engine/rendering/Camera.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshRenderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// ============================================================
// Phong shader sources
// ============================================================
static const char* kPhongVS = R"(
#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uNormalMatrix) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* kPhongFS = R"(
#version 450 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

struct MaterialData {
    vec4 color;
    vec3 specular;
    float shininess;
    int hasDiffuseTex;
};

struct LightData {
    vec3 direction;
    vec3 color;
    vec3 ambient;
};

uniform MaterialData uMaterial;
uniform LightData uLight;
uniform vec3 uCameraPos;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(-uLight.direction);

    // Ambient
    vec3 ambient = uLight.ambient * uMaterial.color.rgb;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = uLight.color * diff * uMaterial.color.rgb;

    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uMaterial.shininess);
    vec3 specular = uLight.color * spec * uMaterial.specular;

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, uMaterial.color.a);
}
)";

// ============================================================
// Rotator component: rotates the object each frame
// ============================================================
class Rotator : public ark::AComponent {
public:
    void Tick(float dt) override {
        auto& transform = GetOwner()->GetTransform();
        angle_ += rotSpeed_ * dt;
        glm::quat rot = glm::angleAxis(angle_, glm::vec3(0, 1, 0))
                       * glm::angleAxis(0.3f, glm::vec3(1, 0, 0));
        transform.SetLocalRotation(rot);
    }

    void SetSpeed(float degreesPerSec) { rotSpeed_ = glm::radians(degreesPerSec); }

private:
    float rotSpeed_ = glm::radians(45.0f);
    float angle_ = 0.0f;
};

// ============================================================
// FrameLogger: logs every N frames
// ============================================================
class FrameLogger : public ark::AComponent {
public:
    void Tick(float /*dt*/) override {
        if (ark::Time::FrameCount() % 60 == 0 && ark::Time::FrameCount() > 0) {
            ARK_LOG_TRACE("Core", "Frame " + std::to_string(ark::Time::FrameCount()) +
                          " | dt: " + std::to_string(ark::Time::DeltaTime() * 1000.0f) + "ms" +
                          " | total: " + std::to_string(ark::Time::TotalTime()) + "s");
        }
    }
};

// ============================================================
// CameraObject: holds Camera component + ESC to close
// ============================================================
class CameraObject : public ark::AObject {
public:
    void Init() override {
        SetName("MainCamera");
        auto* cam = AddComponent<ark::Camera>();
        cam->SetPerspective(60.0f, 0.1f, 100.0f);
        cam->SetClearColor(glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));

        // Position camera
        GetTransform().SetLocalPosition(glm::vec3(0.0f, 1.5f, 3.0f));
        // Look toward origin
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::vec3 pos = GetTransform().GetLocalPosition();
        glm::mat4 lookMat = glm::lookAt(pos, target, glm::vec3(0, 1, 0));
        GetTransform().SetLocalRotation(glm::conjugate(glm::quat_cast(lookMat)));

        ARK_LOG_INFO("Core", "CameraObject initialized at (0, 1.5, 3)");
    }

    void Tick(float /*dt*/) override {
        if (ark::Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(
                ark::EngineBase::Get().GetWindow()->GetNativeHandle(), GLFW_TRUE);
        }
    }
};

// ============================================================
// LightObject: directional Light component
// ============================================================
class LightObject : public ark::AObject {
public:
    void Init() override {
        SetName("DirectionalLight");
        auto* light = AddComponent<ark::Light>();
        light->SetType(ark::Light::Type::Directional);
        light->SetColor(glm::vec3(1.0f, 0.95f, 0.9f));
        light->SetIntensity(1.0f);
        light->SetAmbient(glm::vec3(0.15f));

        // Point downward-forward
        GetTransform().SetLocalRotation(
            glm::angleAxis(glm::radians(-45.0f), glm::vec3(1, 0, 0)));

        ARK_LOG_INFO("Core", "LightObject initialized");
    }
};

// ============================================================
// CubeObject: MeshRenderer + Rotator
// ============================================================
class CubeObject : public ark::AObject {
public:
    void Init() override {
        SetName("Cube");

        auto* device = ark::EngineBase::Get().GetRHIDevice();

        // Shader
        auto shader = std::shared_ptr<ark::RHIShader>(device->CreateShader().release());
        if (!shader->Compile(kPhongVS, kPhongFS)) {
            ARK_LOG_FATAL("RHI", "Failed to compile Phong shader");
        }

        // Mesh
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateCube().release());
        mesh->Upload(device);

        // Material
        auto material = std::make_shared<ark::Material>();
        material->SetShader(shader);
        material->SetColor(glm::vec4(0.8f, 0.3f, 0.2f, 1.0f));
        material->SetSpecular(glm::vec3(0.6f));
        material->SetShininess(64.0f);

        // MeshRenderer component
        auto* mr = AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(material);

        // Rotator
        auto* rot = AddComponent<Rotator>();
        rot->SetSpeed(45.0f);

        // FrameLogger
        AddComponent<FrameLogger>();

        ARK_LOG_INFO("Core", "CubeObject initialized (id=" + std::to_string(GetId()) + ")");
    }
};

// ============================================================
// Demo Scene
// ============================================================
class DemoScene : public ark::AScene {
public:
    void OnLoad() override {
        ARK_LOG_INFO("Core", "DemoScene::OnLoad — Phase 4 3D Rendering");
        CreateObject<CameraObject>();
        CreateObject<LightObject>();
        CreateObject<CubeObject>();
    }

    void OnUnload() override {
        ARK_LOG_INFO("Core", "DemoScene::OnUnload");
    }
};

// ============================================================
// Entry point
// ============================================================
int main() {
    ark::ConsoleDebugListener consoleListener;
    ark::FileDebugListener fileListener;

    ark::EngineBase::Get().Run<DemoScene>(1280, 720, "StarArk Engine — Phase 4");
    return 0;
}
