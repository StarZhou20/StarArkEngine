#pragma once

#include "engine/core/AComponent.h"
#include "engine/core/TypeInfo.h"
#include "Mesh.h"
#include "Material.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ark {

class RHIDevice;
class ShaderManager;

// -----------------------------------------------------------------------------
// MeshRenderer
//
// v0.2 扩展: 反射字段 (spec) + 运行期缓存 (mesh_ / material_)。
//
// 两种工作方式:
//   (a) 代码直接 SetMesh()/SetMaterial()（v0.1 风格，依旧可用，但不序列化）
//   (b) 用 spec 字段 (meshKind_ / materialColor_ 等) + ResolveResources(...)
//       从 TOML 加载场景时走 (b): SceneDoc 把字段还原进来，场景代码调一次
//       ResolveResources 把 primitive mesh + Material 构造出来。
//
// 15.C 为了闭合 "CottageScene from TOML" 验收，暂时把 Material 参数内嵌在
// MeshRenderer 里。15.E Inspector 之后会考虑独立 MaterialSpec 组件。
// -----------------------------------------------------------------------------

class MeshRenderer : public AComponent {
public:
    ARK_DECLARE_REFLECTION(MeshRenderer);

    void OnAttach() override;
    void OnDetach() override;

    // --- Runtime resources（v0.1 风格，仍可用） ---
    void SetMesh(std::shared_ptr<Mesh> mesh) { mesh_ = std::move(mesh); }
    Mesh* GetMesh() const { return mesh_.get(); }

    void SetMaterial(std::shared_ptr<Material> mat) { material_ = std::move(mat); }
    Material* GetMaterial() const { return material_.get(); }

    // --- v0.2 spec 读写（对应反射字段） ---
    // mesh_kind: "cube" | "sphere" | "plane" | "model"
    void SetMeshKind(const std::string& kind)   { meshKind_ = kind; }
    const std::string& GetMeshKind() const      { return meshKind_; }

    void SetMeshPath(const std::string& p)      { meshPath_ = p; }
    const std::string& GetMeshPath() const      { return meshPath_; }

    void SetMeshParamF(float v)                 { meshParamF_ = v; }
    void SetMeshParamI(int v)                   { meshParamI_ = v; }

    void SetMaterialShader(const std::string& s) { materialShader_ = s; }
    void SetMaterialColor(const glm::vec4& c)    { materialColor_ = c; }
    void SetMaterialMetallic(float m)            { materialMetallic_ = m; }
    void SetMaterialRoughness(float r)           { materialRoughness_ = r; }
    void SetMaterialAO(float a)                  { materialAo_ = a; }
    void SetMaterialEmissive(const glm::vec3& e) { materialEmissive_ = e; }

    // 贴图路径（AssetPath；15.D 后走 Paths::ResolveResource）
    void SetAlbedoTex(const std::string& p)    { materialAlbedoTex_ = p; }
    void SetNormalTex(const std::string& p)    { materialNormalTex_ = p; }
    void SetMRATex(const std::string& p)       { materialMRATex_ = p; }
    void SetAOTex(const std::string& p)        { materialAoTex_ = p; }
    void SetEmissiveTex(const std::string& p)  { materialEmissiveTex_ = p; }

    // 根据 spec 字段构造并赋值 mesh_ / material_。幂等。
    void ResolveResources(RHIDevice* device, ShaderManager* shaders);

    // v0.3 — automatically called by SceneDoc after reflection writes finish.
    // Pulls device + shaders from EngineBase singleton and forwards to
    // ResolveResources(). Lets a SceneDoc::Load fully realize MeshRenderers
    // without scene code doing the dynamic_cast walk by hand.
    void OnReflectionLoaded() override;

    // --- Static registry ---
    static const std::vector<MeshRenderer*>& GetAllRenderers();

private:
    static std::vector<MeshRenderer*> allRenderers_;

    // runtime 缓存
    std::shared_ptr<Mesh> mesh_;
    std::shared_ptr<Material> material_;

    // ---- spec 字段（反射序列化这一批） ----
    std::string meshKind_ = "cube";
    std::string meshPath_;
    float       meshParamF_ = 1.0f;
    int         meshParamI_ = 32;

    std::string materialShader_    = "pbr";
    glm::vec4   materialColor_     {1.0f, 1.0f, 1.0f, 1.0f};
    float       materialMetallic_  = 0.0f;
    float       materialRoughness_ = 0.5f;
    float       materialAo_        = 1.0f;
    glm::vec3   materialEmissive_  {0.0f, 0.0f, 0.0f};

    std::string materialAlbedoTex_;
    std::string materialNormalTex_;
    std::string materialMRATex_;
    std::string materialAoTex_;
    std::string materialEmissiveTex_;
};

} // namespace ark
