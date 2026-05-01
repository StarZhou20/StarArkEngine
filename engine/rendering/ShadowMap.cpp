#include "ShadowMap.h"

#include "engine/debug/DebugListenBus.h"
#include "engine/rhi/RHIDevice.h"
#include "engine/rhi/RHIRenderTarget.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>

namespace ark {

ShadowMap::~ShadowMap() = default;

bool ShadowMap::Init(RHIDevice* device, int resolution) {
    if (rt_ && resolution == resolution_) {
        return true;
    }
    rt_.reset();
    resolution_ = resolution;

    if (!device) {
        ARK_LOG_ERROR("Render", "ShadowMap::Init: null RHIDevice");
        return false;
    }

    RenderTargetDesc desc;
    desc.width  = resolution;
    desc.height = resolution;
    // No color attachments — depth-only shadow map.
    desc.depth.format             = RTDepthFormat::Depth24;
    desc.depth.shadowSampler      = true;
    desc.depth.clampToBorderWhite = true;

    rt_ = device->CreateRenderTarget(desc);
    if (!rt_) {
        ARK_LOG_ERROR("Render", "ShadowMap: CreateRenderTarget returned null");
        return false;
    }

    ARK_LOG_INFO("Render", std::string("ShadowMap initialized (") +
                               std::to_string(resolution) + "x" +
                               std::to_string(resolution) + ")");
    return true;
}

bool ShadowMap::IsValid() const {
    return rt_ != nullptr;
}

uint32_t ShadowMap::GetDepthTexture() const {
    return rt_ ? rt_->GetDepthTextureHandle() : 0u;
}

void ShadowMap::Begin() {
    if (!rt_) return;
    rt_->Bind();
    rt_->Clear(/*color*/false, /*depth*/true);
    glEnable(GL_DEPTH_TEST);
}

void ShadowMap::End() {
    if (!rt_) return;
    rt_->Unbind();
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
