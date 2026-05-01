#pragma once

#include "engine/rhi/RHIRenderTarget.h"

#include <GL/glew.h>

namespace ark {

class GLRenderTarget : public RHIRenderTarget {
public:
    explicit GLRenderTarget(const RenderTargetDesc& desc);
    ~GLRenderTarget() override;

    GLRenderTarget(const GLRenderTarget&) = delete;
    GLRenderTarget& operator=(const GLRenderTarget&) = delete;

    void Bind() override;
    void Unbind() override;
    void Clear(bool clearColor, bool clearDepth,
               float r, float g, float b, float a,
               float depth) override;

    int  GetWidth()  const override { return width_; }
    int  GetHeight() const override { return height_; }
    int  GetColorAttachmentCount() const override { return static_cast<int>(colorTex_.size()); }
    bool HasDepth() const override { return depthTex_ != 0 || depthRbo_ != 0; }

    uint32_t GetColorTextureHandle(int index) const override {
        if (index < 0 || index >= static_cast<int>(colorTex_.size())) return 0;
        return colorTex_[index];
    }
    uint32_t GetDepthTextureHandle() const override { return depthTex_; }
    uint32_t GetNativeFramebufferHandle() const override { return fbo_; }

    /// True if construction succeeded and the FBO is COMPLETE.
    bool IsValid() const { return valid_; }

private:
    bool Create(const RenderTargetDesc& desc);

    GLuint              fbo_       = 0;
    std::vector<GLuint> colorTex_;       // 0 if attachment is MS texture or absent
    GLuint              depthTex_  = 0;  // 0 if depth is renderbuffer or absent
    GLuint              depthRbo_  = 0;  // 0 if depth is a texture or absent
    int                 width_     = 0;
    int                 height_    = 0;

    bool                valid_     = false;

    // Begin/End save state.
    int prevViewport_[4] = {0, 0, 0, 0};
    GLint prevFbo_       = 0;
};

} // namespace ark
