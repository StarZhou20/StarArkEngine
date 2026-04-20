#include "AObject.h"
#include "engine/debug/DebugListenBus.h"

namespace ark {

uint64_t AObject::nextId_ = 1;

AObject::AObject()
    : id_(nextId_++), name_("AObject"), transform_(this)
{
}

AObject::~AObject() {
    // Unified cleanup: OnDetach all components before they destruct
    for (auto& comp : components_) {
        comp->OnDetach();
    }
}

void AObject::SetActive(bool active) {
    selfActive_ = active;
    PropagateActiveInHierarchy();
}

void AObject::PropagateActiveInHierarchy() {
    bool parentActive = true;
    if (transform_.GetParent()) {
        parentActive = transform_.GetParent()->GetOwner()->activeInHierarchy_;
    }
    activeInHierarchy_ = parentActive && selfActive_;

    // Recurse children
    for (auto* childTransform : transform_.GetChildren()) {
        childTransform->GetOwner()->PropagateActiveInHierarchy();
    }
}

void AObject::Destroy() {
    if (isDestroyed_) return;  // Idempotent
    isDestroyed_ = true;
    OnDestroy();

    // Notify owner for dirty flag optimization
    if (owner_) {
        owner_->NotifyObjectDestroyed();
    }

    // Cascade to all children
    for (auto* childTransform : transform_.GetChildren()) {
        childTransform->GetOwner()->Destroy();
    }
}

void AObject::SetDontDestroy(bool dontDestroy) {
    if (dontDestroy_ && !dontDestroy) {
        ARK_LOG_WARN("Core", "Cannot transfer persistent object back to scene");
        return;
    }
    if (dontDestroy && !dontDestroy_) {
        dontDestroy_ = true;
        // Detach from parent if parent is in a different owner
        if (transform_.GetParent()) {
            transform_.SetParent(nullptr);
        }
        if (owner_) {
            owner_->TransferToPersistent(this);
        }
    }
}

void AObject::LoopComponents(float dt) {
    for (auto& comp : components_) {
        if (comp->IsEnabled()) {
            comp->Loop(dt);
        }
    }
}

void AObject::PostLoopComponents(float dt) {
    for (auto& comp : components_) {
        if (comp->IsEnabled()) {
            comp->PostLoop(dt);
        }
    }
}

} // namespace ark
