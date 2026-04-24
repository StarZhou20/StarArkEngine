#include "GLTexture.h"
#include <cassert>

namespace ark {

GLTexture::GLTexture() {
    glGenTextures(1, &handle_);
}

GLTexture::~GLTexture() {
    if (handle_) {
        glDeleteTextures(1, &handle_);
    }
}

void GLTexture::Upload(int width, int height, int channels, const uint8_t* data,
                       TextureFormat format) {
    assert(data && "GLTexture::Upload: null data");
    width_ = width;
    height_ = height;

    glBindTexture(GL_TEXTURE_2D, handle_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Client-supplied data format (what's in 'data')
    GLenum dataFormat = GL_RGBA;
    if (channels == 1) dataFormat = GL_RED;
    else if (channels == 2) dataFormat = GL_RG;
    else if (channels == 3) dataFormat = GL_RGB;
    else if (channels == 4) dataFormat = GL_RGBA;

    // GPU storage format: sRGB for color data, linear otherwise.
    // Single / dual channel data is always linear (can't be sRGB anyway).
    const bool useSRGB = (format == TextureFormat::sRGB_Auto) && (channels >= 3);

    GLint internalFormat;
    if (useSRGB) {
        internalFormat = (channels == 4) ? GL_SRGB8_ALPHA8 : GL_SRGB8;
    } else {
        switch (channels) {
            case 1: internalFormat = GL_R8;    break;
            case 2: internalFormat = GL_RG8;   break;
            case 3: internalFormat = GL_RGB8;  break;
            default: internalFormat = GL_RGBA8; break;
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Anisotropic filtering (GL 4.6 or EXT_texture_filter_anisotropic).
    // Cap to 16x or hardware max, whichever is lower.
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        GLfloat aniso = (maxAniso < 16.0f) ? maxAniso : 16.0f;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLTexture::Bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, handle_);
}

} // namespace ark
