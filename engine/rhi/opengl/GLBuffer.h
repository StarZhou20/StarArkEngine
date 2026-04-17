#pragma once

#include "engine/rhi/RHIBuffer.h"
#include <GL/glew.h>

namespace ark {

class GLBuffer : public RHIBuffer {
public:
    GLBuffer(BufferType type, size_t size, BufferUsage usage);
    ~GLBuffer() override;

    GLBuffer(const GLBuffer&) = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;

    void Upload(const void* data, size_t size) override;
    size_t GetSize() const override { return size_; }
    BufferType GetType() const override { return type_; }

    GLuint GetHandle() const { return handle_; }
    GLenum GetGLTarget() const;

private:
    GLuint handle_ = 0;
    BufferType type_;
    size_t size_;
    GLenum glUsage_;
};

} // namespace ark
