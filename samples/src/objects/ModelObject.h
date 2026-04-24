#pragma once

#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/rendering/MeshRenderer.h"
#include "engine/rendering/ForwardRenderer.h"
#include "engine/rendering/ShaderManager.h"
#include "engine/rendering/ModelLoader.h"
#include "engine/debug/DebugListenBus.h"
#include "../components/Rotator.h"
#include <glm/glm.hpp>
#include <string>

class ModelObject : public ark::AObject {
public:
    void Configure(const std::string& filepath, const glm::vec3& pos, float scale = 1.0f) {
        filepath_ = filepath;
        pos_ = pos;
        scale_ = scale;
    }

    void Init() override {
        SetName("Model_" + filepath_);
        auto* device = ark::EngineBase::Get().GetRHIDevice();

        auto shader = ark::EngineBase::Get().GetRenderer()->GetShaderManager()->Get("pbr");
        if (!shader) {
            ARK_LOG_FATAL("RHI", "Failed to load PBR shader");
        }

        auto nodes = ark::ModelLoader::Load(device, shader, filepath_);
        if (nodes.empty()) {
            ARK_LOG_ERROR("Core", "ModelObject: no meshes loaded from " + filepath_);
            return;
        }

        // Attach one MeshRenderer per sub-mesh so the whole model is rendered,
        // not just nodes[0]. All sub-meshes share this object's Transform (FBX
        // sub-meshes are typically pre-baked into model space).
        for (auto& node : nodes) {
            if (!node.mesh || !node.material) continue;
            auto* mr = AddComponent<ark::MeshRenderer>();
            mr->SetMesh(node.mesh);
            mr->SetMaterial(node.material);
        }

        GetTransform().SetLocalPosition(pos_);
        GetTransform().SetLocalScale(glm::vec3(scale_));

        ARK_LOG_INFO("Core", "ModelObject loaded '" + filepath_ + "' (" +
                     std::to_string(nodes.size()) + " meshes)");
    }

private:
    std::string filepath_;
    glm::vec3 pos_{0.0f};
    float scale_ = 1.0f;
};
