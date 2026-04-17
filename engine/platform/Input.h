// Input.h — Keyboard and mouse polling via GLFW
#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <array>

namespace ark {

class Input {
public:
    static void Init(GLFWwindow* window);
    static void Update();

    static bool GetKey(int keyCode);
    static bool GetKeyDown(int keyCode);
    static bool GetKeyUp(int keyCode);
    static bool GetMouseButton(int button);
    static void GetMousePosition(double& x, double& y);

private:
    static GLFWwindow* window_;
    static std::array<bool, GLFW_KEY_LAST + 1> currentKeys_;
    static std::array<bool, GLFW_KEY_LAST + 1> previousKeys_;
};

} // namespace ark
