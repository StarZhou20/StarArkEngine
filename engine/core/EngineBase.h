#pragma once

#include "IObjectOwner.h"
#include "SceneManager.h"
#include "engine/platform/Window.h"
#include "engine/rhi/RHIDevice.h"
#include "engine/rendering/ForwardRenderer.h"
#include <vector>
#include <memory>

namespace ark {

class EngineBase : public IObjectOwner {
public:
    static EngineBase& Get();

    template<typename FirstScene>
    void Run(int width = 1280, int height = 720, const std::string& title = "StarArk Engine") {
        Initialize(width, height, title);
        sceneManager_->LoadSceneImmediate<FirstScene>();
        DrainPendingObjects();
        MainLoop();
        Shutdown();
    }

    // --- IObjectOwner ---
    void TransferToPersistent(AObject* obj) override;
    void NotifyObjectDestroyed() override { hasDestroyedPersistent_ = true; }

    // --- Accessors ---
    SceneManager* GetSceneManager() const { return sceneManager_.get(); }
    Window* GetWindow() const { return window_.get(); }
    RHIDevice* GetRHIDevice() const { return rhiDevice_.get(); }
    ForwardRenderer* GetRenderer() const { return renderer_.get(); }

    // --- Internal ---
    void AcceptPersistentObject(std::unique_ptr<AObject> obj, bool isPending);
    void DrainPendingObjects();

private:
    EngineBase() = default;
    ~EngineBase() = default;

    EngineBase(const EngineBase&) = delete;
    EngineBase& operator=(const EngineBase&) = delete;

    void Initialize(int width, int height, const std::string& title);
    void Shutdown();
    void MainLoop();

    void StepInitPending(float dt);
    void StepTick(float dt);
    void StepPostTick(float dt);
    void StepUpdateTransforms();
    void StepDestroyObjects();

    std::unique_ptr<Window> window_;
    std::unique_ptr<RHIDevice> rhiDevice_;
    std::unique_ptr<ForwardRenderer> renderer_;
    std::unique_ptr<SceneManager> sceneManager_;

    // Persistent objects (DontDestroy)
    std::vector<std::unique_ptr<AObject>> persistentList_;
    std::vector<std::unique_ptr<AObject>> persistentPendingList_;
    bool hasDestroyedPersistent_ = false;
};

} // namespace ark
