// Paths.h — Runtime directory resolution for the game.
// All paths are anchored to the executable directory, NOT cwd.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ark {

class Paths {
public:
    // Must be called exactly once from main() before anything else.
    // argv0 is used as a fallback; on Windows we prefer GetModuleFileNameW.
    static void Init(const char* argv0 = nullptr);

    // Directory containing the executable. All other paths derive from this.
    static const std::filesystem::path& GameRoot();

    // {GameRoot}/content — game-shipped assets
    static std::filesystem::path Content();

    // {GameRoot}/mods — player-installed mods
    static std::filesystem::path Mods();

    // {GameRoot}/logs — runtime log files
    static std::filesystem::path Logs();

    // %APPDATA%/StarArk/{title} — saves, settings, shader cache
    static std::filesystem::path UserData(const std::string& title = "StarArk");

    // Resolve a content-relative VFS-style path ("models/foo.obj")
    // into a real filesystem path. In dev mode (see SetDevContentOverride)
    // may point into source tree instead.
    static std::filesystem::path ResolveContent(const std::string& relative);

    // v0.3 ModSpec §3 — Mod-aware VFS with three-track path syntax.
    //   "./<rest>"           → relative to currentModId (Mods()/<currentModId>/<rest>)
    //   "mod://<id>/<rest>"  → explicit cross-mod (Mods()/<id>/<rest>)
    //   "engine://<rest>"    → engine-shipped fallback (Content()/<rest>, not mod-overridable)
    //   bare "<rest>"        → LEGACY (v0.2): walk load_order.toml mod stack then Content().
    //                          Emits a deprecation warning per call site (rate-limited).
    //
    // currentModId is the id of the mod currently being loaded (for "./" resolution).
    // Empty string means "no mod context" — only legacy/absolute schemes work then;
    // a "./" path with empty currentModId falls back to Content() and warns.
    //
    // Returns the first existing file (or the best-guess fallback path if nothing
    // exists; caller is responsible for reporting load failure).
    static std::filesystem::path ResolveResource(const std::string& logical,
                                                 const std::string& currentModId);

    // Backward-compat: equivalent to ResolveResource(logical, GetCurrentModId()).
    // The 1-arg form transparently picks up the thread-local mod scope (see
    // ModContextScope below), so existing call sites in TextureLoader /
    // ModelLoader resolve "./" paths correctly when the caller has entered
    // a mod scope.
    static std::filesystem::path ResolveResource(const std::string& logical);

    static void ReloadModOrder();

    // v0.3 ModSpec §2 — read-only access to the validated mod registry built
    // by LoadModOrderImpl. Returns nullptr when the id is not enabled, not
    // present, or failed validation. Callers must not retain the pointer
    // across a ReloadModOrder() call.
    static const struct ModInfo* FindModInfo(const std::string& id);
    static const std::vector<std::string>& EnabledModIds();

    // -----------------------------------------------------------------------
    // v0.3 ModSpec §3 — Thread-local "current mod" scope.
    //
    // When loading scene/material/etc. files for a specific mod, push the
    // mod's id onto the per-thread context stack so that loaders called
    // downstream (TextureLoader, ModelLoader, …) can resolve "./" paths
    // even though they don't take an explicit mod argument.
    //
    // Usage:
    //   Paths::ModContextScope scope("vanilla");
    //   sceneDoc.Load(...);   // every "./tex.png" resolves under mods/vanilla/
    //
    // Scopes nest; the innermost active id wins. GetCurrentModId() returns
    // "" when no scope is active.
    // -----------------------------------------------------------------------
    static void               PushCurrentModId(const std::string& modId);
    static void               PopCurrentModId();
    static const std::string& GetCurrentModId();

    class ModContextScope {
    public:
        explicit ModContextScope(const std::string& modId) { Paths::PushCurrentModId(modId); }
        ~ModContextScope()                                  { Paths::PopCurrentModId(); }
        ModContextScope(const ModContextScope&)            = delete;
        ModContextScope& operator=(const ModContextScope&) = delete;
    };

    // Developer convenience: override Content() to point to a source dir
    // (so editing source assets takes effect without install). Called by
    // CMake-generated header or EngineConfig loader.
    static void SetDevContentOverride(const std::filesystem::path& absolutePath);

private:
    Paths() = delete;
};

} // namespace ark
