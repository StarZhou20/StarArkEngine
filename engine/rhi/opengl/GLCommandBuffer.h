#pragma once

#include "engine/rhi/RHICommandBuffer.h"
#include <vector>
#include <functional>

namespace ark {

class GLPipeline;
class GLBuffer;

class GLCommandBuffer : public RHICommandBuffer {
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void Begin() override;
    void End() override;
    void Submit() override;

    void SetRenderTarget(RHIRenderTarget* target) override;
    void BlitToBackBuffer(RHIRenderTarget* src,
                          int dstX, int dstY,
                          int dstW, int dstH) override;

    void SetViewport(int x, int y, int width, int height) override;
    void Clear(float r, float g, float b, float a) override;
    void BindPipeline(RHIPipeline* pipeline) override;
    void BindVertexBuffer(RHIBuffer* buffer) override;
    void BindIndexBuffer(RHIBuffer* buffer) override;
    void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) override;

private:
    std::vector<std::function<void()>> commands_;
    GLPipeline* currentPipeline_ = nullptr;
};

} // namespace ark
