#version 450 core
// G-buffer geometry pass vertex shader (Roadmap #9 Deferred).
//
// Emits world-space attributes used by the lighting fullscreen pass.
// Mirrors pbr.vert exactly so both forward and deferred draws can share
// the same MeshRenderer vertex layout / cached pipeline desc.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;

    mat3 normalMat = mat3(uNormalMatrix);
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    // Gram-Schmidt re-orthogonalise tangent against world-space normal.
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    vNormal    = N;
    vTangent   = T;
    vBitangent = B;
    vTexCoord  = aTexCoord;

    gl_Position = uMVP * vec4(aPosition, 1.0);
}
