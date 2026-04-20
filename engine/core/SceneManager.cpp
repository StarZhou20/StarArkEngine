#include "SceneManager.h"
#include "engine/core/EngineBase.h"
#include "engine/debug/DebugListenBus.h"

namespace ark {

SceneManager::SceneManager(EngineBase* engine)
    : engine_(engine)
{
}

void SceneManager::ProcessPendingSwitch() {
    if (!hasPendingSwitch_) return;
    hasPendingSwitch_ = false;

    // 1. Unload current scene
    if (activeScene_) {
        activeScene_->OnUnload();
        activeScene_.reset(); // Destructor clears ObjectList + PendingList
    }

    // 2. Create new scene
    if (pendingSceneFactory_) {
        activeScene_ = pendingSceneFactory_();
        pendingSceneFactory_ = nullptr;

        ARK_LOG_INFO("Core", "Scene loaded: " + activeScene_->GetSceneName());

        // 3. OnLoad creates initial objects
        activeScene_->OnLoad();

        // 4. Drain pending list (Init/PostInit)
        engine_->DrainPendingObjects();
    }
}

} // namespace ark
