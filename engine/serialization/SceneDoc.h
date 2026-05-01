#pragma once

// -----------------------------------------------------------------------------
// SceneDoc — v0.2 15.C 反射驱动的场景序列化
//
// 职责:
//   - 把 AScene 里的所有 AObject + 每个对象的 AComponent 列表写成 TOML 文本
//   - 反过来把 TOML 文本还原到一个 AScene 里（创建对象、按 TypeRegistry 还原组件字段）
//
// 输出 schema（与 Roadmap 一致）:
//   [scene]
//   name = "..."
//   schema_version = 1
//
//   [[objects]]
//   guid = "..."
//   name = "..."
//   parent = ""                                # 空串或 parent guid
//   transform.position = [x, y, z]
//   transform.rotation = [x, y, z, w]
//   transform.scale    = [sx, sy, sz]
//
//   [[objects.components]]
//   type = "Light"
//   color = [1.0, 0.5, 0.2]
//   intensity = 8.0
//   ...
//
// 注意:
//   - v0.2 只覆盖"通过 TypeRegistry 注册过字段"的组件；未注册的组件跳过并 warning
//   - MeshRenderer 的 mesh/material 字段尚未反射（15.C 后续：加 mesh_path/material_path）
//   - 先做 Save/Load，热重载与 MOD override 留给 15.D
// -----------------------------------------------------------------------------

#include <filesystem>
#include <string>

namespace ark {

class AScene;

class SceneDoc {
public:
    // 把 scene 所有 objectList_ 的对象写到 path（覆盖写）。返回成功。
    static bool Save(const std::filesystem::path& path, AScene* scene);

    // 读 path，对 scene 执行:
    //   1) 对每个 [[objects]] 表，在 scene 里 CreateObject<AObject>()（空对象）
    //   2) 按 TypeRegistry 还原每个 [[objects.components]] 到对应类型
    //   3) 应用 transform.position/rotation/scale
    //   4) 第二遍解析 parent 字段（此时所有 guid → object 映射已建好）
    //
    // 已存在的对象不会被清理；调用方按需自行清空。返回成功。
    static bool Load(const std::filesystem::path& path, AScene* scene);

    // 直接 dump/parse 字符串，供单元测试与热重载逻辑复用。
    static std::string Dump(AScene* scene);
    static bool        LoadFromString(std::string_view text, AScene* scene,
                                      std::string* errorMsg = nullptr);

    // -------------------------------------------------------------------
    // v0.3 ModSpec §5 — addon scene overlay.
    //
    // Layout (one file per addon mod, conventionally
    // `<mod_root>/scene.overlay.toml`):
    //
    //   [overlay]
    //   schema_version = 1
    //   name = "..."                # optional, debug only
    //
    //   # Patch fields on an existing component of an existing object.
    //   [[overrides]]
    //   target_guid    = "..."
    //   component_type = "Light"
    //   intensity      = 12.0
    //   color          = [1.0, 0.4, 0.1]
    //
    //   # Remove an object (and its components) by guid.
    //   [[deletions]]
    //   target_guid = "..."
    //
    //   # Add a brand-new object — same fields as [[objects]] in the
    //   # base scene file.
    //   [[additions]]
    //   guid   = "..."
    //   name   = "extra_lamp"
    //   parent = ""
    //   [additions.transform]
    //   position = [0.0, 1.0, 0.0]
    //   ...
    //
    //   # Attach a new component to an existing object.
    //   [[components_attached]]
    //   target_guid = "..."
    //   type        = "Light"
    //   intensity   = 8.0
    //
    // Order of application:
    //   deletions → overrides → components_attached → additions
    // (deletions first so a later addition can re-use a freed name; additions
    // last so they can target newly-written guids in subsequent overlays.)
    //
    // Overlay does NOT replace the base scene; it patches an already-loaded
    // AScene in place. Callers usually load the base scene first via Load(),
    // then ApplyOverlay() per enabled addon mod.
    static bool ApplyOverlay(const std::filesystem::path& path, AScene* scene);
    static bool ApplyOverlayFromString(std::string_view text, AScene* scene,
                                       std::string* errorMsg = nullptr);
};

} // namespace ark
