#include "Skybox.h"
#include "engine/debug/DebugListenBus.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include <cstring>
#include <vector>

namespace ark {

// ---------- embedded shader sources ----------------------------------------

namespace {

const char* kSkyVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
out vec3 vDir;
uniform mat4 uViewNoTranslation;
uniform mat4 uProjection;
void main() {
    vDir = aPos;
    vec4 clip = uProjection * uViewNoTranslation * vec4(aPos, 1.0);
    // Force depth = 1 (far plane) so skybox draws behind everything.
    gl_Position = clip.xyww;
}
)";

const char* kSkyFS = R"(#version 450 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uCubeMap;
uniform float uIntensity;
void main() {
    vec3 c = texture(uCubeMap, normalize(vDir)).rgb * uIntensity;
    FragColor = vec4(c, 1.0);
}
)";

// 36-vertex unit cube (positions only, CCW from inside).
const float kCubeVerts[] = {
    // +X
     1,-1,-1,  1, 1,-1,  1, 1, 1,
     1, 1, 1,  1,-1, 1,  1,-1,-1,
    // -X
    -1,-1, 1, -1, 1, 1, -1, 1,-1,
    -1, 1,-1, -1,-1,-1, -1,-1, 1,
    // +Y
    -1, 1,-1, -1, 1, 1,  1, 1, 1,
     1, 1, 1,  1, 1,-1, -1, 1,-1,
    // -Y
    -1,-1, 1, -1,-1,-1,  1,-1,-1,
     1,-1,-1,  1,-1, 1, -1,-1, 1,
    // +Z
    -1,-1, 1,  1,-1, 1,  1, 1, 1,
     1, 1, 1, -1, 1, 1, -1,-1, 1,
    // -Z
     1,-1,-1, -1,-1,-1, -1, 1,-1,
    -1, 1,-1,  1, 1,-1,  1,-1,-1,
};

GLuint CompileShader(GLenum stage, const char* src, const char* name) {
    GLuint sh = glCreateShader(stage);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Render",
            std::string("Skybox(") + name + ") " +
            (stage == GL_VERTEX_SHADER ? "vs" : "fs") + " compile failed: " + log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

} // namespace

// ---------- lifecycle ------------------------------------------------------

Skybox::Skybox() = default;

Skybox::~Skybox() {
    if (cubemap_) glDeleteTextures(1, &cubemap_);
    if (cubeVBO_) glDeleteBuffers(1, &cubeVBO_);
    if (cubeVAO_) glDeleteVertexArrays(1, &cubeVAO_);
    if (program_) glDeleteProgram(program_);
}

void Skybox::Init() {
    if (initialized_) return;
    CompileProgram();
    AllocGeometry();
    EnsureCubeMap();
    initialized_ = true;
    if (!hasData_) {
        // Safety: always have something to sample.
        GenerateProceduralGradient(
            0.30f, 0.45f, 0.80f,   // zenith (blue)
            0.80f, 0.85f, 0.95f,   // horizon (pale)
            0.35f, 0.30f, 0.25f,   // ground (brown)
            128);
    }
    ARK_LOG_INFO("Render", "Skybox initialized");
}

void Skybox::EnsureCubeMap() {
    if (cubemap_) return;
    glGenTextures(1, &cubemap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void Skybox::CompileProgram() {
    if (program_) return;
    GLuint v = CompileShader(GL_VERTEX_SHADER,   kSkyVS, "vs");
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, kSkyFS, "fs");
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Render", std::string("Skybox link failed: ") + log);
        glDeleteProgram(program_);
        program_ = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
}

void Skybox::AllocGeometry() {
    if (cubeVAO_) return;
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

// ---------- population -----------------------------------------------------

bool Skybox::SetFromFiles(const std::array<std::string, 6>& faces) {
    Init();
    EnsureCubeMap();
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);

    // Cube map faces use top-left origin (unlike 2D textures in GL); do NOT
    // vertically flip.
    stbi_set_flip_vertically_on_load(false);

    for (int i = 0; i < 6; ++i) {
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(faces[i].c_str(), &w, &h, &ch, 0);
        if (!data) {
            ARK_LOG_ERROR("Render",
                std::string("Skybox: failed to load face ") + std::to_string(i) +
                " '" + faces[i] + "': " + stbi_failure_reason());
            return false;
        }
        GLenum dataFmt  = (ch == 4) ? GL_RGBA : (ch == 3 ? GL_RGB : GL_RED);
        GLenum storeFmt = (ch == 4) ? GL_SRGB8_ALPHA8 : (ch == 3 ? GL_SRGB8 : GL_R8);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, storeFmt, w, h, 0,
                     dataFmt, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    hasData_ = true;

    ARK_LOG_INFO("Render", "Skybox: loaded 6 face cubemap");
    return true;
}

void Skybox::GenerateProceduralGradient(float zr, float zg, float zb,
                                        float hr, float hg, float hb,
                                        float gr, float gg, float gb,
                                        int faceSize) {
    Init();
    EnsureCubeMap();
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);

    const int N = faceSize > 4 ? faceSize : 4;
    std::vector<float> pixels(N * N * 3);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    auto writeFace = [&](GLenum target, auto mapXY) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                // Normalized face coords in [-1,1]
                float u = (float(x) + 0.5f) / float(N) * 2.0f - 1.0f;
                float v = (float(y) + 0.5f) / float(N) * 2.0f - 1.0f;
                glm::vec3 dir = glm::normalize(mapXY(u, v));
                float t = dir.y; // -1 (down) .. +1 (up)
                float r, g, b;
                if (t >= 0.0f) {
                    // horizon -> zenith
                    r = lerp(hr, zr, t);
                    g = lerp(hg, zg, t);
                    b = lerp(hb, zb, t);
                } else {
                    // horizon -> ground
                    float k = -t;
                    r = lerp(hr, gr, k);
                    g = lerp(hg, gg, k);
                    b = lerp(hb, gb, k);
                }
                int idx = (y * N + x) * 3;
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
            }
        }
        glTexImage2D(target, 0, GL_RGB16F, N, N, 0, GL_RGB, GL_FLOAT, pixels.data());
    };

    // Map face-local (u,v) to world-space direction per GL cubemap convention.
    writeFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X, [](float u, float v){ return glm::vec3( 1, -v, -u); });
    writeFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, [](float u, float v){ return glm::vec3(-1, -v,  u); });
    writeFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, [](float u, float v){ return glm::vec3( u,  1,  v); });
    writeFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, [](float u, float v){ return glm::vec3( u, -1, -v); });
    writeFace(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, [](float u, float v){ return glm::vec3( u, -v,  1); });
    writeFace(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, [](float u, float v){ return glm::vec3(-u, -v, -1); });

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    hasData_ = true;
    ARK_LOG_INFO("Render", "Skybox: generated procedural gradient (" +
        std::to_string(N) + "x" + std::to_string(N) + " per face)");
}

// ---------- render ---------------------------------------------------------

void Skybox::Render(const glm::mat4& view, const glm::mat4& projection) {
    if (!enabled_ || !initialized_ || !program_ || !cubemap_) return;

    // Strip translation from view so sky follows the camera.
    glm::mat4 viewNoT = view;
    viewNoT[3][0] = 0.0f;
    viewNoT[3][1] = 0.0f;
    viewNoT[3][2] = 0.0f;

    // Save GL state we touch.
    GLint prevDepthFunc = 0;
    GLboolean prevDepthMask = GL_TRUE;
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(program_);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uViewNoTranslation"),
                       1, GL_FALSE, glm::value_ptr(viewNoT));
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"),
                       1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(program_, "uIntensity"), intensity_);
    glUniform1i(glGetUniformLocation(program_, "uCubeMap"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);

    glBindVertexArray(cubeVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Restore state.
    glDepthFunc(prevDepthFunc);
    glDepthMask(prevDepthMask);
    if (prevCull) glEnable(GL_CULL_FACE);
    glUseProgram(0);
}

} // namespace ark
