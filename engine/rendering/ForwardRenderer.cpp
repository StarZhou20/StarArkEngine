#include "ForwardRenderer.h"
#include "Camera.h"
#include "Light.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Mesh.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"
#include "engine/platform/Window.h"
#include "engine/rhi/RHIShader.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace ark {

ForwardRenderer::ForwardRenderer(RHIDevice* device)
    : device_(device)
    , cmdBuffer_(device->CreateCommandBuffer())
{
}

void ForwardRenderer::RenderFrame(Window* window) {
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

    for (auto* cam : validCameras) {
        RenderCamera(cam, window);
    }
}

void ForwardRenderer::RenderCamera(Camera* camera, Window* window) {
    cmdBuffer_->Begin();
    cmdBuffer_->SetViewport(0, 0, window->GetWidth(), window->GetHeight());

    auto& cc = camera->GetClearColor();
    cmdBuffer_->Clear(cc.r, cc.g, cc.b, cc.a);
    cmdBuffer_->End();
    cmdBuffer_->Submit();

    // Gather visible MeshRenderers
    auto& allRenderers = MeshRenderer::GetAllRenderers();
    std::vector<MeshRenderer*> visible;
    visible.reserve(allRenderers.size());

    for (auto* renderer : allRenderers) {
        auto* owner = renderer->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !renderer->IsEnabled()) {
            continue;
        }
        if (!renderer->GetMesh() || !renderer->GetMaterial() || !renderer->GetMaterial()->GetShader()) {
            continue;
        }
        visible.push_back(renderer);
    }

    // Sort by shader pointer, then material pointer (F5: minimize state switches)
    std::sort(visible.begin(), visible.end(),
        [](const MeshRenderer* a, const MeshRenderer* b) {
            auto* shaderA = a->GetMaterial()->GetShader();
            auto* shaderB = b->GetMaterial()->GetShader();
            if (shaderA != shaderB) return shaderA < shaderB;
            return a->GetMaterial() < b->GetMaterial();
        });

    for (auto* renderer : visible) {
        DrawMeshRenderer(renderer, camera);
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
