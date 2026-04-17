#pragma once

#include <memory>

namespace ark {

class AObject;

class IObjectOwner {
public:
    virtual ~IObjectOwner() = default;
    virtual void TransferToPersistent(AObject* obj) = 0;
    virtual void NotifyObjectDestroyed() = 0;
};

} // namespace ark
