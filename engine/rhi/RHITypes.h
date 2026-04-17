#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ark {

enum class BufferType { Vertex, Index };
enum class BufferUsage { Static, Dynamic };
enum class ShaderStage { Vertex, Fragment };
enum class PrimitiveTopology { Triangles, Lines, Points };
enum class IndexType { UInt16, UInt32 };

enum class VertexAttribType { Float, Float2, Float3, Float4 };

inline uint32_t VertexAttribComponentCount(VertexAttribType type) {
    switch (type) {
        case VertexAttribType::Float:  return 1;
        case VertexAttribType::Float2: return 2;
        case VertexAttribType::Float3: return 3;
        case VertexAttribType::Float4: return 4;
    }
    return 0;
}

inline uint32_t VertexAttribSize(VertexAttribType type) {
    return VertexAttribComponentCount(type) * sizeof(float);
}

struct VertexAttribute {
    std::string name;
    VertexAttribType type;
    uint32_t offset;
    bool normalized = false;
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;
};

} // namespace ark
