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
#include <algorithm>
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
    for (auto* renderer : allRenderers) {
        auto* owner = renderer->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !renderer->IsEnabled()) {
            continue;
        }
        if (!renderer->GetMesh() || !renderer->GetMaterial() || !renderer->GetMaterial()->GetShader()) {
            continue;
        }
        DrawMeshRenderer(renderer, camera);
    }
}

void ForwardRenderer::SetLightUniforms(RHIShader* shader, Camera* camera) {
    auto& allLights = Light::GetAllLights();
    int dirCount = 0;

    for (auto* light : allLights) {
        auto* owner = light->GetOwner();
        if (!owner || owner->IsDestroyed() || !owner->IsActiveInHierarchy() || !light->IsEnabled()) {
            continue;
        }

        if (light->GetType() == Light::Type::Directional && dirCount == 0) {
            // Use transform's forward direction (negative Z in local space)
            glm::mat4 worldMat = const_cast<Transform&>(owner->GetTransform()).GetWorldMatrix();
            glm::vec3 forward = -glm::normalize(glm::vec3(worldMat[2])); // -Z axis

            glm::vec3 lightColor = light->GetColor() * light->GetIntensity();
            glm::vec3 ambient = light->GetAmbient();

            shader->SetUniformVec3("uLight.direction", glm::value_ptr(forward));
            shader->SetUniformVec3("uLight.color", glm::value_ptr(lightColor));
            shader->SetUniformVec3("uLight.ambient", glm::value_ptr(ambient));
            dirCount++;
        }
    }

    // If no directional light, set defaults
    if (dirCount == 0) {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 defaultColor(1.0f);
        glm::vec3 defaultAmbient(0.2f);
        shader->SetUniformVec3("uLight.direction", glm::value_ptr(defaultDir));
        shader->SetUniformVec3("uLight.color", glm::value_ptr(defaultColor));
        shader->SetUniformVec3("uLight.ambient", glm::value_ptr(defaultAmbient));
    }

    // Camera position for specular
    glm::vec3 camPos = camera->GetOwner()->GetTransform().GetWorldPosition();
    shader->SetUniformVec3("uCameraPos", glm::value_ptr(camPos));
}

void ForwardRenderer::DrawMeshRenderer(MeshRenderer* renderer, Camera* camera) {
    auto* mesh = renderer->GetMesh();
    auto* material = renderer->GetMaterial();
    auto* shader = material->GetShader();
    auto* owner = renderer->GetOwner();

    // Create pipeline on-the-fly (could be cached, but simple for now)
    PipelineDesc desc;
    desc.shader = shader;
    desc.vertexLayout = mesh->GetVertexLayout();
    desc.topology = PrimitiveTopology::Triangles;
    desc.depthTest = true;
    desc.depthWrite = true;

    auto pipeline = device_->CreatePipeline(desc);

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
    cmdBuffer_->BindPipeline(pipeline.get());
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

} // namespace ark
