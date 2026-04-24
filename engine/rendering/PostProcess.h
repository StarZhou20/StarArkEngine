#pragma once

#include <cstdint>

namespace ark {

/// PostProcess: owns an HDR offscreen framebuffer + bloom ping-pong chain, and
/// runs the final tone-mapping composite to the default framebuffer.
///
/// Lifecycle:
///   Init(w, h)           -> allocate GL resources
///   BeginScene(w, h)     -> bind HDR FBO (auto-resizes on size change)
///   ...scene draws here (cameras render into HDR FBO)...
///   EndScene()           -> unbind HDR FBO
///   Apply(exposure, ...) -> bright-pass + blur + composite to FBO 0
///
/// Phase 10: HDR FBO + Bloom.
class PostProcess {
public:
    PostProcess();
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    /// Allocate initial GL resources at the given size. Safe to call once.
    void Init(int width, int height);

    /// Bind HDR FBO for scene rendering. Resizes internal targets if the
    /// window size changed since last frame.
    void BeginScene(int width, int height);

    /// Unbind HDR FBO (revert to default framebuffer).
    void EndScene();

    /// Run bright-pass + gaussian blur ping-pong + composite to default FB.
    void Apply(int screenW, int screenH,
               float exposure,
               float bloomThreshold,
               float bloomStrength,
               int blurIterations);

    // Bloom / exposure configuration (also settable at call time via Apply()).
    bool  IsBloomEnabled() const { return bloomEnabled_; }
    void  SetBloomEnabled(bool enabled) { bloomEnabled_ = enabled; }

private:
    void ResizeIfNeeded(int width, int height);
    void AllocTargets(int width, int height);
    void ReleaseTargets();

    void CompilePrograms();
    void ReleasePrograms();

    static uint32_t CompileProgram(const char* vs, const char* fs, const char* name);

    // HDR scene framebuffer (full-res RGBA16F + depth)
    uint32_t hdrFBO_   = 0;
    uint32_t hdrColor_ = 0;  // RGBA16F texture
    uint32_t hdrDepth_ = 0;  // depth renderbuffer

    // Bloom ping-pong (half-res RGBA16F)
    uint32_t bloomFBO_[2]     = {0, 0};
    uint32_t bloomColor_[2]   = {0, 0};

    int width_  = 0;
    int height_ = 0;

    // Fullscreen triangle VAO/VBO
    uint32_t fsVAO_ = 0;
    uint32_t fsVBO_ = 0;

    // Shader programs
    uint32_t progBright_    = 0;
    uint32_t progBlur_      = 0;
    uint32_t progComposite_ = 0;

    bool bloomEnabled_ = true;
    bool initialized_  = false;
};

} // namespace ark
