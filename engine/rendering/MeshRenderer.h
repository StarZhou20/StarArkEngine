#pragma once

#include "engine/core/AComponent.h"
#include "Mesh.h"
#include "Material.h"
#include <memory>
#include <vector>

namespace ark {

class MeshRenderer : public AComponent {
public:
    void OnAttach() override;
    void OnDetach() override;

    // --- Mesh ---
    void SetMesh(std::shared_ptr<Mesh> mesh) { mesh_ = std::move(mesh); }
    Mesh* GetMesh() const { return mesh_.get(); }

    // --- Material ---
    void SetMaterial(std::shared_ptr<Material> mat) { material_ = std::move(mat); }
    Material* GetMaterial() const { return material_.get(); }

    // --- Static registry ---
    static const std::vector<MeshRenderer*>& GetAllRenderers();

private:
    static std::vector<MeshRenderer*> allRenderers_;

    std::shared_ptr<Mesh> mesh_;
    std::shared_ptr<Material> material_;
};

} // namespace ark
