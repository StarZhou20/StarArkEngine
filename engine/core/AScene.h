#pragma once

#include "IObjectOwner.h"
#include "AObject.h"
#include <vector>
#include <memory>
#include <string>
#include <type_traits>

namespace ark {

class EngineBase;

class AScene : public IObjectOwner {
public:
    AScene();
    virtual ~AScene();

    AScene(const AScene&) = delete;
    AScene& operator=(const AScene&) = delete;

    // --- Scene name ---
    const std::string& GetSceneName() const { return sceneName_; }
    void SetSceneName(const std::string& name) { sceneName_ = name; }

    // --- Scene lifecycle (overridable) ---
    virtual void OnLoad() {}
    virtual void OnUnload() {}
    virtual void Tick(float dt) { (void)dt; }

    // --- Object creation ---
    template<typename T, typename... Args>
    T* CreateObject(Args&&... args) {
        static_assert(std::is_base_of_v<AObject, T>, "T must derive from AObject");
        auto obj = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = obj.get();
        raw->SetOwnerInterface(this);
        pendingList_.push_back(std::move(obj));
        return raw;
    }

    // --- IObjectOwner ---
    void TransferToPersistent(AObject* obj) override;
    void NotifyObjectDestroyed() override { hasDestroyedObjects_ = true; }

    // --- Internal (used by EngineBase) ---
    std::vector<std::unique_ptr<AObject>>& GetObjectList() { return objectList_; }
    std::vector<std::unique_ptr<AObject>>& GetPendingList() { return pendingList_; }
    bool HasDestroyedObjects() const { return hasDestroyedObjects_; }
    void ClearDestroyedFlag() { hasDestroyedObjects_ = false; }

    void SetEngine(EngineBase* engine) { engine_ = engine; }
    EngineBase* GetEngine() const { return engine_; }

    // v0.3 ModSpec §6.1 — wipe every object in this scene (both live
    // and pending). Used by F9 quickload before SceneDoc::Load applies
    // the on-disk sidecar.
    //
    // Calls Destroy() on each live object so its OnDetach / OnDestroy
    // hooks run, then drops both vectors. Persistent / dontDestroy
    // objects owned by EngineBase are NOT touched.
    void Clear();

private:
    std::string sceneName_;
    std::vector<std::unique_ptr<AObject>> objectList_;
    std::vector<std::unique_ptr<AObject>> pendingList_;
    EngineBase* engine_ = nullptr;
    bool hasDestroyedObjects_ = false;
};

} // namespace ark
