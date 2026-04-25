// Window.cpp — GLFW window + GLEW OpenGL context initialization
#include "engine/platform/Window.h"
#include "engine/debug/DebugListenBus.h"
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#endif

namespace ark {

Window::Window(int width, int height, const std::string& title)
    : width_(width), height_(height), baseTitle_(title) {
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

void Window::SetTitle(const std::string& title) {
    if (window_) glfwSetWindowTitle(window_, title.c_str());
}

bool Window::SetIconFromFile(const std::string& path) {
    if (!window_) return false;
#if defined(_WIN32)
    // Convert UTF-8 path → wide for LoadImageW.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    HWND hwnd = glfwGetWin32Window(window_);
    if (!hwnd) return false;

    // Load both small (title bar / taskbar small) and large (Alt-Tab) icons
    // so all OS surfaces use the supplied artwork.
    HICON hIconBig = static_cast<HICON>(LoadImageW(
        nullptr, wpath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_LOADFROMFILE | LR_DEFAULTCOLOR));
    HICON hIconSmall = static_cast<HICON>(LoadImageW(
        nullptr, wpath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_LOADFROMFILE | LR_DEFAULTCOLOR));

    if (!hIconBig && !hIconSmall) {
        ARK_LOG_WARN("Platform", "Failed to load window icon from: " + path);
        return false;
    }
    if (hIconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(hIconBig));
    if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
    ARK_LOG_INFO("Platform", "Window icon set from: " + path);
    return true;
#else
    (void)path;
    ARK_LOG_WARN("Platform", "SetIconFromFile not implemented on this platform");
    return false;
#endif
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->width_ = width;
    self->height_ = height;
    self->wasResized_ = true;
    glViewport(0, 0, width, height);
}

} // namespace ark
