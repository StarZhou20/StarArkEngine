#pragma once

#include "RHIPipeline.h"
#include "RHIBuffer.h"
#include <cstdint>

namespace ark {

class RHICommandBuffer {
public:
    virtual ~RHICommandBuffer() = default;
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Submit() = 0;
    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;
    virtual void BindPipeline(RHIPipeline* pipeline) = 0;
    virtual void BindVertexBuffer(RHIBuffer* buffer) = 0;
    virtual void BindIndexBuffer(RHIBuffer* buffer) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;
};

} // namespace ark
