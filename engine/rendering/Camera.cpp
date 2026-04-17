#include "Camera.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cassert>

namespace ark {

std::vector<Camera*> Camera::allCameras_;

void Camera::OnAttach() {
    allCameras_.push_back(this);
}

void Camera::OnDetach() {
    allCameras_.erase(
        std::remove(allCameras_.begin(), allCameras_.end(), this),
        allCameras_.end());
}

void Camera::SetPerspective(float fovDeg, float nearClip, float farClip) {
    projType_ = Projection::Perspective;
    fovDeg_ = fovDeg;
    nearClip_ = nearClip;
    farClip_ = farClip;
    projDirty_ = true;
}

void Camera::SetOrthographic(float size, float nearClip, float farClip) {
    projType_ = Projection::Orthographic;
    orthoSize_ = size;
    nearClip_ = nearClip;
    farClip_ = farClip;
    projDirty_ = true;
}

void Camera::SetAspectRatio(float aspect) {
    aspect_ = aspect;
    projDirty_ = true;
}

const glm::mat4& Camera::GetProjectionMatrix() const {
    if (projDirty_) {
        RecalcProjection();
    }
    return projMatrix_;
}

glm::mat4 Camera::GetViewMatrix() const {
    assert(GetOwner() && "Camera must be attached to an AObject");
    // View matrix = inverse of camera's world transform
    return glm::inverse(const_cast<Transform&>(GetOwner()->GetTransform()).GetWorldMatrix());
}

void Camera::RecalcProjection() const {
    if (projType_ == Projection::Perspective) {
        projMatrix_ = glm::perspective(glm::radians(fovDeg_), aspect_, nearClip_, farClip_);
    } else {
        float halfH = orthoSize_;
        float halfW = halfH * aspect_;
        projMatrix_ = glm::ortho(-halfW, halfW, -halfH, halfH, nearClip_, farClip_);
    }
    projDirty_ = false;
}

const std::vector<Camera*>& Camera::GetAllCameras() {
    return allCameras_;
}

} // namespace ark
