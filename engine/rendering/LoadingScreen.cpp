// LoadingScreen.cpp — Progress bar + Segoe UI text via Win32 GDI.
#include "LoadingScreen.h"
#include "engine/platform/Window.h"
#include "engine/debug/DebugListenBus.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

namespace ark {

// -----------------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------------

namespace {

// Shared VS: vec2 pixel position + vec2 uv.
const char* kVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
uniform vec2 uViewport;
void main() {
    vec2 ndc = vec2(aPos.x / uViewport.x * 2.0 - 1.0,
                    1.0 - aPos.y / uViewport.y * 2.0);
    vUV = aUV;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// Solid color FS (progress bar + background panels).
const char* kSolidFS = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; }
)";

// Textured FS for text: samples an R8 alpha mask and tints with uColor.
const char* kTextFS = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform vec4 uColor;
uniform sampler2D uTex;
void main() {
    float a = texture(uTex, vUV).r;
    FragColor = vec4(uColor.rgb, uColor.a * a);
}
)";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ARK_LOG_ERROR("Rendering", std::string("LoadingScreen shader compile: ") + log);
    }
    return s;
}

GLuint LinkProgram(const char* vs, const char* fs) {
    GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Rasterize `text` at `pixelSize` using Segoe UI via GDI. Returns a tightly
// fit R8 bitmap (0..255 alpha). Success => returns true and fills out params.
bool RasterizeTextGDI(const std::string& text, int pixelSize,
                      std::vector<uint8_t>& outPixels, int& outW, int& outH) {
    if (text.empty() || pixelSize < 2) return false;

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);
    if (!memDC) return false;

    // Negative lfHeight = cell height in pixels (exact).
    LOGFONTW lf = {};
    lf.lfHeight         = -pixelSize;
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    if (!font) { DeleteDC(memDC); return false; }

    HFONT oldFont = (HFONT)SelectObject(memDC, font);

    std::wstring wtext = Utf8ToWide(text);

    SIZE size{};
    GetTextExtentPoint32W(memDC, wtext.c_str(), (int)wtext.size(), &size);
    int w = size.cx + 4;   // padding for ClearType spread
    int h = size.cy + 2;
    if (w <= 0 || h <= 0) {
        SelectObject(memDC, oldFont); DeleteObject(font); DeleteDC(memDC);
        return false;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;  // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* dibPixels = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &dibPixels,
                                   nullptr, 0);
    if (!dib) {
        SelectObject(memDC, oldFont); DeleteObject(font); DeleteDC(memDC);
        return false;
    }
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, dib);

    RECT rect{ 0, 0, w, h };
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(memDC, &rect, black);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));

    DrawTextW(memDC, wtext.c_str(), (int)wtext.size(), &rect,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    // Copy to single-channel (take max(R,G,B) as alpha mask).
    outPixels.resize((size_t)w * (size_t)h);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(dibPixels);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + (size_t)y * (size_t)w * 4;
        uint8_t* dst = outPixels.data() + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t a = (uint8_t)std::max({ b, g, r });
            dst[x] = a;
        }
    }
    outW = w; outH = h;

    SelectObject(memDC, oldBmp);
    SelectObject(memDC, oldFont);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(memDC);
    return true;
}
#endif // _WIN32

} // namespace

// -----------------------------------------------------------------------------
// LoadingScreen
// -----------------------------------------------------------------------------

LoadingScreen::LoadingScreen(Window* window) : window_(window) {
    EnsureGL();
}

LoadingScreen::~LoadingScreen() {
    for (auto& kv : textCache_) {
        if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
    }
    textCache_.clear();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (progSolid_) glDeleteProgram(progSolid_);
    if (progText_)  glDeleteProgram(progText_);
}

void LoadingScreen::EnsureGL() {
    progSolid_ = LinkProgram(kVS, kSolidFS);
    uColorSolid_    = glGetUniformLocation(progSolid_, "uColor");
    uViewportSolid_ = glGetUniformLocation(progSolid_, "uViewport");

    progText_ = LinkProgram(kVS, kTextFS);
    uColorText_    = glGetUniformLocation(progText_, "uColor");
    uViewportText_ = glGetUniformLocation(progText_, "uViewport");
    uTexText_      = glGetUniformLocation(progText_, "uTex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          (void*)(sizeof(float) * 2));
    glBindVertexArray(0);
}

const LoadingScreen::TextTex* LoadingScreen::GetOrCreateTextTex(
        const std::string& text, int pixelSize) {
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "%d|", pixelSize);
    std::string key = std::string(prefix) + text;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) return &it->second;

    TextTex entry;
#ifdef _WIN32
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (RasterizeTextGDI(text, pixelSize, pixels, w, h)) {
        glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0,
                     GL_RED, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        entry.width  = w;
        entry.height = h;
    }
#endif
    auto ins = textCache_.emplace(std::move(key), entry);
    return &ins.first->second;
}

void LoadingScreen::DrawQuadPx(float x, float y, float w, float h,
                               float r, float g, float b, float a) {
    const float verts[24] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y + h, 0.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glUseProgram(progSolid_);
    glUniform4f(uColorSolid_, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

float LoadingScreen::MeasureTextPx(const std::string& text, float pixelSize) {
    int px = std::max(2, (int)std::lround(pixelSize));
    const TextTex* tt = GetOrCreateTextTex(text, px);
    if (!tt || !tt->tex) return 0.0f;
    return (float)tt->width;
}

void LoadingScreen::DrawTextPx(const std::string& text, float x, float y,
                               float pixelSize, float r, float g, float b) {
    if (text.empty() || pixelSize < 1.0f) return;
    int px = std::max(2, (int)std::lround(pixelSize));
    const TextTex* tt = GetOrCreateTextTex(text, px);
    if (!tt || !tt->tex) return;

    const float w = (float)tt->width;
    const float h = (float)tt->height;

    const float verts[24] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y + h, 0.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    glUseProgram(progText_);
    glUniform4f(uColorText_, r, g, b, 1.0f);
    glUniform1i(uTexText_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tt->tex);

    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void LoadingScreen::Render(float progress, const std::string& label) {
    if (!window_) return;
    bool indeterminate = (progress < 0.0f);
    if (!indeterminate) progress = std::clamp(progress, 0.0f, 1.0f);

    const int fbW = window_->GetWidth();
    const int fbH = window_->GetHeight();

    glViewport(0, 0, fbW, fbH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(progSolid_);
    glUniform2f(uViewportSolid_, (float)fbW, (float)fbH);
    glUseProgram(progText_);
    glUniform2f(uViewportText_, (float)fbW, (float)fbH);

    glBindVertexArray(vao_);

    const float marginX   = fbW * 0.06f;
    const float barH      = std::max(3.0f, fbH * 0.006f);
    const float bottomGap = fbH * 0.04f;

    // Pixel heights. Scale with window but clamp to readable minimums.
    const float labelPx = std::max(12.0f, fbH / 55.0f);
    const float pctPx   = std::max(16.0f, fbH / 42.0f);

    char pctBuf[16];
    std::string pctStr;
    if (indeterminate) {
        pctStr = "...";
    } else {
        std::snprintf(pctBuf, sizeof(pctBuf), "%d%%",
                      (int)(progress * 100.0f + 0.5f));
        pctStr = pctBuf;
    }
    const float pctTextW = MeasureTextPx(pctStr, pctPx);
    const float pctGap   = fbW * 0.012f;

    const float barX = marginX;
    const float barY = fbH - bottomGap - barH;
    const float barW = fbW - marginX - pctGap - pctTextW - marginX;

    // Track
    DrawQuadPx(barX, barY, barW, barH, 0.12f, 0.12f, 0.14f, 1.0f);

    // Fill
    if (indeterminate) {
        float t = (float)glfwGetTime();
        float phase = std::fmod(t * 0.6f, 1.0f);
        float segW = barW * 0.25f;
        float segX = barX + (barW - segW) * phase;
        DrawQuadPx(segX, barY, segW, barH, 0.90f, 0.90f, 0.92f, 1.0f);
    } else {
        DrawQuadPx(barX, barY, barW * progress, barH, 0.90f, 0.90f, 0.92f, 1.0f);
    }

    // Percentage text: vertically centered on the bar.
    const TextTex* pctTT = GetOrCreateTextTex(pctStr,
            std::max(2, (int)std::lround(pctPx)));
    const float pctH = pctTT ? (float)pctTT->height : pctPx;
    const float pctX = barX + barW + pctGap;
    const float pctY = barY + barH * 0.5f - pctH * 0.5f;
    DrawTextPx(pctStr, pctX, pctY, pctPx, 0.90f, 0.90f, 0.92f);

    // Label above the bar.
    if (!label.empty()) {
        const TextTex* labTT = GetOrCreateTextTex(label,
                std::max(2, (int)std::lround(labelPx)));
        const float labH = labTT ? (float)labTT->height : labelPx;
        const float labelY = barY - labH - fbH * 0.012f;
        DrawTextPx(label, barX, labelY, labelPx, 0.70f, 0.70f, 0.72f);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);

    window_->SwapBuffers();
    window_->PollEvents();
}

} // namespace ark
