#include "EngineBase.h"
#include "AObject.h"
#include "AScene.h"
#include "engine/platform/Input.h"
#include "engine/platform/Time.h"
#include "engine/platform/Paths.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/rendering/Camera.h"
#include "engine/scripting/ScriptHost.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

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

    // Pick the first .ico shipped under <exeDir>/icon/ as the window icon.
    // Filename is intentionally not hard-coded so the asset can be swapped
    // (CMake copies whatever .ico is under <repo>/icon/ next to the exe).
    {
        std::error_code ec;
        std::filesystem::path iconDir = Paths::GameRoot() / "icon";
        if (std::filesystem::is_directory(iconDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(iconDir, ec)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (ext == ".ico") {
                    window_->SetIconFromFile(entry.path().string());
                    break;
                }
            }
        }
    }

    rhiDevice_ = CreateOpenGLDevice();
    renderer_ = std::make_unique<ForwardRenderer>(rhiDevice_.get());
    sceneManager_ = std::make_unique<SceneManager>(this);

    // Phase 15.F: boot the C# scripting host. Stub when STARARK_ENABLE_SCRIPTING
    // is OFF; never fatal — engine keeps running without scripts on failure.
    ScriptHost::Get().Initialize(Paths::GameRoot().string());

    ARK_LOG_INFO("Core", "Engine initialized successfully");
}

void EngineBase::Shutdown() {
    ARK_LOG_INFO("Core", "StarArk Engine shutting down");
    // Scripts first so managed code can still touch live engine state.
    ScriptHost::Get().Shutdown();
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
    // FPS title refresh: average frame times over a ~0.25s window so the
    // number is readable rather than jittering every frame.
    float fpsAccumTime   = 0.0f;
    int   fpsAccumFrames = 0;

    // Fixed-timestep accumulator drives ScriptHost::OnFixedTick (Unity-style
    // FixedUpdate). Step is 1/60s; accumulator capped to avoid the
    // "spiral of death" after long stalls (e.g. a debug breakpoint).
    constexpr float kFixedDt           = 1.0f / 60.0f;
    constexpr float kMaxAccumPerFrame  = 0.25f; // ≤15 substeps per frame
    float fixedAccum = 0.0f;

    while (!window_->ShouldClose()) {
        // 1. Poll Input
        window_->PollEvents();
        Input::Update();

        // 2. Time update
        Time::Update();
        float dt = Time::DeltaTime();

        // FPS title (updated ~4x/sec)
        fpsAccumTime   += dt;
        fpsAccumFrames += 1;
        if (fpsAccumTime >= 0.25f) {
            float fps = float(fpsAccumFrames) / fpsAccumTime;
            float ms  = fpsAccumTime * 1000.0f / float(fpsAccumFrames);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s  |  %.0f FPS  (%.2f ms)",
                          window_->GetBaseTitle().c_str(), fps, ms);
            window_->SetTitle(buf);
            fpsAccumTime   = 0.0f;
            fpsAccumFrames = 0;
        }

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

        // 4.5a. MOD FixedLoop — drain the fixed-timestep accumulator.
        // Same semantics as Unity FixedUpdate: zero or more calls per frame
        // at exactly kFixedDt seconds each. Runs before MOD Loop so MODs can
        // see physics-style state ahead of game logic.
        fixedAccum += dt;
        if (fixedAccum > kMaxAccumPerFrame) fixedAccum = kMaxAccumPerFrame;
        while (fixedAccum >= kFixedDt) {
            ScriptHost::Get().OnFixedTick(kFixedDt);
            fixedAccum -= kFixedDt;
        }

        // 4.5b. MOD Loop — runs before native StepTick so MOD scripts can
        // observe the previous frame's state plus objects spawned during
        // DrainPendingObjects, and influence what runs this frame.
        ScriptHost::Get().OnTick(dt);

        // 5-6. Loop
        StepTick(dt);

        // 7-8. PostLoop
        StepPostTick(dt);

        // 8.5. MOD PostLoop — after native PostLoop so MOD scripts see the
        // fully-updated world before transforms/render.
        ScriptHost::Get().OnPostTick(dt);

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
