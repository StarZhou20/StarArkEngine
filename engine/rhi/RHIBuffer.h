#pragma once

#include "RHITypes.h"
#include <cstddef>

namespace ark {

class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;
    virtual void Upload(const void* data, size_t size) = 0;
    virtual size_t GetSize() const = 0;
    virtual BufferType GetType() const = 0;
};

} // namespace ark
