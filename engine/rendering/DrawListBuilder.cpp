#include "DrawListBuilder.h"

#include "Camera.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"

#include <algorithm>
#include <glm/glm.hpp>

namespace ark {

void CollectOpaqueDrawList(Camera* /*camera*/,
                           std::vector<MeshRenderer*>& out,
                           bool includeTransparent) {
    out.clear();

    auto& all = MeshRenderer::GetAllRenderers();
    out.reserve(all.size());

    for (auto* r : all) {
        auto* owner = r->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !r->IsEnabled()) {
            continue;
        }
        auto* mat = r->GetMaterial();
        if (!r->GetMesh() || !mat || !mat->GetShader()) {
            continue;
        }
        if (!includeTransparent && mat->IsTransparent()) {
            continue;
        }
        out.push_back(r);
    }

    std::sort(out.begin(), out.end(),
        [](const MeshRenderer* a, const MeshRenderer* b) {
            auto* sa = a->GetMaterial()->GetShader();
            auto* sb = b->GetMaterial()->GetShader();
            if (sa != sb) return sa < sb;
            return a->GetMaterial() < b->GetMaterial();
        });
}

void CollectTransparentDrawList(Camera* camera,
                                std::vector<MeshRenderer*>& out) {
    out.clear();
    if (!camera) return;

    auto& all = MeshRenderer::GetAllRenderers();
    out.reserve(all.size());

    for (auto* r : all) {
        auto* owner = r->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !r->IsEnabled()) continue;
        auto* mat = r->GetMaterial();
        if (!r->GetMesh() || !mat || !mat->GetShader()) continue;
        if (!mat->IsTransparent()) continue;
        out.push_back(r);
    }

    // Back-to-front by camera distance for alpha blending.
    glm::mat4 invView = glm::inverse(camera->GetViewMatrix());
    glm::vec3 camPos  = glm::vec3(invView[3]);
    std::sort(out.begin(), out.end(),
        [&camPos](MeshRenderer* a, MeshRenderer* b) {
            glm::vec3 pa = const_cast<Transform&>(a->GetOwner()->GetTransform()).GetWorldPosition();
            glm::vec3 pb = const_cast<Transform&>(b->GetOwner()->GetTransform()).GetWorldPosition();
            return glm::dot(pa - camPos, pa - camPos) > glm::dot(pb - camPos, pb - camPos);
        });
}

} // namespace ark
