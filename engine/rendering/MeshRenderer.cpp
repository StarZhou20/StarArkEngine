#include "MeshRenderer.h"
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

} // namespace ark
