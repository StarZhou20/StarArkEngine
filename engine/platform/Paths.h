// Paths.h — Runtime directory resolution for the game.
// All paths are anchored to the executable directory, NOT cwd.
#pragma once

#include <filesystem>
#include <string>

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

    // v0.2 15.D — MOD-aware VFS. Given a logical asset path (e.g. "textures/ground.png"),
    // search each enabled mod under {Mods()}/<name>/ in load-order priority, then fall back
    // to Content(). Returns the first existing file. If nothing matches, returns the
    // Content() fallback anyway (caller will report the load failure).
    //
    // Load order is read from {Mods()}/load_order.toml on first call; use
    // ReloadModOrder() to pick up changes. Cheap (filesystem::exists per candidate).
    static std::filesystem::path ResolveResource(const std::string& logical);
    static void ReloadModOrder();

    // Developer convenience: override Content() to point to a source dir
    // (so editing source assets takes effect without install). Called by
    // CMake-generated header or EngineConfig loader.
    static void SetDevContentOverride(const std::filesystem::path& absolutePath);

private:
    Paths() = delete;
};

} // namespace ark
