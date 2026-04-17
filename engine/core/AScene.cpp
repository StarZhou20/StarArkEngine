#include "AScene.h"
#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"
#include <algorithm>

namespace ark {

AScene::AScene() = default;

AScene::~AScene() {
    // pendingList_ objects are directly destructed (no Init/Destroy lifecycle)
    pendingList_.clear();
    // objectList_ objects destruct via unique_ptr → AObject destructor handles OnDetach
    objectList_.clear();
}

void AScene::TransferToPersistent(AObject* obj) {
    if (!engine_) {
        ARK_LOG_ERROR("Core", "AScene::TransferToPersistent called but no engine reference");
        return;
    }

    // Search ObjectList first
    for (auto it = objectList_.begin(); it != objectList_.end(); ++it) {
        if (it->get() == obj) {
            auto ptr = std::move(*it);
            objectList_.erase(it);
            engine_->AcceptPersistentObject(std::move(ptr), false);
            return;
        }
    }

    // Search PendingList
    for (auto it = pendingList_.begin(); it != pendingList_.end(); ++it) {
        if (it->get() == obj) {
            auto ptr = std::move(*it);
            pendingList_.erase(it);
            engine_->AcceptPersistentObject(std::move(ptr), true);
            return;
        }
    }

    ARK_LOG_WARN("Core", "TransferToPersistent: object not found in scene");
}

} // namespace ark
