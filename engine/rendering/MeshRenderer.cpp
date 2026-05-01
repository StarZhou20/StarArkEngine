#include "MeshRenderer.h"
#include "ShaderManager.h"
#include "TextureLoader.h"
#include "ModelLoader.h"
#include "ForwardRenderer.h"
#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"
#include <algorithm>

namespace ark {

std::vector<MeshRenderer*> MeshRenderer::allRenderers_;

void MeshRenderer::OnAttach() {
    allRenderers_.push_back(this);
}

void MeshRenderer::OnDetach() {
    allRenderers_.erase(
        std::remove(allRenderers_.begin(), allRenderers_.end(), this),
        allRenderers_.end());
}

const std::vector<MeshRenderer*>& MeshRenderer::GetAllRenderers() {
    return allRenderers_;
}

// -----------------------------------------------------------------------------
// ResolveResources — v0.2 15.C
// 根据 spec 字段构造 mesh_ 和 material_。场景代码在 SceneDoc::Load 之后
// 对每个 MeshRenderer 调一次。
// -----------------------------------------------------------------------------
void MeshRenderer::ResolveResources(RHIDevice* device, ShaderManager* shaders) {
    // --- Mesh ---
    std::unique_ptr<Mesh> built;
    if      (meshKind_ == "cube")   built = Mesh::CreateCube();
    else if (meshKind_ == "plane")  built = Mesh::CreatePlane(meshParamF_);
    else if (meshKind_ == "sphere") built = Mesh::CreateSphere(meshParamI_, std::max(1, meshParamI_ / 2));
    else if (meshKind_ == "model") {
        // model 路径走 ModelLoader；但 ModelLoader 同时返回 mesh+material，
        // 这里仅取第一 sub-mesh 作为 v0.2 的 MVP。未来拆成多个 MeshRenderer。
        if (!device || !shaders) {
            ARK_LOG_WARN("MeshRenderer", "ResolveResources: model kind requires device+shaders");
            return;
        }
        auto shader = shaders->Get(materialShader_);
        if (!shader) {
            ARK_LOG_WARN("MeshRenderer",
                std::string("shader not found: ") + materialShader_);
            return;
        }
        auto nodes = ModelLoader::Load(device, shader, meshPath_);
        if (nodes.empty()) {
            ARK_LOG_WARN("MeshRenderer",
                std::string("model load failed: ") + meshPath_);
            return;
        }
        mesh_     = nodes[0].mesh;
        material_ = nodes[0].material; // ModelLoader 已填充纹理
        return;
    } else {
        ARK_LOG_WARN("MeshRenderer",
            std::string("unknown mesh_kind: ") + meshKind_);
        return;
    }

    if (device) {
        built->Upload(device);
    }
    mesh_ = std::shared_ptr<Mesh>(built.release());

    // --- Material ---
    if (!shaders) return; // 无 shaders 时只构造 mesh（测试用）

    auto shader = shaders->Get(materialShader_);
    if (!shader) {
        ARK_LOG_WARN("MeshRenderer",
            std::string("shader not found: ") + materialShader_);
        return;
    }

    auto mat = std::make_shared<Material>();
    mat->SetShader(shader);
    mat->SetColor(materialColor_);
    mat->SetMetallic(materialMetallic_);
    mat->SetRoughness(materialRoughness_);
    mat->SetAO(materialAo_);
    mat->SetEmissive(materialEmissive_);
    mat->SetPBR(true);

    auto loadTex = [&](const std::string& path, bool srgb)
                       -> std::shared_ptr<RHITexture> {
        if (path.empty() || !device) return {};
        return TextureLoader::Load(device, path, srgb);
    };
    if (auto t = loadTex(materialAlbedoTex_,   /*srgb=*/true))  mat->SetDiffuseTexture(t);
    if (auto t = loadTex(materialNormalTex_,   /*srgb=*/false)) mat->SetNormalTexture(t);
    if (auto t = loadTex(materialMRATex_,      /*srgb=*/false)) mat->SetMetallicRoughnessTexture(t);
    if (auto t = loadTex(materialAoTex_,       /*srgb=*/false)) mat->SetAOTexture(t);
    if (auto t = loadTex(materialEmissiveTex_, /*srgb=*/true))  mat->SetEmissiveTexture(t);

    material_ = std::move(mat);
}

// v0.3 — Reflection post-load hook. Pulls device + shaders from EngineBase
// and re-runs ResolveResources(). Safe to call repeatedly (idempotent).
void MeshRenderer::OnReflectionLoaded() {
    auto& engine = EngineBase::Get();
    RHIDevice* device = engine.GetRHIDevice();
    ShaderManager* shaders = engine.GetRenderer()
                                ? engine.GetRenderer()->GetShaderManager()
                                : nullptr;
    if (!device) {
        ARK_LOG_WARN("MeshRenderer",
            "OnReflectionLoaded: no RHIDevice yet — skipping resolve");
        return;
    }
    ResolveResources(device, shaders);
}

// -----------------------------------------------------------------------------
// 反射注册 (v0.2 15.A/15.C)
// mesh_ / material_（shared_ptr）不参与序列化；把 spec 字段全部暴露。
// -----------------------------------------------------------------------------
ARK_REFLECT_COMPONENT(MeshRenderer)
    // Mesh spec
    ARK_FIELD(meshKind_,   "mesh_kind",   String)
    ARK_FIELD(meshPath_,   "mesh_path",   AssetPath)
    ARK_FIELD(meshParamF_, "mesh_paramf", Float)
    ARK_FIELD(meshParamI_, "mesh_parami", Int)
    // Material spec
    ARK_FIELD(materialShader_,    "material_shader",    String)
    ARK_FIELD(materialColor_,     "material_color",     Color4)
    ARK_FIELD(materialMetallic_,  "material_metallic",  Float)
    ARK_FIELD(materialRoughness_, "material_roughness", Float)
    ARK_FIELD(materialAo_,        "material_ao",        Float)
    ARK_FIELD(materialEmissive_,  "material_emissive",  Color3)
    // Textures (AssetPath)
    ARK_FIELD(materialAlbedoTex_,   "tex_albedo",   AssetPath)
    ARK_FIELD(materialNormalTex_,   "tex_normal",   AssetPath)
    ARK_FIELD(materialMRATex_,      "tex_mra",      AssetPath)
    ARK_FIELD(materialAoTex_,       "tex_ao",       AssetPath)
    ARK_FIELD(materialEmissiveTex_, "tex_emissive", AssetPath)
ARK_END_REFLECT(MeshRenderer)

} // namespace ark

