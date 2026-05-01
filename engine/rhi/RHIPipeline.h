#pragma once

#include "RHITypes.h"
#include "RHIShader.h"
#include "RHIRenderTarget.h"

#include <vector>

namespace ark {

struct PipelineDesc {
    RHIShader* shader = nullptr;
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTest = true;
    bool depthWrite = true;
    bool blendEnabled = false;

    // Render-target contract: ordered list of color attachment formats
    // the pipeline expects to write into. Empty = backbuffer (legacy /
    // forward path). For deferred MRT this is filled with N entries
    // matching the bound `RHIRenderTarget`'s color attachments. The
    // OpenGL backend uses this only for validation/logging; D3D12/Vulkan
    // backends will require it for PSO creation.
    std::vector<RTColorFormat> colorAttachments;
};

class RHIPipeline {
public:
    virtual ~RHIPipeline() = default;
    virtual void Bind() = 0;
    virtual const PipelineDesc& GetDesc() const = 0;
};

} // namespace ark
