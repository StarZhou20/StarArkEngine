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
    // Path-based: load inside Init() (legacy, no progress reporting).
    void Configure(const std::string& filepath, const glm::vec3& pos, float scale = 1.0f) {
        filepath_ = filepath;
        pos_ = pos;
        scale_ = scale;
    }

    // Pre-loaded nodes: skip the load, just attach renderers. Used when the
    // caller drives the load itself (e.g. with a progress callback).
    void ConfigureNodes(std::vector<ark::ModelNode> nodes, const glm::vec3& pos, float scale = 1.0f) {
        preloaded_ = std::move(nodes);
        pos_ = pos;
        scale_ = scale;
    }

    void Init() override {
        auto* device = ark::EngineBase::Get().GetRHIDevice();

        std::vector<ark::ModelNode> nodes;
        if (!preloaded_.empty()) {
            nodes = std::move(preloaded_);
            SetName("Model_preloaded");
        } else {
            SetName("Model_" + filepath_);
            auto shader = ark::EngineBase::Get().GetRenderer()->GetShaderManager()->Get("pbr");
            if (!shader) {
                ARK_LOG_FATAL("RHI", "Failed to load PBR shader");
            }
            nodes = ark::ModelLoader::Load(device, shader, filepath_);
            if (nodes.empty()) {
                ARK_LOG_ERROR("Core", "ModelObject: no meshes loaded from " + filepath_);
                return;
            }
        }

        // Attach one MeshRenderer per sub-mesh so the whole model is rendered.
        // All sub-meshes share this object's Transform.
        for (auto& node : nodes) {
            if (!node.mesh || !node.material) continue;
            auto* mr = AddComponent<ark::MeshRenderer>();
            mr->SetMesh(node.mesh);
            mr->SetMaterial(node.material);
        }

        GetTransform().SetLocalPosition(pos_);
        GetTransform().SetLocalScale(glm::vec3(scale_));

        ARK_LOG_INFO("Core", "ModelObject attached " + std::to_string(nodes.size()) +
                     " mesh batches");
    }

private:
    std::string filepath_;
    glm::vec3 pos_{0.0f};
    float scale_ = 1.0f;
    std::vector<ark::ModelNode> preloaded_;
};
