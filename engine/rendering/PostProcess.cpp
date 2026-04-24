#include "PostProcess.h"
#include "engine/debug/DebugListenBus.h"

#include <GL/glew.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace ark {

// ---------- embedded shader sources ----------------------------------------

namespace {

const char* kFullscreenVS = R"(#version 450 core
// Fullscreen triangle: 3-vert no-attribute pass (we still feed a VBO so
// the driver is happy, but position/uv are derived from gl_VertexID when
// possible). Here we use a minimal per-vertex attribute for clarity.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Extracts pixels whose luminance exceeds a threshold, for the bloom chain.
const char* kBrightFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uSoftKnee; // 0..1

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee curve around uThreshold.
    float knee = uThreshold * uSoftKnee;
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - uThreshold);
    contribution /= max(brightness, 1e-4);

    FragColor = vec4(c * contribution, 1.0);
}
)";

// Separable 9-tap gaussian blur (horizontal if uHorizontal != 0, else vertical).
const char* kBlurFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform int uHorizontal;

const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uTex, 0));
    vec3 result = texture(uTex, vUV).rgb * weights[0];

    if (uHorizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += texture(uTex, vUV + vec2(texel.x * float(i), 0.0)).rgb * weights[i];
            result += texture(uTex, vUV - vec2(texel.x * float(i), 0.0)).rgb * weights[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(uTex, vUV + vec2(0.0, texel.y * float(i))).rgb * weights[i];
            result += texture(uTex, vUV - vec2(0.0, texel.y * float(i))).rgb * weights[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
)";

// Composite: adds bloom to scene, applies exposure, ACES tone map.
// NOTE: output is **linear**; the default framebuffer has GL_FRAMEBUFFER_SRGB
// enabled (Window.cpp) and performs linear→sRGB encoding on write. Emitting
// sRGB-encoded values here would double-gamma and wash out the image.
const char* kCompositeFS = R"(#version 450 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform int uBloomEnabled;

// ACES filmic tone mapper (Stephen Hill fit). Linear HDR in, linear LDR out.
const mat3 kACESInput = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 kACESOutput = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 c) {
    c = kACESInput  * c;
    c = RRTAndODTFit(c);
    c = kACESOutput * c;
    return clamp(c, 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uBloomEnabled != 0) {
        hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    }
    hdr *= uExposure;
    FragColor = vec4(ACESFitted(hdr), 1.0);
}
)";

} // namespace

// ---------- ctor/dtor ------------------------------------------------------

PostProcess::PostProcess() = default;

PostProcess::~PostProcess() {
    ReleaseTargets();
    ReleasePrograms();
    if (fsVBO_) glDeleteBuffers(1, &fsVBO_);
    if (fsVAO_) glDeleteVertexArrays(1, &fsVAO_);
}

// ---------- resource management --------------------------------------------

void PostProcess::Init(int width, int height) {
    if (initialized_) return;

    // Fullscreen triangle geometry (covers clip space).
    static const float kTri[] = {
        // pos          uv
        -1.0f, -1.0f,   0.0f, 0.0f,
         3.0f, -1.0f,   2.0f, 0.0f,
        -1.0f,  3.0f,   0.0f, 2.0f,
    };
    glGenVertexArrays(1, &fsVAO_);
    glGenBuffers(1, &fsVBO_);
    glBindVertexArray(fsVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, fsVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTri), kTri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    CompilePrograms();
    AllocTargets(width, height);

    initialized_ = true;
    ARK_LOG_INFO("Render",
        "PostProcess initialized (" + std::to_string(width) + "x" +
        std::to_string(height) + ", HDR+Bloom)");
}

void PostProcess::AllocTargets(int w, int h) {
    width_ = w;
    height_ = h;

    // HDR scene FBO: RGBA16F color + depth renderbuffer.
    glGenFramebuffers(1, &hdrFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);

    glGenTextures(1, &hdrColor_);
    glBindTexture(GL_TEXTURE_2D, hdrColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColor_, 0);

    glGenRenderbuffers(1, &hdrDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, hdrDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrDepth_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("Render", "PostProcess: HDR FBO incomplete");
    }

    // Bloom ping-pong (half-res).
    const int bw = w / 2 > 0 ? w / 2 : 1;
    const int bh = h / 2 > 0 ? h / 2 : 1;
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &bloomFBO_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[i]);

        glGenTextures(1, &bloomColor_[i]);
        glBindTexture(GL_TEXTURE_2D, bloomColor_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomColor_[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ARK_LOG_ERROR("Render",
                std::string("PostProcess: bloom FBO[") + std::to_string(i) + "] incomplete");
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::ReleaseTargets() {
    if (hdrFBO_)   { glDeleteFramebuffers(1, &hdrFBO_); hdrFBO_ = 0; }
    if (hdrColor_) { glDeleteTextures(1, &hdrColor_);   hdrColor_ = 0; }
    if (hdrDepth_) { glDeleteRenderbuffers(1, &hdrDepth_); hdrDepth_ = 0; }
    for (int i = 0; i < 2; ++i) {
        if (bloomFBO_[i])   { glDeleteFramebuffers(1, &bloomFBO_[i]);   bloomFBO_[i] = 0; }
        if (bloomColor_[i]) { glDeleteTextures(1, &bloomColor_[i]);     bloomColor_[i] = 0; }
    }
    width_ = height_ = 0;
}

void PostProcess::ResizeIfNeeded(int w, int h) {
    if (w == width_ && h == height_) return;
    if (w <= 0 || h <= 0) return;
    ReleaseTargets();
    AllocTargets(w, h);
    ARK_LOG_INFO("Render",
        "PostProcess resized to " + std::to_string(w) + "x" + std::to_string(h));
}

// ---------- shader compilation ---------------------------------------------

uint32_t PostProcess::CompileProgram(const char* vs, const char* fs, const char* name) {
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
                std::string("PostProcess(") + name + ") " +
                (stage == GL_VERTEX_SHADER ? "vs" : "fs") + " compile failed: " + log);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    };

    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Render",
            std::string("PostProcess(") + name + ") link failed: " + log);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

void PostProcess::CompilePrograms() {
    progBright_    = CompileProgram(kFullscreenVS, kBrightFS,    "bright");
    progBlur_      = CompileProgram(kFullscreenVS, kBlurFS,      "blur");
    progComposite_ = CompileProgram(kFullscreenVS, kCompositeFS, "composite");
}

void PostProcess::ReleasePrograms() {
    if (progBright_)    { glDeleteProgram(progBright_);    progBright_ = 0; }
    if (progBlur_)      { glDeleteProgram(progBlur_);      progBlur_ = 0; }
    if (progComposite_) { glDeleteProgram(progComposite_); progComposite_ = 0; }
}

// ---------- per-frame ------------------------------------------------------

void PostProcess::BeginScene(int width, int height) {
    if (!initialized_) Init(width, height);
    ResizeIfNeeded(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
    glViewport(0, 0, width_, height_);
}

void PostProcess::EndScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::Apply(int screenW, int screenH,
                        float exposure,
                        float bloomThreshold,
                        float bloomStrength,
                        int blurIterations) {
    if (!initialized_) return;

    // Disable depth for all fullscreen passes.
    GLboolean depthWas = GL_FALSE;
    glGetBooleanv(GL_DEPTH_TEST, &depthWas);
    glDisable(GL_DEPTH_TEST);

    const int bw = width_  / 2 > 0 ? width_  / 2 : 1;
    const int bh = height_ / 2 > 0 ? height_ / 2 : 1;

    bool bloomActive = bloomEnabled_ && bloomStrength > 0.0f && blurIterations > 0;

    if (bloomActive) {
        // --- Bright pass: HDR scene -> bloom[0] ---
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[0]);
        glViewport(0, 0, bw, bh);
        glUseProgram(progBright_);
        glUniform1i(glGetUniformLocation(progBright_, "uScene"), 0);
        glUniform1f(glGetUniformLocation(progBright_, "uThreshold"), bloomThreshold);
        glUniform1f(glGetUniformLocation(progBright_, "uSoftKnee"),  0.5f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // --- Gaussian blur ping-pong on bloom chain ---
        glUseProgram(progBlur_);
        glUniform1i(glGetUniformLocation(progBlur_, "uTex"), 0);
        int src = 0;
        for (int i = 0; i < blurIterations * 2; ++i) {
            int dst = 1 - src;
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[dst]);
            glViewport(0, 0, bw, bh);
            glUniform1i(glGetUniformLocation(progBlur_, "uHorizontal"), (i & 1) == 0 ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, bloomColor_[src]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            src = dst;
        }
        // Bloom output lives in bloomColor_[src].
        // --- Composite: HDR + bloom -> default FBO ---
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH);
        glUseProgram(progComposite_);
        glUniform1i(glGetUniformLocation(progComposite_, "uScene"), 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloom"), 1);
        glUniform1f(glGetUniformLocation(progComposite_, "uExposure"),     exposure);
        glUniform1f(glGetUniformLocation(progComposite_, "uBloomStrength"), bloomStrength);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloomEnabled"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomColor_[src]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);
    } else {
        // Bloom off: composite HDR directly.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH);
        glUseProgram(progComposite_);
        glUniform1i(glGetUniformLocation(progComposite_, "uScene"), 0);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloom"), 1);
        glUniform1f(glGetUniformLocation(progComposite_, "uExposure"),     exposure);
        glUniform1f(glGetUniformLocation(progComposite_, "uBloomStrength"), 0.0f);
        glUniform1i(glGetUniformLocation(progComposite_, "uBloomEnabled"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColor_);
        glBindVertexArray(fsVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    if (depthWas) glEnable(GL_DEPTH_TEST);
}

} // namespace ark
