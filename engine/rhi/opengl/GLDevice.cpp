#include "GLDevice.h"
#include "GLBuffer.h"
#include "GLShader.h"
#include "GLTexture.h"
#include "GLPipeline.h"
#include "GLCommandBuffer.h"

namespace ark {

std::unique_ptr<RHIBuffer> GLDevice::CreateVertexBuffer(size_t size, BufferUsage usage) {
    return std::make_unique<GLBuffer>(BufferType::Vertex, size, usage);
}

std::unique_ptr<RHIBuffer> GLDevice::CreateIndexBuffer(size_t size, BufferUsage usage) {
    return std::make_unique<GLBuffer>(BufferType::Index, size, usage);
}

std::unique_ptr<RHIShader> GLDevice::CreateShader() {
    return std::make_unique<GLShader>();
}

std::unique_ptr<RHITexture> GLDevice::CreateTexture() {
    return std::make_unique<GLTexture>();
}

std::unique_ptr<RHIPipeline> GLDevice::CreatePipeline(const PipelineDesc& desc) {
    return std::make_unique<GLPipeline>(desc);
}

std::unique_ptr<RHICommandBuffer> GLDevice::CreateCommandBuffer() {
    return std::make_unique<GLCommandBuffer>();
}

// Factory function
std::unique_ptr<RHIDevice> CreateOpenGLDevice() {
    return std::make_unique<GLDevice>();
}

} // namespace ark
