// LoadingScreen.h — Minimal black-background loading overlay with progress bar
// and text rendered via the OS font rasterizer (Segoe UI on Windows) so the
// loading screen matches the look of modern native apps.
//
// Renders with raw GL (no dependency on ForwardRenderer / materials) so it can
// be shown during the asset loading phase while the main render pipeline isn't
// ready yet.
#pragma once

#include <GL/glew.h>
#include <string>
#include <unordered_map>

namespace ark {

class Window;

class LoadingScreen {
public:
    explicit LoadingScreen(Window* window);
    ~LoadingScreen();

    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    /// Draw one frame of the loading screen. `progress` is clamped to [0, 1].
    /// Pass any negative value (e.g. -1.0f) for an indeterminate (sliding)
    /// animation when real progress is not yet available.
    /// `label` is a short status line (shown above the bar, small text).
    /// Polls window events and swaps buffers so the OS considers the window
    /// responsive (no "not responding" title bar).
    void Render(float progress, const std::string& label);

private:
    Window* window_ = nullptr;

    // Solid-color program (quad / bar fills).
    GLuint progSolid_ = 0;
    GLint  uColorSolid_   = -1;
    GLint  uViewportSolid_ = -1;

    // Textured program (text blits against an alpha mask).
    GLuint progText_ = 0;
    GLint  uColorText_    = -1;
    GLint  uViewportText_ = -1;
    GLint  uTexText_      = -1;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    // Cached text textures, keyed by "size|text" so the same label+size
    // reuses an existing rasterization.
    struct TextTex {
        GLuint tex    = 0;
        int    width  = 0;
        int    height = 0;
    };
    std::unordered_map<std::string, TextTex> textCache_;

    void EnsureGL();

    void DrawQuadPx(float x, float y, float w, float h,
                    float r, float g, float b, float a);

    /// Draw `text` at (x, y) in pixel-space. `pixelSize` is the font's cap
    /// height in pixels. The string is rasterized via the OS (GDI on Windows)
    /// and cached for the lifetime of the LoadingScreen.
    void DrawTextPx(const std::string& text, float x, float y, float pixelSize,
                    float r, float g, float b);

    /// Measure the width (in pixels) that `DrawTextPx` would produce for the
    /// given string + size. Used for right-alignment.
    float MeasureTextPx(const std::string& text, float pixelSize);

    /// Internal: look up or create a text texture for (text, pixelSize).
    const TextTex* GetOrCreateTextTex(const std::string& text, int pixelSize);
};

} // namespace ark
