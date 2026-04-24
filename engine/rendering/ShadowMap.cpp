#include "ShadowMap.h"

#include "engine/debug/DebugListenBus.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace ark {

ShadowMap::~ShadowMap() {
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    if (fbo_)      glDeleteFramebuffers(1, &fbo_);
}

bool ShadowMap::Init(int resolution) {
    if (fbo_ != 0 && resolution == resolution_) {
        return true;
    }

    // (Re)create resources at new resolution.
    if (depthTex_) { glDeleteTextures(1, &depthTex_); depthTex_ = 0; }
    if (fbo_)      { glDeleteFramebuffers(1, &fbo_);  fbo_ = 0; }

    resolution_ = resolution;

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 resolution, resolution, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // Samples outside the map are fully lit (depth = 1.0).
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex_, 0);
    // No color attachment on a shadow-only FBO.
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", std::string("ShadowMap FBO incomplete: 0x") +
                                std::to_string(status));
        return false;
    }

    ARK_LOG_INFO("Render", std::string("ShadowMap initialized (") +
                               std::to_string(resolution) + "x" +
                               std::to_string(resolution) + ")");
    return true;
}

void ShadowMap::Begin() {
    // Save current state.
    glGetIntegerv(GL_VIEWPORT, prevViewport_);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo_);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, resolution_, resolution_);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Front-face culling while rendering the shadow map helps combat
    // "peter-panning"/self-shadow acne on closed meshes.
    glEnable(GL_DEPTH_TEST);
}

void ShadowMap::End() {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    glViewport(prevViewport_[0], prevViewport_[1],
               prevViewport_[2], prevViewport_[3]);
}

void ShadowMap::UpdateMatrix(const glm::vec3& lightDirWorld,
                             const glm::vec3& focusPoint,
                             float orthoHalfSize,
                             float nearPlane,
                             float farPlane) {
    // Place the light "camera" at a distance opposite to the light direction
    // from the focus point, along the light ray.
    const float d = 0.5f * (nearPlane + farPlane);
    glm::vec3 dir = glm::normalize(lightDirWorld);
    glm::vec3 eye = focusPoint - dir * d;

    // Pick a stable up vector that isn't parallel to the light direction.
    glm::vec3 up = (glm::abs(dir.y) > 0.95f) ? glm::vec3(0, 0, 1)
                                             : glm::vec3(0, 1, 0);

    glm::mat4 view = glm::lookAt(eye, focusPoint, up);
    glm::mat4 proj = glm::ortho(-orthoHalfSize, orthoHalfSize,
                                -orthoHalfSize, orthoHalfSize,
                                nearPlane, farPlane);
    lightSpaceMatrix_ = proj * view;
}

} // namespace ark
