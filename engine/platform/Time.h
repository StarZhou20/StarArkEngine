// Time.h — Frame timing and delta time tracking
#pragma once

#include <cstdint>

namespace ark {

class Time {
public:
    static void Init();
    static void Update();

    static float DeltaTime() { return deltaTime_; }
    static float TotalTime() { return totalTime_; }
    static uint64_t FrameCount() { return frameCount_; }

private:
    static float deltaTime_;
    static float totalTime_;
    static double lastFrameTime_;
    static uint64_t frameCount_;
};

} // namespace ark
