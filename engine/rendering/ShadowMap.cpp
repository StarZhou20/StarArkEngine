#include "ShadowMap.h"

#include "engine/debug/DebugListenBus.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
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
    // Enable hardware PCF: sampler2DShadow returns a [0,1] value computed
    // from a 2x2 bilinear depth-compare (LESS_OR_EQUAL). Each tap thus
    // already integrates 4 samples -> dramatically smoother penumbra.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
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
                             float farPlane,
                             int   resolution) {
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

    glm::mat4 lightSpace = proj * view;

    // --- Texel snap (in light-clip space, i.e. the actual basis used) -----
    // Project origin (or any reference point) into clip space, find how far
    // it is from the nearest shadow-texel center, and shift the projection
    // matrix by exactly that delta so static geometry hashes to identical
    // texels frame-to-frame even as `focusPoint` moves continuously.
    if (resolution > 0) {
        glm::vec4 originClip = lightSpace * glm::vec4(focusPoint, 1.0f);
        // Ortho projection: w == 1, no perspective divide needed.
        glm::vec2 ndc = glm::vec2(originClip) / originClip.w;
        glm::vec2 texCoord = (ndc * 0.5f + 0.5f) * float(resolution);
        glm::vec2 rounded  = glm::vec2(std::floor(texCoord.x + 0.5f),
                                       std::floor(texCoord.y + 0.5f));
        glm::vec2 deltaTex = rounded - texCoord;
        glm::vec2 deltaNDC = deltaTex / float(resolution) * 2.0f;
        glm::mat4 snap(1.0f);
        snap[3][0] = deltaNDC.x;
        snap[3][1] = deltaNDC.y;
        lightSpace = snap * lightSpace;
    }

    lightSpaceMatrix_ = lightSpace;
}

} // namespace ark
