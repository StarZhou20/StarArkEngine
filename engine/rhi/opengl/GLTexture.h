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

    void Upload(int width, int height, int channels, const uint8_t* data,
                TextureFormat format = TextureFormat::sRGB_Auto) override;
    bool UploadCompressed(int width, int height, CompressedFormat format,
                          const uint8_t* data, size_t dataSize,
                          int mipCount) override;
    void Bind(int unit = 0) const override;
    int GetWidth() const override { return width_; }
    int GetHeight() const override { return height_; }

    GLuint GetHandle() const { return handle_; }

private:
    GLuint handle_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace ark
