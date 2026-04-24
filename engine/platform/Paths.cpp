// Paths.cpp
#include "engine/platform/Paths.h"
#include "engine/serialization/TomlDoc.h"
#include "engine/debug/DebugListenBus.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

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

namespace {
    std::vector<std::string> g_modLoadOrder;
    bool                     g_modOrderLoaded = false;

    void LoadModOrderImpl() {
        g_modLoadOrder.clear();
        g_modOrderLoaded = true;

        fs::path p = Paths::Mods() / "load_order.toml";
        std::error_code ec;
        if (!fs::exists(p, ec)) return;

        std::ifstream f(p, std::ios::binary);
        if (!f) return;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();

        std::string err;
        int errLine = 0;
        auto doc = TomlDoc::Parse(text, &err, &errLine);
        if (!doc) {
            ARK_LOG_WARN("Paths",
                std::string("load_order.toml parse error (line ") + std::to_string(errLine)
                + "): " + err);
            return;
        }
        const auto* aot = doc->Root().FindArrayOfTables("mod");
        if (!aot) return;
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const auto& t = (*aot)[i];
            bool enabled = true;
            if (const auto* v = t.Find("enabled"); v && v->IsBool()) enabled = v->AsBool();
            if (!enabled) continue;
            const auto* n = t.Find("name");
            if (!n || !n->IsString()) continue;
            const std::string& name = n->AsString();
            if (name.empty()) continue;
            g_modLoadOrder.push_back(name);
        }
        ARK_LOG_INFO("Paths",
            std::string("load_order.toml: ") + std::to_string(g_modLoadOrder.size()) + " mod(s) enabled");
    }
}

fs::path Paths::ResolveResource(const std::string& logical) {
    if (!g_modOrderLoaded) LoadModOrderImpl();

    // Iterate mods in load order; last line wins per TOML convention — use reverse
    // so later-listed mods override earlier ones (common mod-manager convention).
    for (auto it = g_modLoadOrder.rbegin(); it != g_modLoadOrder.rend(); ++it) {
        fs::path candidate = Mods() / *it / logical;
        std::error_code ec;
        if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
            return candidate;
        }
    }
    return Content() / logical;
}

void Paths::ReloadModOrder() {
    g_modOrderLoaded = false;
    LoadModOrderImpl();
}

void Paths::SetDevContentOverride(const fs::path& absolutePath) {
    g_devContentOverride = absolutePath;
}

} // namespace ark
