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

| Phase | 主题 | 动机 |
|-------|------|------|
| 15 | C# Scripting Host | 解锁 AI 写 gameplay |
| 16 | 组件反射 + GUID | Schema 驱动的序列化/Inspector |
| 17 | 通用场景 TOML + Blender 导出插件 | 验证数据契约 |
| 18 | `.ark.mesh` 二进制 + Asset 管线 | 支撑内容量 |
| 19 | Prefab + 场景 chunk 切分 | 流式加载前置 |
| 20 | 行为树（`.bt.toml`） | AI 写 NPC |
| 21 | 动画状态机（`.asm.toml`） | - |
| 22 | Recast/Detour 导航 | - |
| 23 | 流式加载 + camera-relative | 大世界 |
| 24+ | LOD/Impostor、物理、音频、联机 | - |

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
