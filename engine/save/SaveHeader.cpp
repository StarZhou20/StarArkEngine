// engine/save/SaveHeader.cpp — see header for design notes.

#include "engine/save/SaveHeader.h"
#include "engine/core/SchemaHash.h"
#include "engine/mod/ModInfo.h"
#include "engine/platform/Paths.h"
#include "engine/scripting/ScriptHost.h"
#include "engine/serialization/TomlDoc.h"

#include <chrono>
#include <cstdlib>

namespace ark {

namespace {

const char* CurrentPipelineName() {
    if (const char* env = std::getenv("ARK_PIPELINE")) {
        if (env[0] != '\0') return env;
    }
    return "forward";
}

const TomlValue* FindString(const TomlTable& t, const char* key) {
    const TomlValue* v = t.Find(key);
    if (!v || !v->IsString()) return nullptr;
    return v;
}

} // namespace

SaveHeader SaveHeader::CaptureCurrent() {
    SaveHeader h;
    h.schema_version = kSchemaVersion;
    h.engine_version = kEngineVersionString;
    h.script_api_version = ARK_SCRIPT_API_VERSION;
    h.pipeline = CurrentPipelineName();
    h.registry_schema_hash = ComputeRegistrySchemaHash();
    h.timestamp_unix = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    for (const auto& id : Paths::EnabledModIds()) {
        const ModInfo* info = Paths::FindModInfo(id);
        if (!info || !info->valid) continue;
        ActiveMod m;
        m.id          = info->id;
        m.version     = info->version.ToString();
        // Per-mod hash is currently the registry hash (see header comment).
        m.schema_hash = h.registry_schema_hash;
        h.active_mods.push_back(std::move(m));
    }
    return h;
}

void SaveHeader::ToToml(TomlDoc& out) const {
    auto& hdr = out.Root().GetOrCreateTable("header");
    hdr.Set("schema_version",       TomlValue::Int(schema_version));
    hdr.Set("engine_version",       TomlValue::String(engine_version));
    hdr.Set("script_api_version",   TomlValue::Int(script_api_version));
    hdr.Set("pipeline",             TomlValue::String(pipeline));
    hdr.Set("registry_schema_hash", TomlValue::String(registry_schema_hash));
    hdr.Set("timestamp_unix",       TomlValue::Int(timestamp_unix));

    auto& aot = out.Root().GetOrCreateArrayOfTables("active_mods");
    for (const auto& m : active_mods) {
        auto& row = aot.Append();
        row.Set("id",          TomlValue::String(m.id));
        row.Set("version",     TomlValue::String(m.version));
        row.Set("schema_hash", TomlValue::String(m.schema_hash));
    }
}

bool SaveHeader::FromToml(const TomlDoc& in, SaveHeader* out, std::string* errorOut) {
    if (!out) return false;
    *out = SaveHeader{};

    auto setErr = [&](const std::string& m) {
        if (errorOut) *errorOut = m;
        return false;
    };

    const TomlTable* hdr = in.Root().FindTable("header");
    if (!hdr) return setErr("missing [header] table");

    if (const TomlValue* v = hdr->Find("schema_version"); v && v->IsInt()) {
        out->schema_version = static_cast<int>(v->AsInt());
    } else {
        return setErr("header.schema_version missing or not int");
    }
    if (out->schema_version != kSchemaVersion) {
        return setErr("unsupported header.schema_version="
                      + std::to_string(out->schema_version));
    }

    if (const TomlValue* v = FindString(*hdr, "engine_version"))     out->engine_version     = v->AsString();
    if (const TomlValue* v = hdr->Find("script_api_version"); v && v->IsInt()) out->script_api_version = static_cast<int>(v->AsInt());
    if (const TomlValue* v = FindString(*hdr, "pipeline"))           out->pipeline           = v->AsString();
    if (const TomlValue* v = FindString(*hdr, "registry_schema_hash")) out->registry_schema_hash = v->AsString();
    if (const TomlValue* v = hdr->Find("timestamp_unix"); v && v->IsInt()) out->timestamp_unix = v->AsInt();

    if (const auto* aot = in.Root().FindArrayOfTables("active_mods")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const TomlTable& row = (*aot)[i];
            ActiveMod m;
            if (const TomlValue* v = FindString(row, "id"))          m.id          = v->AsString();
            if (const TomlValue* v = FindString(row, "version"))     m.version     = v->AsString();
            if (const TomlValue* v = FindString(row, "schema_hash")) m.schema_hash = v->AsString();
            if (m.id.empty()) {
                return setErr("active_mods["
                              + std::to_string(i) + "] missing id");
            }
            out->active_mods.push_back(std::move(m));
        }
    }
    return true;
}

std::string SaveHeader::DumpString() const {
    TomlDoc doc;
    ToToml(doc);
    return doc.Dump();
}

bool SaveHeader::ParseString(const std::string& text, SaveHeader* out, std::string* errorOut) {
    std::string parseErr;
    int errLine = 0;
    auto parsed = TomlDoc::Parse(text, &parseErr, &errLine);
    if (!parsed) {
        if (errorOut) {
            *errorOut = "TOML parse error at line " + std::to_string(errLine)
                        + ": " + parseErr;
        }
        return false;
    }
    return FromToml(*parsed, out, errorOut);
}

// ---------------------------------------------------------------------------

SaveCompatibilityReport CheckCompatibility(const SaveHeader& saved) {
    SaveCompatibilityReport r;

    // Engine-version downgrade check (string compare via ModVersion::Parse).
    auto savedEng   = ModVersion::Parse(saved.engine_version);
    auto currentEng = ModVersion::Parse(kEngineVersionString);
    if (savedEng && currentEng && currentEng->Compare(*savedEng) < 0) {
        r.status = SaveCompatibility::kEngineDowngrade;
        r.detail = "engine " + currentEng->ToString()
                   + " < save's " + savedEng->ToString();
        return r;
    }

    // Per-mod checks.
    for (const auto& sm : saved.active_mods) {
        const ModInfo* cur = Paths::FindModInfo(sm.id);
        if (!cur || !cur->valid) {
            r.status = SaveCompatibility::kMissingMod;
            r.detail = "mod '" + sm.id + "' not enabled";
            return r;
        }
        auto savedV = ModVersion::Parse(sm.version);
        if (savedV && cur->version.Compare(*savedV) < 0) {
            r.status = SaveCompatibility::kVersionMismatch;
            r.detail = "mod '" + sm.id + "' " + cur->version.ToString()
                       + " < save's " + savedV->ToString();
            return r;
        }
        if (!sm.schema_hash.empty()
            && !saved.registry_schema_hash.empty()
            && sm.schema_hash != saved.registry_schema_hash) {
            // Inconsistent saved file — entry's hash should match registry.
            r.status = SaveCompatibility::kSchemaMismatch;
            r.detail = "mod '" + sm.id + "' schema_hash inconsistent in save";
            return r;
        }
    }

    // Registry-wide schema check (last so per-mod errors surface first).
    if (!saved.registry_schema_hash.empty()) {
        const std::string current = ComputeRegistrySchemaHash();
        if (current != saved.registry_schema_hash) {
            r.status = SaveCompatibility::kSchemaMismatch;
            r.detail = "registry schema_hash drift: save=" + saved.registry_schema_hash
                       + " current=" + current;
            return r;
        }
    }

    r.status = SaveCompatibility::kOk;
    return r;
}

} // namespace ark
