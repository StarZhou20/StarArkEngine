#pragma once

#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshRenderer.h"
#include "engine/rendering/ForwardRenderer.h"
#include "engine/rendering/ShaderManager.h"
#include "engine/debug/DebugListenBus.h"
#include <glm/glm.hpp>
#include <string>

class PBRSphere : public ark::AObject {
public:
    struct Params {
        glm::vec3 position{0.0f};
        glm::vec4 albedo{1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        std::string name = "PBRSphere";
    };

    void Configure(const Params& p) { params_ = p; }

    void Init() override {
        SetName(params_.name);
        auto* device = ark::EngineBase::Get().GetRHIDevice();

        auto shader = ark::EngineBase::Get().GetRenderer()->GetShaderManager()->Get("pbr");
        if (!shader) {
            ARK_LOG_FATAL("RHI", "Failed to load PBR shader");
        }

        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateSphere(48, 24).release());
        mesh->Upload(device);

        auto material = std::make_shared<ark::Material>();
        material->SetShader(shader);
        material->SetColor(params_.albedo);
        material->SetMetallic(params_.metallic);
        material->SetRoughness(params_.roughness);
        material->SetAO(params_.ao);

        auto* mr = AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(material);

        GetTransform().SetLocalPosition(params_.position);

        ARK_LOG_INFO("Core", params_.name + " initialized");
    }

private:
    Params params_;
};
