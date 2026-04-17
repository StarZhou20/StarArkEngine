#pragma once

#include "engine/rhi/RHITexture.h"
#include <GL/glew.h>

namespace ark {

class GLTexture : public RHITexture {
public:
    GLTexture();
    ~GLTexture() override;

    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    void Upload(int width, int height, int channels, const uint8_t* data) override;
    int GetWidth() const override { return width_; }
    int GetHeight() const override { return height_; }

    GLuint GetHandle() const { return handle_; }
    void Bind(GLuint unit = 0) const;

private:
    GLuint handle_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace ark
