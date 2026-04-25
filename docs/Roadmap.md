# StarArk 远期路线图（Post-v0.1）

> **状态**: 远景 / 仅作记录。**当前实际开发阶段是 v0.1-renderer 里程碑**（见 [DevLog.md](DevLog.md)）。
> 本文件只保存"v0.1 之后"的设想，防止它污染当下的工程主线。
>
> 本文件的内容**不等于承诺**，也**不会被现在的代码依赖**。只有当 v0.1-renderer 完全稳定、
> 打上 tag、文档自洽之后，才会开始考虑这里的任何一条。

---

## 1. 长期愿景：Y+（Runtime + 开放数据契约 + 多工具前端）

面向开放世界 + AI 协作。

### 核心理念

- **引擎只做 runtime**：加载数据 → 主循环 → 渲染/逻辑/物理。不做一体化编辑器。
- **所有可编辑内容文本化**：TOML 优先（有注释、无引号 key，对 AI 和人都友好）。
- **重数据独立二进制**：Mesh / Texture / Anim 走 `.ark.mesh` / `.ark.tex` / `.ark.anim`，文本里只引用路径 + hash。
- **序列化按组件表展开**：`objects[]` + `transforms[]` + `lights[]` + ... 而非对象树嵌套。
- **身份用 GUID**：每个 AObject 一个 UUID，组件引用走 GUID 字符串。
- **前端可插拔**：Blender 插件、WPF Inspector、VS Code + C#、AI / CLI。

### 分工契约

| 事务 | 工具 | 谁做 |
|------|------|------|
| 场景布局、地形、建筑摆放 | Blender + exporter 插件 | 人（美术） |
| 光照实时调参 | Lighting Tuner（WPF） | 人 / AI |
| 游戏逻辑 / UI / 行为树 / 动画状态机 / 对话 / 数值表 | C# + TOML | **AI** |
| Shader | `*.glsl` | 人 / AI |

### 脚本语言：C#（CoreCLR Hosting）

- C++ 引擎用 `hostfxr` + `nethost` 启动 CoreCLR，热加载 C# DLL
- 绑定走 P/Invoke + `UnmanagedCallersOnly`（无 marshalling 开销）
- 热重载基于 `AssemblyLoadContext(isCollectible: true)` + mtime 轮询
- 面向 .NET 10

### 目标目录结构（远期）

```
content/
├── scenes/world_chunks/*.scene.toml
├── prefabs/*.prefab.toml
├── meshes/*.ark.mesh
├── textures/*.ark.tex
├── materials/*.mat.toml
├── anims/*.asm.toml
├── ai/*.bt.toml
├── data/*.toml
└── logic/*.cs
```

### 候选 Phase（v0.2+）

**优先级重构（2026-04）**: 根据"面向 Skyrim 级 MOD 生态"的目标倒推，以下顺序绝对不可调：
**数据契约永远是地基**，渲染纵深 / 脚本 / 动画 / 编辑器 GUI 全部建立在它之上。

| Phase | 里程碑 | 主题 | 为什么是这个顺序 |
|-------|-------|------|-----------------|
| 15.A | v0.2 | 组件反射系统 | ✅ 完成 |
| 15.B | v0.2 | GUID + 对象身份 | ✅ 完成 |
| 15.C | v0.2 | 场景 TOML 序列化 | ✅ 完成 |
| 15.D | v0.2 | 资源覆盖 VFS | ✅ 完成（mods/ 覆盖 content/，load_order.toml 启用列表） |
| **v0.2 tag**：理论可 MOD 的最小引擎 ✅ |||
| 15.E | v0.2.x | 反射驱动 Inspector | ⏳ 把 Lighting Tuner 升级为"任意组件自动出 UI" |
| 15.F.1 | v0.2.x | C# 脚本 — CoreCLR 实接入 | ✅ |
| 15.F.2 | v0.2.x | Native↔Managed Bridge + MOD 加载 | ✅ |
| 15.F.2.1 | v0.2.x | Unity 风格 7 阶段 MOD 生命周期 | ✅ |
| 15.F.3 | v0.2.x | 引擎 API 暴露 v2（Spawn/Find/Transform/Scene） | ✅ |
| 15.F.4 | v0.2.x | MOD 热重载（collectible ALC + watcher） | ⏳ |
| 15.F.5 | v0.2.x | mod.toml 元数据 + 依赖排序 | ⏳ |
| 15.F.6 | v0.2.x | ScriptApi v3：Camera + Component 反射访问 | ⏳ |
| 16 | v0.3 | 级联阴影 CSM | 开放世界远景阴影必备，但必须在 RenderSettings 反射之后加（不然 MOD 改不了参数） |
| 17 | v0.3 | `.ark.mesh` + Asset 管线（hash/缓存） | 支撑大量资产；依赖 15.D 的 VFS |
| 18 | v0.3 | Blender exporter 插件 | 依赖 15.C 的 TOML schema |
| 19 | v0.3 | Prefab / 场景 chunk | 开放世界流式加载的前置；依赖 15.B GUID |
| 20 | v0.4 | 骨骼动画 + 动画状态机 | 角色 MOD 刚需；依赖 15.A 反射（不然 anim clip 名字是写死 enum） |
| 21 | v0.4 | 物理（Bullet/Jolt） | - |
| 22 | v0.4 | Recast/Detour 导航 | - |
| 23 | v0.5 | 流式加载 + camera-relative rendering | 解决 float 精度 + 大世界加载；依赖 19 |
| 24 | v0.5 | 行为树 `.bt.toml` + 对话 `.dlg.toml` | AI 写 NPC；依赖 15.F |
| 25+ | v1.0 | LOD / Impostor / 音频 / 多人 | 内容扩张期 |

### v0.2-data-contract 详细规划（当前开工目标）

目标: **理论上任何 MOD 都能修改到的最小引擎**。没有 GUI 编辑器，没有脚本，但已经可以：

1. 替换任意贴图 / 模型 / shader（把文件丢进 `mods/<modname>/...`）
2. 修改任意场景对象的任意组件字段（编辑 TOML，外部工具或文本编辑器）
3. 新增对象 / 组件实例（TOML 里加一段）
4. 两个 MOD 同改一个对象 = 后加载的覆盖（基于 GUID）

#### 关键设计决策

**反射（15.A）**:
- C++ 宏 + 静态注册表，不用 RTTI 之外的运行时魔法
- 每个 `AComponent` 子类写 `ARK_REFLECT_COMPONENT_BEGIN(Light)` ... `ARK_REFLECT_FIELD(intensity, Float)` ... `ARK_REFLECT_COMPONENT_END()`
- `TypeRegistry::Create("Light")` → `unique_ptr<AComponent>`
- `TypeRegistry::GetFields("Light")` → `std::span<const FieldInfo>`；每个 `FieldInfo` 有 name / type / offset / default
- 字段类型枚举: `Bool / Int / Float / Vec2 / Vec3 / Vec4 / Color3 / Color4 / Quat / String / EnumInt / AssetPath`
- 读写通过 `offset + type tag + memcpy` 完成，无虚函数开销

**GUID（15.B）**:
- UUID v4，文本形式 `"a1b2c3d4-5e6f-7a8b-9c0d-e1f2a3b4c5d6"`
- `AObject::GetGuid()` 返回 `const std::string&`；`CreateObject<T>()` 自动生成
- 从 TOML 反序列化时使用文件里的 GUID，不生成新的
- 引用其他对象: 字段类型 `ObjectRef`，序列化为 GUID 字符串；runtime 解析为 `AObject*`

**场景 TOML（15.C）**:

```toml
[scene]
name = "Village_Chunk_0_0"
schema_version = 1

[[objects]]
guid = "a1b2c3d4-..."
name = "Campfire_01"
parent = ""  # empty or parent GUID
transform.position = [2.3, 0.0, 1.5]
transform.rotation = [0.0, 0.0, 0.0, 1.0]  # quat xyzw
transform.scale    = [1.0, 1.0, 1.0]

[[objects.components]]
type = "MeshRenderer"
mesh     = "content://meshes/campfire.obj"
material = "content://materials/wood.mat.toml"

[[objects.components]]
type = "Light"
light_type = "Point"
color = [1.0, 0.5, 0.2]
intensity = 8.0
range = 5.0
```

- **组件表展开**: 一个对象的所有组件平铺，不嵌套；AI 修改一个组件不影响其他
- **schema_version**: 将来升级字段时可批量迁移
- **资源引用**: 用 URL-like 前缀（`content://`, `mods://`, `abs://`）；runtime 走 VFS 解析

**资源覆盖 VFS（15.D）**:

- `Paths::ResolveResource("textures/foo.png")` 新增方法
- 查找顺序（后加载优先）:
  1. `mods/<mod_1>/textures/foo.png`（按 load order 从高到低）
  2. `mods/<mod_2>/textures/foo.png`
  3. `content/textures/foo.png`（原版）
- `mods/load_order.toml` 文件声明启用的 MOD 和顺序（Skyrim `plugins.txt` 等价物）
- 覆盖只是路径替换，**不涉及合并逻辑**；合并冲突留给 15.C 的 GUID override（编辑同一个对象时后加载的胜出）

#### v0.2 tag 的验收标准

1. ✅ `CottageScene` 改为从 `content/scenes/cottage.toml` 加载，而非 C++ 硬编码
2. ✅ 改 `cottage.toml` 里 Light 的 intensity → 重启后生效（mtime 热重载可选，不阻塞）
3. ✅ 把 `mods/testmod/textures/ground.png` 丢进去 → 地面贴图被替换
4. ✅ AI 读 `docs/` 就能写出一个新的 `.toml` 场景，不需要写一行 C++
5. ✅ 两个 MOD 改同一个 Light GUID，后加载的胜出（日志打印 override chain）

### 长期路径上的"不做"（v0.2 范围外）

- GUI 关卡编辑器（Blender 空间布局 + WPF Inspector 表单，够用了）
- 自研 3D 建模 / 动画编辑
- 自研 UI 设计器
- ECS 切换（OOP Component + 反射 = 类 ECS 优势，但不打破现有 API）
- Lua / Python 脚本（单语言路线，C# 够用）

---

### 历史草案（已弃用）

> 以下是 v0.1 冻结前的旧 Roadmap 表格草案，保留仅作历史参考。实际顺序以上面表格为准。

| Phase | 主题 | 备注 |
|-------|------|------|
| ~~15 C# Scripting Host~~ | 改为 15.F，放到 v0.2.x（反射之后） |
| ~~16 组件反射 + GUID~~ | 提前到 15.A-B，列为 v0.2 核心 |
| ~~17 TOML + Blender 导出~~ | 拆成 15.C 和 18，Blender 插件后置到 v0.3 |
| ~~18 .ark.mesh~~ | 移到 v0.3 |
| ~~19 Prefab + chunk~~ | 移到 v0.3 |
| ~~20 行为树~~ | 移到 v0.5，依赖 15.F 脚本 |
| ~~21 动画状态机~~ | 移到 v0.4 一起做 |
| ~~22 导航~~ | v0.4 |
| ~~23 流式加载~~ | v0.5 |

### 有意识**不做**

- 一体化关卡编辑器（短期内）
- 自研 3D 建模 / 动画编辑
- 自研 UI 设计器
- ECS 切换（现 OOP Component 已够用）
- Lua / Python 脚本

---

## 2. Blender 命名约定（导出契约的一部分，规划中）

Blender 里无法摆放的概念（组件/脚本/碰撞体），通过**对象命名 + 自定义属性**传达给 exporter。

### 碰撞体

- 名字含 `collision` 或带 `_col` / `_cx` 后缀的 Mesh/Empty 被视为碰撞体，不参与渲染
- 自定义属性：`collision_type`（box/sphere/capsule/convex/mesh）、`collision_complex`（bool）、`is_trigger`（bool）
- 作为子对象：`Hero / Hero_col_capsule`，挂到父对象 `RigidBody`/`Collider`
- 独立对象：静态 collider，隐式 StaticBody

### 组件

- 自定义属性 `components = ["Light", "AudioSource"]` 数组
- 字段用 `componentName.fieldName` 前缀，导入时按反射填充

---

## 3. 脚本挂载的三种做法（规划中）

### A：Blender 命名/属性（美术主导）

```toml
[[objects]]
guid = "a1b2c3d4-..."
scripts = ["Gameplay.FireFlicker"]
[objects.properties."Gameplay.FireFlicker"]
frequency = 2.5
```

### B：Prefab（可复用的对象类型）

```toml
kind = "Prefab"
name = "EnemyGoblin"
scripts = ["Gameplay.EnemyAI", "Gameplay.HealthSystem"]
```

### C：代码直接挂载（纯代码生成的对象）

```csharp
public class BossFight : IScene {
    public void OnLoad() {
        var boss = Scene.Spawn("FinalBoss");
        boss.AddScript<BossController>(new() { Phase = 1 });
    }
}
```

三种做法底层都走同一套反射：字符串查类 → 实例化 → 字典填字段。依赖 Phase 16 的反射系统。

---

## 4. 三条验证标准（长期）

本路线最终是否成功，看三条：
1. **新增一个组件字段，Inspector UI 自动出现**（= 反射 schema 驱动工作）
2. **Blender 摆一个光源，引擎立刻加载显示**（= 数据契约连通）
3. **AI 改一行 `.bt.toml`，NPC 行为立刻变化**（= AI 一等公民兑现）

在 v0.1-renderer 稳定之前，这些标准一个都不去做。
