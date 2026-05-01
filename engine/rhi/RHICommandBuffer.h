#pragma once

#include "RHIPipeline.h"
#include "RHIBuffer.h"
#include "RHIRenderTarget.h"
#include <cstdint>

namespace ark {

class RHICommandBuffer {
public:
    virtual ~RHICommandBuffer() = default;
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Submit() = 0;

    /// Bind a render target for subsequent draws. Pass `nullptr` to
    /// restore the default framebuffer (backbuffer). The viewport is
    /// implicitly reset to (0,0,rt.width,rt.height); call SetViewport
    /// afterwards to override.
    virtual void SetRenderTarget(RHIRenderTarget* target) = 0;

    /// Resolve / blit a render target's first color attachment into the
    /// default framebuffer. `srcW/srcH` and `dstW/dstH` describe the
    /// blit rectangles; non-multisampled RTs are simply copied. Used at
    /// the end of a deferred frame to present the lit result.
    virtual void BlitToBackBuffer(RHIRenderTarget* src,
                                  int dstX, int dstY,
                                  int dstW, int dstH) = 0;

    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;
    virtual void BindPipeline(RHIPipeline* pipeline) = 0;
    virtual void BindVertexBuffer(RHIBuffer* buffer) = 0;
    virtual void BindIndexBuffer(RHIBuffer* buffer) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;
};

} // namespace ark
