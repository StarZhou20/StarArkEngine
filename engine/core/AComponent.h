#pragma once

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
