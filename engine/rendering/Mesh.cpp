#include "Mesh.h"
#include <cstring>

namespace ark {

void Mesh::SetVertices(const void* data, size_t sizeBytes, const VertexLayout& layout) {
    layout_ = layout;
    vertexData_.resize(sizeBytes);
    std::memcpy(vertexData_.data(), data, sizeBytes);
    vertexCount_ = (layout_.stride > 0) ? static_cast<uint32_t>(sizeBytes / layout_.stride) : 0;
}

void Mesh::SetIndices(const uint32_t* data, size_t indexCount) {
    indexCount_ = static_cast<uint32_t>(indexCount);
    indexData_.assign(data, data + indexCount);
}

void Mesh::Upload(RHIDevice* device) {
    if (vertexData_.empty()) return;

    vbo_ = device->CreateVertexBuffer(vertexData_.size());
    vbo_->Upload(vertexData_.data(), vertexData_.size());

    if (!indexData_.empty()) {
        ibo_ = device->CreateIndexBuffer(indexData_.size() * sizeof(uint32_t));
        ibo_->Upload(indexData_.data(), indexData_.size() * sizeof(uint32_t));
    }
}

// --- Cube: 36 vertices (6 faces * 2 triangles * 3 verts), with normals ---
// Layout: position(3) + normal(3) + uv(2)
std::unique_ptr<Mesh> Mesh::CreateCube() {
    auto mesh = std::make_unique<Mesh>();

    // Each face: 4 vertices → 2 triangles (6 indices)
    // Position(3) + Normal(3) + UV(2) = 8 floats per vertex
    struct Vert { float px, py, pz, nx, ny, nz, u, v; };

    std::vector<Vert> verts = {
        // Front face (+Z)
        {-0.5f, -0.5f,  0.5f,  0, 0, 1,  0, 0},
        { 0.5f, -0.5f,  0.5f,  0, 0, 1,  1, 0},
        { 0.5f,  0.5f,  0.5f,  0, 0, 1,  1, 1},
        {-0.5f,  0.5f,  0.5f,  0, 0, 1,  0, 1},
        // Back face (-Z)
        { 0.5f, -0.5f, -0.5f,  0, 0,-1,  0, 0},
        {-0.5f, -0.5f, -0.5f,  0, 0,-1,  1, 0},
        {-0.5f,  0.5f, -0.5f,  0, 0,-1,  1, 1},
        { 0.5f,  0.5f, -0.5f,  0, 0,-1,  0, 1},
        // Top face (+Y)
        {-0.5f,  0.5f,  0.5f,  0, 1, 0,  0, 0},
        { 0.5f,  0.5f,  0.5f,  0, 1, 0,  1, 0},
        { 0.5f,  0.5f, -0.5f,  0, 1, 0,  1, 1},
        {-0.5f,  0.5f, -0.5f,  0, 1, 0,  0, 1},
        // Bottom face (-Y)
        {-0.5f, -0.5f, -0.5f,  0,-1, 0,  0, 0},
        { 0.5f, -0.5f, -0.5f,  0,-1, 0,  1, 0},
        { 0.5f, -0.5f,  0.5f,  0,-1, 0,  1, 1},
        {-0.5f, -0.5f,  0.5f,  0,-1, 0,  0, 1},
        // Right face (+X)
        { 0.5f, -0.5f,  0.5f,  1, 0, 0,  0, 0},
        { 0.5f, -0.5f, -0.5f,  1, 0, 0,  1, 0},
        { 0.5f,  0.5f, -0.5f,  1, 0, 0,  1, 1},
        { 0.5f,  0.5f,  0.5f,  1, 0, 0,  0, 1},
        // Left face (-X)
        {-0.5f, -0.5f, -0.5f, -1, 0, 0,  0, 0},
        {-0.5f, -0.5f,  0.5f, -1, 0, 0,  1, 0},
        {-0.5f,  0.5f,  0.5f, -1, 0, 0,  1, 1},
        {-0.5f,  0.5f, -0.5f, -1, 0, 0,  0, 1},
    };

    std::vector<uint32_t> indices;
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    VertexLayout layout;
    layout.stride = sizeof(Vert);
    layout.attributes = {
        {"aPosition", VertexAttribType::Float3, 0, false},
        {"aNormal",   VertexAttribType::Float3, 3 * sizeof(float), false},
        {"aTexCoord", VertexAttribType::Float2, 6 * sizeof(float), false},
    };

    mesh->SetVertices(verts.data(), verts.size() * sizeof(Vert), layout);
    mesh->SetIndices(indices.data(), indices.size());
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreatePlane(float size) {
    auto mesh = std::make_unique<Mesh>();

    float h = size * 0.5f;
    struct Vert { float px, py, pz, nx, ny, nz, u, v; };

    std::vector<Vert> verts = {
        {-h, 0, -h,  0, 1, 0,  0, 0},
        { h, 0, -h,  0, 1, 0,  1, 0},
        { h, 0,  h,  0, 1, 0,  1, 1},
        {-h, 0,  h,  0, 1, 0,  0, 1},
    };

    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    VertexLayout layout;
    layout.stride = sizeof(Vert);
    layout.attributes = {
        {"aPosition", VertexAttribType::Float3, 0, false},
        {"aNormal",   VertexAttribType::Float3, 3 * sizeof(float), false},
        {"aTexCoord", VertexAttribType::Float2, 6 * sizeof(float), false},
    };

    mesh->SetVertices(verts.data(), verts.size() * sizeof(Vert), layout);
    mesh->SetIndices(indices.data(), indices.size());
    return mesh;
}

} // namespace ark
