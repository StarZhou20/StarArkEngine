// ModelLoader.cpp — Assimp-based 3D model loading
#include "ModelLoader.h"
#include "TextureLoader.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace ark {

// Vertex layout must match kPBR_VS: position(3) + normal(3) + uv(2) + tangent(3)
struct ModelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
};

// One CPU-side batch of merged geometry (same material index).
struct ParsedMeshCPU {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t>    indices;
    unsigned int             materialIndex = 0;
};

// Plain description of a material extracted from Assimp. Serializable — holds
// only POD + texture file paths, so we can cache it to disk and bypass Assimp
// on subsequent runs.
struct ParsedMaterialDesc {
    glm::vec4 color{1.0f};
    glm::vec3 specular{0.5f};
    float     shininess = 32.0f;
    float     metallic  = 0.0f;
    float     roughness = 0.8f;
    float     ao        = 1.0f;
    glm::vec3 emissive{0.0f};

    std::string diffusePath;         // sRGB
    std::string normalPath;          // linear
    std::string metalRoughPath;      // linear; when Bistro-SPECULAR, R=AO G=Rough B=Metal
    std::string aoPath;              // linear
    std::string emissivePath;        // sRGB
    bool        specularPacksAO = false; // if true → metalRoughPath also feeds aoTex
};

// Held across the worker → main-thread boundary. Keeps the Importer alive so
// aiMaterial* pointers remain valid during Upload() when loaded fresh.
// When loaded from cache, importer/scene are null and `materials` is populated.
struct ParsedScene {
    std::unique_ptr<Assimp::Importer> importer;
    const aiScene* scene = nullptr;
    std::string    filepath;
    std::string    directory;
    std::vector<ParsedMeshCPU> batches;
    std::vector<ParsedMaterialDesc> materials; // index matches ParsedMeshCPU::materialIndex
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

static std::string GetDirectory(const std::string& filepath) {
    std::filesystem::path p(filepath);
    return p.parent_path().string();
}

// CPU-side merge: pack multiple aiMesh* sharing the same material into one
// vertex + index buffer. Safe to call off the main thread (no GL calls).
static void MergeMeshesCPU(const std::vector<aiMesh*>& group,
                           ParsedMeshCPU& out) {
    size_t totalV = 0, totalI = 0;
    for (auto* m : group) {
        totalV += m->mNumVertices;
        for (unsigned int i = 0; i < m->mNumFaces; ++i) totalI += m->mFaces[i].mNumIndices;
    }
    out.vertices.reserve(totalV);
    out.indices.reserve(totalI);

    for (auto* aiM : group) {
        uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());

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
                glm::vec3 up = (std::abs(v.normal.y) < 0.99f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                v.tangent = glm::normalize(glm::cross(up, v.normal));
            }
            out.vertices.push_back(v);
        }

        for (unsigned int i = 0; i < aiM->mNumFaces; ++i) {
            const aiFace& face = aiM->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                out.indices.push_back(baseVertex + face.mIndices[j]);
            }
        }
    }
}

// Main-thread: upload an already-assembled CPU batch to a GL Mesh.
static std::shared_ptr<Mesh> UploadCPUMesh(RHIDevice* device, const ParsedMeshCPU& cpu) {
    auto mesh = std::make_shared<Mesh>();
    VertexLayout layout;
    layout.stride = sizeof(ModelVertex);
    layout.attributes = {
        {"aPosition", VertexAttribType::Float3, 0},
        {"aNormal",   VertexAttribType::Float3, static_cast<uint32_t>(offsetof(ModelVertex, normal))},
        {"aTexCoord", VertexAttribType::Float2, static_cast<uint32_t>(offsetof(ModelVertex, uv))},
        {"aTangent",  VertexAttribType::Float3, static_cast<uint32_t>(offsetof(ModelVertex, tangent))},
    };
    mesh->SetVertices(cpu.vertices.data(),
                      cpu.vertices.size() * sizeof(ModelVertex), layout);
    mesh->SetIndices(cpu.indices.data(), cpu.indices.size());
    mesh->Upload(device);
    return mesh;
}

// Resolve an Assimp texture reference to an absolute on-disk path. Bistro
// uses absolute Windows paths (e.g. "D:\...\Bistro\Textures\foo.dds") inside
// the FBX; OBJ/MTL tends to use relative paths. Try both.
static std::string ResolveTexPath(const aiMaterial* aiMat, aiTextureType type, unsigned int slot,
                                  const std::string& directory) {
    aiString texPath;
    if (aiMat->GetTexture(type, slot, &texPath) != AI_SUCCESS) return {};
    std::string p = texPath.C_Str();
    for (auto& c : p) if (c == '\\') c = '/';
    if (p.empty()) return {};
    // Absolute path or drive-letter: use as-is.
    if (p.size() > 1 && (p[0] == '/' || (p.size() > 2 && p[1] == ':'))) {
        if (std::filesystem::exists(p)) return p;
        // Fall through to directory-relative with the basename only.
        auto slash = p.find_last_of('/');
        if (slash != std::string::npos) p = p.substr(slash + 1);
    }
    return directory + "/" + p;
}

// Extract a serializable material description from an aiMaterial. Runs in the
// worker thread during parse; no GL calls, no texture I/O here.
static ParsedMaterialDesc ExtractMaterialDesc(const aiMaterial* aiMat,
                                              const std::string& directory) {
    ParsedMaterialDesc d;

    aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
    aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    d.color = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};

    aiColor3D specular(0.5f, 0.5f, 0.5f);
    aiMat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
    d.specular = {specular.r, specular.g, specular.b};

    float shininess = 32.0f;
    aiMat->Get(AI_MATKEY_SHININESS, shininess);
    d.shininess = shininess;

    if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
        d.diffusePath = ResolveTexPath(aiMat, aiTextureType_DIFFUSE, 0, directory);
    }

    if (aiMat->GetTextureCount(aiTextureType_NORMALS) > 0) {
        d.normalPath = ResolveTexPath(aiMat, aiTextureType_NORMALS, 0, directory);
    } else if (aiMat->GetTextureCount(aiTextureType_HEIGHT) > 0) {
        // OBJ/MTL map_Bump
        d.normalPath = ResolveTexPath(aiMat, aiTextureType_HEIGHT, 0, directory);
    }

    if (aiMat->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0) {
        d.metalRoughPath = ResolveTexPath(aiMat, aiTextureType_DIFFUSE_ROUGHNESS, 0, directory);
    } else if (aiMat->GetTextureCount(aiTextureType_METALNESS) > 0) {
        d.metalRoughPath = ResolveTexPath(aiMat, aiTextureType_METALNESS, 0, directory);
    } else if (aiMat->GetTextureCount(aiTextureType_SPECULAR) > 0) {
        d.metalRoughPath = ResolveTexPath(aiMat, aiTextureType_SPECULAR, 0, directory);
        d.specularPacksAO = true;
    } else if (aiMat->GetTextureCount(aiTextureType_UNKNOWN) > 0) {
        d.metalRoughPath = ResolveTexPath(aiMat, aiTextureType_UNKNOWN, 0, directory);
    }

    if (aiMat->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0) {
        d.aoPath = ResolveTexPath(aiMat, aiTextureType_AMBIENT_OCCLUSION, 0, directory);
    } else if (aiMat->GetTextureCount(aiTextureType_LIGHTMAP) > 0) {
        d.aoPath = ResolveTexPath(aiMat, aiTextureType_LIGHTMAP, 0, directory);
    }

    if (aiMat->GetTextureCount(aiTextureType_EMISSIVE) > 0) {
        d.emissivePath = ResolveTexPath(aiMat, aiTextureType_EMISSIVE, 0, directory);
    }

    return d;
}

static std::shared_ptr<Material> BuildMaterialFromDesc(RHIDevice* device,
                                                      std::shared_ptr<RHIShader> shader,
                                                      const ParsedMaterialDesc& d) {
    auto material = std::make_shared<Material>();
    material->SetShader(shader);
    material->SetColor(d.color);
    material->SetSpecular(d.specular);
    material->SetShininess(d.shininess);
    material->SetMetallic(d.metallic);
    material->SetRoughness(d.roughness);
    material->SetAO(d.ao);
    material->SetEmissive(d.emissive);

    if (!d.diffusePath.empty()) {
        if (auto t = TextureLoader::Load(device, d.diffusePath, true)) {
            material->SetDiffuseTexture(t);
        }
    }
    if (!d.normalPath.empty()) {
        if (auto t = TextureLoader::Load(device, d.normalPath, false)) {
            material->SetNormalTexture(t);
        }
    }
    if (!d.metalRoughPath.empty()) {
        if (auto t = TextureLoader::Load(device, d.metalRoughPath, false)) {
            material->SetMetallicRoughnessTexture(t);
            if (d.specularPacksAO) material->SetAOTexture(t);
        }
    }
    if (!d.aoPath.empty()) {
        if (auto t = TextureLoader::Load(device, d.aoPath, false)) {
            material->SetAOTexture(t);
        }
    }
    if (!d.emissivePath.empty()) {
        if (auto t = TextureLoader::Load(device, d.emissivePath, true)) {
            material->SetEmissiveTexture(t);
        }
    }
    return material;
}

bool ModelLoader::IsParsedValid(const ParsedScene& parsed) {
    return parsed.scene != nullptr;
}

void ModelLoader::GetParsedBounds(const ParsedScene& parsed, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = parsed.aabbMin;
    outMax = parsed.aabbMax;
}

std::future<std::shared_ptr<ParsedScene>> ModelLoader::ParseAsync(const std::string& filepath) {
    return std::async(std::launch::async, [filepath]() -> std::shared_ptr<ParsedScene> {
        auto parsed = std::make_shared<ParsedScene>();
        parsed->filepath = filepath;

        auto resolved = Paths::ResolveResource(filepath).string();

        // -------- Try binary cache first --------
        // Cache is valid if source FBX hasn't changed since cache write.
        {
            std::filesystem::path cachePath = std::filesystem::path(resolved + ".arkcache");
            std::error_code ec;
            if (std::filesystem::exists(cachePath, ec)) {
                auto cacheTime  = std::filesystem::last_write_time(cachePath, ec);
                auto sourceTime = std::filesystem::last_write_time(resolved, ec);
                if (!ec && cacheTime >= sourceTime) {
                    auto t0 = std::chrono::steady_clock::now();
                    if (LoadCache(cachePath.string(), *parsed)) {
                        parsed->directory = GetDirectory(resolved);
                        auto t1 = std::chrono::steady_clock::now();
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                        ARK_LOG_INFO("Rendering", "Loaded cache '" + cachePath.string() + "' in " +
                            std::to_string(ms) + " ms (" + std::to_string(parsed->batches.size()) +
                            " batches, " + std::to_string(parsed->materials.size()) + " materials)");
                        // Mark scene non-null so downstream checks pass (we don't use it).
                        // Safety: we stash importer so the ParsedScene destructor is a no-op.
                        // A non-null `scene` is required by Upload()'s validity check; use a
                        // sentinel pointer so we don't accidentally dereference it.
                        parsed->scene = reinterpret_cast<const aiScene*>(0x1);
                        return parsed;
                    }
                    ARK_LOG_WARN("Rendering", "Cache load failed — falling back to Assimp parse");
                }
            }
        }

        // -------- Fresh parse via Assimp --------
        parsed->importer = std::make_unique<Assimp::Importer>();
        auto t0 = std::chrono::steady_clock::now();
        const aiScene* scene = parsed->importer->ReadFile(resolved,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace |
            aiProcess_PreTransformVertices);
        auto t1 = std::chrono::steady_clock::now();

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            ARK_LOG_ERROR("Rendering", "ModelLoader: failed to load '" + filepath +
                "' (resolved: " + resolved + "): " + parsed->importer->GetErrorString());
            parsed->scene = nullptr;
            return parsed;
        }

        parsed->scene = scene;
        parsed->directory = GetDirectory(resolved);

        // Group aiMesh* by material index, then CPU-merge each group.
        std::unordered_map<unsigned int, std::vector<aiMesh*>> byMaterial;
        byMaterial.reserve(scene->mNumMaterials);
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* m = scene->mMeshes[i];
            byMaterial[m->mMaterialIndex].push_back(m);
        }

        parsed->batches.reserve(byMaterial.size());
        for (auto& [matIndex, group] : byMaterial) {
            ParsedMeshCPU batch;
            batch.materialIndex = matIndex;
            MergeMeshesCPU(group, batch);
            parsed->batches.push_back(std::move(batch));
        }

        // Extract all materials (CPU-only; no texture I/O here).
        parsed->materials.resize(scene->mNumMaterials);
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
            parsed->materials[i] = ExtractMaterialDesc(scene->mMaterials[i], parsed->directory);
        }

        // Compute AABB over all merged vertices (native units — no transform).
        glm::vec3 bmin( std::numeric_limits<float>::max());
        glm::vec3 bmax(-std::numeric_limits<float>::max());
        for (const auto& b : parsed->batches) {
            for (const auto& v : b.vertices) {
                bmin = glm::min(bmin, v.position);
                bmax = glm::max(bmax, v.position);
            }
        }
        if (!parsed->batches.empty()) {
            parsed->aabbMin = bmin;
            parsed->aabbMax = bmax;
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        glm::vec3 size = bmax - bmin;
        ARK_LOG_INFO("Rendering", "Parsed '" + filepath + "' in " + std::to_string(ms) + " ms (" +
            std::to_string(scene->mNumMeshes) + " sub-meshes → " +
            std::to_string(parsed->batches.size()) + " material batches) AABB size=(" +
            std::to_string(size.x) + "," + std::to_string(size.y) + "," + std::to_string(size.z) + ")");

        // Persist cache for next run.
        std::filesystem::path cachePath = std::filesystem::path(resolved + ".arkcache");
        auto tc0 = std::chrono::steady_clock::now();
        if (SaveCache(cachePath.string(), *parsed)) {
            auto tc1 = std::chrono::steady_clock::now();
            auto cms = std::chrono::duration_cast<std::chrono::milliseconds>(tc1 - tc0).count();
            ARK_LOG_INFO("Rendering", "Saved cache '" + cachePath.string() + "' in " + std::to_string(cms) + " ms");
        }
        return parsed;
    });
}

std::vector<ModelNode> ModelLoader::Upload(RHIDevice* device,
                                            std::shared_ptr<RHIShader> shader,
                                            const ParsedScene& parsed,
                                            ProgressCallback onProgress) {
    std::vector<ModelNode> nodes;
    if (!device || !parsed.scene) return nodes;

    auto tick = [&](float p, const std::string& label) {
        if (onProgress) onProgress(p, label);
    };

    const size_t total = parsed.batches.size();
    nodes.reserve(total);
    // Diagnostic counters — what fraction of batches have each map type.
    int cntDiff=0, cntNormal=0, cntMR=0, cntAO=0, cntEmissive=0, cntSpecPacksAO=0;
    for (size_t i = 0; i < total; ++i) {
        const auto& cpu = parsed.batches[i];

        ModelNode node;
        node.mesh = UploadCPUMesh(device, cpu);

        if (cpu.materialIndex < parsed.materials.size()) {
            const auto& d = parsed.materials[cpu.materialIndex];
            if (!d.diffusePath.empty())    cntDiff++;
            if (!d.normalPath.empty())     cntNormal++;
            if (!d.metalRoughPath.empty()) cntMR++;
            if (!d.aoPath.empty() || d.specularPacksAO) cntAO++;
            if (!d.emissivePath.empty())   cntEmissive++;
            if (d.specularPacksAO)         cntSpecPacksAO++;
            node.material = BuildMaterialFromDesc(device, shader, d);
        } else {
            node.material = std::make_shared<Material>();
            node.material->SetShader(shader);
        }

        if (!node.material->GetDiffuseTexture()) {
            uint32_t h = cpu.materialIndex * 2654435761u;
            float r = ((h >>  0) & 0xFF) / 255.0f;
            float g = ((h >>  8) & 0xFF) / 255.0f;
            float b = ((h >> 16) & 0xFF) / 255.0f;
            r = 0.25f + r * 0.6f;
            g = 0.25f + g * 0.6f;
            b = 0.25f + b * 0.6f;
            node.material->SetColor({r, g, b, 1.0f});
        }

        nodes.push_back(std::move(node));

        float p = static_cast<float>(i + 1) / static_cast<float>(total);
        tick(p, "Batch " + std::to_string(i + 1) + "/" + std::to_string(total));
    }

    ARK_LOG_INFO("Rendering", "Uploaded model '" + parsed.filepath + "' (" +
        std::to_string(nodes.size()) + " batches)");
    ARK_LOG_INFO("Rendering", "  Texture presence: diffuse=" + std::to_string(cntDiff) +
        " normal=" + std::to_string(cntNormal) +
        " MR=" + std::to_string(cntMR) +
        " AO=" + std::to_string(cntAO) +
        " emissive=" + std::to_string(cntEmissive) +
        " (of which " + std::to_string(cntSpecPacksAO) + " use SPECULAR-packed AO)");
    return nodes;
}

std::vector<ModelNode> ModelLoader::Load(RHIDevice* device,
                                          std::shared_ptr<RHIShader> shader,
                                          const std::string& filepath,
                                          ProgressCallback onProgress) {
    if (!device) {
        ARK_LOG_ERROR("Rendering", "ModelLoader::Load: null device");
        return {};
    }
    if (onProgress) onProgress(-1.0f, "Parsing");
    auto parsed = ParseAsync(filepath).get();
    if (!parsed || !parsed->scene) return {};
    return Upload(device, shader, *parsed, onProgress);
}

// =============================================================================
// Binary cache — bypasses Assimp on warm runs. Format (little-endian, host
// byte order assumed = x86/x64 only for now):
//   char[4]  magic = "ARKM"
//   u32      version
//   u32      numBatches
//   for each batch:
//     u32    materialIndex
//     u32    vertexCount
//     u32    indexCount
//     ModelVertex[vertexCount]  // raw blob (POD)
//     u32[indexCount]
//   u32      numMaterials
//   for each material:
//     float[4] color, float[3] specular, float shininess, metallic, roughness, ao
//     float[3] emissive
//     u8       specularPacksAO (0/1)
//     u32 len + bytes  x5  (diffuse, normal, metalRough, ao, emissive paths)
//   float[3] aabbMin, aabbMax
// =============================================================================
namespace {

constexpr uint32_t kCacheMagic   = 0x4D4B5241u; // "ARKM"
constexpr uint32_t kCacheVersion = 4;

template <class T>
void WritePOD(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T>
bool ReadPOD(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<size_t>(f.gcount()) == sizeof(T);
}
// Explicit per-float vector I/O to avoid struct-alignment surprises
// (glm::vec3 may be 12 or 16 bytes depending on SIMD config).
void WriteVec3(std::ofstream& f, const glm::vec3& v) {
    WritePOD(f, v.x); WritePOD(f, v.y); WritePOD(f, v.z);
}
bool ReadVec3(std::ifstream& f, glm::vec3& v) {
    return ReadPOD(f, v.x) && ReadPOD(f, v.y) && ReadPOD(f, v.z);
}
void WriteVec4(std::ofstream& f, const glm::vec4& v) {
    WritePOD(f, v.x); WritePOD(f, v.y); WritePOD(f, v.z); WritePOD(f, v.w);
}
bool ReadVec4(std::ifstream& f, glm::vec4& v) {
    return ReadPOD(f, v.x) && ReadPOD(f, v.y) && ReadPOD(f, v.z) && ReadPOD(f, v.w);
}
void WriteString(std::ofstream& f, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    WritePOD(f, n);
    if (n) f.write(s.data(), n);
}
bool ReadString(std::ifstream& f, std::string& out) {
    uint32_t n = 0;
    if (!ReadPOD(f, n)) return false;
    if (n > (1u << 20)) {
        ARK_LOG_WARN("Rendering", "ReadString: n too large: " + std::to_string(n) +
            " at offset ~" + std::to_string(static_cast<long long>(f.tellg())));
        return false;
    }
    out.resize(n);
    if (n == 0) return true;
    f.read(out.data(), n);
    return static_cast<size_t>(f.gcount()) == n;
}

} // namespace

bool ModelLoader::SaveCache(const std::string& cachePath, const ParsedScene& parsed) {
    std::ofstream f(cachePath, std::ios::binary | std::ios::trunc);
    if (!f) {
        ARK_LOG_WARN("Rendering", "SaveCache: cannot open '" + cachePath + "' for write");
        return false;
    }

    WritePOD(f, kCacheMagic);
    WritePOD(f, kCacheVersion);

    uint32_t nBatches = static_cast<uint32_t>(parsed.batches.size());
    WritePOD(f, nBatches);
    for (const auto& b : parsed.batches) {
        WritePOD(f, b.materialIndex);
        uint32_t vn = static_cast<uint32_t>(b.vertices.size());
        uint32_t in = static_cast<uint32_t>(b.indices.size());
        WritePOD(f, vn);
        WritePOD(f, in);
        if (vn) f.write(reinterpret_cast<const char*>(b.vertices.data()), vn * sizeof(ModelVertex));
        if (in) f.write(reinterpret_cast<const char*>(b.indices.data()),  in * sizeof(uint32_t));
    }

    uint32_t nMats = static_cast<uint32_t>(parsed.materials.size());
    WritePOD(f, nMats);
    for (const auto& m : parsed.materials) {
        WriteVec4(f, m.color);
        WriteVec3(f, m.specular);
        WritePOD(f, m.shininess);
        WritePOD(f, m.metallic);
        WritePOD(f, m.roughness);
        WritePOD(f, m.ao);
        WriteVec3(f, m.emissive);
        uint8_t sp = m.specularPacksAO ? 1 : 0;
        WritePOD(f, sp);
        WriteString(f, m.diffusePath);
        WriteString(f, m.normalPath);
        WriteString(f, m.metalRoughPath);
        WriteString(f, m.aoPath);
        WriteString(f, m.emissivePath);
    }

    WriteVec3(f, parsed.aabbMin);
    WriteVec3(f, parsed.aabbMax);
    return f.good();
}

bool ModelLoader::LoadCache(const std::string& cachePath, ParsedScene& outParsed) {
    std::ifstream f(cachePath, std::ios::binary);
    if (!f) { ARK_LOG_WARN("Rendering", "LoadCache: cannot open"); return false; }

    uint32_t magic = 0, version = 0;
    if (!ReadPOD(f, magic)) { ARK_LOG_WARN("Rendering", "LoadCache: failed to read magic"); return false; }
    if (magic != kCacheMagic) {
        ARK_LOG_WARN("Rendering", "LoadCache: bad magic 0x" + std::to_string(magic));
        return false;
    }
    if (!ReadPOD(f, version) || version != kCacheVersion) {
        ARK_LOG_WARN("Rendering", "LoadCache: version mismatch " + std::to_string(version));
        return false;
    }

    uint32_t nBatches = 0;
    if (!ReadPOD(f, nBatches)) { ARK_LOG_WARN("Rendering", "LoadCache: failed to read nBatches"); return false; }
    if (nBatches > (1u << 24)) { ARK_LOG_WARN("Rendering", "LoadCache: nBatches too large: " + std::to_string(nBatches)); return false; }
    outParsed.batches.resize(nBatches);
    for (size_t bi = 0; bi < outParsed.batches.size(); ++bi) {
        auto& b = outParsed.batches[bi];
        uint32_t vn = 0, in = 0;
        if (!ReadPOD(f, b.materialIndex)) { ARK_LOG_WARN("Rendering", "LoadCache: batch[" + std::to_string(bi) + "] matIdx read fail"); return false; }
        if (!ReadPOD(f, vn) || !ReadPOD(f, in)) { ARK_LOG_WARN("Rendering", "LoadCache: batch[" + std::to_string(bi) + "] counts read fail"); return false; }
        if (vn > (1u << 28) || in > (1u << 28)) { ARK_LOG_WARN("Rendering", "LoadCache: batch[" + std::to_string(bi) + "] counts huge"); return false; }
        b.vertices.resize(vn);
        b.indices.resize(in);
        if (vn) f.read(reinterpret_cast<char*>(b.vertices.data()), vn * sizeof(ModelVertex));
        if (in) f.read(reinterpret_cast<char*>(b.indices.data()),  in * sizeof(uint32_t));
        if (!f) { ARK_LOG_WARN("Rendering", "LoadCache: batch[" + std::to_string(bi) + "] data read fail"); return false; }
    }

    uint32_t nMats = 0;
    if (!ReadPOD(f, nMats)) { ARK_LOG_WARN("Rendering", "LoadCache: nMats read fail"); return false; }
    if (nMats > (1u << 20)) { ARK_LOG_WARN("Rendering", "LoadCache: nMats too large"); return false; }
    outParsed.materials.resize(nMats);
    for (size_t mi = 0; mi < outParsed.materials.size(); ++mi) {
        auto& m = outParsed.materials[mi];
        if (!ReadVec4(f, m.color) ||
            !ReadVec3(f, m.specular) ||
            !ReadPOD(f, m.shininess) ||
            !ReadPOD(f, m.metallic) ||
            !ReadPOD(f, m.roughness) ||
            !ReadPOD(f, m.ao) ||
            !ReadVec3(f, m.emissive)) { ARK_LOG_WARN("Rendering", "LoadCache: mat[" + std::to_string(mi) + "] scalars fail"); return false; }
        uint8_t sp = 0;
        if (!ReadPOD(f, sp)) { ARK_LOG_WARN("Rendering", "LoadCache: mat[" + std::to_string(mi) + "] sp fail"); return false; }
        m.specularPacksAO = (sp != 0);
        if (!ReadString(f, m.diffusePath)    ||
            !ReadString(f, m.normalPath)     ||
            !ReadString(f, m.metalRoughPath) ||
            !ReadString(f, m.aoPath)         ||
            !ReadString(f, m.emissivePath))  { ARK_LOG_WARN("Rendering", "LoadCache: mat[" + std::to_string(mi) + "] paths fail"); return false; }
    }

    if (!ReadVec3(f, outParsed.aabbMin)) { ARK_LOG_WARN("Rendering", "LoadCache: aabbMin fail"); return false; }
    if (!ReadVec3(f, outParsed.aabbMax)) { ARK_LOG_WARN("Rendering", "LoadCache: aabbMax fail"); return false; }
    return true;
}

} // namespace ark
