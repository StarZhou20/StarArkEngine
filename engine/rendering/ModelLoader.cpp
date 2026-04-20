// ModelLoader.cpp — Assimp-based 3D model loading
#include "ModelLoader.h"
#include "TextureLoader.h"
#include "engine/debug/DebugListenBus.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <filesystem>

namespace ark {

// Vertex layout must match kPBR_VS / kPhongVS: position(3) + normal(3) + uv(2)
struct ModelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
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
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        ARK_LOG_ERROR("Rendering", "ModelLoader: failed to load '" + filepath + "': " +
                      importer.GetErrorString());
        return nodes;
    }

    std::string directory = GetDirectory(filepath);

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
