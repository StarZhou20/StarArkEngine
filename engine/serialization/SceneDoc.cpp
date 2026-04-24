#include "engine/serialization/SceneDoc.h"
#include "engine/serialization/TomlDoc.h"

#include "engine/core/AScene.h"
#include "engine/core/AObject.h"
#include "engine/core/AComponent.h"
#include "engine/core/Transform.h"
#include "engine/core/TypeInfo.h"
#include "engine/debug/DebugListenBus.h"

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
            obj->SetGuid(g->AsString());
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
    return LoadFromString(ss.str(), scene);
}

} // namespace ark
