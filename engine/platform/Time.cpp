// Time.cpp — Delta time calculation using GLFW timer
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "engine/platform/Time.h"

namespace ark {

float Time::deltaTime_ = 0.0f;
float Time::totalTime_ = 0.0f;
double Time::lastFrameTime_ = 0.0;
uint64_t Time::frameCount_ = 0;

void Time::Init() {
    lastFrameTime_ = glfwGetTime();
    deltaTime_ = 0.0f;
    totalTime_ = 0.0f;
    frameCount_ = 0;
}

void Time::Update() {
    double currentTime = glfwGetTime();
    deltaTime_ = static_cast<float>(currentTime - lastFrameTime_);
    lastFrameTime_ = currentTime;
    totalTime_ += deltaTime_;
    ++frameCount_;
}

} // namespace ark
