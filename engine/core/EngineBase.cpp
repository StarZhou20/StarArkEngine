#include "EngineBase.h"
#include "AObject.h"
#include "AScene.h"
#include "engine/platform/Input.h"
#include "engine/platform/Time.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/rendering/Camera.h"
#include <algorithm>

namespace ark {

EngineBase& EngineBase::Get() {
    static EngineBase instance;
    return instance;
}

void EngineBase::Initialize(int width, int height, const std::string& title) {
    ARK_LOG_INFO("Core", "StarArk Engine initializing...");

    window_ = std::make_unique<Window>(width, height, title);
    Input::Init(window_->GetNativeHandle());
    Time::Init();

    rhiDevice_ = CreateOpenGLDevice();
    renderer_ = std::make_unique<ForwardRenderer>(rhiDevice_.get());
    sceneManager_ = std::make_unique<SceneManager>(this);

    ARK_LOG_INFO("Core", "Engine initialized successfully");
}

void EngineBase::Shutdown() {
    ARK_LOG_INFO("Core", "StarArk Engine shutting down");
    // Scene is destroyed first
    sceneManager_.reset();
    // Then persistent objects
    persistentPendingList_.clear();
    persistentList_.clear();
    // Then renderer, RHI and window
    renderer_.reset();
    rhiDevice_.reset();
    window_.reset();
}

void EngineBase::MainLoop() {
    while (!window_->ShouldClose()) {
        // 1. Poll Input
        window_->PollEvents();
        Input::Update();

        // 2. Time update
        Time::Update();
        float dt = Time::DeltaTime();

        // 3. Check window resize
        if (window_->WasResized()) {
            window_->ResetResizeFlag();
            // Update camera aspect ratios
            float aspect = static_cast<float>(window_->GetWidth()) / static_cast<float>(window_->GetHeight());
            for (auto* cam : Camera::GetAllCameras()) {
                cam->SetAspectRatio(aspect);
            }
        }

        // 4. PreInit/Init/PostInit new objects
        DrainPendingObjects();

        // 5-6. Loop
        StepTick(dt);

        // 7-8. PostLoop
        StepPostTick(dt);

        // 9. Scene-level Tick
        AScene* scene = sceneManager_->GetActiveScene();
        if (scene) {
            scene->Tick(dt);
        }

        // 10. Update transforms
        StepUpdateTransforms();

        // 11. Render — ForwardRenderer iterates cameras, collects MeshRenderers, draws
        renderer_->RenderFrame(window_.get());

        // 12. Destroy objects
        StepDestroyObjects();

        // 13. Process pending scene switch
        if (sceneManager_->HasPendingSwitch()) {
            sceneManager_->ProcessPendingSwitch();
        }

        // 14. Swap buffers
        window_->SwapBuffers();
    }
}

void EngineBase::DrainPendingObjects() {
    AScene* scene = sceneManager_->GetActiveScene();

    // Drain loop: keep processing until both pending lists are empty
    bool hasPending = true;
    while (hasPending) {
        hasPending = false;

        // Scene pending
        if (scene && !scene->GetPendingList().empty()) {
            hasPending = true;
            auto batch = std::move(scene->GetPendingList());
            scene->GetPendingList().clear();

            for (auto& obj : batch) {
                if (obj->IsDestroyed()) {
                    // Skip: destructor handles cleanup
                    continue;
                }
                AObject* raw = obj.get();
                scene->GetObjectList().push_back(std::move(obj));
                raw->PreInit();
                raw->Init();
                raw->PostInit();
            }
        }

        // Engine persistent pending
        if (!persistentPendingList_.empty()) {
            hasPending = true;
            auto batch = std::move(persistentPendingList_);
            persistentPendingList_.clear();

            for (auto& obj : batch) {
                if (obj->IsDestroyed()) {
                    continue;
                }
                AObject* raw = obj.get();
                persistentList_.push_back(std::move(obj));
                raw->PreInit();
                raw->Init();
                raw->PostInit();
            }
        }
    }
}

void EngineBase::StepTick(float dt) {
    // 5. Loop persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
            obj->Loop(dt);
            obj->LoopComponents(dt);
        }
    }

    // 6. Loop scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
                obj->Loop(dt);
                obj->LoopComponents(dt);
            }
        }
    }
}

void EngineBase::StepPostTick(float dt) {
    // 7. PostLoop persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
            obj->PostLoop(dt);
            obj->PostLoopComponents(dt);
        }
    }

    // 8. PostLoop scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
                obj->PostLoop(dt);
                obj->PostLoopComponents(dt);
            }
        }
    }
}

void EngineBase::StepUpdateTransforms() {
    // Update dirty transforms for persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->GetTransform().GetParent() == nullptr) {
            if (obj->GetTransform().IsDirty()) {
                obj->GetTransform().UpdateWorldMatrix();
            }
        }
    }

    // Update dirty transforms for scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->GetTransform().GetParent() == nullptr) {
                if (obj->GetTransform().IsDirty()) {
                    obj->GetTransform().UpdateWorldMatrix();
                }
            }
        }
    }
}

void EngineBase::StepDestroyObjects() {
    // 12. Scan and remove destroyed objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene && scene->HasDestroyedObjects()) {
        auto& list = scene->GetObjectList();
        list.erase(
            std::remove_if(list.begin(), list.end(),
                [](const std::unique_ptr<AObject>& obj) { return obj->IsDestroyed(); }),
            list.end());
        scene->ClearDestroyedFlag();
    }

    if (hasDestroyedPersistent_) {
        persistentList_.erase(
            std::remove_if(persistentList_.begin(), persistentList_.end(),
                [](const std::unique_ptr<AObject>& obj) { return obj->IsDestroyed(); }),
            persistentList_.end());
        hasDestroyedPersistent_ = false;
    }
}

void EngineBase::AcceptPersistentObject(std::unique_ptr<AObject> obj, bool isPending) {
    obj->SetOwnerInterface(this);
    if (isPending) {
        persistentPendingList_.push_back(std::move(obj));
    } else {
        persistentList_.push_back(std::move(obj));
    }
}

void EngineBase::TransferToPersistent(AObject* obj) {
    // Already persistent — no-op
    (void)obj;
}

} // namespace ark
