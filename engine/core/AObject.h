#pragma once

#include "Transform.h"
#include "AComponent.h"
#include "IObjectOwner.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <type_traits>

namespace ark {

class AObject {
public:
    AObject();
    virtual ~AObject();

    AObject(const AObject&) = delete;
    AObject& operator=(const AObject&) = delete;

    // --- Lifecycle (overridable) ---
    virtual void PreInit() {}
    virtual void Init() {}
    virtual void PostInit() {}
    virtual void Loop(float dt) { (void)dt; }
    virtual void PostLoop(float dt) { (void)dt; }
    virtual void OnDestroy() {}

    // --- Identity ---
    uint64_t GetId() const { return id_; }
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }

    // --- Transform (built-in, mandatory) ---
    Transform& GetTransform() { return transform_; }
    const Transform& GetTransform() const { return transform_; }

    // --- Active state ---
    void SetActive(bool active);
    bool IsSelfActive() const { return selfActive_; }
    bool IsActiveInHierarchy() const { return activeInHierarchy_; }
    void PropagateActiveInHierarchy();

    // --- Destroy ---
    void Destroy();
    bool IsDestroyed() const { return isDestroyed_; }

    // --- DontDestroy ---
    void SetDontDestroy(bool dontDestroy);
    bool IsDontDestroy() const { return dontDestroy_; }

    // --- Component system ---
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<AComponent, T>, "T must derive from AComponent");
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = comp.get();
        raw->owner_ = this;
        components_.push_back(std::move(comp));
        raw->OnAttach();
        raw->PreInit();
        raw->Init();
        raw->PostInit();
        return raw;
    }

    template<typename T>
    T* GetComponent() const {
        for (auto& comp : components_) {
            T* casted = dynamic_cast<T*>(comp.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    template<typename T>
    void RemoveComponent() {
        for (auto it = components_.begin(); it != components_.end(); ++it) {
            T* casted = dynamic_cast<T*>(it->get());
            if (casted) {
                casted->OnDetach();
                components_.erase(it);
                return;
            }
        }
    }

    const std::vector<std::unique_ptr<AComponent>>& GetComponents() const { return components_; }

    // --- Ownership ---
    IObjectOwner* GetOwnerInterface() const { return owner_; }
    void SetOwnerInterface(IObjectOwner* owner) { owner_ = owner; }

    // --- Internal: loop components ---
    void LoopComponents(float dt);
    void PostLoopComponents(float dt);

private:
    static uint64_t nextId_;

    uint64_t id_;
    std::string name_;
    Transform transform_;
    std::vector<std::unique_ptr<AComponent>> components_;

    IObjectOwner* owner_ = nullptr;
    bool selfActive_ = true;
    bool activeInHierarchy_ = true;
    bool isDestroyed_ = false;
    bool dontDestroy_ = false;
};

} // namespace ark
