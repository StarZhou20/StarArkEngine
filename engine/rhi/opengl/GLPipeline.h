#pragma once

#include "engine/rhi/RHIPipeline.h"
#include <GL/glew.h>

namespace ark {

class GLShader;

class GLPipeline : public RHIPipeline {
public:
    explicit GLPipeline(const PipelineDesc& desc);
    ~GLPipeline() override;

    GLPipeline(const GLPipeline&) = delete;
    GLPipeline& operator=(const GLPipeline&) = delete;

    void Bind() override;
    const PipelineDesc& GetDesc() const override { return desc_; }

    GLuint GetVAO() const { return vao_; }
    void SetupVertexAttributes() const;

    GLenum GetGLTopology() const;

private:
    PipelineDesc desc_;
    GLuint vao_ = 0;
};

} // namespace ark
