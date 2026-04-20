#pragma once

#include <cstdint>

namespace ark {

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual void Upload(int width, int height, int channels, const uint8_t* data) = 0;
    virtual void Bind(int unit = 0) const = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace ark
