// Paths.cpp
#include "engine/platform/Paths.h"

#include <filesystem>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

namespace fs = std::filesystem;

namespace ark {

namespace {
    fs::path g_gameRoot;
    fs::path g_devContentOverride;  // empty = not set
    bool     g_initialized = false;

    fs::path DetectExeDir(const char* argv0) {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            return fs::path(buf).parent_path();
        }
#else
        char buf[PATH_MAX] = {0};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            return fs::path(buf).parent_path();
        }
#endif
        if (argv0) {
            std::error_code ec;
            auto abs = fs::absolute(argv0, ec);
            if (!ec) return abs.parent_path();
        }
        return fs::current_path();
    }
}

void Paths::Init(const char* argv0) {
    if (g_initialized) return;
    g_gameRoot = DetectExeDir(argv0);
    std::error_code ec;
    fs::current_path(g_gameRoot, ec);  // anchor cwd to exe dir
    g_initialized = true;
}

const fs::path& Paths::GameRoot() {
    return g_gameRoot;
}

fs::path Paths::Content() {
    if (!g_devContentOverride.empty()) return g_devContentOverride;
    return g_gameRoot / "content";
}

fs::path Paths::Mods()  { return g_gameRoot / "mods"; }
fs::path Paths::Logs()  { return g_gameRoot / "logs"; }

fs::path Paths::UserData(const std::string& title) {
#if defined(_WIN32)
    wchar_t* p = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) {
        base = fs::path(p);
        CoTaskMemFree(p);
    } else {
        base = g_gameRoot;
    }
    return base / "StarArk" / title;
#else
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) / ".local" / "share" : g_gameRoot;
    return base / "StarArk" / title;
#endif
}

fs::path Paths::ResolveContent(const std::string& relative) {
    return Content() / relative;
}

void Paths::SetDevContentOverride(const fs::path& absolutePath) {
    g_devContentOverride = absolutePath;
}

} // namespace ark
