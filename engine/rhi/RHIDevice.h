#pragma once

#include "RHITypes.h"
#include "RHIBuffer.h"
#include "RHIShader.h"
#include "RHITexture.h"
#include "RHIPipeline.h"
#include "RHICommandBuffer.h"
#include "RHIRenderTarget.h"
#include <memory>

namespace ark {

class RHIDevice {
public:
    virtual ~RHIDevice() = default;
    virtual std::unique_ptr<RHIBuffer> CreateVertexBuffer(size_t size, BufferUsage usage = BufferUsage::Static) = 0;
    virtual std::unique_ptr<RHIBuffer> CreateIndexBuffer(size_t size, BufferUsage usage = BufferUsage::Static) = 0;
    virtual std::unique_ptr<RHIShader> CreateShader() = 0;
    virtual std::unique_ptr<RHITexture> CreateTexture() = 0;
    virtual std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;
    virtual std::unique_ptr<RHICommandBuffer> CreateCommandBuffer() = 0;
    virtual std::unique_ptr<RHIRenderTarget> CreateRenderTarget(const RenderTargetDesc& desc) = 0;
};

std::unique_ptr<RHIDevice> CreateOpenGLDevice();

} // namespace ark
