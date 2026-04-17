#pragma once

#include "AScene.h"
#include <memory>
#include <functional>

namespace ark {

class EngineBase;

class SceneManager {
public:
    explicit SceneManager(EngineBase* engine);
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    template<typename T>
    void LoadScene() {
        static_assert(std::is_base_of_v<AScene, T>, "T must derive from AScene");
        pendingSceneFactory_ = [this]() -> std::unique_ptr<AScene> {
            auto scene = std::make_unique<T>();
            scene->SetEngine(engine_);
            return scene;
        };
        hasPendingSwitch_ = true;
    }

    // Immediate load (used for first scene at startup)
    template<typename T>
    void LoadSceneImmediate() {
        static_assert(std::is_base_of_v<AScene, T>, "T must derive from AScene");
        auto scene = std::make_unique<T>();
        scene->SetEngine(engine_);
        activeScene_ = std::move(scene);
        activeScene_->OnLoad();
    }

    AScene* GetActiveScene() const { return activeScene_.get(); }
    bool HasPendingSwitch() const { return hasPendingSwitch_; }

    // Called by EngineBase at end of frame (step 13)
    void ProcessPendingSwitch();

private:
    EngineBase* engine_;
    std::unique_ptr<AScene> activeScene_;
    std::function<std::unique_ptr<AScene>()> pendingSceneFactory_;
    bool hasPendingSwitch_ = false;
};

} // namespace ark
