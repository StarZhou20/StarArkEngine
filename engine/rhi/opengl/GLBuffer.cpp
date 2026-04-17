#include "GLBuffer.h"
#include "engine/debug/DebugListenBus.h"
#include <cassert>

namespace ark {

GLBuffer::GLBuffer(BufferType type, size_t size, BufferUsage usage)
    : type_(type), size_(size)
{
    glUsage_ = (usage == BufferUsage::Static) ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
    glGenBuffers(1, &handle_);
    glBindBuffer(GetGLTarget(), handle_);
    glBufferData(GetGLTarget(), static_cast<GLsizeiptr>(size), nullptr, glUsage_);
    glBindBuffer(GetGLTarget(), 0);
}

GLBuffer::~GLBuffer() {
    if (handle_) {
        glDeleteBuffers(1, &handle_);
    }
}

void GLBuffer::Upload(const void* data, size_t size) {
    assert(data && "GLBuffer::Upload: null data");
    assert(size <= size_ && "GLBuffer::Upload: data exceeds buffer size");
    glBindBuffer(GetGLTarget(), handle_);
    glBufferSubData(GetGLTarget(), 0, static_cast<GLsizeiptr>(size), data);
    glBindBuffer(GetGLTarget(), 0);
}

GLenum GLBuffer::GetGLTarget() const {
    return (type_ == BufferType::Vertex) ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
}

} // namespace ark
