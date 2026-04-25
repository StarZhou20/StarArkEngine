// ModelLoader.h — Load 3D model files via Assimp into Mesh + Material
#pragma once

#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rhi/RHIDevice.h"
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

namespace ark {

/// A single loaded sub-mesh with its associated material.
struct ModelNode {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};

/// Opaque handle to a parsed Assimp scene. Created on a worker thread, then
/// consumed on the main (GL) thread.
struct ParsedScene;

/// Loads 3D model files (OBJ/FBX/glTF) via Assimp.
/// Each call returns a vector of ModelNode (one per sub-mesh).
class ModelLoader {
public:
    /// progress: 0..1  |  label: short human-readable stage (may be updated multiple times).
    using ProgressCallback = std::function<void(float progress, const std::string& label)>;

    /// Synchronous: parse + upload in one call on the calling thread. Handy
    /// for small assets; Bistro should use the async API.
    static std::vector<ModelNode> Load(RHIDevice* device,
                                       std::shared_ptr<RHIShader> shader,
                                       const std::string& filepath,
                                       ProgressCallback onProgress = {});

    /// Async parse (Assimp ReadFile + CPU-side mesh assembly). Safe to call
    /// from any thread; the returned future resolves on the worker thread.
    /// The returned ParsedScene contains CPU-only data — no GL objects yet.
    static std::future<std::shared_ptr<ParsedScene>> ParseAsync(const std::string& filepath);

    /// Returns true if a ParsedScene was produced successfully.
    static bool IsParsedValid(const ParsedScene& parsed);

    /// Returns the AABB of the parsed scene in its native units (no scale).
    /// Only valid if IsParsedValid() is true.
    static void GetParsedBounds(const ParsedScene& parsed, glm::vec3& outMin, glm::vec3& outMax);

    /// Main-thread-only. Consume a ParsedScene: create GL meshes, load/upload
    /// textures, build materials. Reports progress over [0, 1].
    static std::vector<ModelNode> Upload(RHIDevice* device,
                                         std::shared_ptr<RHIShader> shader,
                                         const ParsedScene& parsed,
                                         ProgressCallback onProgress = {});

    /// Serialize a ParsedScene (batches + material descriptors) to a binary
    /// blob on disk. Called automatically after a fresh Assimp parse so that
    /// subsequent runs skip Assimp entirely.
    static bool SaveCache(const std::string& cachePath, const ParsedScene& parsed);

    /// Deserialize a cache file into ParsedScene (batches + materials).
    /// Returns false if the file is missing / corrupt / wrong version.
    static bool LoadCache(const std::string& cachePath, ParsedScene& outParsed);
};

} // namespace ark
