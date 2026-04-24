// Window.cpp — GLFW window + GLEW OpenGL context initialization
#include "engine/platform/Window.h"
#include "engine/debug/DebugListenBus.h"
#include <string>

namespace ark {

Window::Window(int width, int height, const std::string& title)
    : width_(width), height_(height) {
    if (!glfwInit()) {
        ARK_LOG_FATAL("Platform", "Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Request an sRGB-capable default framebuffer so the GPU does
    // linear→sRGB on write when GL_FRAMEBUFFER_SRGB is enabled.
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        ARK_LOG_FATAL("Platform", "Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);

    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::string errMsg = "Failed to initialize GLEW: ";
        errMsg += reinterpret_cast<const char*>(glewGetErrorString(err));
        ARK_LOG_FATAL("Platform", errMsg);
    }

    // Enable sRGB framebuffer: shaders output LINEAR; GPU converts to sRGB.
    glEnable(GL_FRAMEBUFFER_SRGB);

    ARK_LOG_INFO("Platform", "Window created: " + std::to_string(width) + "x" + std::to_string(height));
    ARK_LOG_INFO("Platform", std::string("OpenGL version: ") +
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)));
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(window_);
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::SwapBuffers() {
    glfwSwapBuffers(window_);
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->width_ = width;
    self->height_ = height;
    self->wasResized_ = true;
    glViewport(0, 0, width, height);
}

} // namespace ark
