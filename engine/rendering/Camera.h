#pragma once

#include "engine/core/AComponent.h"
#include "engine/core/TypeInfo.h"
#include <glm/glm.hpp>
#include <vector>

namespace ark {

class Camera : public AComponent {
public:
    enum class Projection { Perspective, Orthographic };

    ARK_DECLARE_REFLECTION(Camera);

    void OnAttach() override;
    void OnDetach() override;

    // --- Projection ---
    void SetPerspective(float fovDeg, float nearClip, float farClip);
    void SetOrthographic(float size, float nearClip, float farClip);

    const glm::mat4& GetProjectionMatrix() const;
    glm::mat4 GetViewMatrix() const;

    float GetFOV() const { return fovDeg_; }
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }

    // --- Priority (higher renders later / on top) ---
    int GetPriority() const { return priority_; }
    void SetPriority(int p) { priority_ = p; }

    // --- Aspect ratio (set by engine on resize) ---
    void SetAspectRatio(float aspect);
    float GetAspectRatio() const { return aspect_; }

    // --- Clear color ---
    void SetClearColor(const glm::vec4& color) { clearColor_ = color; }
    const glm::vec4& GetClearColor() const { return clearColor_; }

    // --- Static registry ---
    static const std::vector<Camera*>& GetAllCameras();

private:
    void RecalcProjection() const;

    static std::vector<Camera*> allCameras_;

    Projection projType_ = Projection::Perspective;
    float fovDeg_ = 60.0f;
    float orthoSize_ = 5.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;
    float aspect_ = 16.0f / 9.0f;
    int priority_ = 0;
    glm::vec4 clearColor_{0.1f, 0.1f, 0.2f, 1.0f};

    mutable glm::mat4 projMatrix_{1.0f};
    mutable bool projDirty_ = true;
};

} // namespace ark
