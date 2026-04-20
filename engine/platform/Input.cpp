// Input.cpp — Keyboard and mouse state tracking
#include "engine/platform/Input.h"

namespace ark {

GLFWwindow* Input::window_ = nullptr;
std::array<bool, GLFW_KEY_LAST + 1> Input::currentKeys_ = {};
std::array<bool, GLFW_KEY_LAST + 1> Input::previousKeys_ = {};
std::array<bool, 8> Input::currentMouseButtons_ = {};
std::array<bool, 8> Input::previousMouseButtons_ = {};
double Input::mouseX_ = 0.0;
double Input::mouseY_ = 0.0;
double Input::prevMouseX_ = 0.0;
double Input::prevMouseY_ = 0.0;
bool Input::firstMouse_ = true;
float Input::scrollDelta_ = 0.0f;

void Input::Init(GLFWwindow* window) {
    window_ = window;
    currentKeys_.fill(false);
    previousKeys_.fill(false);
    currentMouseButtons_.fill(false);
    previousMouseButtons_.fill(false);
    firstMouse_ = true;
    scrollDelta_ = 0.0f;
    glfwSetScrollCallback(window_, ScrollCallback);
}

void Input::Update() {
    previousKeys_ = currentKeys_;
    for (int i = 0; i <= GLFW_KEY_LAST; ++i) {
        currentKeys_[i] = (glfwGetKey(window_, i) == GLFW_PRESS);
    }

    // Mouse buttons
    previousMouseButtons_ = currentMouseButtons_;
    for (int i = 0; i < 8; ++i) {
        currentMouseButtons_[i] = (glfwGetMouseButton(window_, i) == GLFW_PRESS);
    }

    // Mouse position delta
    prevMouseX_ = mouseX_;
    prevMouseY_ = mouseY_;
    glfwGetCursorPos(window_, &mouseX_, &mouseY_);
    if (firstMouse_) {
        prevMouseX_ = mouseX_;
        prevMouseY_ = mouseY_;
        firstMouse_ = false;
    }
}

void Input::ScrollCallback(GLFWwindow* /*window*/, double /*xOffset*/, double yOffset) {
    scrollDelta_ += static_cast<float>(yOffset);
}

bool Input::GetKey(int keyCode) {
    return currentKeys_[keyCode];
}

bool Input::GetKeyDown(int keyCode) {
    return currentKeys_[keyCode] && !previousKeys_[keyCode];
}

bool Input::GetKeyUp(int keyCode) {
    return !currentKeys_[keyCode] && previousKeys_[keyCode];
}

bool Input::GetMouseButton(int button) {
    return currentMouseButtons_[button];
}

bool Input::GetMouseButtonDown(int button) {
    return currentMouseButtons_[button] && !previousMouseButtons_[button];
}

bool Input::GetMouseButtonUp(int button) {
    return !currentMouseButtons_[button] && previousMouseButtons_[button];
}

void Input::GetMousePosition(double& x, double& y) {
    x = mouseX_;
    y = mouseY_;
}

float Input::GetMouseDeltaX() {
    return static_cast<float>(mouseX_ - prevMouseX_);
}

float Input::GetMouseDeltaY() {
    return static_cast<float>(mouseY_ - prevMouseY_);
}

float Input::GetScrollDelta() {
    float delta = scrollDelta_;
    scrollDelta_ = 0.0f;
    return delta;
}

} // namespace ark
