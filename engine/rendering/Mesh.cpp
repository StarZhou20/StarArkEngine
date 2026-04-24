#include "Mesh.h"
#include <cstring>
#include <cmath>

namespace ark {

// All primitive meshes share the same vertex layout now:
//   layout(location=0) vec3 aPosition
//   layout(location=1) vec3 aNormal
//   layout(location=2) vec2 aTexCoord
//   layout(location=3) vec3 aTangent   (Phase 9: for normal mapping)
struct PrimVert {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz;
};

static VertexLayout MakePrimLayout() {
    VertexLayout layout;
    layout.stride = sizeof(PrimVert);
    layout.attributes = {
        {"aPosition", VertexAttribType::Float3, 0, false},
        {"aNormal",   VertexAttribType::Float3, 3 * sizeof(float), false},
        {"aTexCoord", VertexAttribType::Float2, 6 * sizeof(float), false},
        {"aTangent",  VertexAttribType::Float3, 8 * sizeof(float), false},
    };
    return layout;
}

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

// --- Cube: 24 verts (4 per face) + 36 indices, with per-face tangent ---
std::unique_ptr<Mesh> Mesh::CreateCube() {
    auto mesh = std::make_unique<Mesh>();

    std::vector<PrimVert> verts = {
        // Front (+Z), tangent = (1,0,0)
        {-0.5f,-0.5f, 0.5f,  0,0,1,  0,0,  1,0,0},
        { 0.5f,-0.5f, 0.5f,  0,0,1,  1,0,  1,0,0},
        { 0.5f, 0.5f, 0.5f,  0,0,1,  1,1,  1,0,0},
        {-0.5f, 0.5f, 0.5f,  0,0,1,  0,1,  1,0,0},
        // Back (-Z), tangent = (-1,0,0)
        { 0.5f,-0.5f,-0.5f,  0,0,-1, 0,0, -1,0,0},
        {-0.5f,-0.5f,-0.5f,  0,0,-1, 1,0, -1,0,0},
        {-0.5f, 0.5f,-0.5f,  0,0,-1, 1,1, -1,0,0},
        { 0.5f, 0.5f,-0.5f,  0,0,-1, 0,1, -1,0,0},
        // Top (+Y), tangent = (1,0,0)
        {-0.5f, 0.5f, 0.5f,  0,1,0,  0,0,  1,0,0},
        { 0.5f, 0.5f, 0.5f,  0,1,0,  1,0,  1,0,0},
        { 0.5f, 0.5f,-0.5f,  0,1,0,  1,1,  1,0,0},
        {-0.5f, 0.5f,-0.5f,  0,1,0,  0,1,  1,0,0},
        // Bottom (-Y), tangent = (1,0,0)
        {-0.5f,-0.5f,-0.5f,  0,-1,0, 0,0,  1,0,0},
        { 0.5f,-0.5f,-0.5f,  0,-1,0, 1,0,  1,0,0},
        { 0.5f,-0.5f, 0.5f,  0,-1,0, 1,1,  1,0,0},
        {-0.5f,-0.5f, 0.5f,  0,-1,0, 0,1,  1,0,0},
        // Right (+X), tangent = (0,0,-1)
        { 0.5f,-0.5f, 0.5f,  1,0,0,  0,0,  0,0,-1},
        { 0.5f,-0.5f,-0.5f,  1,0,0,  1,0,  0,0,-1},
        { 0.5f, 0.5f,-0.5f,  1,0,0,  1,1,  0,0,-1},
        { 0.5f, 0.5f, 0.5f,  1,0,0,  0,1,  0,0,-1},
        // Left (-X), tangent = (0,0,1)
        {-0.5f,-0.5f,-0.5f, -1,0,0,  0,0,  0,0,1},
        {-0.5f,-0.5f, 0.5f, -1,0,0,  1,0,  0,0,1},
        {-0.5f, 0.5f, 0.5f, -1,0,0,  1,1,  0,0,1},
        {-0.5f, 0.5f,-0.5f, -1,0,0,  0,1,  0,0,1},
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

    VertexLayout layout = MakePrimLayout();
    mesh->SetVertices(verts.data(), verts.size() * sizeof(PrimVert), layout);
    mesh->SetIndices(indices.data(), indices.size());
    return mesh;
}

// --- Plane (Y-up), tangent = (1,0,0) ---
std::unique_ptr<Mesh> Mesh::CreatePlane(float size) {
    auto mesh = std::make_unique<Mesh>();
    float h = size * 0.5f;

    std::vector<PrimVert> verts = {
        {-h, 0,-h,  0,1,0, 0,0,  1,0,0},
        { h, 0,-h,  0,1,0, 1,0,  1,0,0},
        { h, 0, h,  0,1,0, 1,1,  1,0,0},
        {-h, 0, h,  0,1,0, 0,1,  1,0,0},
    };
    std::vector<uint32_t> indices = {0,1,2, 0,2,3};

    VertexLayout layout = MakePrimLayout();
    mesh->SetVertices(verts.data(), verts.size() * sizeof(PrimVert), layout);
    mesh->SetIndices(indices.data(), indices.size());
    return mesh;
}

// --- Sphere (UV-sphere, Y-up after swizzle) ---
std::unique_ptr<Mesh> Mesh::CreateSphere(int sectorCount, int stackCount) {
    auto mesh = std::make_unique<Mesh>();

    std::vector<PrimVert> verts;
    std::vector<uint32_t> indices;

    const float PI = 3.14159265359f;
    float radius = 0.5f;

    for (int i = 0; i <= stackCount; ++i) {
        float stackAngle = PI / 2.0f - PI * static_cast<float>(i) / stackCount;
        float xy = radius * std::cos(stackAngle);
        float z  = radius * std::sin(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = 2.0f * PI * static_cast<float>(j) / sectorCount;

            float x = xy * std::cos(sectorAngle);
            float y = xy * std::sin(sectorAngle);

            float nx = x / radius;
            float ny = y / radius;
            float nz = z / radius;

            float u = static_cast<float>(j) / sectorCount;
            float v = static_cast<float>(i) / stackCount;

            // Tangent along +sectorAngle direction (longitude).
            // d(original_pos)/d(sectorAngle) = (-xy*sin, xy*cos, 0)
            float tx = -std::sin(sectorAngle);
            float ty =  std::cos(sectorAngle);
            float tz =  0.0f;

            // Apply same swizzle as position (Y-up): (x, z, y)
            verts.push_back({x, z, y,  nx, nz, ny,  u, v,  tx, tz, ty});
        }
    }

    for (int i = 0; i < stackCount; ++i) {
        int k1 = i * (sectorCount + 1);
        int k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(static_cast<uint32_t>(k1));
                indices.push_back(static_cast<uint32_t>(k2));
                indices.push_back(static_cast<uint32_t>(k1 + 1));
            }
            if (i != stackCount - 1) {
                indices.push_back(static_cast<uint32_t>(k1 + 1));
                indices.push_back(static_cast<uint32_t>(k2));
                indices.push_back(static_cast<uint32_t>(k2 + 1));
            }
        }
    }

    VertexLayout layout = MakePrimLayout();
    mesh->SetVertices(verts.data(), verts.size() * sizeof(PrimVert), layout);
    mesh->SetIndices(indices.data(), indices.size());
    return mesh;
}

} // namespace ark
