// engine/mod/ModInfo.cpp — v0.3 ModSpec §2 parser.

#include "engine/mod/ModInfo.h"

#include "engine/debug/DebugListenBus.h"
#include "engine/serialization/TomlDoc.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ark {

namespace fs = std::filesystem;

// ───────────────────────────── ModVersion ────────────────────────────────

std::optional<ModVersion> ModVersion::Parse(const std::string& s) {
    // Accept "MAJOR.MINOR.PATCH". Trailing pre-release/build is rejected for
    // now (we don't need it; ModSpec just calls it SemVer informally).
    ModVersion v;
    int  parts[3] = {0, 0, 0};
    int  idx      = 0;
    int  acc      = 0;
    bool sawDigit = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            acc = acc * 10 + (c - '0');
            sawDigit = true;
            continue;
        }
        if (c == '.') {
            if (!sawDigit || idx >= 2) return std::nullopt;
            parts[idx++] = acc;
            acc = 0;
            sawDigit = false;
            continue;
        }
        return std::nullopt;
    }
    if (!sawDigit || idx != 2) return std::nullopt;
    parts[2] = acc;
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    return v;
}

std::string ModVersion::ToString() const {
    std::ostringstream os;
    os << major << '.' << minor << '.' << patch;
    return os.str();
}

int ModVersion::Compare(const ModVersion& o) const {
    if (major != o.major) return major < o.major ? -1 : 1;
    if (minor != o.minor) return minor < o.minor ? -1 : 1;
    if (patch != o.patch) return patch < o.patch ? -1 : 1;
    return 0;
}

// ───────────────────────────── helpers ───────────────────────────────────

namespace {

bool IsValidId(const std::string& s) {
    if (s.empty()) return false;
    if (!(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::string> AsStringArray(const TomlValue* v) {
    std::vector<std::string> out;
    if (!v || !v->IsArray()) return out;
    for (const auto& e : v->AsArray()) {
        if (e.IsString()) out.push_back(e.AsString());
    }
    return out;
}

ModInfo MakeFailure(const fs::path& root, std::string id, std::string err) {
    ModInfo m;
    m.root  = root;
    m.id    = std::move(id);
    m.valid = false;
    m.error = std::move(err);
    return m;
}

} // namespace

ModInfo ModInfo::LoadFromDirectory(const fs::path& modDir,
                                   const ModVersion& engineVer,
                                   int               scriptApiVer) {
    const std::string folderName = modDir.filename().string();
    const fs::path    tomlPath   = modDir / "mod.toml";

    std::error_code ec;
    if (!fs::exists(tomlPath, ec)) {
        return MakeFailure(modDir, folderName, "mod.toml not found");
    }

    std::ifstream f(tomlPath, std::ios::binary);
    if (!f) {
        return MakeFailure(modDir, folderName, "failed to open mod.toml");
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    std::string err;
    int         errLine = 0;
    auto doc = TomlDoc::Parse(text, &err, &errLine);
    if (!doc) {
        return MakeFailure(modDir, folderName,
            "mod.toml parse error (line " + std::to_string(errLine) + "): " + err);
    }
    const TomlTable& root = doc->Root();

    ModInfo m;
    m.root = modDir;

    // schema_version
    if (const auto* v = root.Find("schema_version"); v && v->IsInt()) {
        m.schema_version = static_cast<int>(v->AsInt());
    }
    if (m.schema_version != 1) {
        return MakeFailure(modDir, folderName,
            "schema_version must be 1 (got " + std::to_string(m.schema_version) + ")");
    }

    // id
    if (const auto* v = root.Find("id"); v && v->IsString()) {
        m.id = v->AsString();
    }
    if (!IsValidId(m.id)) {
        return MakeFailure(modDir, folderName,
            "id missing or invalid (must match [a-z][a-z0-9_]*)");
    }
    if (m.id != folderName) {
        // Not a hard failure (modder may rename folder), but warn.
        ARK_LOG_WARN("Mod",
            "mod.toml id='" + m.id + "' does not match folder name '"
            + folderName + "' — using id from mod.toml.");
    }

    // type
    if (const auto* v = root.Find("type"); v && v->IsString()) {
        const auto& t = v->AsString();
        if      (t == "game")  m.type = ModType::Game;
        else if (t == "addon") m.type = ModType::Addon;
        else return MakeFailure(modDir, m.id,
            "type must be 'game' or 'addon' (got '" + t + "')");
    } else {
        return MakeFailure(modDir, m.id, "type missing");
    }

    // version
    if (const auto* v = root.Find("version"); v && v->IsString()) {
        auto parsed = ModVersion::Parse(v->AsString());
        if (!parsed) return MakeFailure(modDir, m.id,
            "version not in MAJOR.MINOR.PATCH form: '" + v->AsString() + "'");
        m.version = *parsed;
    } else {
        return MakeFailure(modDir, m.id, "version missing");
    }

    // display_name
    if (const auto* v = root.Find("display_name"); v && v->IsString()) {
        m.display_name = v->AsString();
    } else {
        return MakeFailure(modDir, m.id, "display_name missing");
    }

    // authors
    m.authors = AsStringArray(root.Find("authors"));
    if (m.authors.empty()) {
        return MakeFailure(modDir, m.id, "authors missing or empty");
    }

    // engine_min
    if (const auto* v = root.Find("engine_min"); v && v->IsString()) {
        auto parsed = ModVersion::Parse(v->AsString());
        if (!parsed) return MakeFailure(modDir, m.id,
            "engine_min not in MAJOR.MINOR.PATCH form: '" + v->AsString() + "'");
        m.engine_min = *parsed;
    } else {
        return MakeFailure(modDir, m.id, "engine_min missing");
    }

    // script_api_min
    if (const auto* v = root.Find("script_api_min"); v && v->IsInt()) {
        m.script_api_min = static_cast<int>(v->AsInt());
    } else {
        return MakeFailure(modDir, m.id, "script_api_min missing");
    }

    // supported_pipelines
    m.supported_pipelines = AsStringArray(root.Find("supported_pipelines"));
    if (m.supported_pipelines.empty()) {
        return MakeFailure(modDir, m.id,
            "supported_pipelines missing or empty");
    }

    // ---- optional ----
    if (const auto* v = root.Find("description");    v && v->IsString()) m.description    = v->AsString();
    if (const auto* v = root.Find("license");        v && v->IsString()) m.license        = v->AsString();
    if (const auto* v = root.Find("homepage");       v && v->IsString()) m.homepage       = v->AsString();
    if (const auto* v = root.Find("default_locale"); v && v->IsString()) m.default_locale = v->AsString();
    m.locales     = AsStringArray(root.Find("locales"));
    m.applies_to  = AsStringArray(root.Find("applies_to"));
    m.load_after  = AsStringArray(root.Find("load_after"));
    m.load_before = AsStringArray(root.Find("load_before"));

    if (const auto* aot = root.FindArrayOfTables("depends_on")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const auto& t = (*aot)[i];
            ModDependency dep;
            if (const auto* idV = t.Find("id"); idV && idV->IsString()) {
                dep.id = idV->AsString();
            }
            if (const auto* vV = t.Find("version"); vV && vV->IsString()) {
                dep.version_constraint = vV->AsString();
            }
            if (!dep.id.empty()) m.depends_on.push_back(std::move(dep));
        }
    }

    // ---- gating: engine_min / script_api_min ----
    if (engineVer < m.engine_min) {
        return MakeFailure(modDir, m.id,
            "engine_min " + m.engine_min.ToString()
            + " > current engine " + engineVer.ToString());
    }
    if (scriptApiVer < m.script_api_min) {
        return MakeFailure(modDir, m.id,
            "script_api_min " + std::to_string(m.script_api_min)
            + " > current ScriptApi " + std::to_string(scriptApiVer));
    }

    // type=addon must have non-empty applies_to.
    if (m.type == ModType::Addon && m.applies_to.empty()) {
        return MakeFailure(modDir, m.id,
            "type=addon requires non-empty applies_to");
    }

    m.valid = true;
    return m;
}

} // namespace ark
