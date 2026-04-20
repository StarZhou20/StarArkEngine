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

    // --- Keyboard ---
    static bool GetKey(int keyCode);
    static bool GetKeyDown(int keyCode);
    static bool GetKeyUp(int keyCode);

    // --- Mouse buttons ---
    static bool GetMouseButton(int button);
    static bool GetMouseButtonDown(int button);
    static bool GetMouseButtonUp(int button);

    // --- Mouse position & delta ---
    static void GetMousePosition(double& x, double& y);
    static float GetMouseDeltaX();
    static float GetMouseDeltaY();

    // --- Mouse scroll ---
    static float GetScrollDelta();

private:
    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    static GLFWwindow* window_;
    static std::array<bool, GLFW_KEY_LAST + 1> currentKeys_;
    static std::array<bool, GLFW_KEY_LAST + 1> previousKeys_;

    // Mouse button state
    static std::array<bool, 8> currentMouseButtons_;
    static std::array<bool, 8> previousMouseButtons_;

    // Mouse position tracking
    static double mouseX_, mouseY_;
    static double prevMouseX_, prevMouseY_;
    static bool firstMouse_;

    // Scroll accumulator (reset each frame)
    static float scrollDelta_;
};

} // namespace ark
