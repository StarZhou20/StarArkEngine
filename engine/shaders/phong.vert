#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uNormalMatrix) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
