#include "ForwardRenderer.h"
#include "Camera.h"
#include "DrawListBuilder.h"
#include "Light.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Mesh.h"
#include "SceneSerializer.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Window.h"
#include "engine/platform/Paths.h"
#include "engine/mod/ModInfo.h"
#include "engine/rhi/RHIShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace ark {

ForwardRenderer::ForwardRenderer(RHIDevice* device)
    : device_(device)
    , cmdBuffer_(device->CreateCommandBuffer())
    , shaderManager_(std::make_unique<ShaderManager>(device))
    , postProcess_(std::make_unique<PostProcess>())
    , skybox_(std::make_unique<Skybox>())
    , ibl_(std::make_unique<IBL>())
    , shadowMap_(std::make_unique<ShadowMap>())
{
    // Wire RHI device into PostProcess so its lazy AllocTargets() can route
    // through `RHIRenderTarget` instead of raw glGenFramebuffers/Textures.
    postProcess_->SetDevice(device_);
    // IBL routes only the BrdfLUT 2D bake through the RHI for now;
    // cubemap face/mip attach bakes still use raw GL.
    ibl_->SetDevice(device_);

    // Stage G: pipeline selector. Order of precedence:
    //   1. ARK_PIPELINE env (debug override, highest priority)
    //   2. <GameRoot>/game.config.json   "pipeline": "deferred" | "forward"
    //   3. RenderSettings::pipeline default = Forward
    // The JSON read uses a tiny substring scan rather than the full parser
    // in SceneSerializer.cpp — this runs once at ctor and we only care
    // about a single string key.
    auto applyPipelineString = [this](const std::string& v, const char* origin) {
        if (v == "deferred" || v == "Deferred" || v == "DEFERRED") {
            settings_.pipeline = RenderPipeline::Deferred;
            ARK_LOG_INFO("Render", std::string("pipeline=deferred (") + origin + ") \u2014 using DeferredRenderer");
        } else if (v == "forward" || v == "Forward" || v == "FORWARD") {
            settings_.pipeline = RenderPipeline::Forward;
        }
    };
    {
        namespace fs = std::filesystem;
        fs::path cfg = Paths::GameRoot() / "game.config.json";
        std::ifstream in(cfg);
        if (in) {
            std::stringstream buf; buf << in.rdbuf();
            std::string body = buf.str();
            // Find `"pipeline"` then the first quoted string after the colon.
            auto k = body.find("\"pipeline\"");
            if (k != std::string::npos) {
                auto colon = body.find(':', k);
                if (colon != std::string::npos) {
                    auto q1 = body.find('"', colon + 1);
                    auto q2 = (q1 == std::string::npos) ? std::string::npos
                                                        : body.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        applyPipelineString(body.substr(q1 + 1, q2 - q1 - 1),
                                            "game.config.json");
                    }
                }
            }
        }
    }
    if (const char* env = std::getenv("ARK_PIPELINE")) {
        applyPipelineString(env, "ARK_PIPELINE env");
    }

    // v0.3 ModSpec §2 — once the pipeline is known, warn (don't skip) on any
    // enabled mod whose `supported_pipelines` list does not include the
    // current pipeline. We don't disable the mod here because the pipeline
    // is a runtime debug-overridable choice; the player owns the trade-off.
    {
        const char* pipName =
            (settings_.pipeline == RenderPipeline::Deferred) ? "deferred" : "forward";
        for (const auto& id : Paths::EnabledModIds()) {
            const ModInfo* info = Paths::FindModInfo(id);
            if (!info || info->supported_pipelines.empty()) continue;
            bool ok = false;
            for (const auto& p : info->supported_pipelines) if (p == pipName) { ok = true; break; }
            if (!ok) {
                ARK_LOG_WARN("Render",
                    "Mod '" + id + "' supported_pipelines does not include '"
                    + pipName + "' — visual artifacts possible.");
            }
        }
    }
}

void ForwardRenderer::RebakeIBL() {
    if (!skybox_) return;
    skybox_->Init();
    ibl_->Bake(skybox_->GetCubeMap());
    iblBaked_ = ibl_->IsValid();
}

bool ForwardRenderer::RenderShadowPass() {
    if (!settings_.shadow.enabled) return false;

    // Find the first enabled directional light.
    Light* dirLight = nullptr;
    glm::vec3 lightDir(0, -1, 0);
    for (auto* light : Light::GetAllLights()) {
        auto* owner = light->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !light->IsEnabled())
            continue;
        if (light->GetType() != Light::Type::Directional) continue;
        dirLight = light;
        glm::mat4 worldMat = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
        // Forward = -Z of the owner transform, which is the light's direction of travel.
        lightDir = -glm::normalize(glm::vec3(worldMat[2]));
        break;
    }
    if (!dirLight) { hasMainLight_ = false; return false; }

    // Cache for later passes (contact shadows, etc.).
    mainLightDirWorld_[0] = lightDir.x;
    mainLightDirWorld_[1] = lightDir.y;
    mainLightDirWorld_[2] = lightDir.z;
    hasMainLight_ = true;

    if (!shadowMap_->Init(device_, settings_.shadow.resolution)) return false;

    // --- Pick focus point: snap to camera ground projection ----------------
    // Bistro and similar large scenes can't be covered by a fixed frustum
    // at the world origin. Center the ortho frustum on the active camera
    // (projected onto the ground), pushed slightly forward along view.
    glm::vec3 focus(0.0f);
    {
        Camera* primaryForShadow = nullptr;
        for (auto* cam : Camera::GetAllCameras()) {
            auto* owner = cam->GetOwner();
            if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !cam->IsEnabled())
                continue;
            primaryForShadow = cam;
            break;
        }
        if (primaryForShadow) {
            const glm::mat4 invView = glm::inverse(primaryForShadow->GetViewMatrix());
            glm::vec3 camPos = glm::vec3(invView[3]);
            glm::vec3 fwd = -glm::normalize(glm::vec3(invView[2]));
            fwd.y = 0.0f;
            if (glm::dot(fwd, fwd) < 1e-4f) fwd = glm::vec3(0, 0, -1);
            else                            fwd = glm::normalize(fwd);
            focus = glm::vec3(camPos.x, 0.0f, camPos.z) +
                    fwd * (settings_.shadow.orthoHalfSize * 0.25f);
        }
    }

    // Texel-snap is now performed inside ShadowMap::UpdateMatrix using its
    // own light-space basis (passing resolution > 0 enables it).
    shadowMap_->UpdateMatrix(lightDir, focus,
                             settings_.shadow.orthoHalfSize,
                             settings_.shadow.nearPlane,
                             settings_.shadow.farPlane,
                             settings_.shadow.resolution);

    auto depthShader = shaderManager_->Get("depth");
    if (!depthShader) return false;

    // Gather shadow casters (same filter as the main pass for now).
    auto& allRenderers = MeshRenderer::GetAllRenderers();
    std::vector<MeshRenderer*> casters;
    casters.reserve(allRenderers.size());
    for (auto* r : allRenderers) {
        auto* owner = r->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !r->IsEnabled())
            continue;
        if (!r->GetMesh() || !r->GetMaterial() || !r->GetMaterial()->GetShader()) continue;
        casters.push_back(r);
    }

    shadowMap_->Begin();

    // Front-face culling reduces shadow acne on solid meshes.
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLint prevCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    const glm::mat4 lightSpace = shadowMap_->GetLightSpaceMatrix();

    for (auto* r : casters) {
        auto* mesh = r->GetMesh();
        auto* owner = r->GetOwner();

        PipelineDesc desc;
        desc.shader = depthShader.get();
        desc.vertexLayout = mesh->GetVertexLayout();
        desc.topology = PrimitiveTopology::Triangles;
        desc.depthTest = true;
        desc.depthWrite = true;
        auto* pipeline = GetOrCreatePipeline(desc);

        cmdBuffer_->Begin();
        glm::mat4 model = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
        pipeline->Bind();
        depthShader->SetUniformMat4("uLightSpaceMatrix", glm::value_ptr(lightSpace));
        depthShader->SetUniformMat4("uModel", glm::value_ptr(model));

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

    // Restore cull state.
    glCullFace(prevCullMode);
    if (!prevCull) glDisable(GL_CULL_FACE);

    shadowMap_->End();
    return true;
}

void ForwardRenderer::RenderFrame(Window* window) {
    // Skip the entire frame when the window framebuffer has zero extent
    // (initial frame on some drivers, or window minimized). Touching FBO
    // creation / glm::perspective with width=0 or height=0 yields hard
    // GL errors and a glm aspect-ratio assert that aborts the process.
    if (!window || window->GetWidth() <= 0 || window->GetHeight() <= 0) {
        return;
    }

    // Poll for shader file changes (no-op when hot reload is off).
    shaderManager_->CheckHotReload();

    // Poll for scene JSON file changes (mini Phase M10 hot reload).
    SceneSerializer::Tick(this);

    // Gather valid cameras, sorted by priority
    auto& allCameras = Camera::GetAllCameras();
    std::vector<Camera*> validCameras;
    validCameras.reserve(allCameras.size());

    for (auto* cam : allCameras) {
        auto* owner = cam->GetOwner();
        if (owner && !owner->IsDestroyed() && owner->IsActiveInHierarchy() && cam->IsEnabled()) {
            validCameras.push_back(cam);
        }
    }

    if (validCameras.empty()) return;

    std::sort(validCameras.begin(), validCameras.end(),
        [](const Camera* a, const Camera* b) { return a->GetPriority() < b->GetPriority(); });

    // --- Phase 12: ensure IBL is baked once the skybox is ready --------
    // (Hoisted above the deferred dispatch so DeferredRenderer can sample
    // the IBL textures in its lighting pass.)
    if (!iblBaked_ && skybox_ && skybox_->IsEnabled()) {
        skybox_->Init();
        ibl_->Bake(skybox_->GetCubeMap());
        iblBaked_ = ibl_->IsValid();
    }

    // --- Phase 13: directional shadow depth pass ------------------------
    // (Hoisted above the deferred dispatch so DeferredRenderer can sample
    // the shadow map. ForwardRenderer's main path also re-uses the same
    // result — RenderShadowPass writes into shadowMap_ + sets
    // mainLightDirWorld_ regardless of which pipeline runs after.)
    shadowThisFrame_ = RenderShadowPass();

    // --- Roadmap #9: Deferred dispatch -----------------------------------
    // When the pipeline is set to Deferred, route the primary camera
    // through DeferredRenderer. Shadow + IBL state is already populated
    // above; we forward it via SetFrameContext. If the deferred path
    // fails (e.g. shader load), fall through to forward.
    if (settings_.pipeline == RenderPipeline::Deferred) {
        if (!deferredRenderer_) {
            deferredRenderer_ = std::make_unique<DeferredRenderer>(device_, shaderManager_.get());
        }
        deferredRenderer_->SetFrameContext(&settings_,
                                           shadowMap_.get(), shadowThisFrame_,
                                           ibl_.get(),       iblBaked_);
        const int dsw = window->GetWidth();
        const int dsh = window->GetHeight();
        if (deferredRenderer_->Render(validCameras.front(), dsw, dsh)) {
            // Stage F: forward transparent overlay. Copy gbuffer depth
            // into lit_, then render IsTransparent() meshes back-to-front
            // with the standard PBR shader (full shadow + IBL).
            deferredRenderer_->BlitGBufferDepthToLit();
            RenderTransparentOverlay(validCameras.front(),
                                     deferredRenderer_->GetLitTarget());

            // Stage E/G: feed lit_ (linear HDR) + gbuffer depth into
            // PostProcess. With shared depth, SSAO/Fog/SSR/contactShadow
            // run on the deferred path too. TAA still off (needs prev VP +
            // motion vectors; emissive RT alpha is the natural slot for
            // motion later).
            auto* lit = deferredRenderer_->GetLitTarget();
            auto* gb  = deferredRenderer_->GetGBuffer();
            if (lit && gb) {
                Camera*   primary = validCameras.front();
                glm::mat4 proj    = primary->GetProjectionMatrix();
                glm::mat4 view    = primary->GetViewMatrix();
                glm::mat4 invProj = glm::inverse(proj);
                glm::mat4 invVP   = glm::inverse(proj * view);
                glm::mat4 invView = glm::inverse(view);
                glm::vec3 camPos  = glm::vec3(invView[3]);
                float camPosArr[3] = { camPos.x, camPos.y, camPos.z };
                postProcess_->SetFog(settings_.fog.enabled,
                                     glm::value_ptr(invVP),
                                     camPosArr,
                                     settings_.fog.color,
                                     settings_.fog.density,
                                     settings_.fog.heightStart,
                                     settings_.fog.heightFalloff,
                                     settings_.fog.maxOpacity);

                bool ssaoOn = settings_.ssao.enabled &&
                              std::getenv("ARK_NO_SSAO") == nullptr;

                postProcess_->SetBloomEnabled(settings_.bloom.enabled);
                postProcess_->SetMsaaSamples(0);  // lit_ is already resolved
                postProcess_->IngestHDRColor(lit->GetColorTextureHandle(0), dsw, dsh,
                                             gb->GetDepthTextureHandle());
                if (ssaoOn) {
                    postProcess_->ApplySSAO(glm::value_ptr(proj),
                                            settings_.ssao.intensity,
                                            settings_.ssao.radius,
                                            settings_.ssao.bias,
                                            settings_.ssao.samples);
                }
                if (settings_.contactShadow.enabled) {
                    glm::vec3 worldToLight = -glm::vec3(mainLightDirWorld_[0],
                                                        mainLightDirWorld_[1],
                                                        mainLightDirWorld_[2]);
                    glm::vec3 viewToLight = glm::normalize(
                        glm::vec3(view * glm::vec4(worldToLight, 0.0f)));
                    float viewDirArr[3] = { viewToLight.x, viewToLight.y, viewToLight.z };
                    postProcess_->ApplyContactShadow(glm::value_ptr(proj),
                                                     viewDirArr,
                                                     settings_.contactShadow.steps,
                                                     settings_.contactShadow.maxDistance,
                                                     settings_.contactShadow.thickness,
                                                     settings_.contactShadow.strength);
                }
                if (settings_.ssr.enabled) {
                    postProcess_->ApplySSR(glm::value_ptr(proj),
                                           glm::value_ptr(invProj),
                                           1.0f,
                                           settings_.ssr.maxDistance,
                                           settings_.ssr.steps,
                                           settings_.ssr.thickness,
                                           settings_.ssr.fadeEdge);
                }
                glm::mat4 curViewProj    = proj * view;
                glm::mat4 curInvViewProj = glm::inverse(curViewProj);
                bool taaOn = settings_.taa.enabled;
                postProcess_->Apply(dsw, dsh,
                                    settings_.exposure,
                                    settings_.bloom.threshold,
                                    settings_.bloom.strength,
                                    settings_.bloom.iterations,
                                    /*ssao*/ ssaoOn,
                                    /*contactShadow*/ settings_.contactShadow.enabled,
                                    /*fxaa*/ settings_.fxaa.enabled,
                                    /*tonemap*/ settings_.tonemap.mode,
                                    /*ssr*/ settings_.ssr.enabled,
                                    /*taa*/ taaOn,
                                    settings_.taa.blendNew,
                                    hasPrevViewProj_ ? prevViewProj_
                                                     : glm::value_ptr(curViewProj),
                                    glm::value_ptr(curInvViewProj));
                if (taaOn) {
                    std::memcpy(prevViewProj_, glm::value_ptr(curViewProj),
                                sizeof(prevViewProj_));
                    hasPrevViewProj_ = true;
                } else {
                    hasPrevViewProj_ = false;
                }
            }
            return;  // deferred frame done; skip the forward pipeline
        }
        // Fall back to forward this frame. A repeated failure logs once
        // (DeferredRenderer's internal guard).
    }

    // --- Phase 10: bind HDR FBO for the entire scene, then composite ---
    const int sw = window->GetWidth();
    const int sh = window->GetHeight();
    postProcess_->SetBloomEnabled(settings_.bloom.enabled);
    postProcess_->SetMsaaSamples(settings_.msaa.samples);

    // (IBL bake + shadow pass already ran before the deferred dispatch.)

    // If shadow pass was disabled/skipped, we still need the main light dir
    // for contact shadows. Do a cheap scan.
    if (!hasMainLight_) {
        for (auto* light : Light::GetAllLights()) {
            auto* owner = light->GetOwner();
            if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !light->IsEnabled())
                continue;
            if (light->GetType() != Light::Type::Directional) continue;
            glm::mat4 worldMat = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
            glm::vec3 dir = -glm::normalize(glm::vec3(worldMat[2]));
            mainLightDirWorld_[0] = dir.x;
            mainLightDirWorld_[1] = dir.y;
            mainLightDirWorld_[2] = dir.z;
            hasMainLight_ = true;
            break;
        }
    }

    postProcess_->BeginScene(sw, sh);

    for (auto* cam : validCameras) {
        RenderCamera(cam, window);
    }

    postProcess_->EndScene();

    // --- Phase 14: SSAO (uses primary camera's projection) ---
    bool ssaoOn    = settings_.ssao.enabled          && std::getenv("ARK_NO_SSAO")    == nullptr;
    bool contactOn = settings_.contactShadow.enabled && std::getenv("ARK_NO_CONTACT") == nullptr;
    if (ssaoOn && !validCameras.empty()) {
        glm::mat4 proj = validCameras.front()->GetProjectionMatrix();
        postProcess_->ApplySSAO(glm::value_ptr(proj),
                                settings_.ssao.intensity,
                                settings_.ssao.radius,
                                settings_.ssao.bias,
                                settings_.ssao.samples);
    }

    // --- Phase 15: Contact shadows (screen-space ray-march toward main light) ---
    if (contactOn && hasMainLight_ && !validCameras.empty()) {        Camera* primary = validCameras.front();
        glm::mat4 proj  = primary->GetProjectionMatrix();
        glm::mat4 view  = primary->GetViewMatrix();

        // World-space direction TOWARD the light = -mainLightDir (we stored
        // the direction the light's rays travel).
        glm::vec3 worldToLight = -glm::vec3(mainLightDirWorld_[0],
                                            mainLightDirWorld_[1],
                                            mainLightDirWorld_[2]);
        // Transform as direction (ignore translation).
        glm::vec3 viewToLight = glm::normalize(glm::vec3(view * glm::vec4(worldToLight, 0.0f)));

        float viewDirArr[3] = { viewToLight.x, viewToLight.y, viewToLight.z };
        postProcess_->ApplyContactShadow(glm::value_ptr(proj),
                                         viewDirArr,
                                         settings_.contactShadow.steps,
                                         settings_.contactShadow.maxDistance,
                                         settings_.contactShadow.thickness,
                                         settings_.contactShadow.strength);
    }

    // --- Phase 16: SSR (depth-only, upward-facing surfaces) ---
    if (settings_.ssr.enabled && !validCameras.empty()) {
        glm::mat4 proj    = validCameras.front()->GetProjectionMatrix();
        glm::mat4 invProj = glm::inverse(proj);
        postProcess_->ApplySSR(glm::value_ptr(proj),
                               glm::value_ptr(invProj),
                               1.0f,
                               settings_.ssr.maxDistance,
                               settings_.ssr.steps,
                               settings_.ssr.thickness,
                               settings_.ssr.fadeEdge);
    }

    // --- Phase 16: height fog uses primary camera matrices ---
    if (!validCameras.empty()) {
        Camera*   primary = validCameras.front();
        glm::mat4 proj    = primary->GetProjectionMatrix();
        glm::mat4 view    = primary->GetViewMatrix();
        glm::mat4 invVP   = glm::inverse(proj * view);
        // Camera world position = -inverseView * (translation column).
        glm::mat4 invView = glm::inverse(view);
        glm::vec3 camPos  = glm::vec3(invView[3]);
        float camPosArr[3] = { camPos.x, camPos.y, camPos.z };
        postProcess_->SetFog(settings_.fog.enabled,
                             glm::value_ptr(invVP),
                             camPosArr,
                             settings_.fog.color,
                             settings_.fog.density,
                             settings_.fog.heightStart,
                             settings_.fog.heightFalloff,
                             settings_.fog.maxOpacity);
    } else {
        postProcess_->SetFog(false, nullptr, nullptr, nullptr, 0, 0, 0, 0);
    }

    // --- Phase 16: TAA needs prev-frame view-projection + cur inv-VP ---
    glm::mat4 curViewProjForTAA(1.0f);
    glm::mat4 curInvViewProjForTAA(1.0f);
    bool taaInputsValid = false;
    if (settings_.taa.enabled && !validCameras.empty()) {
        Camera*   primary = validCameras.front();
        glm::mat4 proj    = primary->GetProjectionMatrix();
        glm::mat4 view    = primary->GetViewMatrix();
        curViewProjForTAA    = proj * view;
        curInvViewProjForTAA = glm::inverse(curViewProjForTAA);
        taaInputsValid = true;
    }

    postProcess_->Apply(sw, sh, settings_.exposure,
                       settings_.bloom.threshold,
                       settings_.bloom.enabled ? settings_.bloom.strength : 0.0f,
                       settings_.bloom.iterations,
                       ssaoOn,
                       contactOn,
                       settings_.fxaa.enabled,
                       settings_.tonemap.mode,
                       settings_.ssr.enabled,
                       settings_.taa.enabled && taaInputsValid,
                       settings_.taa.blendNew,
                       hasPrevViewProj_ ? prevViewProj_ : glm::value_ptr(curViewProjForTAA),
                       glm::value_ptr(curInvViewProjForTAA));

    // Update prev VP for next frame.
    if (taaInputsValid) {
        std::memcpy(prevViewProj_, glm::value_ptr(curViewProjForTAA), sizeof(prevViewProj_));
        hasPrevViewProj_ = true;
    } else {
        hasPrevViewProj_ = false;
    }
}

void ForwardRenderer::RenderCamera(Camera* camera, Window* window) {
    cmdBuffer_->Begin();
    cmdBuffer_->SetViewport(0, 0, window->GetWidth(), window->GetHeight());

    auto& cc = camera->GetClearColor();
    cmdBuffer_->Clear(cc.r, cc.g, cc.b, cc.a);
    cmdBuffer_->End();
    cmdBuffer_->Submit();

    // Gather visible MeshRenderers (legacy forward: opaques + transparents in one
    // shader-sorted list — transparency is handled by per-material blend state
    // rather than a separate pass).
    std::vector<MeshRenderer*> visible;
    CollectOpaqueDrawList(camera, visible, /*includeTransparent=*/true);

    for (auto* renderer : visible) {
        DrawMeshRenderer(renderer, camera);
    }

    // --- Phase 11: skybox draws after opaques, into the HDR FBO ---
    if (skybox_ && settings_.sky.enabled && skybox_->IsEnabled()) {
        skybox_->Init();
        skybox_->SetIntensity(settings_.sky.intensity);
        skybox_->Render(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }
}

void ForwardRenderer::SetLightUniforms(RHIShader* shader, Camera* camera) {
    constexpr int MAX_DIR   = 4;
    constexpr int MAX_POINT = 8;
    constexpr int MAX_SPOT  = 4;

    auto& allLights = Light::GetAllLights();
    int dirCount = 0, pointCount = 0, spotCount = 0;

    for (auto* light : allLights) {
        auto* owner = light->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !light->IsEnabled())
            continue;

        glm::mat4 worldMat = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();

        if (light->GetType() == Light::Type::Directional && dirCount < MAX_DIR) {
            std::string prefix = "uDirLights[" + std::to_string(dirCount) + "].";

            glm::vec3 forward = -glm::normalize(glm::vec3(worldMat[2]));
            glm::vec3 lightColor = light->GetColor() * light->GetIntensity();
            glm::vec3 ambient = light->GetAmbient();

            shader->SetUniformVec3((prefix + "direction").c_str(), glm::value_ptr(forward));
            shader->SetUniformVec3((prefix + "color").c_str(), glm::value_ptr(lightColor));
            shader->SetUniformVec3((prefix + "ambient").c_str(), glm::value_ptr(ambient));
            dirCount++;
        }
        else if (light->GetType() == Light::Type::Point && pointCount < MAX_POINT) {
            std::string prefix = "uPointLights[" + std::to_string(pointCount) + "].";

            glm::vec3 pos = glm::vec3(worldMat[3]);
            glm::vec3 lightColor = light->GetColor() * light->GetIntensity();

            shader->SetUniformVec3((prefix + "position").c_str(), glm::value_ptr(pos));
            shader->SetUniformVec3((prefix + "color").c_str(), glm::value_ptr(lightColor));
            shader->SetUniformFloat((prefix + "constant").c_str(), light->GetConstant());
            shader->SetUniformFloat((prefix + "linear").c_str(), light->GetLinear());
            shader->SetUniformFloat((prefix + "quadratic").c_str(), light->GetQuadratic());
            shader->SetUniformFloat((prefix + "range").c_str(), light->GetRange());
            pointCount++;
        }
        else if (light->GetType() == Light::Type::Spot && spotCount < MAX_SPOT) {
            std::string prefix = "uSpotLights[" + std::to_string(spotCount) + "].";

            glm::vec3 pos = glm::vec3(worldMat[3]);
            glm::vec3 forward = -glm::normalize(glm::vec3(worldMat[2]));
            glm::vec3 lightColor = light->GetColor() * light->GetIntensity();

            shader->SetUniformVec3((prefix + "position").c_str(), glm::value_ptr(pos));
            shader->SetUniformVec3((prefix + "direction").c_str(), glm::value_ptr(forward));
            shader->SetUniformVec3((prefix + "color").c_str(), glm::value_ptr(lightColor));
            shader->SetUniformFloat((prefix + "constant").c_str(), light->GetConstant());
            shader->SetUniformFloat((prefix + "linear").c_str(), light->GetLinear());
            shader->SetUniformFloat((prefix + "quadratic").c_str(), light->GetQuadratic());
            shader->SetUniformFloat((prefix + "range").c_str(), light->GetRange());
            shader->SetUniformFloat((prefix + "innerCutoff").c_str(),
                glm::cos(glm::radians(light->GetSpotInnerAngle())));
            shader->SetUniformFloat((prefix + "outerCutoff").c_str(),
                glm::cos(glm::radians(light->GetSpotOuterAngle())));
            spotCount++;
        }
    }

    shader->SetUniformInt("uNumDirLights", dirCount);
    shader->SetUniformInt("uNumPointLights", pointCount);
    shader->SetUniformInt("uNumSpotLights", spotCount);

    // Camera position for specular
    glm::vec3 camPos = camera->GetOwner()->GetTransform().GetWorldPosition();
    shader->SetUniformVec3("uCameraPos", glm::value_ptr(camPos));

    // Global exposure (used by PBR tone mapping)
    shader->SetUniformFloat("uExposure", settings_.exposure);

    // --- Phase 12: IBL ambient uniforms + texture bindings -------------
    const bool iblUse = settings_.ibl.enabled && iblBaked_ && ibl_ && ibl_->IsValid();
    shader->SetUniformInt("uIBLEnabled", iblUse ? 1 : 0);
    if (iblUse) {
        shader->SetUniformFloat("uIBLDiffuseIntensity",  settings_.ibl.diffuseIntensity);
        shader->SetUniformFloat("uIBLSpecularIntensity", settings_.ibl.specularIntensity);
        shader->SetUniformFloat("uPrefilterMaxLod",
            float(ibl_->GetPrefilterMipLevels() > 0 ? ibl_->GetPrefilterMipLevels() - 1 : 0));
        // Reserve texture units 5/6/7 for IBL (material uses 0-4).
        shader->SetUniformInt("uIrradianceMap", 5);
        shader->SetUniformInt("uPrefilterMap",  6);
        shader->SetUniformInt("uBrdfLUT",       7);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_->GetIrradianceMap());
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_->GetPrefilterMap());
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, ibl_->GetBrdfLUT());
        glActiveTexture(GL_TEXTURE0);
    }

    // --- Phase 13: shadow map binding ----------------------------------
    const bool shadowUse = shadowThisFrame_ && shadowMap_ && shadowMap_->IsValid();
    shader->SetUniformInt("uShadowEnabled", shadowUse ? 1 : 0);
    if (shadowUse) {
        shader->SetUniformMat4("uLightSpaceMatrix",
                               glm::value_ptr(shadowMap_->GetLightSpaceMatrix()));
        shader->SetUniformFloat("uShadowDepthBias",  settings_.shadow.depthBias);
        shader->SetUniformFloat("uShadowNormalBias", settings_.shadow.normalBias);
        shader->SetUniformInt  ("uShadowPcfKernel",  settings_.shadow.pcfKernel);
        shader->SetUniformFloat("uShadowTexelSize",
                                1.0f / float(shadowMap_->GetResolution()));
        shader->SetUniformInt("uShadowMap", 8);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, shadowMap_->GetDepthTexture());
        glActiveTexture(GL_TEXTURE0);
    }

    // --- Debug visualisation (env: ARK_DEBUG = off|albedo|normal|geom|metal|
    //     rough|ao|mr|uv|tangent, or numeric 0-9) ---
    static int debugMode = []() {
        const char* env = std::getenv("ARK_DEBUG");
        if (!env) return 0;
        std::string v(env);
        if (v == "albedo")  return 1;
        if (v == "normal")  return 2;
        if (v == "geom")    return 3;
        if (v == "metal")   return 4;
        if (v == "rough")   return 5;
        if (v == "ao")      return 6;
        if (v == "mr")      return 7;
        if (v == "uv")      return 8;
        if (v == "tangent") return 9;
        try { return std::stoi(v); } catch (...) { return 0; }
    }();
    shader->SetUniformInt("uDebugMode", debugMode);
}

void ForwardRenderer::DrawMeshRenderer(MeshRenderer* renderer, Camera* camera) {
    auto* mesh = renderer->GetMesh();
    auto* material = renderer->GetMaterial();
    auto* shader = material->GetShader();
    auto* owner = renderer->GetOwner();

    // Look up or create cached pipeline (F6)
    PipelineDesc desc;
    desc.shader = shader;
    desc.vertexLayout = mesh->GetVertexLayout();
    desc.topology = PrimitiveTopology::Triangles;
    desc.depthTest = true;
    desc.depthWrite = true;

    auto* pipeline = GetOrCreatePipeline(desc);

    cmdBuffer_->Begin();

    // Set transforms
    glm::mat4 model = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix();
    glm::mat4 mvp = proj * view * model;
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

    pipeline->Bind();
    shader->SetUniformMat4("uModel", glm::value_ptr(model));
    shader->SetUniformMat4("uMVP", glm::value_ptr(mvp));
    shader->SetUniformMat4("uNormalMatrix", glm::value_ptr(glm::mat4(normalMat)));

    // Light uniforms
    SetLightUniforms(shader, camera);

    // Material uniforms
    material->Bind();

    // Bind pipeline + mesh + draw
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

void ForwardRenderer::RenderTransparentOverlay(Camera* camera, RHIRenderTarget* targetRT) {
    if (!camera || !targetRT) return;
    auto pbrShader = shaderManager_->Get("pbr");
    if (!pbrShader) return;

    std::vector<MeshRenderer*> transparents;
    CollectTransparentDrawList(camera, transparents);
    if (transparents.empty()) return;

    targetRT->Bind();

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix();

    for (auto* r : transparents) {
        auto* mesh     = r->GetMesh();
        auto* material = r->GetMaterial();
        auto* owner    = r->GetOwner();

        // Use the PBR forward shader so we get full shadow + IBL parity
        // with the forward path (the material's own shader may be a
        // gbuffer-only program in deferred-aware materials; pbr is the
        // canonical forward overlay shader).
        PipelineDesc desc;
        desc.shader        = pbrShader.get();
        desc.vertexLayout  = mesh->GetVertexLayout();
        desc.topology      = PrimitiveTopology::Triangles;
        desc.depthTest     = true;
        desc.depthWrite    = false;       // transparents must not write depth
        desc.blendEnabled  = true;        // SRC_ALPHA / 1-SRC_ALPHA
        auto* pipeline = GetOrCreatePipeline(desc);

        cmdBuffer_->Begin();

        glm::mat4 model    = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
        glm::mat4 mvp      = proj * view * model;
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

        pipeline->Bind();
        pbrShader->SetUniformMat4("uModel",        glm::value_ptr(model));
        pbrShader->SetUniformMat4("uMVP",          glm::value_ptr(mvp));
        pbrShader->SetUniformMat4("uNormalMatrix", glm::value_ptr(glm::mat4(normalMat)));

        SetLightUniforms(pbrShader.get(), camera);
        material->BindToShader(pbrShader.get());

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

    targetRT->Unbind();
}

uint64_t ForwardRenderer::HashPipelineDesc(const PipelineDesc& desc) {
    // FNV-1a style hash combining the relevant pipeline fields
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

RHIPipeline* ForwardRenderer::GetOrCreatePipeline(const PipelineDesc& desc) {
    uint64_t key = HashPipelineDesc(desc);
    auto it = pipelineCache_.find(key);
    if (it != pipelineCache_.end()) {
        return it->second.pipeline.get();
    }
    auto pipeline = device_->CreatePipeline(desc);
    auto* raw = pipeline.get();
    pipelineCache_[key] = PipelineCacheEntry{std::move(pipeline)};
    return raw;
}

} // namespace ark
