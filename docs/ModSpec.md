# StarArk Mod Specification (ModSpec) v0.1

> 这是 StarArk 引擎的 Mod 范式宪法。所有引擎实现、SDK 工具、Mod 制作、AI 写作都必须遵守。
> 一旦发布给外部 modder，本文规则只允许 append-only，不允许破坏性变更。

适用版本：v0.3+ 起开始落地。v0.2 的现有实现（cottage.toml / hellomod 等）将按本文档迁移。

---

## 0. 总体哲学

StarArk 是 **Mod-First 引擎**，目标是 Skyrim 级别 mod 生态 + AI-friendly 创作。
范式定位：**Bannerlord 模块骨架 + Minecraft 命名空间寻址 + Doom 风自包含资产 + Bevy 风 ECS 数据契约**。

三层发布物：
- **引擎本体**（远期目标 `StarArkEngine.dll`，**v0.3/v0.4 期内仍为静态库**——渲染层/RHI/反射尚在调整，未接到引擎作者明确通知前**不打包 DLL**）
- **Mod SDK**（`StarArk.Scripting.dll` + 文档 + CLI 工具）
- **玩家发布包**（**WPF 启动器**（.NET 10）+ 引擎可执行二进制 + `content/mods/` 文件夹）

> **启动器技术栈固定为 WPF**：与 Lighting Tuner、Object Inspector 共享前端栈、共用反射 schema 与 DataBinding 模式。
> 不使用 ImGui / Win32 / Avalonia / web 前端实现启动器。

---

## 1. Mod 模型：自包含为主，增强为辅

### 1.1 🔴 一个 mod 文件夹 = 一个完整游戏（默认模式）

```
content/mods/<mod_id>/
  mod.toml              ← 必需，元数据
  scenes/
  meshes/
  textures/
  materials/
  audio/
  shaders/
  scripts/              ← C# DLL（编译后）
  strings/              ← i18n（可选）
```

启动时玩家**只激活一个 game 类型 mod**，即"选战役"心智。

**失败时**：mod.toml 缺失或格式错误 → 该 mod 在启动器列表中显示为不可用，红色错误提示，不进入加载流程。

### 1.2 🟡 增强型 Addon mod（叠加在 game mod 之上）

```toml
# mod.toml
type        = "addon"
applies_to  = ["vanilla", "space_survival"]
```

Addon mod 走字段级覆盖语法（见 §5），不能独立启动。

**失败时**：addon 应用到不在 `applies_to` 白名单的 game mod → 启动器禁用，给警告。

---

## 2. mod.toml Schema v1

### 2.1 🔴 必需字段

```toml
schema_version = 1
id             = "space_survival"   # 全局唯一，[a-z][a-z0-9_]*
type           = "game"             # "game" | "addon"
version        = "1.0.0"            # SemVer
display_name   = "Space Survival"   # 玩家可见名，支持任意 Unicode（含中文）
authors        = ["Player A"]
engine_min     = "0.3.0"
script_api_min = 2                  # 最低 ScriptApi 版本，见 §15
supported_pipelines = ["forward"]   # 该 mod 的 shader 支持的渲染管线，见 §7.4
```

### 2.2 🟡 可选字段

```toml
description    = "..."              # 支持任意 Unicode
license        = "MIT"
homepage       = "https://..."
default_locale = "zh-CN"
locales        = ["zh-CN", "en-US"]
depends_on     = [{ id = "core_lib", version = ">=1.0" }]
applies_to     = ["vanilla"]        # 仅 type=addon
load_after     = ["other_addon"]    # 仅 type=addon
load_before    = []
```

**失败时**：
- `engine_min` 或 `script_api_min` 不满足 → 拒绝加载，启动器红字
- `supported_pipelines` 与玩家当前管线不匹配 → 启动器禁用该 mod，提示原因
- `depends_on` 缺失 → 启动器列出缺失依赖，禁用该 mod
- `depends_on` 循环依赖 → 拓扑排序失败，所有涉及 mod 全部禁用并报告

---

## 3. 资产路径与命名空间

### 3.1 🔴 三轨路径语法

| 语法 | 含义 | 用途 |
|---|---|---|
| `./<path>` | 相对当前 mod | **默认推荐**，写 mod 时优先用 |
| `mod://<mod_id>/<path>` | 显式跨 mod 引用 | 罕见，必要时使用 |
| `engine://<path>` | 引擎二进制内嵌的硬编码 fallback | 仅 fallback / 调试 |

**`engine://` vs `mod://core/` 区别**：
- `engine://` = 引擎绝对保证存在的最小集（紫红 shader、白方块 mesh、空音频），不可被 mod 替换
- `mod://core/` = core mod 提供的正常游戏资产，可被替换/覆盖

**禁止**：裸路径（不带前缀）一律拒绝，避免歧义。

**为什么默认 `./`**：保证整个子文件夹（`textures/`、`materials/`、`shaders/`）可原样复制粘贴到另一个 mod，无需修改引用。

### 3.2 🔴 引擎实现要求

- `Paths::ResolveResource(logical_path, current_mod_ctx)` 必须接受当前 mod 上下文参数
- 加载 scene/material 等文件时，引擎记录"当前 mod"传给路径解析器
- 跨 mod 引用必须显式写 `mod://`，否则解析失败

**失败时**：
- 路径无前缀 → 加载失败，日志"missing scheme prefix"
- `mod://other/...` 但 other 未激活 → 加载失败，日志列出缺失 mod
- 资源文件不存在 → 见 §9 资源严重性等级

---

## 4. 持久身份（GUID）

### 4.1 🔴 双层身份

| 层 | 形式 | 持久化 | 用途 |
|---|---|---|---|
| **持久身份** | `"<mod_id>:<local_id>"` 字符串 | ✅ 写入 TOML/存档 | mod 之间引用、存档恢复 |
| **运行时身份** | `uint64`（现 `AObject::GetId()`） | ❌ 进程内唯一 | 内存查找、ScriptApi handle |

**示例**：`"core:cottage_door"`、`"my_overhaul:secret_chest"`

### 4.2 🔴 扁平 ID + 不变性

- ID 是**扁平**的，不带层级路径（理由：Blender 友好、重组父子层级不会断引用、AI 心智简单）
- 一旦写入 TOML，**禁止重新生成已有对象的持久 ID**
- 新增对象才分配新 ID
- ark-validate 必须能检测"对象 ID 在两次保存之间发生变化"并报错
- Blender 导出插件按"object name → local_id"约定自动生成，modder 无心智负担

**失败时**：modder 误改 ID → ark-validate 拒绝打包；运行时存档引用的 ID 找不到 → 见 §6.3 缺失策略。

---

## 5. 数据覆盖语法（仅 addon mod 使用）

### 5.1 🔴 四种操作

```toml
# 覆盖：改已有对象/组件的字段
[[overrides]]
target = "core:cottage_door"
[overrides.components.MeshRenderer]
mesh = "./meshes/fancy_door.fbx"

# 添加：在已有 scene 里追加新对象
[[additions]]
target_scene = "core:cottage"
[[additions.objects]]
id = "my_addon:secret_chest"
# ...

# 挂组件：给已有对象追加新组件
[[components_attached]]
target = "core:player"
type   = "my_addon:Reputation"
faction_likes = { ... }

# 删除：移除已有对象
[[deletions]]
target = "core:annoying_npc"
```

### 5.2 🔴 冲突解决

- 多个 addon 改同一字段 → 后加载者胜，引擎日志列出冲突
- `[[deletions]]` 与 `[[overrides]]` 同 target → 删除胜，覆盖被丢弃并告警

**失败时**：
- target 不存在 → 该条 patch 跳过，记录 warning
- 字段类型不匹配 → 该字段跳过，记录 error

---

## 6. 存档契约

### 6.1 🔴 三段式格式

```json
{
  "header": {
    "engine_version": "0.3.0",
    "save_version": 1,
    "playtime_seconds": 12345,
    "active_mods": [
      { "id": "vanilla",  "version": "1.0.0", "schema_hash": "abc123..." },
      { "id": "my_addon", "version": "0.5.2", "schema_hash": "def456..." }
    ]
  },
  "world": { /* 反射驱动序列化 */ },
  "mod_state": {
    "vanilla":  { /* mod 自定义 */ },
    "my_addon": { /* mod 自定义 */ }
  }
}
```

### 6.2 🔴 schema_hash 算法

- **输入**：`sorted([field_full_name + ":" + field_type_name] for each Required field)`
- **算法**：SHA-256，截前 16 字符（够用、可读）
- **只包含 Required 字段**——新增 Optional 字段完全向后兼容，不改变 hash
- **忽略字段顺序、忽略默认值、忽略字段注释**

理由：让"Required 字段"成为 mod 的稳定契约面，"Optional 字段"成为可演进面。

### 6.3 🔴 兼容性策略

加载存档时校验 hash：

| 情况 | 严格模式（默认） | 宽松模式（可选启动选项） |
|---|---|---|
| schema_hash 一致 | 直接加载 | 直接加载 |
| schema_hash 不同但有 Migrate 钩子 | 跑 Migrate 后加载，提示 | 同 |
| schema_hash 不同且无 Migrate | 拒绝加载 | 跳过该 mod 的 mod_state，世界对象保留为"幽灵 record"不实例化 |
| game mod 缺失 | 拒绝加载 | 拒绝加载（不可降级） |
| addon mod 缺失 | 拒绝加载 | 跳过该 mod 的 mod_state，幽灵 record |

### 6.4 🟡 字段级兼容性标注

```csharp
[ArkSaveField(Required = true)]              // 参与 schema_hash，缺失即拒绝读档
public int FactionId;

[ArkSaveField(Optional = true, Default = 0)] // 不参与 hash，缺失用默认值
public int ReputationBonus;

[ArkSaveField(Migrated = true)]              // 跨版本迁移，必须实现 Migrate 钩子
public Dictionary<string, int> FactionLikes;
```

**失败时**：Required 字段缺失 → 读档失败，明确错误提示。

---

## 7. 渲染范式

### 7.1 🔴 ECS 优先 + 允许 per-object 脚本

- **主要范式**：组件 = 数据，行为 = 全局 system（C# mod 写 system 函数，按"哪些 entity 拥有特定组件"批处理）
- **次要范式**：允许 per-object 脚本（`ScriptComponent` 模式），用于 NPC AI、UI 回调等典型 per-object 行为
- 文档、示例、Object Inspector **优先展示 ECS 范式**

### 7.2 🟡 Object Inspector（v0.3 必做工具）

WPF GUI 工具，反射驱动，让 modder 可视化处理对象与组件：

- 选择 entity → 看它挂了哪些组件
- 增加/删除组件（含数据组件、ScriptComponent）
- 编辑组件字段（按反射 schema 动态生成 UI）
- 写回 TOML（保留注释/顺序）

**禁止硬编码组件类型**——所有 UI 由反射 schema 动态生成。命名为 "Object Inspector"，因为它的核心动作是"为对象增删改组件 + 编辑组件字段"，跨数据组件和 ScriptComponent 中性表达。

### 7.3 🔴 Material 独立 sidecar

```toml
# scene.toml
[[objects.components]]
type     = "MeshRenderer"
mesh     = "./meshes/wheel.fbx"
material = "./materials/wheel.mat.toml"

# materials/wheel.mat.toml
shader     = "engine://shaders/pbr_standard"
albedo_map = "./textures/wheel_albedo.png"
metallic   = 0.8
roughness  = 0.3
```

**v0.3 必须迁移**：当前 MeshRenderer 15 个 spec 字段从 inline 改为 sidecar。

### 7.4 🔴 Shader 标准接口冻结 + 渲染管线可选

引擎单独维护 `docs/ShaderSpec.md`（独立文档），规定：
- 顶点属性 layout（location 0=position, 1=normal, ...）
- 标准 uniform 命名（u_albedoMap, u_normalMap, ...）
- texture unit 分配

**渲染管线开放性**：
- 当前默认 **forward** 管线，shader 接口按 forward 设计
- v0.4+ 计划实现 **deferred** 管线作为可选项
- mod.toml 用 `supported_pipelines = ["forward", "deferred"]` 声明该 mod 的 shader 支持哪些管线
- 不同管线**保留独立的 shader 接口契约**，同一份 PBR shader 通常需要写两套（pbr_forward / pbr_deferred）
- 玩家在启动器选择渲染管线 → 启动器按 `supported_pipelines` 过滤可用 mod

**约束**：`engine://` 内嵌 shader 必须同时提供两种管线版本，保证任何场景下 fallback 可用。

**失败时**：shader 编译错误 → fallback 到 `engine://shaders/error_magenta`（明显紫红色），不黑屏。

---

## 8. C# Mod 编程模型

### 8.1 🔴 Mod 入口（IMod）

继承自 v0.2 现有 7 阶段生命周期接口（PreInit / OnLoad / Init / FixedLoop / Loop / PostLoop / OnUnload），不变。

### 8.2 🔴 数据组件注册

```csharp
[ArkComponent("my_addon:Reputation")]
public class ReputationData {
    [ArkField] public int FactionId;
    [ArkField] public float Trust;
}
```

mod 启动时通过 ScriptApi 反向注册到引擎 TypeRegistry，TOML 即可写 `type = "my_addon:Reputation"`。

### 8.3 🔴 跨 Mod 类型可见性

**默认走反射字符串访问**：
```csharp
var rep = Bridge.GetComponent("my_addon:Reputation", entity);
var trust = rep.GetField<float>("Trust");
```

**允许（非默认）**：mod 提供独立"API 程序集"（`MyAddon.Api.dll`），其他 mod 引用 API 而非主 DLL。

**禁止**：直接引用其他 mod 的主 DLL。

### 8.4 🔴 ScriptApi 调用线程契约

- 所有 ScriptApi 函数**仅可在主线程的 OnTick / OnFixedTick / OnPostTick / OnLoad 等回调栈内调用**
- 后台线程访问 ScriptApi = 未定义行为
- 未来可能提供 `Bridge.RunOnMainThread(Action)` 主线程任务队列

### 8.5 🟡 Time / DeltaTime 上下文一致性

- `Bridge.DeltaTime()` 在 FixedLoop 内必须返回 fixed dt（1/60），与传入参数一致
- 引擎用 thread-local "tick context" 实现

---

## 9. 资源严重性等级

### 9.1 🔴 三级 fallback 策略

| 等级 | 缺失行为 | 例子 |
|---|---|---|
| **Critical** | 立即停止加载，红字弹窗 | shader、core engine asset |
| **Important** | 用 fallback 资源，红色 log 警告 | 主要 mesh、贴图 |
| **Optional** | 静默跳过，info 日志 | 可选音效、装饰 mesh |

资源加载点必须显式声明等级，不允许"找不到就崩"或"找不到就黑屏不报错"。

**失败时**：见各等级；fallback 资源由 `engine://` 提供（白方块 mesh、紫红 shader、空音频）。

---

## 10. Mod 加载隔离

### 10.1 🔴 单 Mod 失败不影响整体

- `DiscoverAndLoadMods` 必须 try/catch per mod
- 一个 mod 加载抛异常 → 跳过，记录详细错误（含 stack trace），其他 mod 继续

### 10.2 🔴 启动器列出每个 mod 状态

- 已激活 / 未激活 / 加载失败（可点查看错误详情）
- 玩家可见，便于排错

---

## 11. 本地化（预留）

### 11.1 🟡 字符串引用语法

```csharp
[ArkField, Localizable]
public string DisplayName;
```

字段值可写：
- 字面量：`"铁剑"`
- 引用：`"{{loc:my_addon:item.iron_sword.name}}"` 查 `strings/<locale>.toml`

### 11.2 🟡 字符串文件结构

```
mods/<mod>/strings/
  zh-CN.toml
  en-US.toml
```

```toml
"my_addon:item.iron_sword.name" = "铁剑"
```

**失败时**：当前 locale 缺 key → fallback 到 default_locale → 仍缺 → 显示原 key 作为占位。

---

## 12. 工具链承诺

### 12.1 🔴 必备 CLI 工具（Mod SDK 包含）

| 工具 | 功能 |
|---|---|
| `ark-init <name>` | 生成 mod 模板（mod.toml、csproj、目录骨架） |
| `ark-validate <mod_dir>` | 校验 schema、引用完整性、循环依赖、ID 不变性 |
| `ark-pack <mod_dir>` | 打包 `.spk`（zip + 元数据） |

### 12.2 🟡 GUI 工具

- **Object Inspector**（见 §7.2）—— 反射驱动的对象/组件编辑器
- **Lighting Tuner**（已存在）—— 光照参数调试

### 12.3 🟡 Schema 导出（给 AI / IDE）

引擎支持 `--dump-schema <out.toml>`，导出所有反射组件字段元数据。
该 schema 文件是 AI Prompt / VS Code LSP / Object Inspector 的共享数据源。

---

## 13. 命名规则

### 13.1 🔴 强制约定

| 类型 | 规则 | 例子 |
|---|---|---|
| mod_id | `[a-z][a-z0-9_]*`（**强制英文小写**） | `my_addon`、`space_survival`、`dongfang_wuxia` |
| local_id | `[a-z][a-z0-9_]*`，可含 `.`（**强制英文小写**） | `cottage_door`、`item.iron_sword` |
| 完整 ID | `<mod_id>:<local_id>` | `my_addon:item.iron_sword` |
| 组件 type | `<mod_id>:<TypeName>`（PascalCase 类名） | `core:Light`、`my_addon:Reputation` |
| 文件路径 | 小写 + 下划线 + 单数 | `wheel_albedo.png` |
| display_name / description | 任意 Unicode（含中文） | `"东方武侠"`、`"中国古代仙侠世界"` |

**保留前缀**：`engine_`、`ark_`、`core_` 不允许 modder 使用（除官方 `core` mod 外）。

**理由**：ID 在 grep / 引用 / URL / log 里频繁出现，强制英文保证一致性、跨平台安全、AI 友好。玩家可见的字符串（display_name 等）支持中文，不影响中文圈用户体验。

---

## 14. AI-Friendly 验证

### 14.1 🟡 Prompt 模板与回归

- 在 `docs/ai_prompts/` 维护一组 LLM prompt + 期望输出对
- 每次 ModSpec 修改后，跑这组 prompt 验证 LLM 仍能正确写出符合规范的 mod
- 不通过 = ModSpec 存在歧义，需要修订

示例 prompt：
> 给定 ModSpec.md 和 cottage.toml，请添加一个会发光的红宝石到场景中央。

期望：LLM 输出符合 §3 路径语法、§13 命名规则、§7.3 sidecar 模式的 TOML 片段。

---

## 15. ScriptApi 版本兼容性

### 15.1 🔴 Append-Only + 版本号

- ScriptApi 函数指针表只允许**追加**新字段，不允许删除/重排
- 每次追加 → 版本号 +1（v2 → v3 → v4 ...）
- 引擎实现版本 = "我支持的 API 最高版本"
- mod.toml 声明 `script_api_min` = "我需要的最低版本"

**当前状态**：v0.2 已发布到 ScriptApi v2（含 v1 基础 + v2 Object/Transform/Scene 访问）。
v0.3 计划升 v3，加入数据组件反射注册 + ScriptComponent 实例化 + 旋转/层级访问。

### 15.2 🔴 加载时校验

| 情况 | 行为 |
|---|---|
| 引擎 API 版本 ≥ mod 的 `script_api_min` | 加载 |
| 引擎 API 版本 < mod 的 `script_api_min` | 拒绝加载，提示"需升级引擎" |
| mod 调用了自己声明版本之外的函数 | 引擎检测不到（mod 责任），运行时崩 |

### 15.3 🟡 函数级 deprecation

- 不允许真正删除 ScriptApi 函数
- 但可标记 `deprecated_since = N`，新 mod 不应使用
- 旧 mod 仍能调用，引擎记 warn 日志

---

## 附录 A：与 v0.2 现状的迁移清单（v0.3 必须完成）

| v0.2 现状 | v0.3 目标 | 影响范围 |
|---|---|---|
| `AObject::guid_` = UUID v4 随机 | 改为 `"<mod_id>:<local_id>"` 持久身份字符串 | AObject、SceneDoc、ScriptApi |
| MeshRenderer 15 字段 inline | 拆 Material 为独立 sidecar TOML | MeshRenderer、cottage.toml、ResolveResources |
| `mods://...` 单一前缀 | 改为 `./` + `mod://<id>/...` + `engine://...` 三轨 | Paths::ResolveResource、所有 TOML |
| `mods/load_order.toml` | 改为每个 mod 自带 mod.toml + 拓扑排序 | Engine.cs 加载逻辑、Paths |
| `samples/cottage.toml` 直接放 content/scenes/ | 重构为 `mods/vanilla/scenes/cottage.toml` | 整个 samples 目录 |
| IMod 全局 Loop | 鼓励"全局 system + 数据组件"心智，IMod 仍保留 | 文档、示例、HelloMod 重写 |
| ScriptApi v2 | 升 v3，加数据组件注册 + 旋转/层级 + ScriptComponent | ScriptHost、Bridge.cs |

---

## 附录 B：开发态 vs 分发态目录差异

### B.1 🔴 开发态（modder 工作环境）

```
<mod_workspace>/
  mods-src/<mod_id>/         ← 源代码（C# 工程、原始 .blend、PSD 等）
    HelloMod.csproj
    src/HelloMod.cs
  mods/<mod_id>/             ← 资产 + 编译产物（被 mod 系统读取）
    mod.toml
    scripts/HelloMod.dll     ← 由 csproj 构建产出
    textures/...
    scenes/...
```

构建命令：`dotnet build mods-src/<mod_id>` → 产物输出到 `mods/<mod_id>/scripts/`

### B.2 🔴 分发态（玩家收到的发行包）

```
<game_install>/
  StarArk.exe
  StarArkEngine.dll
  content/mods/<mod_id>/     ← 只有运行时所需
    mod.toml
    scripts/HelloMod.dll
    textures/...
```

**禁止**：分发包中包含 `mods-src/`、`*.csproj`、`*.cs`、`.blend` 等源文件。
**ark-pack** 工具自动剔除非必需文件，生成 `.spk` 或纯净文件夹。

### B.3 🟡 引擎本身的 vanilla 也走相同布局

- 开发态：`mods-src/vanilla/` + `mods/vanilla/`
- 分发态：`content/mods/vanilla/`
- 不特殊化 core/vanilla，统一规则

---

## 附录 C：暂时不做的事

- 一体化 GUI 关卡编辑器
- C++ Mod ABI 公开
- 可视化触发器/蓝图
- Mod 数字签名 / 反作弊
- 在线 Mod 仓库
- 多线程 ScriptApi
- 物理引擎集成（Transform 与物理状态同步契约延后）

---

**版本历史**
- v0.1 (2026-04-26)：初稿，定下 §1~§15 主体规则、附录 A/B/C
- v0.2 (2026-04-26)：v0.3 ModSpec 主路径全部落地（见下方"v0.3 实现状态")

---

## 附录 D：v0.3 实现状态（2026-04-26）

> 给下次接手 AI / 开发者用。本节是**实现侧的真相来源**，与 §1–§15 的"应该怎样"对齐核对。

### D.1 已落（在 main 分支可验证）

| 节 | 主题 | 实现位置 | 在线校验/自检 |
|---|---|---|---|
| §2 | mod.toml schema v1（含 `depends_on` / `applies_to`） | `engine/mod/ModInfo.{h,cpp}` | mod.toml 解析失败时 mod 标记 invalid + WARN |
| §3 | 三轨路径 `./` / `mod://` / `engine://` | `engine/platform/Paths.cpp` | 裸路径走 fallback 但发 deprecation WARN |
| §4.2 | 持久 ID `"<mod_id>:<local_id>"` 文法 | `engine/core/PersistentId.{h,cpp}` | 启动期 16 用例 + 2 legacy spot-check 自检；SceneDoc 加载时校验 |
| §5 | addon scene overlay（4 操作） | `engine/serialization/SceneDoc.cpp` `ApplyOverlay*` | F6 手动触发 / `ARK_AUTO_OVERLAY=1` 第 5 帧自动触发 |
| §6.1 | 存档头部 + 兼容性检查 | `engine/save/SaveHeader.{h,cpp}` | 启动时 capture+round-trip+self-CheckCompatibility，失败 ARK_LOG_ERROR |
| §6.2 | registry schema_hash（SHA-256/16） | `engine/util/Sha256.{h,cpp}` + `engine/core/SchemaHash.{h,cpp}` | 启动期 4 路负向自检（顺序无关 / 重命名敏感 / 删除敏感） |
| — | F5/F9 quicksave/quickload（含 sidecar 场景文本） | `EngineBase::HandleQuicksave/Quickload` | 写 `<UserData>/saves/quicksave{,.scene}.toml`，sidecar 解析失败保留原场景 |
| — | SceneDoc 反射后置钩子 `OnReflectionLoaded` | `AComponent.h` + `MeshRenderer.cpp` | MeshRenderer 在 SceneDoc 5 个写入点全自动 ResolveResources |

### D.2 已落 dogfood（cottage scene 走完整新链路）

- `samples/content/scenes/cottage.toml` 8 个对象全部用 persistent ID（`core:main_camera` / `core:sun` / `core:torch_l/r` / `core:ground` / `core:cottage_body/roof` / `core:metal_orb`）。
- `samples/mods/hellomod/scene.overlay.toml` 用 `core:sun` / `core:torch_l` 做 overrides，验证 §5 端到端。
- 四种 smoke（fwd default / def default / cottage / cottage+ARK_AUTO_OVERLAY=1）全部 stderr=0。

### D.3 v0.3 仍未做（推迟到 v0.3.x / v0.4）

- **§4.2 ark-validate "ID 漂移检测"**：离线工具范畴，需要 CLI；当前在线校验已可在 SceneDoc::Load 把非法 ID 推到 stderr。
- **§5 deletions / additions / components_attached e2e 实测**：✅ 2026-04-28：hellomod overlay 扩展后打全四种操作，`ARK_AUTO_OVERLAY=1` 烟测日志 `applied: deletions=1 overrides=2 components_attached=1 additions=1`，stderr=0。
- **§6.1 save scope = full ECS state**：当前 quicksave 保存 `SaveHeader` + 整个场景 dump；ECS systems 自定义状态（计时器、随机种子等）尚未参与。
- **§6.3 缺失策略（mod 不在）**：CheckCompatibility 已能识别 kMissingMod / kVersionMismatch，但 quickload 在 kMissingMod 下行为是"保留原场景"——尚未走 spec 所述"询问玩家继续 / 取消"。
- **§7 渲染 pipeline 标识**：`ARK_PIPELINE` env + SaveHeader.pipeline 字段已通；§7.4 mod 自带 pipeline-specific shader 路径未做。
- **§15 ScriptApi v3**：当前仍是 v2；未把 quicksave/F5、Camera 控制、Component 反射访问暴露到 C# 端。
- **附录 A**：MeshRenderer Material sidecar 拆分 / `samples/cottage` 迁移到 `mods/vanilla/` 均未做（cottage 仍在 `samples/content/`）。

### D.4 工程级 chore（次要）

- `cottage.toml` 旧 `tex_albedo = "textures/ground.png"` 已清空（dangling 引用 + §3 deprecation 双重污染）。
- `Window::FramebufferSizeCallback` 在 deferred 启动期推 0×0 帧的 race 已修（`MainLoop` + `ForwardRenderer::RenderFrame` 双重 zero-size 早返）。
- 新增 `.cpp` 后必须 `cmake -B build` reconfigure；GLOB_RECURSE 不会自动发现新文件（已在 repo memory 记账）。
