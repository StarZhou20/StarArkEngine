# StarArk 远期路线图（Post-v0.1）

> **状态**: 当前阶段 = **v0.3-modspec 主路径全部落地**（详见 [DevLog.md §0](DevLog.md) + [ModSpec.md 附录 D](ModSpec.md)）。
> 本文件保存中长期路线；**v0.3 主线规则已被 [ModSpec.md](ModSpec.md) 接管**，本文件只补充其未覆盖的渲染/资产/工具支线。
>
> 本文件的内容**不等于承诺**，也**不会被现在的代码依赖**。一旦与 ModSpec 冲突，**以 ModSpec 为准**。

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
| 15.F.5 | **取消** | ~~mod.toml 元数据 + 依赖排序~~ | 升级到 v0.3 ModSpec §2，作为 v0.3 必做项 |
| 15.F.6 | **取消** | ~~ScriptApi v3：Camera + Component 反射访问~~ | 升级到 v0.3 ModSpec §15，ScriptApi v2→v3 全量规划 |
| **v0.3 ModSpec 主路径** | v0.3 | **§2/§3/§4.2/§5/§6.1/§6.2 全部已落** | ✅ 详见 [ModSpec.md 附录 D](ModSpec.md) |
| 16 | v0.3 | 级联阴影 CSM | 开放世界远景阴影必备，但必须在 RenderSettings 反射之后加（不然 MOD 改不了参数） |
| 17 | v0.3 | `.ark.mesh` + Asset 管线（hash/缓存） | 支撑大量资产；依赖 15.D 的 VFS |
| 18 | v0.3 | Blender exporter 插件 | 依赖 ModSpec §4 持久 ID + §3 路径语法 |
| 19 | v0.3 | Prefab / 场景 chunk | 开放世界流式加载的前置；依赖 ModSpec §4 持久 ID |
| 20 | v0.4 | 骨骼动画 + 动画状态机 | 角色 MOD 刚需；依赖 15.A 反射（不然 anim clip 名字是写死 enum） |
| 21 | v0.4 | 物理（Bullet/Jolt） | - |
| 22 | v0.4 | Recast/Detour 导航 | - |
| 23 | v0.5 | 流式加载 + camera-relative rendering | 解决 float 精度 + 大世界加载；依赖 19 |
| 24 | v0.5 | 行为树 `.bt.toml` + 对话 `.dlg.toml` | AI 写 NPC；依赖 15.F |
| 25+ | v1.0 | LOD / Impostor / 音频 / 多人 | 内容扩张期 |

### v0.2 / v0.2.x 历史规划（已完结，保留备查）

> 详细决策（反射宏 / GUID / TOML schema / VFS / load_order / 验收标准）见 `git log` 早期版本。当前实现状态以 [DevLog.md §0](DevLog.md) + [ModSpec.md 附录 D](ModSpec.md) 为准。
>
> v0.2-data-contract 五条验收 ✅ 全部达成；v0.2.x 收尾包括 ScriptApi v2 / Deferred Pipeline / v0.3 ModSpec 主路径。

### 长期路径上的"不做"（v0.2 范围外）

- GUI 关卡编辑器（Blender 空间布局 + WPF Object Inspector 表单，够用了）
- 自研 3D 建模 / 动画编辑
- 自研 UI 设计器
- 强制 ECS 切换（v0.3 起 ECS 范式优先，但 OOP Component + ScriptComponent 仍保留为 secondary 范式）
- Lua / Python 脚本（单语言路线，C# 够用）

> 进一步"暂时不做"清单见 [ModSpec.md 附录 C](ModSpec.md)。

### 工程级硬性约定（不随版本演进，未接到作者通知前不变）

1. **不打包引擎为 DLL**：`StarArkEngine` 在 v0.3/v0.4 期内保持 STATIC 库形态。渲染层/RHI/反射尚在频繁调整，过早稳定二进制边界会拖慢迭代。ModSpec §0 中提到的 `StarArkEngine.dll` 是远期目标，触发条件需作者明确通知。
2. **启动器使用 WPF**（.NET 10）：放在 `tools/Launcher/`，与 `tools/LightingTuner/` 同前端栈。不使用 ImGui / Win32 / Avalonia / web 实现启动器。理由：现代化、设计可控，且与 Object Inspector（ModSpec §7.2）共享反射 schema + DataBinding。
3. **不开放完整自定义渲染管线（SRP 风格）给 Mod**：渲染扩展走"后处理链 + shader override + 少量稳定钩子"三件套，不暴露 RHI command buffer 给托管端。理由见 [DevLog.md](DevLog.md) 同名讨论；详细方案在延迟管线落地之后另起 Phase。

---

## 延迟渲染（Deferred Pipeline）✅ 2026-04-26 完结

v0.2.x 周期内 9 项前置 + 出口验收全部落地：`RHIRenderTarget` / MRT / Depth32F+RGBA16F 格式 / `Material::BindToShader` / `Material::IsTransparent` / `DrawListBuilder` / `gbuffer + lighting` shader。`RenderSettings::pipeline = Forward | Deferred` 切换，`game.config.json::pipeline` < `ARK_PIPELINE` env override；透明物体走 forward overlay 叠在 lit_ 上；Bistro deferred 烟测全过。详细记录见 [DevLog.md](DevLog.md) / `/memories/repo/v0.2-progress.md`。

**未来后续**（不阻塞现状，按需另起 Phase）：

- Tile-based / Cluster-based 多光源剔除（当前几十光源够用）
- `PostProcess` / `IBL` 残余 raw `glGenFramebuffers` 19 处迁到 `RHIRenderTarget`
- DX12 / Vulkan 后端
- 多线程命令录制（与 [KnownIssues §B1](KnownIssues.md) 一并立项）
- Mod 后处理 / 渲染钩子（"效果级扩展点"，另起 Phase）

---

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
