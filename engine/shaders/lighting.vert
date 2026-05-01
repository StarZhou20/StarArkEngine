#version 450 core
// Fullscreen-triangle vertex shader for the deferred lighting pass.
// Uses gl_VertexID with a static 3-vertex no-buffer trick: the GPU expands
// the triangle to cover [-1,1] clip space and provides UVs in vUV.
//
// Bound with a no-attribute VAO and `glDrawArrays(GL_TRIANGLES, 0, 3)`.

out vec2 vUV;

void main() {
    // Two-bit (x = id&1, y = id&2) → corners (0,0)/(2,0)/(0,2) →
    // clip-space (-1,-1)/(3,-1)/(-1,3). The single triangle that covers
    // the full screen is faster than a quad.
    vec2 p = vec2(float((gl_VertexID & 1) << 2),
                  float((gl_VertexID & 2) << 1));
    vUV = p * 0.5;
    gl_Position = vec4(p - 1.0, 0.0, 1.0);
}
