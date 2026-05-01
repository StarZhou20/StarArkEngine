// engine/save/SaveHeader.h
//
// v0.3 ModSpec §6.1 — Save-file header.
//
// Every save file begins with a [header] table + [[active_mods]] array of
// tables describing exactly which mods (id, version, per-mod schema_hash)
// were active when the save was written. On load we compare the captured
// header against the *current* runtime state and decide whether the save
// is compatible.
//
// Per-mod schema_hash semantics:
//   ModSpec §6.2 envisions one hash per mod (the SHA-256 over its own
//   reflected types). v0.3 currently has reflection registered only by
//   engine code, so all mods share the registry-wide hash returned by
//   ComputeRegistrySchemaHash(). The field is wired through anyway so
//   that adding per-mod reflection in v0.4 doesn't break the file format.
//
// Compatibility rules implemented in CheckCompatibility:
//   - kOk                : every active_mods entry matches by id+version+hash
//   - kMissingMod        : header lists a mod that is not currently enabled
//   - kVersionMismatch   : current mod version < header version
//                          (downgrade — refuse) or > header version (warn)
//   - kSchemaMismatch    : id+version match but schema_hash differs
//                          (reflected fields drifted under same version)
//   - kEngineDowngrade   : current engine version < save's engine_version
//
// Side-effect-free in v0.3: this header is purely a library; no save UI
// or save-file I/O is wired yet. EngineBase::Initialize logs the captured
// header at INFO so we can verify it round-trips cleanly across runs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ark {

class TomlDoc;
class TomlTable;

struct SaveHeader {
    static constexpr int kSchemaVersion = 1;

    int          schema_version = kSchemaVersion;
    std::string  engine_version;            // e.g. "0.3.0"
    int          script_api_version = 0;
    std::string  pipeline;                  // "forward" / "deferred"
    std::string  registry_schema_hash;      // ComputeRegistrySchemaHash() at save time
    std::int64_t timestamp_unix = 0;

    struct ActiveMod {
        std::string id;
        std::string version;                // SemVer string
        std::string schema_hash;            // see semantics above
    };
    std::vector<ActiveMod> active_mods;

    // Build a header reflecting the current process state: queries
    // Paths::EnabledModIds, FindModInfo, ComputeRegistrySchemaHash, and
    // resolves the pipeline from ARK_PIPELINE (defaults to "forward").
    static SaveHeader CaptureCurrent();

    // TOML round-trip. Layout:
    //   [header]
    //   schema_version = 1
    //   engine_version = "0.3.0"
    //   ...
    //   [[active_mods]]
    //   id = "..."
    //   version = "..."
    //   schema_hash = "..."
    void   ToToml(TomlDoc& out) const;
    static bool FromToml(const TomlDoc& in, SaveHeader* out, std::string* errorOut);

    // Convenience wrappers around TomlDoc::Parse / Dump.
    std::string DumpString() const;
    static bool ParseString(const std::string& text, SaveHeader* out, std::string* errorOut);
};

enum class SaveCompatibility {
    kOk,
    kMissingMod,
    kVersionMismatch,
    kSchemaMismatch,
    kEngineDowngrade,
};

struct SaveCompatibilityReport {
    SaveCompatibility status = SaveCompatibility::kOk;
    std::string       detail;     // human-readable
};

// Compare a header read from a save against the current runtime.
// Stops at the first incompatibility; never throws.
SaveCompatibilityReport CheckCompatibility(const SaveHeader& saved);

} // namespace ark
