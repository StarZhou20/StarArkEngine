#include "GLRenderTarget.h"

#include "engine/debug/DebugListenBus.h"

#include <string>

namespace ark {

namespace {

struct GLFormatTriple {
    GLenum internalFormat;
    GLenum format;
    GLenum type;
};

GLFormatTriple ColorTriple(RTColorFormat f) {
    switch (f) {
        case RTColorFormat::RGBA8_UNorm: return { GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE };
        case RTColorFormat::RGBA16F:     return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
        case RTColorFormat::RG16F:       return { GL_RG16F,   GL_RG,   GL_HALF_FLOAT };
        case RTColorFormat::R16F:        return { GL_R16F,    GL_RED,  GL_HALF_FLOAT };
        case RTColorFormat::R32F:        return { GL_R32F,    GL_RED,  GL_FLOAT };
        case RTColorFormat::R8_UNorm:    return { GL_R8,      GL_RED,  GL_UNSIGNED_BYTE };
        default:                         return { GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE };
    }
}

GLenum DepthInternal(RTDepthFormat f) {
    switch (f) {
        case RTDepthFormat::Depth24:         return GL_DEPTH_COMPONENT24;
        case RTDepthFormat::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
        case RTDepthFormat::Depth32F:        return GL_DEPTH_COMPONENT32F;
        default:                             return 0;
    }
}

GLenum DepthAttachPoint(RTDepthFormat f) {
    return (f == RTDepthFormat::Depth24Stencil8) ? GL_DEPTH_STENCIL_ATTACHMENT
                                                 : GL_DEPTH_ATTACHMENT;
}

GLenum DepthUploadFormat(RTDepthFormat f) {
    return (f == RTDepthFormat::Depth24Stencil8) ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
}

GLenum DepthUploadType(RTDepthFormat f) {
    switch (f) {
        case RTDepthFormat::Depth24Stencil8: return GL_UNSIGNED_INT_24_8;
        case RTDepthFormat::Depth32F:        return GL_FLOAT;
        default:                             return GL_FLOAT;
    }
}

} // namespace

GLRenderTarget::GLRenderTarget(const RenderTargetDesc& desc) {
    valid_ = Create(desc);
}

GLRenderTarget::~GLRenderTarget() {
    for (GLuint t : colorTex_) {
        if (t) glDeleteTextures(1, &t);
    }
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
    if (fbo_)      glDeleteFramebuffers(1, &fbo_);
}

bool GLRenderTarget::Create(const RenderTargetDesc& desc) {
    width_  = desc.width;
    height_ = desc.height;
    if (width_ <= 0 || height_ <= 0) {
        ARK_LOG_ERROR("RHI", "GLRenderTarget: invalid size");
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // ---- Color attachments ----
    colorTex_.assign(desc.colors.size(), 0);
    std::vector<GLenum> drawBuffers;
    drawBuffers.reserve(desc.colors.size());
    for (size_t i = 0; i < desc.colors.size(); ++i) {
        const auto& c = desc.colors[i];
        if (c.format == RTColorFormat::None) {
            drawBuffers.push_back(GL_NONE);
            continue;
        }
        GLFormatTriple t = ColorTriple(c.format);
        GLuint tex = 0;
        glGenTextures(1, &tex);

        if (c.samples > 1) {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, c.samples,
                                    t.internalFormat, width_, height_, GL_TRUE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_2D_MULTISAMPLE, tex, 0);
        } else {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, t.internalFormat,
                         width_, height_, 0, t.format, t.type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_2D, tex, 0);
        }
        colorTex_[i] = tex;
        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
    }

    if (drawBuffers.empty()) {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    } else {
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    }

    // ---- Depth/stencil ----
    if (desc.depth.format != RTDepthFormat::None) {
        GLenum internal = DepthInternal(desc.depth.format);
        GLenum attach   = DepthAttachPoint(desc.depth.format);
        if (desc.depth.renderbuffer) {
            glGenRenderbuffers(1, &depthRbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
            if (desc.depth.samples > 1) {
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, desc.depth.samples,
                                                 internal, width_, height_);
            } else {
                glRenderbufferStorage(GL_RENDERBUFFER, internal, width_, height_);
            }
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, attach, GL_RENDERBUFFER, depthRbo_);
        } else {
            glGenTextures(1, &depthTex_);
            GLenum target = (desc.depth.samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
            glBindTexture(target, depthTex_);
            if (desc.depth.samples > 1) {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, desc.depth.samples,
                                        internal, width_, height_, GL_TRUE);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, internal, width_, height_, 0,
                             DepthUploadFormat(desc.depth.format),
                             DepthUploadType(desc.depth.format), nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                if (desc.depth.shadowSampler && desc.depth.clampToBorderWhite) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
                } else {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                }
                if (desc.depth.shadowSampler) {
                    // Hardware PCF: sampler2DShadow returns [0,1] from 2x2
                    // bilinear depth-compare (LESS_OR_EQUAL).
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                }
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, attach, target, depthTex_, 0);
        }
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ARK_LOG_ERROR("RHI", std::string("GLRenderTarget FBO incomplete: 0x") +
                                 std::to_string(status));
        return false;
    }
    return true;
}

void GLRenderTarget::Bind() {
    glGetIntegerv(GL_VIEWPORT, prevViewport_);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void GLRenderTarget::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    glViewport(prevViewport_[0], prevViewport_[1],
               prevViewport_[2], prevViewport_[3]);
}

void GLRenderTarget::Clear(bool clearColor, bool clearDepth,
                           float r, float g, float b, float a,
                           float depth) {
    GLbitfield mask = 0;
    if (clearColor && !colorTex_.empty()) {
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clearDepth && HasDepth()) {
        glClearDepth(depth);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (mask) glClear(mask);
}

} // namespace ark
