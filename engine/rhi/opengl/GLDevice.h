#pragma once

#include "engine/rhi/RHIDevice.h"

namespace ark {

class GLDevice : public RHIDevice {
public:
    GLDevice() = default;
    ~GLDevice() override = default;

    std::unique_ptr<RHIBuffer> CreateVertexBuffer(size_t size, BufferUsage usage = BufferUsage::Static) override;
    std::unique_ptr<RHIBuffer> CreateIndexBuffer(size_t size, BufferUsage usage = BufferUsage::Static) override;
    std::unique_ptr<RHIShader> CreateShader() override;
    std::unique_ptr<RHITexture> CreateTexture() override;
    std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc& desc) override;
    std::unique_ptr<RHICommandBuffer> CreateCommandBuffer() override;
};

} // namespace ark
