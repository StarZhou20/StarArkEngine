#pragma once

#include "RHITypes.h"
#include "RHIShader.h"

namespace ark {

struct PipelineDesc {
    RHIShader* shader = nullptr;
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTest = true;
    bool depthWrite = true;
    bool blendEnabled = false;
};

class RHIPipeline {
public:
    virtual ~RHIPipeline() = default;
    virtual void Bind() = 0;
    virtual const PipelineDesc& GetDesc() const = 0;
};

} // namespace ark
