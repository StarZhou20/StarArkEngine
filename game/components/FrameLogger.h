#pragma once

#include "engine/core/AComponent.h"
#include "engine/platform/Time.h"
#include "engine/debug/DebugListenBus.h"
#include <string>

class FrameLogger : public ark::AComponent {
public:
    void Loop(float /*dt*/) override {
        if (ark::Time::FrameCount() % 60 == 0 && ark::Time::FrameCount() > 0) {
            ARK_LOG_TRACE("Core", "Frame " + std::to_string(ark::Time::FrameCount()) +
                          " | dt: " + std::to_string(ark::Time::DeltaTime() * 1000.0f) + "ms" +
                          " | total: " + std::to_string(ark::Time::TotalTime()) + "s");
        }
    }
};
