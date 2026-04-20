#pragma once

#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshRenderer.h"
#include "engine/rendering/ShaderSources.h"
#include "engine/debug/DebugListenBus.h"
#include "../utils/TextureUtils.h"
#include <glm/glm.hpp>

class GroundObject : public ark::AObject {
public:
    void Init() override {
        SetName("Ground");
        auto* device = ark::EngineBase::Get().GetRHIDevice();

        auto shader = std::shared_ptr<ark::RHIShader>(device->CreateShader().release());
        if (!shader->Compile(ark::kPBR_VS, ark::kPBR_FS)) {
            ARK_LOG_FATAL("RHI", "Failed to compile PBR shader");
        }

        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreatePlane(10.0f).release());
        mesh->Upload(device);

        auto checkerTex = utils::CreateCheckerTexture(device);

        auto material = std::make_shared<ark::Material>();
        material->SetShader(shader);
        material->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        material->SetDiffuseTexture(checkerTex);
        material->SetMetallic(0.0f);
        material->SetRoughness(0.8f);
        material->SetAO(1.0f);

        auto* mr = AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(material);

        GetTransform().SetLocalPosition(glm::vec3(0.0f, -0.5f, 0.0f));
        ARK_LOG_INFO("Core", "GroundObject initialized (PBR + checker texture)");
    }
};
