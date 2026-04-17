#include "GLPipeline.h"
#include "GLShader.h"
#include "engine/rhi/RHITypes.h"

namespace ark {

GLPipeline::GLPipeline(const PipelineDesc& desc)
    : desc_(desc)
{
    glGenVertexArrays(1, &vao_);
}

GLPipeline::~GLPipeline() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void GLPipeline::Bind() {
    auto* glShader = static_cast<GLShader*>(desc_.shader);
    glShader->Bind();
    glBindVertexArray(vao_);

    if (desc_.depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(desc_.depthWrite ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    if (desc_.blendEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void GLPipeline::SetupVertexAttributes() const {
    glBindVertexArray(vao_);
    const auto& layout = desc_.vertexLayout;
    for (uint32_t i = 0; i < layout.attributes.size(); ++i) {
        const auto& attr = layout.attributes[i];
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(
            i,
            static_cast<GLint>(VertexAttribComponentCount(attr.type)),
            GL_FLOAT,
            attr.normalized ? GL_TRUE : GL_FALSE,
            static_cast<GLsizei>(layout.stride),
            reinterpret_cast<const void*>(static_cast<uintptr_t>(attr.offset))
        );
    }
}

GLenum GLPipeline::GetGLTopology() const {
    switch (desc_.topology) {
        case PrimitiveTopology::Triangles: return GL_TRIANGLES;
        case PrimitiveTopology::Lines:     return GL_LINES;
        case PrimitiveTopology::Points:    return GL_POINTS;
    }
    return GL_TRIANGLES;
}

} // namespace ark
