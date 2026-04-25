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

bool GLTexture::UploadCompressed(int width, int height, CompressedFormat format,
                                 const uint8_t* data, size_t dataSize,
                                 int mipCount) {
    if (!data || dataSize == 0) return false;
    if (mipCount < 1) mipCount = 1;

    width_ = width;
    height_ = height;

    GLenum internalFormat = 0;
    int blockSize = 0; // bytes per 4x4 block
    switch (format) {
        case CompressedFormat::BC1_RGB:
            internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;      blockSize = 8; break;
        case CompressedFormat::BC1_RGB_sRGB:
            internalFormat = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;     blockSize = 8; break;
        case CompressedFormat::BC3_RGBA:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;     blockSize = 16; break;
        case CompressedFormat::BC3_RGBA_sRGB:
            internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT; blockSize = 16; break;
        case CompressedFormat::BC5_RG:
            // RGTC2 is core in GL 3.0; GLEW exposes GL_COMPRESSED_RG_RGTC2.
            internalFormat = GL_COMPRESSED_RG_RGTC2;               blockSize = 16; break;
        case CompressedFormat::BC7_RGBA:
            // BPTC is core in GL 4.2 / ARB_texture_compression_bptc.
            internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;        blockSize = 16; break;
        case CompressedFormat::BC7_RGBA_sRGB:
            internalFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;  blockSize = 16; break;
        default:
            return false;
    }

    glBindTexture(GL_TEXTURE_2D, handle_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

    const uint8_t* cursor = data;
    size_t remaining = dataSize;
    int mipW = width;
    int mipH = height;

    for (int level = 0; level < mipCount; ++level) {
        int blocksW = (mipW + 3) / 4;
        int blocksH = (mipH + 3) / 4;
        size_t mipBytes = static_cast<size_t>(blocksW) * blocksH * blockSize;
        if (mipBytes == 0 || mipBytes > remaining) {
            // Corrupt or truncated — fall back to what we have uploaded so far.
            break;
        }
        glCompressedTexImage2D(GL_TEXTURE_2D, level, internalFormat,
                               mipW, mipH, 0,
                               static_cast<GLsizei>(mipBytes), cursor);
        cursor += mipBytes;
        remaining -= mipBytes;
        mipW = (mipW > 1) ? mipW / 2 : 1;
        mipH = (mipH > 1) ? mipH / 2 : 1;
    }

    if (mipCount == 1) {
        // Only mip 0 in file — let GL build a mip chain for far-view quality.
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        GLfloat aniso = (maxAniso < 16.0f) ? maxAniso : 16.0f;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

} // namespace ark
