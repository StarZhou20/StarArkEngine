// Window.h — GLFW window management with OpenGL context
#pragma once

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <string>

namespace ark {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void PollEvents();
    void SwapBuffers();

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    bool WasResized() const { return wasResized_; }
    void ResetResizeFlag() { wasResized_ = false; }

    /// Update the OS-level window title (used for runtime FPS display).
    void SetTitle(const std::string& title);

    /// The title the window was originally constructed with (for FPS prefix).
    const std::string& GetBaseTitle() const { return baseTitle_; }

    GLFWwindow* GetNativeHandle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    int width_;
    int height_;
    bool wasResized_ = false;
    std::string baseTitle_;

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace ark
