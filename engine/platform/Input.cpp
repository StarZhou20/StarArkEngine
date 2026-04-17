// Input.cpp — Keyboard and mouse state tracking
#include "engine/platform/Input.h"

namespace ark {

GLFWwindow* Input::window_ = nullptr;
std::array<bool, GLFW_KEY_LAST + 1> Input::currentKeys_ = {};
std::array<bool, GLFW_KEY_LAST + 1> Input::previousKeys_ = {};

void Input::Init(GLFWwindow* window) {
    window_ = window;
    currentKeys_.fill(false);
    previousKeys_.fill(false);
}

void Input::Update() {
    previousKeys_ = currentKeys_;
    for (int i = 0; i <= GLFW_KEY_LAST; ++i) {
        currentKeys_[i] = (glfwGetKey(window_, i) == GLFW_PRESS);
    }
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
    return glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

void Input::GetMousePosition(double& x, double& y) {
    glfwGetCursorPos(window_, &x, &y);
}

} // namespace ark
