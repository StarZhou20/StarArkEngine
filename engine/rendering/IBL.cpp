#include "IBL.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/rhi/RHIDevice.h"
#include "engine/rhi/RHIRenderTarget.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>

namespace ark {

// ---------- embedded shader sources ----------------------------------------
namespace {

const char* kCubeVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
out vec3 vLocalPos;
uniform mat4 uProjection;
uniform mat4 uView;
void main() {
    vLocalPos = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
)";

// Irradiance: hemispherical integration of the env map (Lambertian diffuse).
const char* kIrradianceFS = R"(#version 450 core
in vec3 vLocalPos;
out vec4 FragColor;
uniform samplerCube uEnv;
const float PI = 3.14159265359;
void main() {
    vec3 N = normalize(vLocalPos);
    vec3 irradiance = vec3(0.0);
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));
    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                      sin(theta) * sin(phi),
                                      cos(theta));
            vec3 sampleVec = tangentSample.x * right +
                             tangentSample.y * up +
                             tangentSample.z * N;
            irradiance += texture(uEnv, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }
    irradiance = PI * irradiance / nrSamples;
    FragColor = vec4(irradiance, 1.0);
}
)";

// Prefilter: GGX-importance-sampled env map for given roughness mip.
const char* kPrefilterFS = R"(#version 450 core
in vec3 vLocalPos;
out vec4 FragColor;
uniform samplerCube uEnv;
uniform float uRoughness;
const float PI = 3.14159265359;
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N) { return vec2(float(i)/float(N), RadicalInverse_VdC(i)); }
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 H = vec3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}
void main() {
    vec3 N = normalize(vLocalPos);
    vec3 R = N;
    vec3 V = R;
    const uint SAMPLE_COUNT = 1024u;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += texture(uEnv, L).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }
    FragColor = vec4(prefilteredColor / max(totalWeight, 1e-4), 1.0);
}
)";

// BRDF LUT: 2D texture of (NdotV, roughness) → (scale, bias).
const char* kBrdfVS = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kBrdfFS = R"(#version 450 core
in vec2 vUV;
out vec2 FragColor;
const float PI = 3.14159265359;
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N) { return vec2(float(i)/float(N), RadicalInverse_VdC(i)); }
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness*roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    vec3 H = vec3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a*a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N,V),0.0), roughness) *
           GeometrySchlickGGX(max(dot(N,L),0.0), roughness);
}
vec2 IntegrateBRDF(float NdotV, float roughness) {
    vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);
    float A = 0.0, B = 0.0;
    vec3 N = vec3(0,0,1);
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V,H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V,H), 0.0);
        if (NdotL > 0.0) {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 1e-4);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return vec2(A, B) / float(SAMPLE_COUNT);
}
void main() { FragColor = IntegrateBRDF(vUV.x, vUV.y); }
)";

const float kUnitCube[] = {
    // +X
     1,-1,-1,  1, 1,-1,  1, 1, 1,   1, 1, 1,  1,-1, 1,  1,-1,-1,
    // -X
    -1,-1, 1, -1, 1, 1, -1, 1,-1,  -1, 1,-1, -1,-1,-1, -1,-1, 1,
    // +Y
    -1, 1,-1, -1, 1, 1,  1, 1, 1,   1, 1, 1,  1, 1,-1, -1, 1,-1,
    // -Y
    -1,-1, 1, -1,-1,-1,  1,-1,-1,   1,-1,-1,  1,-1, 1, -1,-1, 1,
    // +Z
    -1,-1, 1,  1,-1, 1,  1, 1, 1,   1, 1, 1, -1, 1, 1, -1,-1, 1,
    // -Z
     1,-1,-1, -1,-1,-1, -1, 1,-1,  -1, 1,-1,  1, 1,-1,  1,-1,-1,
};

const float kQuadVerts[] = {
    -1,-1, 0,0,   3,-1, 2,0,   -1, 3, 0,2,
};

} // namespace

// ---------- ctor/dtor ------------------------------------------------------

IBL::IBL() = default;

IBL::~IBL() {
    ReleaseTextures();
    if (progIrradiance_) glDeleteProgram(progIrradiance_);
    if (progPrefilter_)  glDeleteProgram(progPrefilter_);
    if (progBrdfLut_)    glDeleteProgram(progBrdfLut_);
    if (cubeVBO_) glDeleteBuffers(1, &cubeVBO_);
    if (cubeVAO_) glDeleteVertexArrays(1, &cubeVAO_);
    if (quadVBO_) glDeleteBuffers(1, &quadVBO_);
    if (quadVAO_) glDeleteVertexArrays(1, &quadVAO_);
}

void IBL::ReleaseTextures() {
    if (irradianceMap_) { glDeleteTextures(1, &irradianceMap_); irradianceMap_ = 0; }
    if (prefilterMap_)  { glDeleteTextures(1, &prefilterMap_);  prefilterMap_  = 0; }
    if (rtBrdfLUT_) {
        // RT owns the texture; just drop it and zero the cached handle.
        rtBrdfLUT_.reset();
        brdfLUT_ = 0;
    } else if (brdfLUT_) {
        glDeleteTextures(1, &brdfLUT_);
        brdfLUT_ = 0;
    }
    valid_ = false;
    prefilterMipLevels_ = 0;
}

// ---------- compile helpers ------------------------------------------------

uint32_t IBL::CompileProgram(const char* vs, const char* fs, const char* name) {
    auto compile = [&](GLenum stage, const char* src) -> GLuint {
        GLuint sh = glCreateShader(stage);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            ARK_LOG_ERROR("Render",
                std::string("IBL(") + name + ") " +
                (stage == GL_VERTEX_SHADER ? "vs" : "fs") +
                " compile failed: " + log);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    };
    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Render", std::string("IBL(") + name + ") link failed: " + log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

void IBL::EnsurePrograms() {
    if (programsReady_) return;
    progIrradiance_ = CompileProgram(kCubeVS, kIrradianceFS, "irradiance");
    progPrefilter_  = CompileProgram(kCubeVS, kPrefilterFS,  "prefilter");
    progBrdfLut_    = CompileProgram(kBrdfVS, kBrdfFS,       "brdfLUT");

    // Cube geometry.
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitCube), kUnitCube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Quad (fullscreen triangle with UV).
    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    programsReady_ = true;
}

// ---------- bake -----------------------------------------------------------

namespace {
    // View matrices for each cube face.
    const glm::mat4 kCaptureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 kCaptureViews[6] = {
        glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
    };
}

void IBL::BakeIrradiance(uint32_t envCube, int size) {
    glGenTextures(1, &irradianceMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap_);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    glUseProgram(progIrradiance_);
    glUniform1i(glGetUniformLocation(progIrradiance_, "uEnv"), 0);
    glUniformMatrix4fv(glGetUniformLocation(progIrradiance_, "uProjection"),
                       1, GL_FALSE, glm::value_ptr(kCaptureProj));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);

    glViewport(0, 0, size, size);
    glBindVertexArray(cubeVAO_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    for (int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(glGetUniformLocation(progIrradiance_, "uView"),
                           1, GL_FALSE, glm::value_ptr(kCaptureViews[i]));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                               irradianceMap_, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);
}

void IBL::BakePrefilter(uint32_t envCube, int size) {
    glGenTextures(1, &prefilterMap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap_);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    GLuint fbo = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glUseProgram(progPrefilter_);
    glUniform1i(glGetUniformLocation(progPrefilter_, "uEnv"), 0);
    glUniformMatrix4fv(glGetUniformLocation(progPrefilter_, "uProjection"),
                       1, GL_FALSE, glm::value_ptr(kCaptureProj));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);

    const int maxMip = 5;
    prefilterMipLevels_ = maxMip;
    glBindVertexArray(cubeVAO_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    for (int mip = 0; mip < maxMip; ++mip) {
        int mipW = size >> mip;
        int mipH = size >> mip;
        if (mipW < 1) mipW = 1;
        if (mipH < 1) mipH = 1;
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipW, mipH);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        glViewport(0, 0, mipW, mipH);

        float roughness = float(mip) / float(maxMip - 1);
        glUniform1f(glGetUniformLocation(progPrefilter_, "uRoughness"), roughness);
        for (int i = 0; i < 6; ++i) {
            glUniformMatrix4fv(glGetUniformLocation(progPrefilter_, "uView"),
                               1, GL_FALSE, glm::value_ptr(kCaptureViews[i]));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                                   prefilterMap_, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);
}

void IBL::BakeBrdfLUT(int size) {
    GLuint fbo = 0, rbo = 0;
    bool ownRT = false;

    if (device_) {
        // RHI path: a single-color RG16F + depth-RBO 2D render target.
        RenderTargetDesc d;
        d.width  = size;
        d.height = size;
        RTColorAttachmentDesc c;
        c.format = RTColorFormat::RG16F;
        d.colors.push_back(c);
        d.depth.format       = RTDepthFormat::Depth24;
        d.depth.renderbuffer = true;
        rtBrdfLUT_ = device_->CreateRenderTarget(d);
        if (rtBrdfLUT_) {
            brdfLUT_ = rtBrdfLUT_->GetColorTextureHandle(0);
            fbo = static_cast<GLuint>(rtBrdfLUT_->GetNativeFramebufferHandle());
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            ownRT = true;
        }
    }

    if (!ownRT) {
        // Raw fallback (kept verbatim for safety).
        glGenTextures(1, &brdfLUT_);
        glBindTexture(GL_TEXTURE_2D, brdfLUT_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &rbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUT_, 0);
    }

    glViewport(0, 0, size, size);
    glUseProgram(progBrdfLut_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(quadVAO_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ownRT) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &rbo);
    }
}

void IBL::Bake(uint32_t envCubeMap, int irradianceSize, int prefilterSize, int brdfLutSize) {
    if (envCubeMap == 0) {
        ARK_LOG_WARN("Render", "IBL::Bake: env cube map is 0, skipping");
        return;
    }

    ReleaseTextures();
    EnsurePrograms();

    // Save state.
    GLint prevFBO = 0, prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);

    BakeIrradiance(envCubeMap, irradianceSize);
    BakePrefilter(envCubeMap, prefilterSize);
    BakeBrdfLUT(brdfLutSize);

    // Restore.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevCull) glEnable(GL_CULL_FACE);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
    glBindVertexArray(0);

    valid_ = (irradianceMap_ && prefilterMap_ && brdfLUT_);
    if (valid_) {
        ARK_LOG_INFO("Render",
            "IBL baked: irr=" + std::to_string(irradianceSize) +
            " prefilter=" + std::to_string(prefilterSize) +
            " brdfLUT=" + std::to_string(brdfLutSize));
    } else {
        ARK_LOG_ERROR("Render", "IBL bake failed");
    }
}

} // namespace ark
