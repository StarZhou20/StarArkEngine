// IDebugListener.h — Abstract listener interface with RAII auto-register/unregister
#pragma once

#include "engine/debug/DebugListenBus.h"

namespace ark {

class IDebugListener {
public:
    IDebugListener() {
        DebugListenBus::Get().RegisterListener(this);
    }

    virtual ~IDebugListener() {
        DebugListenBus::Get().UnregisterListener(this);
    }

    IDebugListener(const IDebugListener&) = delete;
    IDebugListener& operator=(const IDebugListener&) = delete;

    virtual void OnDebugMessage(const LogMessage& msg) = 0;
};

} // namespace ark
