#pragma once

#include "engine/rhi/RHIDevice.h"
#include "engine/rhi/RHICommandBuffer.h"
#include "engine/rhi/RHIPipeline.h"
#include <memory>

namespace ark {

class Camera;
class Light;
class MeshRenderer;
class Window;

/// ForwardRenderer: iterates over cameras → collects visible MeshRenderers + lights → draws.
/// Called by EngineBase in main loop step 11.
class ForwardRenderer {
public:
    explicit ForwardRenderer(RHIDevice* device);
    ~ForwardRenderer() = default;

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    /// Render all cameras. Called once per frame.
    void RenderFrame(Window* window);

private:
    void RenderCamera(Camera* camera, Window* window);
    void SetLightUniforms(RHIShader* shader, Camera* camera);
    void DrawMeshRenderer(MeshRenderer* renderer, Camera* camera);

    RHIDevice* device_;
    std::unique_ptr<RHICommandBuffer> cmdBuffer_;
};

} // namespace ark
