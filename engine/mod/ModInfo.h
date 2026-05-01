// engine/mod/ModInfo.h
//
// v0.3 ModSpec §2: parsed `<mod_root>/mod.toml` representation.
//
// Loaded once per mod folder by ModRegistry. Engine code consults the registry
// to gate mod activation by engine_min / script_api_min / type / applies_to.
//
// Required fields (per ModSpec §2.1) are mandatory; their absence makes the
// mod unloadable. Optional fields (§2.2) default to empty.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ark {

enum class ModType {
    Game,
    Addon,
};

// SemVer triple "major.minor.patch". Pre-release / build metadata not parsed.
struct ModVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static std::optional<ModVersion> Parse(const std::string& s);
    std::string ToString() const;

    // Lexicographic compare on (major, minor, patch).
    int Compare(const ModVersion& other) const;
    bool operator<(const ModVersion& o)  const { return Compare(o) <  0; }
    bool operator<=(const ModVersion& o) const { return Compare(o) <= 0; }
    bool operator==(const ModVersion& o) const { return Compare(o) == 0; }
};

struct ModDependency {
    std::string id;
    std::string version_constraint;  // raw, e.g. ">=1.0" — not yet enforced
};

struct ModInfo {
    // ---- §2.1 required ----
    int          schema_version = 0;            // must be 1
    std::string  id;                             // [a-z][a-z0-9_]*
    ModType      type = ModType::Game;
    ModVersion   version;                        // SemVer
    std::string  display_name;                   // unicode allowed
    std::vector<std::string> authors;
    ModVersion   engine_min;                     // engine must be >= this
    int          script_api_min = 0;             // ScriptApi version
    std::vector<std::string> supported_pipelines; // "forward" / "deferred"

    // ---- §2.2 optional ----
    std::string  description;
    std::string  license;
    std::string  homepage;
    std::string  default_locale;
    std::vector<std::string>    locales;
    std::vector<ModDependency>  depends_on;
    std::vector<std::string>    applies_to;     // type=addon only
    std::vector<std::string>    load_after;     // type=addon only
    std::vector<std::string>    load_before;    // type=addon only

    // ---- runtime bookkeeping (not from TOML) ----
    std::filesystem::path root;                  // <Mods()>/<id>/
    bool                  valid = false;         // false if validation failed
    std::string           error;                 // human-readable, when valid==false

    // Parse + validate. Returns ModInfo with valid==true on success, or
    // valid==false + error filled on any failure.
    //
    // engineVer / scriptApiVer are the *current* engine values used to gate
    // engine_min / script_api_min.
    static ModInfo LoadFromDirectory(const std::filesystem::path& modDir,
                                     const ModVersion& engineVer,
                                     int               scriptApiVer);
};

// Engine-wide constant. Bumped together with ModSpec compatibility breaks.
//
// v0.3.0 == ModSpec v0.1 landing point (mod.toml schema_version=1,
//          ScriptApi v2). This is the version that gates `engine_min`
//          checks across loaded mods.
constexpr const char* kEngineVersionString = "0.3.0";

} // namespace ark
