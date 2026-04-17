#include "Transform.h"
#include "engine/core/AObject.h"
#include "engine/debug/DebugListenBus.h"
#include <cassert>

namespace ark {

Transform::Transform(AObject* owner)
    : owner_(owner)
{
    assert(owner && "Transform must have an owner");
}

Transform::~Transform() {
    // Bidirectional cleanup: safe regardless of destruction order
    if (parent_) {
        parent_->RemoveChild(this);
    }
    for (auto* child : children_) {
        child->parent_ = nullptr;
    }
    children_.clear();
}

void Transform::SetLocalPosition(const glm::vec3& pos) {
    localPosition_ = pos;
    MarkDirty();
}

void Transform::SetLocalRotation(const glm::quat& rot) {
    localRotation_ = rot;
    MarkDirty();
}

void Transform::SetLocalScale(const glm::vec3& scale) {
    localScale_ = scale;
    MarkDirty();
}

glm::vec3 Transform::GetWorldPosition() const {
    if (parent_) {
        return glm::vec3(const_cast<Transform*>(this)->GetWorldMatrix()[3]);
    }
    return localPosition_;
}

const glm::mat4& Transform::GetWorldMatrix() {
    if (dirty_) {
        UpdateWorldMatrix();
    }
    return worldMatrix_;
}

void Transform::SetParent(Transform* newParent) {
    if (newParent == parent_) return;
    if (newParent == this) return;

    // Cross-boundary parent-child forbidden
    if (newParent) {
        assert(owner_->GetOwnerInterface() == newParent->owner_->GetOwnerInterface()
               && "Cross-boundary parent-child is forbidden");
    }

    // Detach from old parent
    if (parent_) {
        parent_->RemoveChild(this);
    }

    parent_ = newParent;

    // Attach to new parent
    if (parent_) {
        parent_->children_.push_back(this);
    }

    MarkDirty();

    // Propagate activeInHierarchy for moved subtree
    owner_->PropagateActiveInHierarchy();
}

void Transform::AddChild(Transform* child) {
    assert(child && "Cannot add null child");
    child->SetParent(this);
}

void Transform::RemoveChild(Transform* child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        children_.erase(it);
    }
}

void Transform::MarkDirty() {
    if (dirty_) return;
    dirty_ = true;
    for (auto* child : children_) {
        child->MarkDirty();
    }
}

void Transform::UpdateWorldMatrix() {
    RecalculateLocalMatrix();
    if (parent_) {
        worldMatrix_ = parent_->GetWorldMatrix() * localMatrix_;
    } else {
        worldMatrix_ = localMatrix_;
    }
    dirty_ = false;
    for (auto* child : children_) {
        if (child->dirty_) {
            child->UpdateWorldMatrix();
        }
    }
}

void Transform::RecalculateLocalMatrix() {
    localMatrix_ = glm::translate(glm::mat4(1.0f), localPosition_)
                 * glm::mat4_cast(localRotation_)
                 * glm::scale(glm::mat4(1.0f), localScale_);
}

} // namespace ark
