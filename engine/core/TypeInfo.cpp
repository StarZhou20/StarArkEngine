#include "engine/core/TypeInfo.h"

#include <cassert>

namespace ark {

const char* FieldTypeName(FieldType t) {
    switch (t) {
        case FieldType::Bool:      return "Bool";
        case FieldType::Int:       return "Int";
        case FieldType::Float:     return "Float";
        case FieldType::Vec2:      return "Vec2";
        case FieldType::Vec3:      return "Vec3";
        case FieldType::Vec4:      return "Vec4";
        case FieldType::Color3:    return "Color3";
        case FieldType::Color4:    return "Color4";
        case FieldType::Quat:      return "Quat";
        case FieldType::String:    return "String";
        case FieldType::EnumInt:   return "EnumInt";
        case FieldType::AssetPath: return "AssetPath";
        case FieldType::ObjectRef: return "ObjectRef";
    }
    return "Unknown";
}

const FieldInfo* TypeInfo::FindField(std::string_view field_name) const {
    for (const auto& f : fields) {
        if (f.name == field_name) {
            return &f;
        }
    }
    return nullptr;
}

TypeRegistry& TypeRegistry::Get() {
    static TypeRegistry instance;
    return instance;
}

void TypeRegistry::Register(TypeInfo info) {
    // 重复注册视为编程错误（通常源自重复链接或拼错名字）
    for (const auto& existing : types_) {
        assert(existing.name != info.name && "TypeRegistry: duplicate type name");
        (void)existing;
    }
    types_.push_back(std::move(info));
}

const TypeInfo* TypeRegistry::Find(std::string_view type_name) const {
    for (const auto& t : types_) {
        if (t.name == type_name) {
            return &t;
        }
    }
    return nullptr;
}

} // namespace ark
