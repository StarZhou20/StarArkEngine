#pragma once

// -----------------------------------------------------------------------------
// TypeInfo — v0.2 组件反射系统（15.A）
//
// 目标: 让每个 AComponent 子类声明自己的字段 (name + type + byte offset)，
// 供序列化器 / Inspector UI / 脚本桥 在运行期按名字读写。
// 不使用 RTTI 之外的运行时魔法，不引入第三方反射库。
//
// 字段约定:
//   - 字段类型由 FieldType 枚举标记；序列化器按 tag 做 memcpy
//   - offset 由 offsetof 算出；size 由 sizeof 算出（调试校验）
//   - 默认值暂不反射 (v0.2 版本靠构造函数赋默认值；将来按需扩展)
//
// 用法见本文件末尾宏注释。
// -----------------------------------------------------------------------------

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ark {

class AComponent;

// 反射支持的字段基本类型。新增类型必须同步更新 FieldTypeName() 和序列化器。
enum class FieldType {
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Color3,     // glm::vec3，sRGB 语义
    Color4,     // glm::vec4，sRGB + alpha
    Quat,       // glm::quat，xyzw
    String,     // std::string
    EnumInt,    // 序列化为 int；将来扩 StringEnum
    AssetPath,  // std::string，走 VFS 解析
    ObjectRef,  // std::string GUID，runtime 解析为 AObject*
};

const char* FieldTypeName(FieldType t);

struct FieldInfo {
    std::string name;    // 序列化名（TOML key），不一定等于 C++ 字段名
    FieldType   type;
    std::size_t offset;  // 相对组件实例起始地址的字节偏移
    std::size_t size;    // sizeof，用于调试校验
};

struct TypeInfo {
    std::string name;  // 类型名，如 "Light"
    std::function<std::unique_ptr<AComponent>()> factory;
    std::vector<FieldInfo> fields;

    const FieldInfo* FindField(std::string_view field_name) const;
};

// 全局静态注册表。线程约束: 仅在静态初始化阶段 / 主线程访问。
class TypeRegistry {
public:
    static TypeRegistry& Get();

    // 重复注册同名类型会 assert。
    void Register(TypeInfo info);

    const TypeInfo* Find(std::string_view type_name) const;

    const std::vector<TypeInfo>& All() const { return types_; }

private:
    TypeRegistry() = default;

    std::vector<TypeInfo> types_;
};

} // namespace ark

// -----------------------------------------------------------------------------
// 反射宏
//
// 在类头文件的 public 段落里:
//     class Light : public AComponent {
//     public:
//         ARK_DECLARE_REFLECTION(Light);
//         ...
//     };
//
// 在对应 .cpp 文件（文件底部或任意位置）:
//     ARK_REFLECT_COMPONENT(Light)
//         ARK_FIELD(type_,      "type",       EnumInt)
//         ARK_FIELD(color_,     "color",      Color3)
//         ARK_FIELD(intensity_, "intensity",  Float)
//     ARK_END_REFLECT(Light)
//
// 展开后:
//   - ClassName::ArkBuildTypeInfo() 返回填好字段表的 TypeInfo
//   - 匿名命名空间内一个静态 registrar 对象，在程序启动时把 TypeInfo 注册到 TypeRegistry
//
// 注意事项:
//   - ARK_FIELD 的第一个参数是 **C++ 字段标识符**（可能带下划线后缀）
//   - 第二个参数是 **序列化 key**（TOML 里看到的）
//   - offsetof 在非 standard-layout 类上是 conditionally-supported；
//     MSVC 仅发 warning（已在 CMake 层面对工程全局容忍）
// -----------------------------------------------------------------------------

#define ARK_DECLARE_REFLECTION(ClassName)                                       \
    static ::ark::TypeInfo ArkBuildTypeInfo();                                  \
    std::string_view GetReflectTypeName() const override { return #ClassName; }

#define ARK_REFLECT_COMPONENT(ClassName)                                        \
    ::ark::TypeInfo ClassName::ArkBuildTypeInfo() {                             \
        using T = ClassName;                                                    \
        ::ark::TypeInfo info;                                                   \
        info.name = #ClassName;                                                 \
        info.factory = []() -> std::unique_ptr<::ark::AComponent> {             \
            return std::make_unique<T>();                                       \
        };

#define ARK_FIELD(cpp_member, serial_name, type_enum)                           \
        info.fields.push_back(::ark::FieldInfo{                                 \
            std::string(serial_name),                                           \
            ::ark::FieldType::type_enum,                                        \
            offsetof(T, cpp_member),                                            \
            sizeof(T::cpp_member)                                               \
        });

#define ARK_END_REFLECT(ClassName)                                              \
        return info;                                                            \
    }                                                                           \
    namespace {                                                                 \
        struct ArkReflectReg_##ClassName {                                      \
            ArkReflectReg_##ClassName() {                                       \
                ::ark::TypeRegistry::Get().Register(                            \
                    ClassName::ArkBuildTypeInfo());                             \
            }                                                                  \
        };                                                                      \
        static ArkReflectReg_##ClassName g_arkReflectReg_##ClassName;           \
    }
