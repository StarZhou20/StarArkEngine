// Paths.cpp
#include "engine/platform/Paths.h"
#include "engine/serialization/TomlDoc.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/mod/ModInfo.h"
#include "engine/scripting/ScriptHost.h"  // ARK_SCRIPT_API_VERSION

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>

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
    std::vector<std::string>                    g_modLoadOrder;
    std::unordered_map<std::string, ModInfo>    g_modInfos;     // id -> validated ModInfo
    bool                                        g_modOrderLoaded = false;

    void LoadModOrderImpl() {
        g_modLoadOrder.clear();
        g_modInfos.clear();
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

        // v0.3 ModSpec §2 gating context.
        ModVersion engineVer = ModVersion::Parse(kEngineVersionString)
                                   .value_or(ModVersion{0, 0, 0});
        const int  scriptApi = ARK_SCRIPT_API_VERSION;

        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const auto& t = (*aot)[i];
            bool enabled = true;
            if (const auto* v = t.Find("enabled"); v && v->IsBool()) enabled = v->AsBool();
            if (!enabled) continue;
            const auto* n = t.Find("name");
            if (!n || !n->IsString()) continue;
            const std::string& name = n->AsString();
            if (name.empty()) continue;

            // v0.3 ModSpec §2 — try to load mod.toml for this entry.
            // Backwards-compat: if the mod has no mod.toml at all (legacy v0.2
            // mod folder, e.g. textures-only override), accept it but warn so
            // existing samples keep working through the migration.
            const fs::path modDir   = Paths::Mods() / name;
            const fs::path tomlPath = modDir / "mod.toml";
            if (!fs::exists(tomlPath, ec)) {
                ARK_LOG_WARN("Paths",
                    "Mod '" + name + "' has no mod.toml (legacy v0.2 layout)."
                    " v0.3+ mods are required to ship one. Loading anyway.");
                g_modLoadOrder.push_back(name);
                continue;
            }

            ModInfo info = ModInfo::LoadFromDirectory(modDir, engineVer, scriptApi);
            if (!info.valid) {
                ARK_LOG_ERROR("Paths",
                    "Mod '" + name + "' failed validation: " + info.error
                    + " — skipped.");
                continue;
            }
            g_modLoadOrder.push_back(name);
            g_modInfos.emplace(name, std::move(info));
        }

        // v0.3 ModSpec §2 — second pass: depends_on enforcement.
        // A mod whose any depends_on.id is not in the validated registry is
        // unloadable. Drop it from g_modInfos AND g_modLoadOrder. Iterate to
        // fixed point because dropping A may invalidate B that depends on A.
        for (bool changed = true; changed; ) {
            changed = false;
            for (auto it = g_modInfos.begin(); it != g_modInfos.end(); ) {
                std::string missing;
                for (const auto& dep : it->second.depends_on) {
                    if (g_modInfos.find(dep.id) == g_modInfos.end()) {
                        missing = dep.id;
                        break;
                    }
                }
                if (!missing.empty()) {
                    ARK_LOG_ERROR("Paths",
                        "Mod '" + it->first + "' depends_on '" + missing
                        + "' which is not enabled — skipped.");
                    auto orderIt = std::find(g_modLoadOrder.begin(), g_modLoadOrder.end(), it->first);
                    if (orderIt != g_modLoadOrder.end()) g_modLoadOrder.erase(orderIt);
                    it = g_modInfos.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }

        // v0.3 ModSpec §2.2 — addon applies_to soft check.
        // If an addon lists applies_to=[...] but none of the listed game ids
        // is enabled, log advisory. Game ids may be implicit (e.g. "vanilla"
        // not yet packaged), and addon-only smoke tests intentionally run
        // without a game mod, so this stays advisory through v0.3.
        // Demote from WARN to INFO when zero game mods are enabled (likely
        // a smoke / unit-test scenario rather than a real misconfiguration).
        bool anyGameModEnabled = false;
        for (const auto& [id, info] : g_modInfos) {
            if (info.type == ModType::Game) { anyGameModEnabled = true; break; }
        }
        for (const auto& [id, info] : g_modInfos) {
            if (info.type != ModType::Addon || info.applies_to.empty()) continue;
            bool anyMatch = false;
            for (const auto& targetGameId : info.applies_to) {
                auto tIt = g_modInfos.find(targetGameId);
                if (tIt != g_modInfos.end() && tIt->second.type == ModType::Game) {
                    anyMatch = true;
                    break;
                }
            }
            if (anyMatch) continue;
            const std::string msg = "Addon mod '" + id
                + "' applies_to has no enabled game mod"
                  " — running anyway (advisory).";
            if (anyGameModEnabled) ARK_LOG_WARN("Paths", msg);
            else                   ARK_LOG_INFO("Paths", msg);
        }

        ARK_LOG_INFO("Paths",
            std::string("load_order.toml: ") + std::to_string(g_modLoadOrder.size())
            + " mod(s) enabled (" + std::to_string(g_modInfos.size()) + " with valid mod.toml)");
    }
}

namespace {
    // Strip a known scheme prefix and return the remainder. Out-param 'modIdOut'
    // is filled when scheme == "mod://<id>/...". Returns the matched scheme tag,
    // or empty string when no scheme present.
    enum class Scheme { None, Engine, Mod, Relative };

    Scheme DetectScheme(const std::string& logical, std::string& rest, std::string& modIdOut) {
        modIdOut.clear();
        rest.clear();
        if (logical.rfind("./", 0) == 0) {
            rest = logical.substr(2);
            return Scheme::Relative;
        }
        if (logical.rfind("engine://", 0) == 0) {
            rest = logical.substr(9);
            return Scheme::Engine;
        }
        if (logical.rfind("mod://", 0) == 0) {
            std::string body = logical.substr(6);
            auto slash = body.find('/');
            if (slash == std::string::npos || slash == 0) {
                return Scheme::None; // malformed, treat as bare
            }
            modIdOut = body.substr(0, slash);
            rest     = body.substr(slash + 1);
            return Scheme::Mod;
        }
        rest = logical;
        return Scheme::None;
    }

    // Rate-limit deprecation log so we don't spam if a TOML has 100 bare paths.
    bool g_warnedBare = false;
    bool g_warnedRelativeNoCtx = false;
}

fs::path Paths::ResolveResource(const std::string& logical, const std::string& currentModId) {
    if (!g_modOrderLoaded) LoadModOrderImpl();

    // Absolute paths bypass VFS entirely — they're already resolved by the caller.
    // (Some scenes pass absolute Bistro/SDK asset paths into the loaders.)
    if (fs::path(logical).is_absolute()) {
        return fs::path(logical);
    }

    std::string rest, modId;
    Scheme s = DetectScheme(logical, rest, modId);

    switch (s) {
    case Scheme::Engine:
        // engine:// is engine-shipped, not mod-overridable.
        return Content() / rest;

    case Scheme::Mod:
        return Mods() / modId / rest;

    case Scheme::Relative: {
        if (currentModId.empty()) {
            if (!g_warnedRelativeNoCtx) {
                ARK_LOG_WARN("Paths",
                    std::string("'./' path without current mod context: '") + logical
                    + "'. Falling back to Content().");
                g_warnedRelativeNoCtx = true;
            }
            return Content() / rest;
        }
        return Mods() / currentModId / rest;
    }

    case Scheme::None:
    default:
        break;
    }

    // Legacy bare path: walk load_order stack then Content().
    if (!g_warnedBare) {
        ARK_LOG_WARN("Paths",
            std::string("Bare path without scheme prefix is deprecated (v0.3 ModSpec §3): '")
            + logical + "'. Use './', 'mod://<id>/' or 'engine://'.");
        g_warnedBare = true;
    }
    for (auto it = g_modLoadOrder.rbegin(); it != g_modLoadOrder.rend(); ++it) {
        fs::path candidate = Mods() / *it / logical;
        std::error_code ec;
        if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
            return candidate;
        }
    }
    return Content() / logical;
}

fs::path Paths::ResolveResource(const std::string& logical) {
    return ResolveResource(logical, GetCurrentModId());
}

// ---------------------------------------------------------------------------
// Thread-local mod context stack (v0.3 ModSpec §3)
// ---------------------------------------------------------------------------
namespace {
    thread_local std::vector<std::string> tls_modStack;
    const std::string                     k_emptyModId;
}

void Paths::PushCurrentModId(const std::string& modId) {
    tls_modStack.push_back(modId);
}

void Paths::PopCurrentModId() {
    if (!tls_modStack.empty()) tls_modStack.pop_back();
}

const std::string& Paths::GetCurrentModId() {
    return tls_modStack.empty() ? k_emptyModId : tls_modStack.back();
}

void Paths::ReloadModOrder() {
    g_modOrderLoaded = false;
    LoadModOrderImpl();
}

const ModInfo* Paths::FindModInfo(const std::string& id) {
    if (!g_modOrderLoaded) LoadModOrderImpl();
    auto it = g_modInfos.find(id);
    return it == g_modInfos.end() ? nullptr : &it->second;
}

const std::vector<std::string>& Paths::EnabledModIds() {
    if (!g_modOrderLoaded) LoadModOrderImpl();
    return g_modLoadOrder;
}

void Paths::SetDevContentOverride(const fs::path& absolutePath) {
    g_devContentOverride = absolutePath;
}

} // namespace ark
