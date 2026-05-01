#pragma once

#include <string_view>

namespace ark {

class AObject;

class AComponent {
public:
    virtual ~AComponent() = default;

    AComponent(const AComponent&) = delete;
    AComponent& operator=(const AComponent&) = delete;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void PreInit() {}
    virtual void Init() {}
    virtual void PostInit() {}
    virtual void Loop(float dt) { (void)dt; }
    virtual void PostLoop(float dt) { (void)dt; }

    // 反射支持：反射宏会 override 为字面类名。未反射组件返回空。
    virtual std::string_view GetReflectTypeName() const { return {}; }

    // v0.3 — Hook fired by SceneDoc after all reflected fields have been
    // written into the component (SceneDoc::Load, addition, components_attached,
    // and overlay overrides). Default no-op. Components that need to translate
    // serialized spec into runtime resources (e.g. MeshRenderer building Mesh+
    // Material from mesh_kind/material_color) override this to drop the manual
    // post-Load fixup that scenes used to do by hand.
    virtual void OnReflectionLoaded() {}

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    AObject* GetOwner() const { return owner_; }

protected:
    AComponent() = default;

private:
    friend class AObject;
    AObject* owner_ = nullptr;
    bool enabled_ = true;
};

} // namespace ark
