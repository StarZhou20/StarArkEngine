#include "GLCommandBuffer.h"
#include "GLPipeline.h"
#include "GLBuffer.h"
#include <GL/glew.h>

namespace ark {

void GLCommandBuffer::Begin() {
    commands_.clear();
    currentPipeline_ = nullptr;
}

void GLCommandBuffer::End() {
    // Nothing to finalize for OpenGL
}

void GLCommandBuffer::Submit() {
    for (auto& cmd : commands_) {
        cmd();
    }
    commands_.clear();
    currentPipeline_ = nullptr;
}

void GLCommandBuffer::SetViewport(int x, int y, int width, int height) {
    commands_.push_back([x, y, width, height]() {
        glViewport(x, y, width, height);
    });
}

void GLCommandBuffer::Clear(float r, float g, float b, float a) {
    commands_.push_back([r, g, b, a]() {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    });
}

void GLCommandBuffer::BindPipeline(RHIPipeline* pipeline) {
    auto* glPipeline = static_cast<GLPipeline*>(pipeline);
    currentPipeline_ = glPipeline;
    commands_.push_back([glPipeline]() {
        glPipeline->Bind();
    });
}

void GLCommandBuffer::BindVertexBuffer(RHIBuffer* buffer) {
    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    auto* pipeline = currentPipeline_;
    commands_.push_back([glBuffer, pipeline]() {
        glBindBuffer(GL_ARRAY_BUFFER, glBuffer->GetHandle());
        if (pipeline) {
            pipeline->SetupVertexAttributes();
        }
    });
}

void GLCommandBuffer::BindIndexBuffer(RHIBuffer* buffer) {
    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    commands_.push_back([glBuffer]() {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->GetHandle());
    });
}

void GLCommandBuffer::Draw(uint32_t vertexCount, uint32_t firstVertex) {
    auto* pipeline = currentPipeline_;
    commands_.push_back([pipeline, vertexCount, firstVertex]() {
        GLenum topology = pipeline ? pipeline->GetGLTopology() : GL_TRIANGLES;
        glDrawArrays(topology, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
    });
}

void GLCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t firstIndex) {
    auto* pipeline = currentPipeline_;
    commands_.push_back([pipeline, indexCount, firstIndex]() {
        GLenum topology = pipeline ? pipeline->GetGLTopology() : GL_TRIANGLES;
        const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex * sizeof(uint32_t)));
        glDrawElements(topology, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, offset);
    });
}

} // namespace ark
