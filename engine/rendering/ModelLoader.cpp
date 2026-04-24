// ModelLoader.cpp — Assimp-based 3D model loading
#include "ModelLoader.h"
#include "TextureLoader.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <filesystem>

namespace ark {

// Vertex layout must match kPBR_VS: position(3) + normal(3) + uv(2) + tangent(3)
struct ModelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
};

static std::string GetDirectory(const std::string& filepath) {
    std::filesystem::path p(filepath);
    return p.parent_path().string();
}

static std::shared_ptr<Mesh> ProcessMesh(RHIDevice* device, aiMesh* aiM) {
    std::vector<ModelVertex> vertices;
    vertices.reserve(aiM->mNumVertices);

    for (unsigned int i = 0; i < aiM->mNumVertices; ++i) {
        ModelVertex v{};
        v.position = {aiM->mVertices[i].x, aiM->mVertices[i].y, aiM->mVertices[i].z};
        if (aiM->HasNormals()) {
            v.normal = {aiM->mNormals[i].x, aiM->mNormals[i].y, aiM->mNormals[i].z};
        }
        if (aiM->mTextureCoords[0]) {
            v.uv = {aiM->mTextureCoords[0][i].x, aiM->mTextureCoords[0][i].y};
        }
        if (aiM->HasTangentsAndBitangents()) {
            v.tangent = {aiM->mTangents[i].x, aiM->mTangents[i].y, aiM->mTangents[i].z};
        } else {
            // Fallback tangent — any vector orthogonal to the normal.
            glm::vec3 up = (std::abs(v.normal.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
            v.tangent = glm::normalize(glm::cross(up, v.normal));
        }
        vertices.push_back(v);
    }

    std::vector<uint32_t> indices;
    for (unsigned int i = 0; i < aiM->mNumFaces; ++i) {
        const aiFace& face = aiM->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(face.mIndices[j]);
        }
    }

    auto mesh = std::make_shared<Mesh>();

    VertexLayout layout;
    layout.stride = sizeof(ModelVertex);
    layout.attributes = {
        {"aPosition", VertexAttribType::Float3, 0},
        {"aNormal",   VertexAttribType::Float3, static_cast<uint32_t>(offsetof(ModelVertex, normal))},
        {"aTexCoord", VertexAttribType::Float2, static_cast<uint32_t>(offsetof(ModelVertex, uv))},
        {"aTangent",  VertexAttribType::Float3, static_cast<uint32_t>(offsetof(ModelVertex, tangent))},
    };

    mesh->SetVertices(vertices.data(), vertices.size() * sizeof(ModelVertex), layout);
    mesh->SetIndices(indices.data(), indices.size());
    mesh->Upload(device);

    return mesh;
}

static std::shared_ptr<Material> ProcessMaterial(RHIDevice* device,
                                                  const aiScene* scene,
                                                  unsigned int matIndex,
                                                  std::shared_ptr<RHIShader> shader,
                                                  const std::string& directory) {
    auto material = std::make_shared<Material>();
    material->SetShader(shader);

    if (matIndex >= scene->mNumMaterials) return material;

    const aiMaterial* aiMat = scene->mMaterials[matIndex];

    // Diffuse color
    aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
    aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    material->SetColor({diffuse.r, diffuse.g, diffuse.b, diffuse.a});

    // Specular color
    aiColor3D specular(0.5f, 0.5f, 0.5f);
    aiMat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
    material->SetSpecular({specular.r, specular.g, specular.b});

    // Shininess
    float shininess = 32.0f;
    aiMat->Get(AI_MATKEY_SHININESS, shininess);
    material->SetShininess(shininess);

    // Diffuse texture (first slot)
    if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
        aiString texPath;
        aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);

        std::string fullPath = directory + "/" + texPath.C_Str();
        // Normalize path separators
        for (auto& c : fullPath) {
            if (c == '\\') c = '/';
        }

        auto tex = TextureLoader::Load(device, fullPath);
        if (tex) {
            material->SetDiffuseTexture(tex);
        }
    }

    // --- Phase 9: additional PBR maps (non-color = linear) ---
    auto loadTex = [&](aiTextureType type, bool isSRGB) -> std::shared_ptr<RHITexture> {
        if (aiMat->GetTextureCount(type) == 0) return nullptr;
        aiString texPath;
        aiMat->GetTexture(type, 0, &texPath);
        std::string fullPath = directory + "/" + texPath.C_Str();
        for (auto& c : fullPath) if (c == '\\') c = '/';
        return TextureLoader::Load(device, fullPath, isSRGB);
    };

    // Normal map
    if (auto t = loadTex(aiTextureType_NORMALS, false)) {
        material->SetNormalTexture(t);
    } else if (auto t = loadTex(aiTextureType_HEIGHT, false)) {
        // OBJ/MTL uses `map_Bump` which Assimp tags as HEIGHT — treat as normal map.
        material->SetNormalTexture(t);
    }

    // Metallic-roughness: glTF packs MR into a single texture.
    // Assimp 5.x typically exposes it as DIFFUSE_ROUGHNESS (combined) or UNKNOWN.
    if (auto t = loadTex(aiTextureType_DIFFUSE_ROUGHNESS, false)) {
        material->SetMetallicRoughnessTexture(t);
    } else if (auto t = loadTex(aiTextureType_METALNESS, false)) {
        material->SetMetallicRoughnessTexture(t);
    } else if (auto t = loadTex(aiTextureType_UNKNOWN, false)) {
        material->SetMetallicRoughnessTexture(t);
    }

    // Ambient occlusion
    if (auto t = loadTex(aiTextureType_AMBIENT_OCCLUSION, false)) {
        material->SetAOTexture(t);
    } else if (auto t = loadTex(aiTextureType_LIGHTMAP, false)) {
        material->SetAOTexture(t);
    }

    // Emissive
    if (auto t = loadTex(aiTextureType_EMISSIVE, true)) {
        material->SetEmissiveTexture(t);
    }

    return material;
}

std::vector<ModelNode> ModelLoader::Load(RHIDevice* device,
                                          std::shared_ptr<RHIShader> shader,
                                          const std::string& filepath) {
    std::vector<ModelNode> nodes;

    if (!device) {
        ARK_LOG_ERROR("Rendering", "ModelLoader::Load: null device");
        return nodes;
    }

    Assimp::Importer importer;
    // v0.2 15.D — MOD-aware VFS
    auto resolved = Paths::ResolveResource(filepath).string();
    const aiScene* scene = importer.ReadFile(resolved,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        ARK_LOG_ERROR("Rendering", "ModelLoader: failed to load '" + filepath +
            "' (resolved: " + resolved + "): " + importer.GetErrorString());
        return nodes;
    }

    std::string directory = GetDirectory(resolved);

    // Flatten all meshes from the scene (ignore node hierarchy for now)
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh* aiM = scene->mMeshes[i];

        ModelNode node;
        node.mesh = ProcessMesh(device, aiM);
        node.material = ProcessMaterial(device, scene, aiM->mMaterialIndex, shader, directory);

        nodes.push_back(std::move(node));
    }

    ARK_LOG_INFO("Rendering", "Loaded model '" + filepath + "' (" +
        std::to_string(nodes.size()) + " meshes)");

    return nodes;
}

} // namespace ark
