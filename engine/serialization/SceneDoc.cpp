#include "engine/serialization/SceneDoc.h"
#include "engine/serialization/TomlDoc.h"

#include "engine/core/AScene.h"
#include "engine/core/AObject.h"
#include "engine/core/AComponent.h"
#include "engine/core/PersistentId.h"
#include "engine/core/Transform.h"
#include "engine/core/TypeInfo.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace ark {

// =============================================================================
// 辅助: 反射字段 ↔ TomlValue 的读写
// =============================================================================
namespace {

// 获取某组件中 FieldInfo 指定字段的原始地址（读/写共用）。
std::uint8_t* FieldAddr(AComponent* comp, const FieldInfo& f) {
    return reinterpret_cast<std::uint8_t*>(comp) + f.offset;
}
const std::uint8_t* FieldAddr(const AComponent* comp, const FieldInfo& f) {
    return reinterpret_cast<const std::uint8_t*>(comp) + f.offset;
}

TomlValue ReadFieldToToml(const AComponent* comp, const FieldInfo& f) {
    const std::uint8_t* p = FieldAddr(comp, f);
    switch (f.type) {
        case FieldType::Bool:
            return TomlValue::Bool(*reinterpret_cast<const bool*>(p));
        case FieldType::Int:
        case FieldType::EnumInt: {
            // 支持 32-bit int 或 enum（尺寸按 sizeof 分流）
            if (f.size == sizeof(std::int32_t)) {
                return TomlValue::Int(static_cast<std::int64_t>(
                    *reinterpret_cast<const std::int32_t*>(p)));
            }
            if (f.size == sizeof(std::int64_t)) {
                return TomlValue::Int(*reinterpret_cast<const std::int64_t*>(p));
            }
            return TomlValue::Int(0);
        }
        case FieldType::Float:
            return TomlValue::Float(static_cast<double>(
                *reinterpret_cast<const float*>(p)));
        case FieldType::Vec2: {
            auto& v = *reinterpret_cast<const glm::vec2*>(p);
            TomlArray a;
            a.push_back(TomlValue::Float(v.x));
            a.push_back(TomlValue::Float(v.y));
            return TomlValue::Array(std::move(a));
        }
        case FieldType::Vec3:
        case FieldType::Color3: {
            auto& v = *reinterpret_cast<const glm::vec3*>(p);
            TomlArray a;
            a.push_back(TomlValue::Float(v.x));
            a.push_back(TomlValue::Float(v.y));
            a.push_back(TomlValue::Float(v.z));
            return TomlValue::Array(std::move(a));
        }
        case FieldType::Vec4:
        case FieldType::Color4:
        case FieldType::Quat: {
            auto& v = *reinterpret_cast<const glm::vec4*>(p);
            TomlArray a;
            a.push_back(TomlValue::Float(v.x));
            a.push_back(TomlValue::Float(v.y));
            a.push_back(TomlValue::Float(v.z));
            a.push_back(TomlValue::Float(v.w));
            return TomlValue::Array(std::move(a));
        }
        case FieldType::String:
        case FieldType::AssetPath:
        case FieldType::ObjectRef:
            return TomlValue::String(*reinterpret_cast<const std::string*>(p));
    }
    return TomlValue::String(std::string("<unknown>"));
}

float ArrFloat(const TomlArray& a, std::size_t i, float fallback) {
    if (i >= a.size()) return fallback;
    return a[i].IsNumber() ? static_cast<float>(a[i].AsFloat()) : fallback;
}

void WriteFieldFromToml(AComponent* comp, const FieldInfo& f, const TomlValue& v) {
    std::uint8_t* p = FieldAddr(comp, f);
    switch (f.type) {
        case FieldType::Bool:
            if (v.IsBool()) *reinterpret_cast<bool*>(p) = v.AsBool();
            else if (v.IsNumber()) *reinterpret_cast<bool*>(p) = (v.AsInt() != 0);
            break;
        case FieldType::Int:
        case FieldType::EnumInt: {
            if (!v.IsNumber()) break;
            std::int64_t raw = v.AsInt();
            if (f.size == sizeof(std::int32_t))
                *reinterpret_cast<std::int32_t*>(p) = static_cast<std::int32_t>(raw);
            else if (f.size == sizeof(std::int64_t))
                *reinterpret_cast<std::int64_t*>(p) = raw;
            break;
        }
        case FieldType::Float:
            if (v.IsNumber())
                *reinterpret_cast<float*>(p) = static_cast<float>(v.AsFloat());
            break;
        case FieldType::Vec2: {
            if (!v.IsArray()) break;
            auto& dst = *reinterpret_cast<glm::vec2*>(p);
            const auto& a = v.AsArray();
            dst.x = ArrFloat(a, 0, dst.x);
            dst.y = ArrFloat(a, 1, dst.y);
            break;
        }
        case FieldType::Vec3:
        case FieldType::Color3: {
            if (!v.IsArray()) break;
            auto& dst = *reinterpret_cast<glm::vec3*>(p);
            const auto& a = v.AsArray();
            dst.x = ArrFloat(a, 0, dst.x);
            dst.y = ArrFloat(a, 1, dst.y);
            dst.z = ArrFloat(a, 2, dst.z);
            break;
        }
        case FieldType::Vec4:
        case FieldType::Color4:
        case FieldType::Quat: {
            if (!v.IsArray()) break;
            auto& dst = *reinterpret_cast<glm::vec4*>(p);
            const auto& a = v.AsArray();
            dst.x = ArrFloat(a, 0, dst.x);
            dst.y = ArrFloat(a, 1, dst.y);
            dst.z = ArrFloat(a, 2, dst.z);
            dst.w = ArrFloat(a, 3, dst.w);
            break;
        }
        case FieldType::String:
        case FieldType::AssetPath:
        case FieldType::ObjectRef:
            if (v.IsString())
                *reinterpret_cast<std::string*>(p) = v.AsString();
            break;
    }
}

TomlArray Vec3ToArray(const glm::vec3& v) {
    TomlArray a;
    a.push_back(TomlValue::Float(v.x));
    a.push_back(TomlValue::Float(v.y));
    a.push_back(TomlValue::Float(v.z));
    return a;
}
TomlArray QuatToArray(const glm::quat& q) {
    TomlArray a;
    a.push_back(TomlValue::Float(q.x));
    a.push_back(TomlValue::Float(q.y));
    a.push_back(TomlValue::Float(q.z));
    a.push_back(TomlValue::Float(q.w));
    return a;
}

} // anonymous namespace

// =============================================================================
// Save
// =============================================================================

std::string SceneDoc::Dump(AScene* scene) {
    TomlDoc doc;

    auto& sceneTbl = doc.Root().GetOrCreateTable("scene");
    sceneTbl.Set("name", TomlValue::String(scene->GetSceneName()));
    sceneTbl.Set("schema_version", TomlValue::Int(1));

    auto& objectsAot = doc.Root().GetOrCreateArrayOfTables("objects");
    auto dumpObjects = [&](std::vector<std::unique_ptr<AObject>>& list) {
        for (auto& up : list) {
            AObject* obj = up.get();
            if (!obj || obj->IsDestroyed()) continue;

            TomlTable& ot = objectsAot.Append();
            ot.Set("guid",   TomlValue::String(obj->GetGuid()));
            ot.Set("name",   TomlValue::String(obj->GetName()));

            std::string parentGuid;
            if (auto* p = obj->GetTransform().GetParent()) {
                parentGuid = p->GetOwner()->GetGuid();
            }
            ot.Set("parent", TomlValue::String(parentGuid));

            auto& trTbl = ot.GetOrCreateTable("transform");
            trTbl.Set("position", TomlValue::Array(Vec3ToArray(obj->GetTransform().GetLocalPosition())));
            trTbl.Set("rotation", TomlValue::Array(QuatToArray(obj->GetTransform().GetLocalRotation())));
            trTbl.Set("scale",    TomlValue::Array(Vec3ToArray(obj->GetTransform().GetLocalScale())));

            auto& compsAot = ot.GetOrCreateArrayOfTables("components");
            for (auto& compUp : obj->GetComponents()) {
                AComponent* comp = compUp.get();
                std::string_view tname = comp->GetReflectTypeName();
                if (tname.empty()) continue; // 未注册反射，跳过

                const TypeInfo* ti = TypeRegistry::Get().Find(tname);
                if (!ti) continue;

                TomlTable& ct = compsAot.Append();
                ct.Set("type", TomlValue::String(std::string(tname)));
                for (const auto& f : ti->fields) {
                    ct.Set(f.name, ReadFieldToToml(comp, f));
                }
            }
        }
    };
    dumpObjects(scene->GetObjectList());
    dumpObjects(scene->GetPendingList());

    return doc.Dump();
}

bool SceneDoc::Save(const std::filesystem::path& path, AScene* scene) {
    std::string text = Dump(scene);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        ARK_LOG_ERROR("SceneDoc", std::string("Save: cannot open ") + path.string());
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return true;
}

// =============================================================================
// Load
// =============================================================================

namespace {

void ApplyTransformFromTable(AObject* obj, const TomlTable& trTbl) {
    if (const auto* pos = trTbl.Find("position");
        pos && pos->IsArray()) {
        const auto& a = pos->AsArray();
        obj->GetTransform().SetLocalPosition(
            glm::vec3(ArrFloat(a, 0, 0.0f), ArrFloat(a, 1, 0.0f), ArrFloat(a, 2, 0.0f)));
    }
    if (const auto* rot = trTbl.Find("rotation");
        rot && rot->IsArray()) {
        const auto& a = rot->AsArray();
        obj->GetTransform().SetLocalRotation(
            glm::quat(ArrFloat(a, 3, 1.0f),  // w
                     ArrFloat(a, 0, 0.0f),   // x
                     ArrFloat(a, 1, 0.0f),   // y
                     ArrFloat(a, 2, 0.0f))); // z
    }
    if (const auto* scale = trTbl.Find("scale");
        scale && scale->IsArray()) {
        const auto& a = scale->AsArray();
        obj->GetTransform().SetLocalScale(
            glm::vec3(ArrFloat(a, 0, 1.0f), ArrFloat(a, 1, 1.0f), ArrFloat(a, 2, 1.0f)));
    }
}

} // anonymous namespace

bool SceneDoc::LoadFromString(std::string_view text, AScene* scene,
                              std::string* errorMsg) {
    std::string err;
    int errLine = 0;
    auto parsed = TomlDoc::Parse(text, &err, &errLine);
    if (!parsed) {
        if (errorMsg) {
            *errorMsg = "parse failed at line " + std::to_string(errLine) + ": " + err;
        }
        ARK_LOG_ERROR("SceneDoc",
            std::string("parse failed at line ") + std::to_string(errLine) + ": " + err);
        return false;
    }
    const TomlTable& root = parsed->Root();

    if (const auto* sceneTbl = root.FindTable("scene")) {
        if (const auto* nm = sceneTbl->Find("name"); nm && nm->IsString()) {
            scene->SetSceneName(nm->AsString());
        }
    }

    // guid → AObject*，供第二遍 parent 解析用
    std::unordered_map<std::string, AObject*> byGuid;

    const auto* objectsAot = root.FindArrayOfTables("objects");
    if (!objectsAot) {
        return true; // 空场景
    }

    // pass 1: 创建所有对象 + transform + components（暂不 parent）
    struct PendingParent { AObject* child; std::string parentGuid; };
    std::vector<PendingParent> pendingParents;

    for (std::size_t i = 0; i < objectsAot->Size(); ++i) {
        const TomlTable& ot = (*objectsAot)[i];
        AObject* obj = scene->CreateObject<AObject>();

        if (const auto* g = ot.Find("guid"); g && g->IsString() && !g->AsString().empty()) {
            const std::string& gid = g->AsString();
            // v0.3 ModSpec §4.2 — accept persistent IDs ("<mod>:<local>"); legacy
            // UUIDs still load but emit a one-shot deprecation WARN per file.
            if (!IsValidPersistentId(gid)) {
                if (IsValidLegacyUuid(gid)) {
                    ARK_LOG_WARN("SceneDoc",
                        "guid '" + gid + "' is a legacy UUID — please migrate to "
                        "<mod_id>:<local_id> (v0.3 ModSpec §4.2)");
                } else {
                    ARK_LOG_WARN("SceneDoc",
                        "guid '" + gid + "' is not a valid persistent ID — accepted "
                        "as-is, but ark-validate will reject this");
                }
            }
            obj->SetGuid(gid);
        }
        if (const auto* n = ot.Find("name"); n && n->IsString()) {
            obj->SetName(n->AsString());
        }
        if (const auto* trTbl = ot.FindTable("transform")) {
            ApplyTransformFromTable(obj, *trTbl);
        }
        byGuid[obj->GetGuid()] = obj;

        if (const auto* p = ot.Find("parent"); p && p->IsString() && !p->AsString().empty()) {
            pendingParents.push_back({ obj, p->AsString() });
        }

        if (const auto* compsAot = ot.FindArrayOfTables("components")) {
            for (std::size_t j = 0; j < compsAot->Size(); ++j) {
                const TomlTable& ct = (*compsAot)[j];
                const auto* ty = ct.Find("type");
                if (!ty || !ty->IsString()) continue;

                const TypeInfo* ti = TypeRegistry::Get().Find(ty->AsString());
                if (!ti || !ti->factory) {
                    ARK_LOG_WARN("SceneDoc",
                        std::string("unknown component type '") + ty->AsString() + "' — skipped");
                    continue;
                }
                auto compUp = ti->factory();
                AComponent* comp = compUp.get();
                if (!comp) continue;

                // 还原字段（在 AddComponentRaw 之前，避免 Init 读到脏值）
                for (const auto& f : ti->fields) {
                    if (const auto* v = ct.Find(f.name)) {
                        WriteFieldFromToml(comp, f, *v);
                    }
                }
                // v0.3 — translate spec → runtime resources (MeshRenderer etc.)
                comp->OnReflectionLoaded();
                obj->AddComponentRaw(std::move(compUp));
            }
        }
    }

    // pass 2: parent 连接
    for (const auto& pp : pendingParents) {
        auto it = byGuid.find(pp.parentGuid);
        if (it == byGuid.end()) {
            ARK_LOG_WARN("SceneDoc",
                std::string("parent guid '") + pp.parentGuid + "' not found for object '" +
                pp.child->GetName() + "'");
            continue;
        }
        pp.child->GetTransform().SetParent(&it->second->GetTransform());
    }

    return true;
}

bool SceneDoc::Load(const std::filesystem::path& path, AScene* scene) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ARK_LOG_ERROR("SceneDoc", std::string("Load: cannot open ") + path.string());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    // v0.3 ModSpec §3 — infer the mod id from the scene's filesystem location
    // so any "./" paths inside the scene resolve under the correct mod root.
    // Layout convention: <Mods()>/<modId>/scenes/<...>.toml.  Anything else
    // (e.g. content-shipped scenes) loads with no mod context.
    std::string modId;
    std::error_code ec;
    auto absScene = std::filesystem::weakly_canonical(path, ec);
    auto absMods  = std::filesystem::weakly_canonical(Paths::Mods(), ec);
    if (!ec) {
        auto rel = std::filesystem::relative(absScene, absMods, ec);
        if (!ec && !rel.empty() && rel.begin() != rel.end()) {
            const std::string first = rel.begin()->string();
            if (!first.empty() && first != ".." && first != ".") {
                modId = first;
            }
        }
    }

    if (!modId.empty()) {
        Paths::ModContextScope scope(modId);
        return LoadFromString(ss.str(), scene);
    }
    return LoadFromString(ss.str(), scene);
}

// =============================================================================
// v0.3 ModSpec §5 — Addon scene overlay
// =============================================================================

namespace {

// Find an AObject by guid in a scene's object list. Linear scan: scenes are
// small (≤ a few hundred objects in v0.3) and overlays apply once at load.
AObject* FindByGuid(AScene* scene, const std::string& guid) {
    if (!scene || guid.empty()) return nullptr;
    for (auto& up : scene->GetObjectList()) {
        AObject* obj = up.get();
        if (!obj || obj->IsDestroyed()) continue;
        if (obj->GetGuid() == guid) return obj;
    }
    return nullptr;
}

// Find a component of the given reflected type name on an object.
AComponent* FindComponentByType(AObject* obj, const std::string& typeName) {
    if (!obj) return nullptr;
    for (auto& cup : obj->GetComponents()) {
        AComponent* c = cup.get();
        if (!c) continue;
        if (c->GetReflectTypeName() == typeName) return c;
    }
    return nullptr;
}

// Apply only the keys present in `tbl` that match a reflected field of `comp`.
// Unknown keys are silently ignored (typical TOML extras like target_guid /
// component_type are filtered by the caller before getting here).
void ApplyReflectedFields(AComponent* comp, const TypeInfo& ti, const TomlTable& tbl) {
    for (const auto& f : ti.fields) {
        if (const TomlValue* v = tbl.Find(f.name)) {
            WriteFieldFromToml(comp, f, *v);
        }
    }
}

// Build a brand-new AObject from a [[additions]] table — same layout as
// [[objects]] in the base scene file. Returns the new object (already added
// to the scene) or nullptr on hard failure (ignored for additions).
AObject* BuildObjectFromTable(AScene* scene, const TomlTable& ot) {
    AObject* obj = scene->CreateObject<AObject>();
    if (const auto* g = ot.Find("guid"); g && g->IsString() && !g->AsString().empty()) {
        obj->SetGuid(g->AsString());
    }
    if (const auto* n = ot.Find("name"); n && n->IsString()) {
        obj->SetName(n->AsString());
    }
    if (const auto* trTbl = ot.FindTable("transform")) {
        ApplyTransformFromTable(obj, *trTbl);
    }
    if (const auto* compsAot = ot.FindArrayOfTables("components")) {
        for (std::size_t j = 0; j < compsAot->Size(); ++j) {
            const TomlTable& ct = (*compsAot)[j];
            const auto* ty = ct.Find("type");
            if (!ty || !ty->IsString()) continue;
            const TypeInfo* ti = TypeRegistry::Get().Find(ty->AsString());
            if (!ti || !ti->factory) {
                ARK_LOG_WARN("SceneOverlay",
                    std::string("addition: unknown component type '") + ty->AsString() + "'");
                continue;
            }
            auto compUp = ti->factory();
            AComponent* comp = compUp.get();
            if (!comp) continue;
            ApplyReflectedFields(comp, *ti, ct);
            comp->OnReflectionLoaded();
            obj->AddComponentRaw(std::move(compUp));
        }
    }
    return obj;
}

} // anonymous namespace

bool SceneDoc::ApplyOverlayFromString(std::string_view text, AScene* scene,
                                      std::string* errorMsg) {
    if (!scene) {
        if (errorMsg) *errorMsg = "scene is null";
        return false;
    }

    std::string err;
    int errLine = 0;
    auto parsed = TomlDoc::Parse(text, &err, &errLine);
    if (!parsed) {
        if (errorMsg) {
            *errorMsg = "TOML parse error at line " + std::to_string(errLine)
                        + ": " + err;
        }
        return false;
    }
    const TomlTable& root = parsed->Root();

    // Schema version gate (lenient: missing [overlay] is allowed for now).
    if (const auto* ovTbl = root.FindTable("overlay")) {
        if (const auto* sv = ovTbl->Find("schema_version"); sv && sv->IsInt()) {
            if (sv->AsInt() != 1) {
                if (errorMsg) *errorMsg = "unsupported overlay.schema_version="
                                          + std::to_string(sv->AsInt());
                return false;
            }
        }
    }

    int countDel = 0, countOv = 0, countAtt = 0, countAdd = 0;

    // ---- 1) deletions -----------------------------------------------------
    if (const auto* aot = root.FindArrayOfTables("deletions")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const TomlTable& row = (*aot)[i];
            const auto* tg = row.Find("target_guid");
            if (!tg || !tg->IsString() || tg->AsString().empty()) {
                ARK_LOG_WARN("SceneOverlay", "deletions[" + std::to_string(i)
                             + "] missing target_guid");
                continue;
            }
            AObject* obj = FindByGuid(scene, tg->AsString());
            if (!obj) {
                ARK_LOG_WARN("SceneOverlay", "deletions: target_guid '"
                             + tg->AsString() + "' not found");
                continue;
            }
            obj->Destroy();
            ++countDel;
        }
    }

    // ---- 2) overrides -----------------------------------------------------
    if (const auto* aot = root.FindArrayOfTables("overrides")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const TomlTable& row = (*aot)[i];
            const auto* tg = row.Find("target_guid");
            const auto* ct = row.Find("component_type");
            if (!tg || !tg->IsString() || tg->AsString().empty()
                || !ct || !ct->IsString() || ct->AsString().empty()) {
                ARK_LOG_WARN("SceneOverlay", "overrides[" + std::to_string(i)
                             + "] missing target_guid / component_type");
                continue;
            }
            AObject* obj = FindByGuid(scene, tg->AsString());
            if (!obj) {
                ARK_LOG_WARN("SceneOverlay", "overrides: target_guid '"
                             + tg->AsString() + "' not found");
                continue;
            }
            AComponent* comp = FindComponentByType(obj, ct->AsString());
            if (!comp) {
                ARK_LOG_WARN("SceneOverlay", "overrides: component_type '"
                             + ct->AsString() + "' not on object '" + tg->AsString() + "'");
                continue;
            }
            const TypeInfo* ti = TypeRegistry::Get().Find(ct->AsString());
            if (!ti) continue;
            ApplyReflectedFields(comp, *ti, row);
            comp->OnReflectionLoaded();
            ++countOv;
        }
    }

    // ---- 3) components_attached ------------------------------------------
    if (const auto* aot = root.FindArrayOfTables("components_attached")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const TomlTable& row = (*aot)[i];
            const auto* tg = row.Find("target_guid");
            const auto* ty = row.Find("type");
            if (!tg || !tg->IsString() || tg->AsString().empty()
                || !ty || !ty->IsString() || ty->AsString().empty()) {
                ARK_LOG_WARN("SceneOverlay", "components_attached["
                             + std::to_string(i)
                             + "] missing target_guid / type");
                continue;
            }
            AObject* obj = FindByGuid(scene, tg->AsString());
            if (!obj) {
                ARK_LOG_WARN("SceneOverlay", "components_attached: target_guid '"
                             + tg->AsString() + "' not found");
                continue;
            }
            const TypeInfo* ti = TypeRegistry::Get().Find(ty->AsString());
            if (!ti || !ti->factory) {
                ARK_LOG_WARN("SceneOverlay",
                    "components_attached: unknown type '" + ty->AsString() + "'");
                continue;
            }
            auto compUp = ti->factory();
            AComponent* comp = compUp.get();
            if (!comp) continue;
            ApplyReflectedFields(comp, *ti, row);
            comp->OnReflectionLoaded();
            obj->AddComponentRaw(std::move(compUp));
            ++countAtt;
        }
    }

    // ---- 4) additions -----------------------------------------------------
    if (const auto* aot = root.FindArrayOfTables("additions")) {
        for (std::size_t i = 0; i < aot->Size(); ++i) {
            const TomlTable& row = (*aot)[i];
            if (BuildObjectFromTable(scene, row)) ++countAdd;
        }
    }

    ARK_LOG_INFO("SceneOverlay",
        "applied: deletions=" + std::to_string(countDel)
        + " overrides=" + std::to_string(countOv)
        + " components_attached=" + std::to_string(countAtt)
        + " additions=" + std::to_string(countAdd));

    return true;
}

bool SceneDoc::ApplyOverlay(const std::filesystem::path& path, AScene* scene) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ARK_LOG_ERROR("SceneOverlay",
            std::string("ApplyOverlay: cannot open ") + path.string());
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();

    std::string err;
    bool ok = ApplyOverlayFromString(ss.str(), scene, &err);
    if (!ok) {
        ARK_LOG_ERROR("SceneOverlay",
            std::string("ApplyOverlay '") + path.string() + "': " + err);
    }
    return ok;
}

} // namespace ark
