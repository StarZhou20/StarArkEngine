#include "DeferredRenderer.h"

#include "Camera.h"
#include "DrawListBuilder.h"
#include "IBL.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "RenderSettings.h"
#include "ShaderManager.h"
#include "ShadowMap.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/rhi/RHICommandBuffer.h"
#include "engine/rhi/RHIDevice.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include <string>
#include <vector>

namespace ark {

DeferredRenderer::DeferredRenderer(RHIDevice* device, ShaderManager* shaderManager)
    : device_(device)
    , shaderManager_(shaderManager)
    , cmdBuffer_(device ? device->CreateCommandBuffer() : nullptr) {}

DeferredRenderer::~DeferredRenderer() {
    ReleaseTargets();
    if (fullscreenVAO_) {
        glDeleteVertexArrays(1, &fullscreenVAO_);
        fullscreenVAO_ = 0;
    }
    if (blitTmpFBO_) {
        glDeleteFramebuffers(1, &blitTmpFBO_);
        blitTmpFBO_ = 0;
    }
}

bool DeferredRenderer::Initialize(int width, int height) {
    if (!device_) {
        ARK_LOG_ERROR("Render", "DeferredRenderer::Initialize: null device");
        return false;
    }
    if (initialized_ && width == width_ && height == height_) return true;
    if (!AllocateTargets(width, height)) return false;

    initialized_ = true;
    ARK_LOG_INFO("Render",
        "DeferredRenderer initialized (" + std::to_string(width) + "x" +
        std::to_string(height) + ", 4-RT G-buffer + RGBA16F lit)");
    return true;
}

void DeferredRenderer::SetFrameContext(const RenderSettings* settings,
                                       ShadowMap* shadowMap, bool shadowEnabledThisFrame,
                                       IBL* ibl, bool iblBaked) {
    settings_                = settings;
    shadowMap_               = shadowMap;
    shadowEnabledThisFrame_  = shadowEnabledThisFrame;
    ibl_                     = ibl;
    iblBaked_                = iblBaked;
}

void DeferredRenderer::BlitGBufferDepthToLit() {
    if (!gbuffer_ || !lit_) return;

    GLint prevReadFBO = 0, prevDrawFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFBO);

    // Source: cached temp FBO with gbuffer's depth texture attached.
    // Lazily created (single allocation per renderer instance).
    if (!blitTmpFBO_) {
        glGenFramebuffers(1, &blitTmpFBO_);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, blitTmpFBO_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, gbuffer_->GetDepthTextureHandle(), 0);

    // Destination: lit_'s FBO. lit_->Bind() sets it as both READ+DRAW;
    // we override READ to our temp FBO immediately after.
    lit_->Bind();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, blitTmpFBO_);
    glBlitFramebuffer(0, 0, width_, height_,
                      0, 0, width_, height_,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    lit_->Unbind();  // restores prev FBO bindings

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFBO));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFBO));
}

bool DeferredRenderer::Resize(int width, int height) {
    if (width == width_ && height == height_) return true;
    if (width <= 0 || height <= 0) return false;
    if (!AllocateTargets(width, height)) return false;
    initialized_ = true;
    return true;
}

bool DeferredRenderer::Render(Camera* camera, int backBufferWidth, int backBufferHeight) {
    if (!device_ || !shaderManager_ || !camera) return false;

    // Lazily resolve shaders (cached for the rest of the run; ShaderManager
    // returns the same shared_ptr each call so hot reload still updates
    // the underlying program in-place).
    if (!gbufferShader_) {
        gbufferShader_ = shaderManager_->Get("gbuffer");
        if (!gbufferShader_) {
            static bool warned = false;
            if (!warned) {
                ARK_LOG_ERROR("Render",
                    "DeferredRenderer: failed to load 'gbuffer' shader; "
                    "deferred path is unusable, caller should fall back");
                warned = true;
            }
            return false;
        }
    }
    if (!lightingShader_) {
        lightingShader_ = shaderManager_->Get("lighting");
        if (!lightingShader_) {
            static bool warned = false;
            if (!warned) {
                ARK_LOG_ERROR("Render",
                    "DeferredRenderer: failed to load 'lighting' shader; "
                    "deferred path is unusable, caller should fall back");
                warned = true;
            }
            return false;
        }
    }

    // (Re)allocate RTs to match the backbuffer size on the first frame
    // or after a window resize.
    if (!initialized_ || width_ != backBufferWidth || height_ != backBufferHeight) {
        if (!Initialize(backBufferWidth, backBufferHeight)) return false;
    }

    // --- 1. G-buffer geometry pass ---------------------------------------
    cmdBuffer_->Begin();
    cmdBuffer_->SetRenderTarget(gbuffer_.get());
    cmdBuffer_->SetViewport(0, 0, width_, height_);
    // Clear all 4 color attachments to (0,0,0,0). Lighting pass treats
    // alpha=0 as "no geometry" and substitutes sky.
    auto& cc = camera->GetClearColor();
    cmdBuffer_->Clear(cc.r, cc.g, cc.b, 0.0f);
    cmdBuffer_->End();
    cmdBuffer_->Submit();

    // Gather visible opaque MeshRenderers (transparency: forward overlay,
    // not yet implemented).
    std::vector<MeshRenderer*> visible;
    CollectOpaqueDrawList(camera, visible, /*includeTransparent=*/false);

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix();

    for (auto* renderer : visible) {
        auto* mesh     = renderer->GetMesh();
        auto* material = renderer->GetMaterial();
        auto* owner    = renderer->GetOwner();
        if (!mesh || !material || !owner) continue;

        PipelineDesc desc;
        desc.shader       = gbufferShader_.get();
        desc.vertexLayout = mesh->GetVertexLayout();
        desc.topology     = PrimitiveTopology::Triangles;
        desc.depthTest    = true;
        desc.depthWrite   = true;

        auto* pipeline = GetOrCreatePipeline(desc);

        cmdBuffer_->Begin();

        glm::mat4 model = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
        glm::mat4 mvp   = proj * view * model;
        glm::mat3 nrm   = glm::transpose(glm::inverse(glm::mat3(model)));

        pipeline->Bind();
        gbufferShader_->SetUniformMat4("uModel", glm::value_ptr(model));
        gbufferShader_->SetUniformMat4("uMVP",   glm::value_ptr(mvp));
        gbufferShader_->SetUniformMat4("uNormalMatrix",
                                       glm::value_ptr(glm::mat4(nrm)));

        // Per-material uniforms + texture bindings on the gbuffer shader.
        material->BindToShader(gbufferShader_.get());

        cmdBuffer_->BindPipeline(pipeline);
        cmdBuffer_->BindVertexBuffer(mesh->GetVertexBuffer());
        if (mesh->HasIndices()) {
            cmdBuffer_->BindIndexBuffer(mesh->GetIndexBuffer());
            cmdBuffer_->DrawIndexed(mesh->GetIndexCount());
        } else {
            cmdBuffer_->Draw(mesh->GetVertexCount());
        }

        cmdBuffer_->End();
        cmdBuffer_->Submit();
    }

    // --- 2. Fullscreen lighting pass: G-buffer → lit_ -------------------
    // Stage C: directional + point + spot lights only. Tone mapping is
    // inline so a plain blit can present lit_ to the backbuffer; stage D
    // will route lit_ through PostProcess + plug shadow + IBL.

    // Lazy-create the no-attribute VAO. The fullscreen-VS uses gl_VertexID
    // to synthesise positions, but GL still requires *some* VAO bound.
    if (!fullscreenVAO_) {
        glGenVertexArrays(1, &fullscreenVAO_);
    }

    // Bind lit_ FBO + viewport (RT bind/unbind go through RHIRenderTarget;
    // the actual draw is raw GL because PSO + cmdBuffer don't yet expose
    // a fullscreen-no-VBO primitive — same pattern as PostProcess uses).
    lit_->Bind();
    lit_->Clear(/*color=*/true, /*depth=*/false, 0, 0, 0, 1);

    GLint prevProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);

    // The lighting pass writes a fullscreen quad: depth test would reject
    // most fragments because lit_ has no depth buffer of its own anyway.
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    GLboolean prevDepthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glDepthMask(GL_FALSE);

    glUseProgram(0);  // RHIShader::SetUniform* binds the program internally

    // Bind G-buffer textures + depth to units 0..4
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gbuffer_->GetColorTextureHandle(0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gbuffer_->GetColorTextureHandle(1));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gbuffer_->GetColorTextureHandle(2));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gbuffer_->GetColorTextureHandle(3));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, gbuffer_->GetDepthTextureHandle());
    glActiveTexture(GL_TEXTURE0);

    lightingShader_->SetUniformInt("uGAlbedoMetallic",  0);
    lightingShader_->SetUniformInt("uGNormalRoughness", 1);
    lightingShader_->SetUniformInt("uGEmissiveMotion",  2);
    lightingShader_->SetUniformInt("uGAOFlags",         3);
    lightingShader_->SetUniformInt("uGDepth",           4);

    glm::mat4 invViewProj = glm::inverse(proj * view);
    glm::vec3 camPos = camera->GetOwner()->GetTransform().GetWorldPosition();
    lightingShader_->SetUniformMat4("uInvViewProj", glm::value_ptr(invViewProj));
    lightingShader_->SetUniformVec3("uCameraPos",   glm::value_ptr(camPos));
    float exposure = settings_ ? settings_->exposure : 1.30f;
    lightingShader_->SetUniformFloat("uExposure",   exposure);
    // Stage E: write linear HDR into lit_ so PostProcess can run
    // bloom/exposure/ACES/FXAA against it. The caller is responsible for
    // routing lit_ through PostProcess::IngestHDRColor + Apply.
    lightingShader_->SetUniformInt("uApplyToneMap", 0);

    // --- IBL bindings (units 5/6/7) -------------------------------------
    const bool iblUse = settings_ && settings_->ibl.enabled && iblBaked_ &&
                        ibl_ && ibl_->IsValid();
    lightingShader_->SetUniformInt("uIBLEnabled", iblUse ? 1 : 0);
    if (iblUse) {
        lightingShader_->SetUniformFloat("uIBLDiffuseIntensity",  settings_->ibl.diffuseIntensity);
        lightingShader_->SetUniformFloat("uIBLSpecularIntensity", settings_->ibl.specularIntensity);
        lightingShader_->SetUniformFloat("uPrefilterMaxLod",
            float(ibl_->GetPrefilterMipLevels() > 0 ? ibl_->GetPrefilterMipLevels() - 1 : 0));
        lightingShader_->SetUniformInt("uIrradianceMap", 5);
        lightingShader_->SetUniformInt("uPrefilterMap",  6);
        lightingShader_->SetUniformInt("uBrdfLUT",       7);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_->GetIrradianceMap());
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_->GetPrefilterMap());
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, ibl_->GetBrdfLUT());
        glActiveTexture(GL_TEXTURE0);
    }

    // --- Shadow map (unit 8) --------------------------------------------
    const bool shadowUse = settings_ && shadowEnabledThisFrame_ &&
                           shadowMap_ && shadowMap_->IsValid();
    lightingShader_->SetUniformInt("uShadowEnabled", shadowUse ? 1 : 0);
    if (shadowUse) {
        lightingShader_->SetUniformMat4("uLightSpaceMatrix",
            glm::value_ptr(shadowMap_->GetLightSpaceMatrix()));
        lightingShader_->SetUniformFloat("uShadowDepthBias",  settings_->shadow.depthBias);
        lightingShader_->SetUniformFloat("uShadowNormalBias", settings_->shadow.normalBias);
        lightingShader_->SetUniformInt  ("uShadowPcfKernel",  settings_->shadow.pcfKernel);
        lightingShader_->SetUniformFloat("uShadowTexelSize",
            1.0f / float(shadowMap_->GetResolution()));
        lightingShader_->SetUniformInt("uShadowMap", 8);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, shadowMap_->GetDepthTexture());
        glActiveTexture(GL_TEXTURE0);
    }

    // --- Light uniforms (mirrors ForwardRenderer::SetLightUniforms) ----
    constexpr int MAX_DIR   = 4;
    constexpr int MAX_POINT = 8;
    constexpr int MAX_SPOT  = 4;
    auto& allLights = Light::GetAllLights();
    int dirCount = 0, pointCount = 0, spotCount = 0;
    for (auto* light : allLights) {
        auto* lightOwner = light->GetOwner();
        if (!lightOwner || lightOwner->IsDestroyed() ||
            !lightOwner->IsActiveInHierarchy() || !light->IsEnabled())
            continue;
        glm::mat4 lightWorld =
            const_cast<Transform&>(lightOwner->GetTransform()).GetWorldMatrix();
        if (light->GetType() == Light::Type::Directional && dirCount < MAX_DIR) {
            std::string p = "uDirLights[" + std::to_string(dirCount) + "].";
            glm::vec3 fwd = -glm::normalize(glm::vec3(lightWorld[2]));
            glm::vec3 col = light->GetColor() * light->GetIntensity();
            glm::vec3 amb = light->GetAmbient();
            lightingShader_->SetUniformVec3((p + "direction").c_str(), glm::value_ptr(fwd));
            lightingShader_->SetUniformVec3((p + "color").c_str(),     glm::value_ptr(col));
            lightingShader_->SetUniformVec3((p + "ambient").c_str(),   glm::value_ptr(amb));
            ++dirCount;
        } else if (light->GetType() == Light::Type::Point && pointCount < MAX_POINT) {
            std::string p = "uPointLights[" + std::to_string(pointCount) + "].";
            glm::vec3 pos = glm::vec3(lightWorld[3]);
            glm::vec3 col = light->GetColor() * light->GetIntensity();
            lightingShader_->SetUniformVec3((p + "position").c_str(), glm::value_ptr(pos));
            lightingShader_->SetUniformVec3((p + "color").c_str(),    glm::value_ptr(col));
            lightingShader_->SetUniformFloat((p + "constant").c_str(),  light->GetConstant());
            lightingShader_->SetUniformFloat((p + "linear").c_str(),    light->GetLinear());
            lightingShader_->SetUniformFloat((p + "quadratic").c_str(), light->GetQuadratic());
            lightingShader_->SetUniformFloat((p + "range").c_str(),     light->GetRange());
            ++pointCount;
        } else if (light->GetType() == Light::Type::Spot && spotCount < MAX_SPOT) {
            std::string p = "uSpotLights[" + std::to_string(spotCount) + "].";
            glm::vec3 pos = glm::vec3(lightWorld[3]);
            glm::vec3 fwd = -glm::normalize(glm::vec3(lightWorld[2]));
            glm::vec3 col = light->GetColor() * light->GetIntensity();
            lightingShader_->SetUniformVec3((p + "position").c_str(),  glm::value_ptr(pos));
            lightingShader_->SetUniformVec3((p + "direction").c_str(), glm::value_ptr(fwd));
            lightingShader_->SetUniformVec3((p + "color").c_str(),     glm::value_ptr(col));
            lightingShader_->SetUniformFloat((p + "constant").c_str(),  light->GetConstant());
            lightingShader_->SetUniformFloat((p + "linear").c_str(),    light->GetLinear());
            lightingShader_->SetUniformFloat((p + "quadratic").c_str(), light->GetQuadratic());
            lightingShader_->SetUniformFloat((p + "range").c_str(),     light->GetRange());
            lightingShader_->SetUniformFloat((p + "innerCutoff").c_str(),
                glm::cos(glm::radians(light->GetSpotInnerAngle())));
            lightingShader_->SetUniformFloat((p + "outerCutoff").c_str(),
                glm::cos(glm::radians(light->GetSpotOuterAngle())));
            ++spotCount;
        }
    }
    lightingShader_->SetUniformInt("uNumDirLights",   dirCount);
    lightingShader_->SetUniformInt("uNumPointLights", pointCount);
    lightingShader_->SetUniformInt("uNumSpotLights",  spotCount);

    // Issue the fullscreen draw. SetUniform* leaves the lighting program
    // bound, so we don't need a manual glUseProgram.
    glBindVertexArray(fullscreenVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore depth state.
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    glDepthMask(prevDepthMask);
    if (prevProg) glUseProgram(static_cast<GLuint>(prevProg));

    lit_->Unbind();

    // Stage E: do NOT blit lit_ to backbuffer here. ForwardRenderer drives
    // PostProcess (IngestHDRColor + Apply) which composites the final
    // image (bloom/exposure/ACES/FXAA) directly to FBO 0.
    return true;
}

bool DeferredRenderer::AllocateTargets(int width, int height) {
    ReleaseTargets();
    width_  = width;
    height_ = height;

    // 4-RT G-buffer + Depth32F. See header for slot semantics.
    RenderTargetDesc gb;
    gb.width  = width;
    gb.height = height;
    gb.colors.push_back({ RTColorFormat::RGBA8_UNorm });  // albedo + metallic.a
    gb.colors.push_back({ RTColorFormat::RGBA16F });      // normal.xyz + roughness
    gb.colors.push_back({ RTColorFormat::RGBA16F });      // emissive + motion
    gb.colors.push_back({ RTColorFormat::RGBA8_UNorm });  // ao + flags
    gb.depth.format       = RTDepthFormat::Depth32F;
    gb.depth.renderbuffer = false;  // sampled by lighting pass for worldPos
    gbuffer_ = device_->CreateRenderTarget(gb);
    if (!gbuffer_) {
        ARK_LOG_ERROR("Render", "DeferredRenderer: G-buffer alloc failed");
        return false;
    }

    // HDR lit target (lighting pass output, fed into PostProcess).
    // Depth32F renderbuffer: not sampled, but needed for the Stage F
    // forward transparent overlay. We blit gbuffer's depth into it after
    // the lighting pass (BlitGBufferDepthToLit).
    RenderTargetDesc lt;
    lt.width  = width;
    lt.height = height;
    lt.colors.push_back({ RTColorFormat::RGBA16F });
    lt.depth.format       = RTDepthFormat::Depth32F;
    lt.depth.renderbuffer = true;
    lit_ = device_->CreateRenderTarget(lt);
    if (!lit_) {
        ARK_LOG_ERROR("Render", "DeferredRenderer: lit RT alloc failed");
        gbuffer_.reset();
        return false;
    }
    return true;
}

void DeferredRenderer::ReleaseTargets() {
    gbuffer_.reset();
    lit_.reset();
    pipelineCache_.clear();
    initialized_ = false;
    width_ = height_ = 0;
}

uint64_t DeferredRenderer::HashPipelineDesc(const PipelineDesc& desc) {
    uint64_t hash = 14695981039346656037ULL;
    auto combine = [&](uint64_t val) {
        hash ^= val;
        hash *= 1099511628211ULL;
    };
    combine(reinterpret_cast<uintptr_t>(desc.shader));
    combine(static_cast<uint64_t>(desc.vertexLayout.stride));
    combine(static_cast<uint64_t>(desc.vertexLayout.attributes.size()));
    combine(static_cast<uint64_t>(desc.topology));
    combine(static_cast<uint64_t>(desc.depthTest));
    combine(static_cast<uint64_t>(desc.depthWrite));
    combine(static_cast<uint64_t>(desc.blendEnabled));
    return hash;
}

RHIPipeline* DeferredRenderer::GetOrCreatePipeline(const PipelineDesc& desc) {
    uint64_t key = HashPipelineDesc(desc);
    auto it = pipelineCache_.find(key);
    if (it != pipelineCache_.end()) {
        return it->second.pipeline.get();
    }
    auto pipeline = device_->CreatePipeline(desc);
    auto* raw = pipeline.get();
    pipelineCache_[key] = PipelineCacheEntry{ std::move(pipeline) };
    return raw;
}

} // namespace ark
