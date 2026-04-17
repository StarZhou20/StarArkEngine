#pragma once

#include "engine/rhi/RHITypes.h"
#include "engine/rhi/RHIBuffer.h"
#include "engine/rhi/RHIDevice.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace ark {

/// A Mesh holds vertex + index data and uploads them to GPU buffers.
class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // --- Build mesh from raw data ---
    void SetVertices(const void* data, size_t sizeBytes, const VertexLayout& layout);
    void SetIndices(const uint32_t* data, size_t indexCount);

    // --- Upload to GPU (requires RHIDevice) ---
    void Upload(RHIDevice* device);
    bool IsUploaded() const { return vbo_ != nullptr; }

    // --- Accessors ---
    RHIBuffer* GetVertexBuffer() const { return vbo_.get(); }
    RHIBuffer* GetIndexBuffer() const { return ibo_.get(); }
    const VertexLayout& GetVertexLayout() const { return layout_; }
    uint32_t GetVertexCount() const { return vertexCount_; }
    uint32_t GetIndexCount() const { return indexCount_; }
    bool HasIndices() const { return indexCount_ > 0; }

    // --- Primitive generators ---
    static std::unique_ptr<Mesh> CreateCube();
    static std::unique_ptr<Mesh> CreatePlane(float size = 10.0f);

private:
    VertexLayout layout_;
    std::vector<uint8_t> vertexData_;
    std::vector<uint32_t> indexData_;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;

    std::unique_ptr<RHIBuffer> vbo_;
    std::unique_ptr<RHIBuffer> ibo_;
};

} // namespace ark
