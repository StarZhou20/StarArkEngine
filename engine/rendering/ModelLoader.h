// ModelLoader.h — Load 3D model files via Assimp into Mesh + Material
#pragma once

#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rhi/RHIDevice.h"
#include <memory>
#include <string>
#include <vector>

namespace ark {

/// A single loaded sub-mesh with its associated material.
struct ModelNode {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

/// Loads 3D model files (OBJ/FBX/glTF) via Assimp.
/// Each call returns a vector of ModelNode (one per sub-mesh).
class ModelLoader {
public:
    /// Load a model file. Textures are resolved relative to the model's directory.
    /// Returns empty vector on failure.
    static std::vector<ModelNode> Load(RHIDevice* device,
                                       std::shared_ptr<RHIShader> shader,
                                       const std::string& filepath);
};

} // namespace ark
