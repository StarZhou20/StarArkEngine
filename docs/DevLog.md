# StarArk 工程开发日志 (DevLog)

> **用途**: 供 AI 编码助手阅读，快速了解项目当前状态、已完成内容、待办事项和技术约束。  
> **最后更新**: 2026-04-26（v0.3 ModSpec 主路径全部落地 — §2/§3/§4.2/§5/§6.1/§6.2，详见 [ModSpec.md 附录 D](ModSpec.md)）

## 工程级硬性约定（开发顺序之外的红线）

以下两条**未接到作者明确通知前不变**，AI 助手与外部贡献者一律遵守：

1. **🔴 不要把引擎打包为 DLL**。当前 `StarArkEngine` 是 **静态库**（`add_library(... STATIC)`），
   `game/` 与 `samples/` 直接静态链接。渲染层/RHI/反射 在 v0.3 期内会持续微调（Material sidecar 拆分、
   ScriptApi v3、ECS system 注册等），二进制接口尚未稳定，过早做 DLL 边界会强迫每次改动都做
   ABI/导出符号兼容修补，得不偿失。**ModSpec §0 中"StarArkEngine.dll"是远期目标**，v0.3/v0.4 期内
   不要触发任何 `BUILD_SHARED_LIBS=ON` / `__declspec(dllexport)` / `.def` 文件相关改动。

2. **🔴 玩家启动器使用 WPF 实现**，不用 ImGui / Win32 / Avalonia / web frontend。
   理由：WPF 与现有 Lighting Tuner / 计划中的 Object Inspector（ModSpec §7.2）共享同一前端栈，
   可以共用反射 schema + DataBinding 模式，且 WPF 在 .NET 10 下的设计自由度（XAML / 矢量 / 主题）
   远超 ImGui。启动器项目预期落在 `tools/Launcher/`（与 `tools/LightingTuner/` 同级），
   通过 CLI 启动引擎二进制，不嵌入 CoreCLR。


---

## 0. 当前里程碑：v0.3-modspec（**主路径全部落地，e2e 验证完成**）

> v0.3 ModSpec 实现状态以 [ModSpec.md 附录 D](ModSpec.md) 为单一真相来源。本节给出"接手者第一眼"摘要。

**v0.3 已落（全部在 main 分支可验证，build EXIT=0，四种 smoke stderr=0）：**

| 节 | 主题 | 关键产物 |
|---|---|---|
| §2 | mod.toml schema v1 | `engine/mod/ModInfo.{h,cpp}`（含 depends_on / applies_to） |
| §3 | 三轨路径 `./` / `mod://` / `engine://` | `engine/platform/Paths.cpp` |
| §4.2 | 持久 ID `<mod>:<local>` | `engine/core/PersistentId.{h,cpp}` + 启动期自检 |
| §5 | addon scene overlay | `SceneDoc::ApplyOverlay*`，F6 / `ARK_AUTO_OVERLAY=1` 触发 |
| §6.1 | 存档 header + sidecar scene + F5/F9 | `engine/save/SaveHeader.{h,cpp}` + `EngineBase::HandleQuicksave/Quickload` |
| §6.2 | registry schema_hash | `engine/util/Sha256.{h,cpp}` + `engine/core/SchemaHash.{h,cpp}` + 启动期 4 路负向自检 |
| chore | 反射后置钩子 `OnReflectionLoaded` | `AComponent.h` + `MeshRenderer::OnReflectionLoaded`，删 `resolveAll` |
| chore | cottage 场景全部走 persistent ID | `samples/content/scenes/cottage.toml` 8 个对象 |

**v0.3 仍未做（v0.3.x / v0.4 候选）：**

- ark-validate CLI（§4.2 ID 漂移离线检测）
- §5 additions / deletions / components_attached 端到端实测（overrides 已实测）
- §6.1 save scope = full ECS 状态（当前只 dump scene）
- §6.3 缺失 mod 时的 quickload 玩家询问 UX
- §7.4 pipeline-specific shader 路径
- §15 ScriptApi v3（暴露 quicksave / Camera / Component 反射）
- 附录 A：Material sidecar 拆分；`samples/content/cottage` 迁到 `mods/vanilla/`

---

### 🔖 下一步开工锚点（2026-04-28）

**目标**：把混乱的 `samples/` 收敛为"一个真正的 mod-first 游戏样例" + 启动器选 mod。

**待办（按依赖序）**：

1. **资产迁移**（C++ 几乎不动）— 把现存三种内置场景拆成三个 game-type mod 文件夹：
   - `samples/mods/vanilla/`   ← 原 cottage（`samples/content/scenes/cottage.toml` + 引用的贴图/mesh 全部移入）
   - `samples/mods/bistro/`    ← 原 FBXDemoScene 的硬编码场景，导出为 `scenes/bistro.toml`（不能再用 C++ class 写死）
   - `samples/mods/demo/`      ← 原 PBR DemoScene 同上
   - 每个目录加 `mod.toml`（`type="game"`），`samples/content/` 清空或仅留 engine fallback
   - `samples/mods/hellomod/`（addon）保留，`applies_to=["vanilla"]`

2. **可加载 game-mod 的引擎入口**（C++ 改动主要点）：
   - `EngineBase` 暴露 `RunGameMod(mod_id, addon_ids)`：内部走 `ModInfo::Load → 路径上下文 → SceneDoc::Load(scenes/main.toml) → 应用 addon overlays`
   - 删 `CottageScene` / `FBXDemoScene` / `DemoScene` 三个 C++ 类（场景渲染逻辑 100% TOML 驱动后已无意义）
   - `StarArkSamples.exe` 改为 `--game=<id> [--addon=<id>...]`，无参时进入"启动器外部唤起"模式

3. **WPF 启动器**（`tools/Launcher/`，独立 .NET 10 项目，参考 `tools/LightingTuner`）：
   - 扫描 `samples/mods/`，解析所有 `mod.toml`
   - 左侧列表：所有 `type="game"` mod，单选
   - 右侧列表：所有 `type="addon"` 且 `applies_to` 含选中 game 的 mod，多选
   - 校验 `engine_min` / `script_api_min` / `supported_pipelines`（与 ModSpec §2.2 一致）
   - "启动" 按钮 → `Process.Start("StarArkSamples.exe --game=vanilla --addon=hellomod")`
   - 引擎二进制独立 .exe 进程，启动器不嵌入 CoreCLR（ModSpec §0 红线）

4. **smoke 4 模更新**：从 `cottage` 位置参数改为 `--game=vanilla` / `--game=bistro` / `--game=vanilla --addon=hellomod`。

**预期收益**：
- 三个 game-mod 是引擎自身"吃自己狗粮"最强的回归用例（任何 mod 加载/反射/路径 bug 立刻暴露）
- ModSpec §1.1（一个 mod 文件夹 = 一个完整游戏）首次端到端验证
- 部分覆盖 ModSpec 附录 A（Material sidecar 之外的迁移工作）

**风险**：
- Bistro FBX 场景目前是 C++ 代码硬编码相机/灯光/导入 FBX，导出为 TOML 需要先确认 `MeshRenderer` 字段够用、灯光/相机能完整序列化
- WPF .NET 10 SDK 需要确认开发机已装（Lighting Tuner 已经在用，应该没问题）

**完成判据**：
- 启动器双击能选 vanilla/bistro/demo + hellomod 任意组合启动，画面正常
- `samples/content/` 不再有 scene/mesh 资产
- 4 模 smoke stderr=0 + cottage+overlay 仍输出 `applied: deletions=1 overrides=2 components_attached=1 additions=1`

---

**给下次接手 AI 的最短路径：**

1. 看 [ModSpec.md 附录 D](ModSpec.md) 知道 v0.3 实现侧每节状态。
2. 看 `/memories/repo/v0.2-progress.md` 知道每轮（Round 8–24）的具体改动 + 踩坑。
3. 验证当前状态：`cd D:\Code\StarArkEngine; cmd /c tmp\build.bat *>build_log.txt 2>&1`，预期 EXIT=0；
   `cd build\samples; .\StarArkSamples.exe` 预期 stderr=0；
   `$env:ARK_AUTO_OVERLAY='1'; .\StarArkSamples.exe cottage` 预期日志含 `applied: overrides=2`。
4. 想继续 v0.3 收尾就从"v0.3 仍未做"清单挑一项；想做新功能就走 [Roadmap.md](Roadmap.md) Phase 16+。

---

## 0.0 上个里程碑：v0.2-data-contract（**已收尾**），承前启后

> **v0.3 起 mod 范式由 [ModSpec.md](ModSpec.md) 全权定义**（自包含 mod 文件夹 / mod.toml schema / 三轨路径 / 双层身份 / 字段级覆盖 / ECS 优先 / ScriptApi v3 等）。
> v0.2 现有实现（`mods://` 单前缀、UUID GUID、inline material、`load_order.toml`）将按 ModSpec 附录 A 清单迁移。



**目标定位**: 把引擎从"API 驱动的 PBR 后端"升级为"**数据契约驱动的 MOD 基础**"。
对标 Skyrim/Creation Engine 的 MOD 生态基础（ESP/ESM record override + load order + 资源路径覆盖），
但契约本身是 TOML 文本而非专有二进制。

**为什么是这个，不是"继续做渲染"或"开始写编辑器"**:

- Skyrim 级 MOD 生态 90% 靠的不是编辑器、也不是超强渲染器，而是**稳定的数据契约 + 资源覆盖层 + GUID 引用**
- 如果先做渲染纵深（CSM / 大世界坐标 / streaming），每加一个功能就是"又一个 MOD 改不了的硬编码"，v0.3 做反射时全部推倒重改
- 如果先做编辑器 GUI，相当于把引擎内部 `unique_ptr<AObject>` 这种数据结构写死到编辑器里，AI 读不懂，MOD 工具链对接不上
- 先做反射 + TOML + VFS，编辑器是反射的"副产品"（WPF Lighting Tuner 已经验证过这套模式）

**v0.2 拆解**（详见 [Roadmap.md](Roadmap.md)）:

| 阶段 | 产物 | 状态 |
|------|------|------|
| 15.A | 组件反射系统（TypeRegistry + FieldInfo） | ✅ |
| 15.B | GUID + `AObject::GetGuid()` | ✅ |
| 15.C | 场景 TOML 序列化（全组件覆盖） | ✅ |
| 15.D | 资源覆盖 VFS（`mods/` 覆盖 `content/`） | ✅ |
| **v0.2 tag**: 理论可 MOD 的最小引擎 | | ✅ |
| 15.E | 反射驱动 Inspector（WPF） | ⏳ v0.2.x |
| 15.F.1 | CoreCLR 实接入（hostfxr + UnmanagedCallersOnly） | ✅ |
| 15.F.2 | Native↔Managed Bridge + MOD 发现/加载 | ✅ |
| 15.F.2.1 | Unity 风格 7 阶段 MOD 生命周期 | ✅ |
| 15.F.3 | 引擎 API 暴露 v2（Spawn/Find/Destroy/Transform/Scene） | ✅ |
| 15.F.4 | MOD 热重载（collectible ALC + file watcher） | ⏳ |
| 15.F.5 | mod.toml 元数据 + load_order 排序 | ⏳ |
| 15.F.6 | ScriptApi v3：Camera 控制 + Component 反射访问 | ⏳ |

**v0.2 验收**（5 条，全部可被 AI 验证）:

1. `CottageScene` 改为从 `content/scenes/cottage.toml` 加载，不写 C++
2. 改 TOML 里 Light intensity → 重启后生效
3. `mods/testmod/textures/ground.png` → 地面贴图被替换
4. AI 只读 `docs/` 能写出一个新的 `.toml` 场景
5. 两 MOD 同改同 GUID，后加载的胜出，日志打印 override chain

---

## 0.1 上个里程碑：v0.1-renderer（已冻结 2026-04）**目标定位**: StarArkEngine v0.1 是一个**可被 AI 直接调用的 PBR 渲染后端**。不是"完整游戏引擎"，
也不是"开发中的开放世界平台"，而是一个**已经冻结的渲染器 SDK**。

**成功判据**（唯一判据）:  
> **一个陌生的 AI 或陌生开发者，只读 `docs/` 目录，就能写出可运行的 demo。不需要问作者任何问题。**

**v0.1 范围**（= 已实现的 Phase 1–14）:

- Platform: GLFW 窗口 + OpenGL 4.5 Core + Input + Time + 日志
- 架构: AScene / AObject / AComponent + Transform + 14 步主循环
- RHI: 抽象接口 + OpenGL 后端
- 渲染: Forward + PBR（Cook-Torrance）+ sRGB + ACES Hill + 多贴图（albedo/normal/MR/AO/emissive）
- 资源: Assimp（OBJ/FBX/glTF）+ stb_image（PNG/JPG/...）+ 16× 各向异性
- 光照: 方向光/点光/聚光灯 + 物理光衰减 + IBL（irradiance + prefilter + BRDF LUT）
- 阴影: 方向光 shadow map + 5×5 PCF
- 后处理: HDR FBO + Bloom（ping-pong Gauss）+ ACES Hill composite
- Skybox: cubemap + 程序化渐变 fallback
- Shader: ShaderManager 文件加载 + mtime 热重载 + 嵌入 fallback
- Tuning: `content/lighting.json` + mtime 热重载 + WPF Lighting Tuner 工具

**v0.1 不包含**（明确画线）:

- 任何脚本语言（C#/Lua/Python）
- 场景对象序列化（只有 RenderSettings + Light 序列化）
- 物理 / 碰撞
- 骨骼动画 / 动画状态机
- 音频
- 网络 / 多人
- 编辑器 GUI / 运行时 Inspector
- Prefab 系统
- Streaming / 大世界

v0.1 之后的远期设想全部移至 [Roadmap.md](Roadmap.md)，与当前主线解耦。

**v0.1 冻结前的任务清单**: 见 §5"v0.1 收官清单"。

---

## 1. 项目概述

StarArk 是一个 **C++20 3D 游戏框架**，采用 Unity 风格的架构（AObject + Component 组合模式）。使用 OpenGL 渲染（GLEW），设有 RHI 抽象层以支持未来扩展到 DX12。不内置编辑器，使用 Blender 作为外部场景编辑器。

- **命名空间**: `ark::`
- **错误处理**: `assert` + `ARK_LOG_FATAL` + `std::abort()`，不使用异常
- **内存管理**: `unique_ptr` 所有权（AScene 拥有 AObject，AObject 拥有 AComponent）

---

## 2. 构建环境

| 项目 | 值 |
|------|------|
| 语言标准 | C++20 |
| 编译器 | MSVC 19.50 (Visual Studio 2025 Community) |
| CMake | 4.2, 使用 NMake Makefiles 生成器 |
| vcvarsall | `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64` |
| 构建目录 | `build/` |
| 代理 | `http://127.0.0.1:9910`（下载依赖时需要） |

### 构建命令

```powershell
# 配置
cmd /c "call vcvarsall x64 >nul 2>&1 && cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# 编译
cmd /c "call vcvarsall x64 >nul 2>&1 && cmake --build build"

# 运行
build\game\StarArkGame.exe
```

### 依赖管理

使用 `FetchContent` + 本地 zip 文件（`deps_cache/` 目录），因为代理不稳定导致 cmake 直接下载经常失败。

| 依赖 | 版本 | 文件 |
|------|------|------|
| GLFW | 3.4 | `deps_cache/glfw-3.4.zip` |
| GLM | 1.0.1 | `deps_cache/glm-1.0.1-light.zip` |
| GLEW (cmake fork) | 2.2.0 | `deps_cache/glew-cmake-2.2.0.zip` |
| spdlog | 1.15.0 | `deps_cache/spdlog-1.15.0.zip` |
| Assimp | 5.4.3 | `deps_cache/assimp-5.4.3.zip` |

**GLM 特殊处理**: glm-light zip 提取后为扁平结构（`glm-src/glm.hpp` 而非 `glm-src/glm/glm.hpp`），通过 `mklink /J` junction 解决：cmake 在 `_deps/glm-wrapper/glm` 创建指向 `_deps/glm-src` 的 junction，使 `#include <glm/glm.hpp>` 正常工作。

**GLEW 特殊处理**: 需要 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`（glew-cmake 的 cmake_minimum_required 版本过旧）。

---

## 3. 目录结构

```
StarArk/
├── CMakeLists.txt              # 根 CMake，FetchContent 声明
├── Plan.md                     # 架构设计文档
├── deps_cache/                 # 本地依赖 zip 缓存
├── engine/
│   ├── CMakeLists.txt          # StarArkEngine 静态库
│   ├── platform/               # Layer 1: 平台基础
│   │   ├── Window.h/cpp        # GLFW 窗口 + OpenGL 4.5 上下文
│   │   ├── Input.h/cpp         # 键盘/鼠标输入轮询
│   │   └── Time.h/cpp          # 帧计时（deltaTime/totalTime/frameCount）
│   ├── debug/                  # 调试日志系统
│   │   ├── DebugListenBus.h/cpp    # 全局日志总线单例 + ARK_LOG_* 宏
│   │   ├── IDebugListener.h        # 日志监听者抽象接口（RAII 自动注册）
│   │   ├── ConsoleDebugListener.h/cpp  # 控制台彩色输出
│   │   └── FileDebugListener.h/cpp     # 文件轮转日志（spdlog）
│   ├── rhi/                    # Layer 2: 渲染硬件接口（抽象层）
│   │   ├── RHITypes.h          # 枚举 + VertexLayout
│   │   ├── RHIBuffer.h         # 缓冲区接口
│   │   ├── RHIShader.h         # 着色器接口
│   │   ├── RHITexture.h        # 纹理接口
│   │   ├── RHIPipeline.h       # 管线状态接口 + PipelineDesc
│   │   ├── RHICommandBuffer.h  # 命令缓冲区接口
│   │   ├── RHIDevice.h         # 设备工厂接口
│   │   └── opengl/             # OpenGL 后端实现
│   │       ├── GLBuffer.h/cpp
│   │       ├── GLShader.h/cpp
│   │       ├── GLTexture.h/cpp
│   │       ├── GLPipeline.h/cpp
│   │       ├── GLCommandBuffer.h/cpp
│   │       └── GLDevice.h/cpp
│   ├── core/                   # Layer 4: 核心框架
│   │   ├── EngineBase.h/cpp    # 引擎单例 + 主循环（14 步）
│   │   ├── SceneManager.h/cpp  # 场景管理器（延迟切换）
│   │   ├── AScene.h/cpp        # 场景基类（OnLoad/Tick/OnUnload）
│   │   ├── AObject.h/cpp       # 游戏对象基类（组件系统 + 内置 Transform）
│   │   ├── AComponent.h        # 组件基类
│   │   ├── Transform.h/cpp     # 变换组件（位置/旋转/缩放/层级/脏标记）
│   │   └── IObjectOwner.h      # 所有权接口
│   └── rendering/              # Layer 3: 渲染层
│       ├── Camera.h/cpp        # 相机组件（透视/正交、优先级、静态注册）
│       ├── Light.h/cpp         # 光源组件（方向光/点光/聚光灯）
│       ├── Mesh.h/cpp          # 网格资源（顶点+索引 + GPU 上传 + 原始体生成）
│       ├── Material.h/cpp      # 材质（shader + 颜色/PBR参数/纹理）
│       ├── MeshRenderer.h/cpp  # 网格渲染器组件
│       ├── ForwardRenderer.h/cpp # 前向渲染管线（Blinn-Phong + PBR）
│       ├── ShaderSources.h     # 内置 GLSL fallback（Phase M1 迁移到 engine/shaders/）
│       ├── ShaderManager.h/cpp # 从文件加载 GLSL + mtime 热重载 (Phase M1)
│       ├── OrbitCamera.h/cpp   # 轨道相机控制器组件
│       ├── TextureLoader.h/cpp # 纹理文件加载（stb_image）
│       └── ModelLoader.h/cpp  # 3D模型加载（Assimp）
├── engine/shaders/             # GLSL 文件（Phase M1，运行时由 ShaderManager 加载）
│   ├── pbr.vert / pbr.frag
│   └── phong.vert / phong.frag
├── engine/third_party/
│   └── stb/                    # stb_image 单头文件库
│       ├── stb_image.h
│       └── stb_image_impl.cpp
└── game/
    ├── CMakeLists.txt          # StarArkGame 可执行文件
    └── main.cpp                # 当前 demo：旋转立方体 + 光照
```

---

## 4. 已完成阶段

### Phase 1: Foundation ✅

- GLFW 窗口创建 + OpenGL 4.5 Core 上下文
- 键盘/鼠标输入系统（GetKey/GetKeyDown/GetKeyUp/GetMouseButton/GetMousePosition）
- 帧计时（DeltaTime/TotalTime/FrameCount）
- DebugListenBus 日志系统 + Console/File 监听者 + ARK_LOG_* 宏
- **验证**: 窗口打开、清屏、日志输出到控制台和 logs/ 文件夹、60fps

### Phase 2: RHI ✅

- RHI 抽象接口：RHIDevice / RHIBuffer / RHIShader / RHITexture / RHIPipeline / RHICommandBuffer
- OpenGL 后端完整实现（GLDevice / GLBuffer / GLShader / GLTexture / GLPipeline / GLCommandBuffer）
- GLCommandBuffer 使用 `std::function` 录制命令，Submit() 时回放
- **验证**: 彩色三角形通过完整 RHI 管线渲染

### Phase 3: Core Framework ✅

- **EngineBase**: 单例，`Run<FirstScene>(w, h, title)` 入口，14 步主循环
- **SceneManager**: 拥有当前 AScene，`LoadScene<T>()` 延迟切换，`LoadSceneImmediate<T>()` 启动用
- **AScene**: 可继承场景，ObjectList + PendingList，`CreateObject<T>()` 创建对象
- **AObject**: 基类，自增 uint64_t id，内置 Transform，组件系统（Add/Get/RemoveComponent），生命周期（PreInit/Init/PostInit/Loop/PostLoop/OnDestroy），Destroy 级联到子节点，SetDontDestroy 转移到 EngineBase
- **AComponent**: 基类，OnAttach/OnDetach/PreInit/Init/PostInit/Loop/PostLoop，enabled 标记
- **Transform**: position/rotation(quat)/scale，父子层级，局部/世界矩阵 + 脏标记，析构时双向清理
- **IObjectOwner**: AScene 和 EngineBase 共同实现的接口
- **验证**: DemoScene 加载、TriangleObject 创建、组件系统工作、60fps

### Phase 4: 3D Rendering Pipeline ✅

- **Camera 组件**: 透视/正交投影，优先级排序，静态注册表（OnAttach 注册、OnDetach 注销）
- **Light 组件**: 方向光/点光/聚光灯类型，颜色/强度/范围/锥角，静态注册表
- **Mesh 资源**: 顶点+索引数据，GPU 上传，`CreateCube()` / `CreatePlane()` / `CreateSphere()` 原始体生成
- **Material**: shader 引用 + 颜色/PBR 参数（metallic/roughness/ao）/漫反射纹理，Bind() 设置 per-material uniforms 并绑定纹理
- **MeshRenderer 组件**: 持有 Mesh + Material（shared_ptr），静态注册表
- **ForwardRenderer**: 遍历相机（按优先级排序）→ 收集可见 MeshRenderer → 多光源渲染 → 绘制
- **多光源渲染**: 支持最多 4 方向光 + 8 点光 + 4 聚光灯，shader uniform 数组
- **验证**: 旋转立方体 + 透视相机 + 方向光 + Blinn-Phong 着色，稳定 60fps

### Phase 5: PBR + 相机控制 + 纹理加载 ✅

- **PBR 着色器 (Cook-Torrance)**:
  - GGX 法线分布函数 (NDF)
  - Smith-GGX 几何遮蔽函数 (Schlick 近似)
  - Fresnel-Schlick 菲涅尔
  - Metallic-Roughness 工作流
  - Reinhard HDR 色调映射 + gamma 校正 (pow 1/2.2)
  - 内置着色器：`ark::kPhongVS/kPhongFS`（Blinn-Phong）、`ark::kPBR_VS/kPBR_FS`（PBR）
- **OrbitCamera 组件**: 围绕目标点的轨道相机控制器
  - 右键拖拽旋转（yaw/pitch），滚轮缩放，中键平移
  - 可配置灵敏度、距离范围、俯仰角限制
- **Input 系统扩展**: 鼠标移动增量（GetMouseDeltaX/Y）、滚轮回调（GetScrollDelta）、鼠标按键边沿检测（GetMouseButtonDown/Up）
- **纹理加载系统**:
  - stb_image 集成（PNG/JPG/BMP/TGA），位于 `engine/third_party/stb/`
  - `TextureLoader::Load(device, filepath)` 从文件加载到 RHITexture
  - `RHITexture` 接口新增 `Bind(int unit)` 虚函数
  - `Material::Bind()` 自动绑定 diffuse 纹理到 unit 0 + 设置 sampler uniform
  - Phong 和 PBR 着色器均支持 `uDiffuseTex` 采样
- **验证**: 10 个 PBR 球体（金属/电介质，粗糙度渐变）+ 棋盘格纹理地面 + 轨道相机，多光源

### Phase 6: 模型加载 (Assimp) ✅

- **Assimp 集成**: v5.4.3 通过 FetchContent + 本地 zip，仅启用 OBJ/FBX/glTF 导入器以减小编译体积
- **ModelLoader**: `ModelLoader::Load(device, shader, filepath)` → 返回 `vector<ModelNode>`（mesh + material）
  - 自动解析 Assimp 顶点/法线/UV → 引擎 Mesh
  - 自动提取材质（漫反射颜色/高光/光泽度/漫反射纹理）
  - 纹理路径相对于模型文件目录自动解析
  - 支持 aiProcess_Triangulate / GenNormals / FlipUVs / CalcTangentSpace
- **ModelNode 结构**: `shared_ptr<Mesh>` + `shared_ptr<Material>` 组合，允许场景中多实例复用
- **验证**: 加载 OBJ 二十面体 + PBR 材质 + 自动旋转，与 PBR 球体共存于同一场景

### Phase 7: 渲染优化 (Pipeline 缓存 + 渲染排序) ✅

- **Pipeline 缓存 (F6)**: `ForwardRenderer` 中新增 `pipelineCache_`（`unordered_map<uint64_t, PipelineCacheEntry>`）
  - FNV-1a 哈希键：shader 指针 + 顶点布局 stride/属性数 + 拓扑/深度/混合状态
  - `GetOrCreatePipeline(desc)` 缓存命中时复用，避免每帧重建 VAO
- **渲染排序 (F5)**: `RenderCamera()` 中收集可见 MeshRenderer 后按 shader 指针 → material 指针排序
  - 减少状态切换（shader 绑定、材质 uniform 设置）
- **验证**: 构建通过，PBR 场景渲染正常

### Phase G: 引擎/游戏/样例三分架构 ✅

- **分层**：
  - `engine/`：静态库 `StarArkEngine`（可复用内核）
  - `game/`：可执行 `StarArkGame`，保持空壳（玩家项目起点）
  - `samples/`：可执行 `StarArkSamples`，承载所有演示场景（DemoScene / FBXDemoScene / 所有 PBR/Light/Model 样例对象）
- **路径抽象**：新增 `engine/platform/Paths.{h,cpp}`
  - `Init(argv[0])` 必须在 `main()` 首行调用
  - `GameRoot()` 返回 exe 所在目录（Windows 下用 `GetModuleFileNameW`，非 cwd）
  - `Content()` / `Mods()` / `Logs()` 派生；`UserData(title)` 指向 `%APPDATA%/StarArk/<title>`
  - `ResolveContent("models/foo.obj")` → 绝对路径
  - `SetDevContentOverride(p)` 允许源码树运行
- **CMake**：顶层 `StarArkSDK` 工程，`STARARK_BUILD_SAMPLES` / `STARARK_BUILD_GAME` 默认 ON；两个可执行目标都 POST_BUILD 拷 content/models 到 exe 旁边
- **VSCode**：`.vscode/tasks.json` 定义 vcvarsall+NMake 的 Configure/Build/Clean Rebuild；`launch.json` 分别调试两个 exe；`settings.json` 禁用 CMake Tools 扩展的自动重配置（避免覆盖 NMake build/）
- **验证**: 两个 exe 均能独立启动；样例场景走 `Paths::ResolveContent` 正确加载资源

### Phase 8: sRGB + ACES Tonemap + 物理光衰减 ✅

- **sRGB 纹理工作流**：
  - `RHITexture::Upload(..., TextureFormat format)`，新增枚举 `TextureFormat::{sRGB_Auto, Linear}`
  - OpenGL 后端根据 format + 通道数选 `GL_SRGB8_ALPHA8` / `GL_SRGB8` / 普通 `GL_RGBA8` 等内部格式
  - `TextureLoader::Load(..., bool isSRGB = true)` 默认色彩贴图按 sRGB 处理；法线/MR/AO/数据贴图用 `false`
  - 同时为纹理启用 16× 各向异性过滤（`GLEW_EXT_texture_filter_anisotropic` 检测可用时）
- **sRGB framebuffer**：
  - `Window.cpp` 加 `glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE)` + `glEnable(GL_FRAMEBUFFER_SRGB)`
  - shader 不再手写 `pow(1/2.2)`；从 linear 工作空间由驱动自动写回 sRGB 输出
- **ACES Filmic Tone Mapping**（Narkowicz 2015 近似）：替换之前的 Reinhard
  - PBR_FS 在加 emissive + 乘 uExposure 之后，使用 `(L*(a*L+b))/(L*(c*L+d)+e)` 映射
- **物理光衰减**：
  - 新增 `PhysicalAttenuation(dist, range) = 1/(dist²) * (1 - smoothstep(range*0.75, range, dist))`
  - 点光源 / 聚光灯的衰减公式从 `constant/linear/quadratic` 改为物理正确的平方反比 + 软截断；范围外早退
  - `Light` 结构体中旧字段仍保留（兼容），shader 不再使用
- **Exposure & emissive**：
  - `ForwardRenderer::SetExposure(f)` 写 `uExposure`（默认 1.0）
  - `Material::SetEmissive(vec3)` 写 `uMaterial.emissive`，自发光在 tonemap 前加入
- **Demo 光照强度适配**：物理衰减比线性/二次项衰减暗约 4-5×，样例中点光 1.5→10、聚光 2.0→30

### Phase 9: 多贴图 PBR + 法线贴图 ✅

- **顶点格式统一**（11 floats / 44 字节）：`position(3) + normal(3) + uv(2) + tangent(3)`
  - `Mesh::CreateCube/Plane/Sphere` 三个基元的硬编码切线
  - `ModelLoader` 的 `ModelVertex` 新增 tangent，读取 `aiMesh::mTangents`（已开 `aiProcess_CalcTangentSpace`），缺失时按 normal-up 叉积兜底
- **Material 新增 4 组贴图接口**：
  - Unit 1：`SetNormalTexture`（linear）
  - Unit 2：`SetMetallicRoughnessTexture`（linear；glTF 约定 G=roughness, B=metallic）
  - Unit 3：`SetAOTexture`（linear，R 通道）
  - Unit 4：`SetEmissiveTexture`（sRGB）
  - 每次 `Bind()` 更新 `hasNormalTex / hasMetalRoughTex / hasAOTex / hasEmissiveTex` 分支标志；所有 sampler uniform 每帧写入保证定义良好
- **PBR 着色器（TBN + 多贴图）**：
  - PBR_VS 读 `aTangent`，Gram-Schmidt 正交化 → 输出 `vTangent / vBitangent / vNormal`
  - PBR_FS：
    - `hasNormalTex` → 构造 TBN，采样 tangent-space 法线 `*2-1` → 世界空间 N
    - `hasMetalRoughTex` → `metallic/roughness` 与贴图 B/G 通道相乘（标量是倍率）
    - `hasAOTex` → `ao` 与 R 通道相乘，累加到 Lo
    - `hasEmissiveTex` → 叠加 emissive
- **ModelLoader 自动 pickup**（Assimp 5.x 约定多覆盖）：
  - Normal：`aiTextureType_NORMALS` → `HEIGHT`（OBJ `map_Bump`）
  - MR：`DIFFUSE_ROUGHNESS` → `METALNESS` → `UNKNOWN`（glTF 不同导出器差异大）
  - AO：`AMBIENT_OCCLUSION` → `LIGHTMAP`
  - Emissive：`EMISSIVE`
  - 正确传 `isSRGB`：color/emissive=true；normal/MR/AO=false

### Phase M1: ShaderManager + 热重载 ✅

- **动机**：Phase 10/11/12 会引入大量 shader（tonemap、bloom、skybox、IBL 预计算、IBL 采样），改一行 GLSL 就要全量重编译非常痛。同时支持玩家改光影（MC/Skyrim Creation Kit 级 mod）。
- **文件组织**：
  - 源：`engine/shaders/{pbr,phong}.{vert,frag}`，CMake POST_BUILD 拷到 `$<TARGET_FILE_DIR>/content/shaders/`
  - Fallback：`engine/rendering/ShaderSources.h` 保留原字符串，文件找不到时用
- **ShaderManager API**（`engine/rendering/ShaderManager.h`）：
  - `Get(name)` → `shared_ptr<RHIShader>`，懒加载；按名去 `Paths::ResolveContent("shaders/{name}.{vert,frag}")` 读
  - `CheckHotReload()` 每帧调用；对比 `last_write_time`，有变化就调 `RHIShader::Compile()` 在原 program 上就地重编
  - 失败时保留旧 program 不换（`GLShader::Compile` 已调整为先编译到临时 program，成功才替换）
  - 内部 cache 以 name 为键，同名查询同一 shared_ptr，已有 Material 自动看到新 program
- **编译期开关**：`ARK_SHADER_HOT_RELOAD` CMake option（默认 ON）→ `-DARK_SHADER_HOT_RELOAD=0` 发布时禁用轮询
- **ForwardRenderer 集成**：构造时创建 `ShaderManager`；`RenderFrame` 开头调 `CheckHotReload()`
- **样例迁移**：`PBRSphere / GroundObject / ModelObject` 不再调 `device->CreateShader() + Compile(kPBR_VS, kPBR_FS)`，改为 `engine.GetRenderer()->GetShaderManager()->Get("pbr")`
- **验证**：构建通过，两个 exe 旁 `content/shaders/` 下 4 个 .vert/.frag 文件齐全；运行时 `ShaderManager: loaded 'pbr' from ...` 日志

### Phase 10: HDR FBO + Bloom ✅

- **动机**：让强光源（灯泡、自发光、金属高光）产生"溢光"效果，并把 tonemap 从场景 shader 移到后处理单通道，后面接 IBL/天空盒/体积光都要共享同一 HDR 缓冲。
- **设计**：
  - **HDR 场景 FBO**：RGBA16F 颜色 + 24/8 深度模板，全分辨率
  - **Bloom 半分辨率 ping-pong**：2 个 RGBA16F FBO（亮度提取 + 可分离高斯模糊交替）
  - **Fullscreen triangle**：单 VAO/VBO，3 顶点覆盖 NDC
  - **三个后处理 program**（内嵌 GLSL，不走 ShaderManager，因为与场景 shader 耦合低）：
    - `bright`：阈值 + soft knee 提取高亮
    - `blur`：可分离 9-tap 高斯，uHorizontal 切换水平/垂直
    - `composite`：`result = ACES((scene + bloom*strength) * exposure)`，写入默认 FB（sRGB 自动 gamma）
- **场景 shader 改动**：`pbr.frag` 与 `ShaderSources.h::kPBR_FS` 去掉 `Lo *= uExposure` 和 ACES 段，直接输出线性 HDR `vec4(Lo, alpha)`
- **PostProcess 类**（`engine/rendering/PostProcess.{h,cpp}`）：
  - `Init(w,h)` / `Resize(w,h)` / `BeginScene` / `EndScene` / `Apply(exposure, threshold, strength, iterations)`
  - 首帧或窗口尺寸变化时自动分配/重建 GL 资源
- **ForwardRenderer 集成**：
  - 构造时 `new PostProcess`
  - `RenderFrame` 在相机循环前后分别调 `BeginScene/EndScene/Apply`
  - 新增 `SetBloomEnabled/SetBloomThreshold/SetBloomStrength/SetBloomIterations` 运行时参数
  - 默认值：阈值 1.0，强度 0.6，5 次高斯迭代（即 10 次 blur pass）
- **验证**：构建通过，日志出现 `PostProcess initialized (1280x720, HDR+Bloom)`；后续场景绘制无 GL error；点光/聚光灯高亮处可见光晕（肉眼可验证）

### Phase 11: Skybox ✅

- **动机**：场景需要一个真实的远景背景；也是 Phase 12 IBL 的输入源（辐照图/预过滤镜都从 cubemap 卷积得来）。
- **设计**：不走 RHI 抽象，直接在 [engine/rendering/Skybox.cpp](engine/rendering/Skybox.cpp) 里用原生 GL —— 与 `PostProcess` 同策略，避免污染 RHI。
- **接口**（[engine/rendering/Skybox.h](engine/rendering/Skybox.h)）：
  - `Init()`：懒初始化 GL 资源（cubemap + 单位立方体 VAO + 内嵌 GLSL program）
  - `SetFromFiles({+X,-X,+Y,-Y,+Z,-Z})`：6 面 LDR 图片加载成 sRGB cubemap，自动生成 mipmap
  - `GenerateProceduralGradient(zenith, horizon, ground, faceSize)`：运行时生成天顶→地平线→地面的线性 HDR 渐变（RGB16F），默认值（蓝→白→棕）在 `Init()` 中自动填充，确保开箱即用
  - `Render(view, projection)`：depth func LEQUAL + depth mask OFF + 剥离 view 平移；shader 强制 `gl_Position.zw` 让深度恒为 1
  - `SetEnabled(bool)` / `SetIntensity(float)`
- **ForwardRenderer 集成**：构造时 `new Skybox`；`RenderCamera` 在不透明网格绘制后、`EndScene` 前调 `skybox_->Render(viewMat, projMat)`；这样天空盒绘制到 HDR FBO 内，自动被 bloom 和 tonemap 处理
- **验证**：构建通过；运行日志 `Skybox: generated procedural gradient (128x128 per face)` + `Skybox initialized`；场景背景从纯黑变成渐变天空

### Phase 12: IBL + RenderSettings 聚合 ✅

- **动机**：让材质在无直接光照下也有基于环境的 ambient 贡献（PBR "镜面/漫反射环境光"），接近 UE5 效果；同时为将来 M10 场景序列化做准备，先把散落的渲染参数聚合到一个结构体里。
- **新增文件**：
  - [engine/rendering/RenderSettings.h](engine/rendering/RenderSettings.h)：`RenderSettings` 聚合结构体（`exposure` + `bloom{enabled,threshold,strength,iterations}` + `sky{enabled,intensity}` + `ibl{enabled,diffuseIntensity,specularIntensity}`）。当前只作为运行时参数中枢；M10 阶段添加 JSON (de)serialize 即可一次性持久化所有渲染调参。
  - [engine/rendering/IBL.h](engine/rendering/IBL.h) / [.cpp](engine/rendering/IBL.cpp)：原生 GL（不走 RHI），烘焙 split-sum IBL 三件套：
    - `irradianceMap_` (RGB16F cubemap, 32×32)：辐照度卷积（hemisphere 积分，`sampleDelta=0.025`），用于漫反射环境项
    - `prefilterMap_` (RGB16F cubemap, 128×128, 5 mips)：按 roughness 分级 GGX 重要性采样（1024 样本 / Hammersley），用于镜面环境项
    - `brdfLUT_` (RG16F 2D, 512×512)：Schlick-Fresnel 的 `(NdotV, roughness) → (A,B)` 查找表，全屏 quad 预计算
  - `Bake(envCube, ...)` 保存/恢复 GL 状态（FBO、viewport、剔除、深度），不干扰主循环。
- **Shader 更新**（[engine/shaders/pbr.frag](engine/shaders/pbr.frag)）：
  - 新增 uniform：`uIBLEnabled`, `uIrradianceMap`（unit 5），`uPrefilterMap`（unit 6），`uBrdfLUT`（unit 7），`uIBLDiffuseIntensity`, `uIBLSpecularIntensity`, `uPrefilterMaxLod`
  - main 末尾新增 ambient 分支：`uIBLEnabled != 0` 时 `Lo += kD * irradiance * albedo + prefiltered * (F * envBRDF.x + envBRDF.y)`（粗糙度感知的 `FresnelSchlickRoughness`）；否则保留原 `vec3(0.03)*albedo*ao` 兜底
  - `ShaderSources.h` 中嵌入的 fallback `kPBR_FS` 未同步（缺失的 uniform 会被自动优化掉，不影响兼容）
- **ForwardRenderer 重构**：
  - 引入 `RenderSettings settings_` 成员作为单一事实源；原有 `exposure_/bloomEnabled_/...` 字段删除，setter/getter 全部改为读写 `settings_` 的对应字段（API 向后兼容）
  - 新增成员 `std::unique_ptr<IBL> ibl_` + `bool iblBaked_`；`RenderFrame` 首帧检测到 Skybox 就绪时调 `ibl_->Bake(skybox_->GetCubeMap())`，之后每帧复用；提供 `RebakeIBL()` 供切换 skybox 后手动触发
  - `DrawMeshRenderer` 里绑定 IBL 三贴图到单元 5/6/7，并设置 `uIBLEnabled/uPrefilterMaxLod/uIBLDiffuseIntensity/uIBLSpecularIntensity`
  - `skybox_->SetIntensity(settings_.sky.intensity)`、`postProcess_->Apply(..., settings_.exposure, settings_.bloom.*)` 全部走聚合参数
- **验证**：构建全部目标通过；运行日志出现 `IBL baked: irr=32 prefilter=128 brdfLUT=512`（IBL.cpp:470），场景在无直接光区域出现正确的环境漫反射 + 镜面反射。

### Phase 13: Directional Shadow Mapping ✅

- **动机**：阴影是画面真实感最大的贡献来源；直接光（主方向光）没有投影会让物体看起来"飘"。先落地单层方向光 shadow map + PCF，后续可升级到 CSM。
- **设计**：继承既有"引擎内部 pass 走原生 GL"的风格（与 PostProcess / Skybox / IBL 一致），不污染 RHI 抽象。
- **新增文件**：
  - [engine/shaders/depth.vert](engine/shaders/depth.vert) / [depth.frag](engine/shaders/depth.frag)：纯 depth-only，vert 只读取 `aPosition`，frag 为空。同时镜像到 [engine/rendering/ShaderSources.h](engine/rendering/ShaderSources.h) 的 `kDepth_VS/kDepth_FS`，并在 [engine/rendering/ShaderManager.cpp](engine/rendering/ShaderManager.cpp) 的 `LookupEmbeddedSource` 注册为 `"depth"`。
  - [engine/rendering/ShadowMap.h](engine/rendering/ShadowMap.h) / [.cpp](engine/rendering/ShadowMap.cpp)：GL_DEPTH_COMPONENT24 纹理 + FBO 封装。
    - `Init(resolution)`：幂等；`GL_CLAMP_TO_BORDER` + border color (1,1,1,1) —— 采样到 shadow map 之外视为"完全受光"；`glDrawBuffer(GL_NONE)` 声明无颜色附件。
    - `Begin()/End()`：保存/恢复 viewport + 当前 FBO；清空 depth。
    - `UpdateMatrix(lightDir, focus, orthoHalfSize, near, far)`：沿光线方向在 `focus - dir*d` 处放置光源"相机"，正交投影覆盖 `[-half, half]` × `[-half, half]`。上向量在 `|dir.y|>0.95` 时切换到 (0,0,1) 防止退化。
- **RenderSettings 扩展**：新增 `shadow { enabled, resolution, orthoHalfSize, near/farPlane, depthBias, normalBias, pcfKernel }`；`pcfKernel=k` 产生 `(2k+1)²` 个采样（默认 2 → 5×5 = 25 taps，结合 `GL_LINEAR` 硬件双线性 ≈ 100 tap 等效）。
- **Shader（[pbr.frag](engine/shaders/pbr.frag) + 镜像 kPBR_FS 略）**：
  - 新增 uniform：`uShadowEnabled`, `uShadowMap`（unit 8），`uLightSpaceMatrix`, `uShadowDepthBias`, `uShadowNormalBias`, `uShadowPcfKernel`, `uShadowTexelSize`
  - `SampleDirShadow(worldPos, N, L)`：将世界坐标投射到光源 clip 空间 → [0,1]；超出 far plane 或 XY 范围直接返回 0（完全受光）；`bias = max(normalBias*(1-NdotL), depthBias)` 对斜入射面加大偏移；PCF 循环采样
  - `CalcDirLightPBRShadowed(light, ..., shadow)`：返回 `ambient + (1-shadow) * Lo`（ambient/环境光不被 shadow 抑制，避免死黑）
  - main 循环首个方向光 (`i==0`) 且 `uShadowEnabled!=0` 时计算 shadow，否则 `shadow=0`
- **ForwardRenderer 集成**：
  - 新增 `std::unique_ptr<ShadowMap> shadowMap_` 成员 + `bool shadowThisFrame_` 帧级标志
  - `RenderFrame` 在 `BeginScene` 之前先调 `RenderShadowPass()`
  - `RenderShadowPass()`：找到第一个启用的方向光 → `ShadowMap::Init(settings_.shadow.resolution)` → `UpdateMatrix(...)` → 用 `ShaderManager::Get("depth")` + 现有 `GetOrCreatePipeline` 遍历所有可见 MeshRenderer 绘制 depth；渲染期间切换为 `glCullFace(GL_FRONT)` 抑制 self-shadow acne
  - `SetLightUniforms`：`shadowThisFrame_ && shadowMap_->IsValid()` 时绑定 depth 纹理到单元 8 并写入所有 shadow uniform
- **验证**：构建全部目标通过；运行日志 `ShadowMap initialized (2048x2048)` + `ShaderManager: loaded 'depth' from ...content/shaders/depth.vert`；无 GL error；方向光下可见物体在地面上的软阴影（PCF 5×5）。

### Phase 14: ACES Hill 修复（双重 sRGB）+ Mini-M10 SceneSerializer ✅

- **问题**：场景泛白/过曝。根因是 [engine/platform/Window.cpp](engine/platform/Window.cpp) 启用了 `GL_FRAMEBUFFER_SRGB`（正确），但 PostProcess composite 里用的 Narkowicz ACES 公式**已经输出 sRGB-ready 空间**，导致硬件再做一次 linear→sRGB，相当于两次 gamma。中灰 0.5 被抬到约 0.73。
- **修复**：[engine/rendering/PostProcess.cpp](engine/rendering/PostProcess.cpp) 的 `kCompositeFS` 换成 **ACES Hill fitted**（`ACESInputMat` + `RRTAndODTFit` + `ACESOutputMat`），输入线性 HDR → 输出**线性** LDR，留给 `GL_FRAMEBUFFER_SRGB` 唯一一次编码。
- **Mini Phase M10（为外部调光工具准备的最小序列化）**：
  - [engine/rendering/SceneSerializer.h](engine/rendering/SceneSerializer.h) / [.cpp](engine/rendering/SceneSerializer.cpp)：
    - 零依赖；手写 JSON writer + 约 150 行的手写 parser（够用即可的子集）
    - Schema：`{ renderSettings: {exposure,bloom,sky,ibl,shadow}, lights: [{name,type,color,intensity,ambient,position,rotationEuler,range,constant,linear,quadratic,innerAngle,outerAngle}, ...] }`
    - `Save(path, renderer)`：遍历 `Light::GetAllLights()` 把当前运行时状态 dump 成 JSON；旋转用欧拉角（度）表示便于人肉编辑
    - `Load(path, renderer)`：把 JSON 读回 `RenderSettings`；光源按 `AObject::GetName()` 匹配回 runtime 的 `Light*`，未匹配的条目忽略，runtime 中 JSON 里没有的光源保留原值
    - `EnableHotReload(path)` + `Tick(renderer)`：mtime 轮询（复用 ShaderManager 的思路）；**首次 Tick 延迟执行初始 Save/Load**（因为 `AObject::Init()` 在 OnLoad 之后的主循环里才被调用，光组件此时才真正进入 `allLights_` 列表）
  - [engine/rendering/ForwardRenderer.cpp](engine/rendering/ForwardRenderer.cpp)：`RenderFrame` 开头调 `SceneSerializer::Tick(this)`
  - [samples/src/scenes/DemoScene.cpp](samples/src/scenes/DemoScene.cpp)：`OnLoad` 末尾 `SceneSerializer::EnableHotReload(Paths::ResolveContent("lighting.json"))`
- **验证**：
  1. 启动 → 生成 `content/lighting.json`，正确写入 3 盏光 + 全量 RenderSettings
  2. 外部编辑 `lighting.json`（改 `exposure: 0.5`、改 `intensity: 5`）保存 → 日志出现 `SceneSerializer: loaded ... (3 lights matched)`，画面实时反映
- **使用方式（外部工具）**：任何语言（Python/Node/纯记事本）修改 JSON 保存即生效。下一步可以用 200 行 Dear PyGui 或 tkinter 做一个滑块工具，实时调光。

---

### Phase 15.F: C# 脚本宿主 + MOD 系统 ✅（15.F.1 → 15.F.3 完成）

**目标**: 把"可改数据 + 可换资源"的 v0.2 引擎升级为"可改行为" — MOD 用 C# 写脚本，引擎调度生命周期，MOD 通过 `Bridge` 直接读写引擎对象。

#### 15.F.1 CoreCLR 实接入
- 托管项目 `scripts/StarArk.Scripting/`：`net10.0` lib，`Engine.cs` 暴露 `[UnmanagedCallersOnly]` 静态入口
- `engine/scripting/ScriptHost.cpp`：nethost.dll → `get_hostfxr_path` → hostfxr → `hostfxr_initialize_for_runtime_config(<exe>/scripting/StarArk.Scripting.runtimeconfig.json)` → `load_assembly_and_get_function_pointer` 解出托管入口
- `engine/CMakeLists.txt`：`option STARARK_ENABLE_SCRIPTING`（OFF 默认）；ON 时自动找 `Microsoft.NETCore.App.Host.win-x64` pack + `dotnet build -c Release -o build/managed`
- exe POST_BUILD 把 `build/managed/*` 部署到 `<exe>/scripting/` + `nethost.dll` 到 `<exe>/`
- runtimeconfig.json 必须由 `dotnet build` 生成，纯 csc 不行；同进程不能二次 host CoreCLR

#### 15.F.2 Bridge + MOD 发现
- **ScriptApi 函数指针表**（`engine/scripting/ScriptHost.h`）：append-only struct，内含 `version` 字段；managed 端 `Bridge.RequiredApiVersion` 不匹配则拒绝绑定
- v1 字段：`Log` / `GetDeltaTime` / `GetTotalTime` / `GetKey` / `GetKeyDown`；native trampolines 走 `DebugListenBus::Broadcast` / `Time::*` / `Input::*`
- `OnEngineStart` 签名：`int(ArkScriptApi*, byte* modsDirUtf8)`，把 `<gameRoot>/mods` UTF-8 传给托管
- managed 三件套：
  - `Bridge.cs`：`ArkScriptApi` struct 1:1 镜像；静态门面 `Bridge.Log/LogInfo/...`；UTF-8 走 `stackalloc(<=1024)`，省 GC
  - `IMod.cs`：MOD 接口（default 方法 = 旧 MOD 不必全实现）
  - `Engine.cs`：`OnEngineStart` 调 `Bridge.Initialize`；扫 `<modsDir>/<modname>/scripts/*.dll`；每个 DLL 一个 `ModLoadContext : AssemblyLoadContext(isCollectible:true)`；`Activator.CreateInstance` 实例化 `IMod`
- **关键坑**：`ModLoadContext.Load` 必须显式返回 `typeof(IMod).Assembly` 当请求 `"StarArk.Scripting"` —— 返回 null 会让 Default ALC 在 `<exe>/` probing 不到 `<exe>/scripting/StarArk.Scripting.dll`，触发 `ReflectionTypeLoadException`
- 示例 MOD：`samples/mods-src/hellomod/`；CMake 单独 `dotnet build` → `build/managed-mods/hellomod/HelloMod.dll`，POST_BUILD 部署到 `<exe>/mods/hellomod/scripts/HelloMod.dll`
- **目录约定**：MOD **源码**放 `samples/mods-src/<name>/`（不会被部署），**运行时资源**（贴图/网格）放 `samples/mods/<name>/`（整目录 `copy_directory` 到 exe 旁）

#### 15.F.2.1 Unity 风格生命周期
- `IMod` 由 `OnLoad/OnFrame/OnUnload` 升级为 7 个 default 方法：
  `PreInit (Awake) → OnLoad (OnEnable) → Init (Start) → FixedLoop (FixedUpdate) → Loop (Update) → PostLoop (LateUpdate) → OnUnload (OnDestroy)`
- `ScriptHost` 把单一 `OnFrame` 拆为 `OnFixedTick / OnTick / OnPostTick`；`Engine.cs` 暴露 5 个 `[UnmanagedCallersOnly]`
- `EngineBase::MainLoop` 帧内插入：
  1. `fixedAccum += dt`（cap 0.25s 防 spiral-of-death），while ≥ 1/60 调 `OnFixedTick(1/60)`
  2. `OnTick(dt)` ＜在 `StepTick` 之前＞
  3. `StepTick / StepPostTick`
  4. `OnPostTick(dt)` ＜在 `StepPostTick` 之后＞
- `Init` 在 managed 端 lazy-fire：每个 mod 第一次 `OnTick` 之前由 `LoadedMod.Started` 标记触发
- 实测 hellomod 全 7 阶段命中：`FixedLoop=600/10s=60Hz` 准确，`Loop≈177fps`，关闭窗口正常 `OnUnload`

#### 15.F.3 引擎 API 暴露（ScriptApi v2）
- ScriptApi 升级到 v2，append-only 加 8 个函数指针：
  - `FindObjectByName(name) → handle`（active scene 内查找，0=未找到）
  - `SpawnObject(name) → handle`（创建空 `AObject` 进 active scene 的 pendingList）
  - `DestroyObject(handle)`
  - `GetObjectName / SetObjectName`（"两次调用"协议：第一次 buf=null 探所需字节）
  - `GetPosition / SetPosition`（**LOCAL** 坐标，spawn 出来对象通常无 parent）
  - `GetActiveSceneName`
- `ArkObjectHandle = uint64`，等于 `AObject::GetId()`
- `ScriptHost.cpp` 内 `ArkLookup_(handle)` 同时遍历 `GetObjectList() + GetPendingList()`，让本帧 spawn 出来的对象当帧可寻
- `Bridge.cs`：新增强类型 `ObjectHandle`/`Vec3` + 8 个静态门面（`Bridge.Find/Spawn/Destroy/GetName/SetName/GetPosition/SetPosition/ActiveSceneName`）
- `HelloMod` 升级演示：F1 spawn `"HelloMod.Marker"` → 在 origin 周围半径 3、周期 4s 圆周运动；F2 destroy；F3 print pos + 找 `MainCamera`
- 实测：`[INFO] [HelloMod] OnLoad. Active scene = ''. F1=spawn  F2=destroy  F3=print` —— 注意 `OnLoad` 在 `DiscoverAndLoadMods` 时 fire，此时 active scene 还没 activate，符合预期

#### 15.F 后续候选

- **15.F.4 MOD 热重载（v0.2.x 内可做）**：ALC 已 collectible，加 `FileSystemWatcher` → `Unload()` 旧 ALC → 重新 `LoadFromAssemblyPath`；MOD 状态需 mod 自己持久化（可提供 `IMod.SaveState/LoadState`）
- ~~**15.F.5 mod.toml**~~ → 升级为 **v0.3 ModSpec §2** 的全量规范（schema_version / supported_pipelines / depends_on 拓扑排序 / script_api_min 校验）
- ~~**15.F.6 ScriptApi v3**~~ → 升级为 **v0.3 ModSpec §15** 的全量规划（数据组件反射注册 + ScriptComponent + 旋转/层级访问 + Camera 控制 + 主线程契约）

> **v0.3 主线由 [ModSpec.md](ModSpec.md) 驱动**，迁移清单见 ModSpec 附录 A。15.F.5/.6 的旧规划被 ModSpec 接管，本节不再单独跟踪。

---

### Phase 15.G.1: Paths 三轨路径语法（v0.3 ModSpec §3 第一步，2026-04-26）

- **动机**：ModSpec §3 要求 mod 资产引用必须显式带 scheme 前缀（`./` / `mod://<id>/` / `engine://`），且解析时需知道"当前 mod"。从最小、向后兼容的一刀切下，所有后续迁移（Material sidecar / vanilla 重构 / GUID 字符串化）都要这把钥匙。
- **改动文件**：仅 [engine/platform/Paths.h](../engine/platform/Paths.h) + [engine/platform/Paths.cpp](../engine/platform/Paths.cpp)
- **新增 API**：
  - `Paths::ResolveResource(logical, currentModId)` — 双参重载，识别三种 scheme：
    - `"./<rest>"`           → `Mods()/<currentModId>/<rest>`
    - `"mod://<id>/<rest>"`  → `Mods()/<id>/<rest>`
    - `"engine://<rest>"`    → `Content()/<rest>`（不可被 mod 覆盖）
    - 裸路径（兼容 v0.2）   → 旧 `load_order.toml` 栈遍历 + 一次性 deprecation WARN
  - `Paths::ModContextScope` — RAII，进入/离开当前 mod 作用域
  - `Paths::PushCurrentModId / PopCurrentModId / GetCurrentModId` — 线程本地栈底层 API
- **零侵入兼容**：旧的 `ResolveResource(logical)` 单参重载被改写为 `ResolveResource(logical, GetCurrentModId())`，所以 `TextureLoader` / `ModelLoader` / `MeshRenderer::ResolveResources` 完全不动；只要外层 SceneDoc::Load 之类的入口包一层 `Paths::ModContextScope scope("vanilla");`，downstream 自动获得 mod 上下文。
- **绝对路径 fast-path**：`fs::path(logical).is_absolute()` 时直接返还，**不告警 / 不走 VFS**。这避免 Bistro 等"用户在 scene 代码里直接传绝对路径"场景被误标 deprecated。
- **失败时**（按 ModSpec §3 失败语义草案，本期未强制 reject）：
  - 裸路径 → WARN 一次后回落旧行为；正式 reject 留到 v0.3 vanilla mod 重构后
  - `mod://<id>/<rest>` 但 id 不存在 → 返回 `Mods()/<id>/<rest>` 不存在路径，由 caller 报失败
  - `"./"` 无 mod ctx → fallback Content() + 一次性 WARN
- **验证**：full build green；StarArkSamples（含 Bistro+Cottage 双场景）启动 stderr=0 行，全部贴图/模型加载日志正常。
- **下一步**：在 `SceneDoc::Load(toml_path)` 入口套 `ModContextScope`（推断 mod_id 自 toml 路径），然后把 `cottage.toml` 里的 `tex_albedo = "textures/ground.png"` 改写为 `"./textures/ground.png"`，端到端验证三轨语法。

---

### 下轮开工锚点：延迟渲染管线（2026-04-26 作者决策）

v0.3 ModSpec 迁移中场休息。**下一次开工不继续 ModSpec §4/§7.3/§1**，而是优先推进**延迟渲染管线**——这是"多点光源 + 后期深化 PBR"的前提，是顶上限制业务表现力的头号阶段任务。ModSpec 迁移可在 deferred 推进期间作为支线穿插。

**入口**：[Roadmap.md "延迟渲染前置工作清单"](Roadmap.md)。9 项前置阻挡按下面顺序执行（已按依赖排好）：

1. **Shader 双源治理**（disk 为唯一真值，CMake 生成嵌入头）— 不先做这步，后续每加一个 G-Buffer/Lighting 着色器都要改两份
2. **清理 `pbr.frag` 末尾 RED/GREEN 调试残留**（[tech-debt](../memories/repo/tech-debt.md) 已记）
3. **RHI `RenderTarget` 抽象**（`RHIDevice::CreateRenderTarget`；把现有 IBL/PostProcess/ShadowMap 的 20+ 处 raw `glGenFramebuffers` 迁过来）
4. **RHI MRT 支持**（`PipelineDesc.colorAttachments[]` + `CommandBuffer::SetRenderTarget` / `BlitToBackBuffer`）
5. **`RHITexture` 格式扩展**（`RGBA16F` / `RG16F` / `R32F` / `Depth32F` 等 RT 必备格式 + `UploadEmpty`）
6. **Material 多 shader 槽**（按 pass 查 shader：`gbuffer` / `forward` / `shadow` / `transparent`，缺失自动降级）
7. **Material RenderQueue + `IsTransparent()`**（deferred 只渲染 Opaque/AlphaTest，Transparent 走旁路 forward）
8. **抽 `CollectOpaqueDrawList(camera)`**（DeferredRenderer / ForwardRenderer 共用）
9. **G-Buffer + Lighting Pass 着色器**（`gbuffer.vert/frag` 写 4 个 RT；`lighting.frag` 全屏 pass 从深度重建 worldPos）

**最终骨架**：新建 `engine/rendering/DeferredRenderer.{h,cpp}`；`RenderSettings` 加 `pipeline = forward | deferred` 运行时开关；`ForwardRenderer` 保留作为 transparent / sky / UI overlay 通道。

**不在本阶段做**（明确推后）：
- Mod 自定义渲染管线 / 后处理链扩展点（昨天讨论结论：不开放完整 SRP；做完 deferred 后另起 Phase 实现"后处理 + shader override + 渲染钩子"三件套）
- Tile/Cluster 多光源剔除
- DX12 / Vulkan 后端
- 多线程命令录制

**进入 deferred 实现前必须确认**：build green、StarArkSamples（Bistro+Cottage）烟测 stderr=0 行（当前已满足）。

---

### Phase R9: Deferred Pipeline ✅（2026-04-26 完结）

> **代号**：Roadmap #9。9 项前置阻挡 + 出口验收全部完成。详细日志见 `/memories/repo/v0.2-progress.md` 阶段 A→G4。

**最终架构**
- `RenderSettings::pipeline = Forward | Deferred`，配置来源优先级：`game.config.json::pipeline` < `ARK_PIPELINE` env（debug override）。
- Forward 路径不变；Deferred 路径骨架：
  1. `DrawListBuilder::CollectOpaqueDrawList` → 不透明渲染体进 `DeferredRenderer::Render`
  2. G-buffer pass：4 RT（RGBA8 albedo+metallic / RGBA16F worldN+roughness / RGBA16F emissive+motion / RGBA8 ao+flags）+ Depth32F；`Material::BindToShader(gbufferShader)` 显式喂参
  3. Lighting pass：fullscreen triangle (gl_VertexID, 无 VBO)；`lighting.frag` 采 4 G-buffer + Depth32F，重建 worldPos via `uInvViewProj`，跑完整 Cook-Torrance + PCF Shadow + IBL（irradiance + prefilter + envBRDF），写 linear HDR 到 `lit_`
  4. Stage F transparent overlay：`BlitGBufferDepthToLit` 把 gbuffer 深度 blit 到 lit_ 的 Depth32F renderbuffer；`CollectTransparentDrawList` 按 camera distance 平方 back-to-front 排序；用 forward pbr shader 渲到 lit_（depthTest=on, depthWrite=off, blend=src_alpha/one_minus）
  5. PostProcess ingest：`IngestHDRColor(lit_color, w, h, gbuffer_depth)` 同时把颜色和深度 blit 进 hdrFBO_，让 SSAO/Fog/SSR/ContactShadow/TAA 全部跑得起来；`Apply(...)` 完成 bloom/exposure/ACES/FXAA composite

**关键工程取舍**
- Material 多 shader 槽未做"shader by pass"完整方案，而是直接 `Material::BindToShader(RHIShader*)` 由 renderer 显式选 shader（gbuffer / pbr）；轻量足够。
- TAA 复用 forward 的 depth-based reprojection（`uPrevViewProj * worldP` 投回上一帧 UV），不需要显式 motion vector RT；emissive RT.alpha 槽位预留作 v0.3 高质量 motion blur / disocclusion mask 扩展。
- IBL/PostProcess 内部 19 处 raw `glGenFramebuffers` 未迁；ShadowMap 已迁。剩下 follow-up 不阻塞 deferred 落地，挂 `/memories/repo/tech-debt.md`。

**验证**：build green，forward 与 `ARK_PIPELINE=deferred` 烟测均 stderr=0；StarArkGame `game.config.json::pipeline` "forward"/"deferred" 切换均通过；Bistro 在 deferred 路径 IBL/Shadow/Bloom/SSAO 视觉无回归（plumbing 全部就位）。

**Roadmap #9 关单**。下一步开工：v0.3 ModSpec 主线 / 编辑器 / 后处理 mod 钩子 等长线议程。

---

## 4.5 长期路线（摘要）

本节已迁移至 [Roadmap.md](Roadmap.md)。v0.1-renderer 里程碑期内不做远景开发；本文件只维护到 v0.1 冻结为止的进度与约定。

---

## 5. v0.1 收官清单（冻结前必须做完的事）

v0.1-renderer 的定位是**交付一个可被 AI 直接调用的 PBR 渲染后端**。Phase 1–14 的功能代码已经到位；
剩下的工作全部是**稳定化 + 文档化**。完成后打 git tag `v0.1-renderer` 冻结。

| # | 任务 | 产物 | 状态 |
|---|------|------|------|
| S1 | 能力清单 | [docs/Capabilities.md](Capabilities.md) —— 引擎能做什么、不能做什么、需要什么输入 | ⏳ |
| S2 | 已知问题清单 | [docs/KnownIssues.md](KnownIssues.md) —— 技术债、性能边界、已知 bug | ⏳ |
| S3 | 公共 API 稳定化 | [docs/API.md](API.md) 与实际代码对齐，标注 stable / internal，函数签名不会再动 | ⏳ |
| S4 | README 定位改写 | `README.md` 写清"v0.1 是什么 / 不是什么 / 如何调用" | ⏳ |
| S5 | 最小完整样例（Cottage Scene） | `samples/` 里一个场景：FBX 小屋 + 地面 + 方向光 + 2 点光 + skybox + bloom + 阴影。**只用公共 API**，证明 v0.1 是自洽的 | ✅ [samples/src/scenes/CottageScene.{h,cpp}](../samples/src/scenes/CottageScene.cpp) |
| S6 | 构建指南 | 任何陌生机器按 [docs/Build.md](Build.md)（待建或合并进 README）一次成功 | ✅ 已合并进 [README.md](../README.md) "构建环境要求" |
| S7 | git tag `v0.1-renderer` | 前 6 项完成后打 tag，之后进入维护模式，不再加新功能直到路线 Y+ 启动 | ⏳ 由作者手动打 tag（文档/代码已就绪） |

**不做**（刻意排除）：脚本系统、场景对象序列化、反射、编辑器、物理、动画、音频、联机——全部留给 v0.2+。

### 已提前落地的远期准备（可以不动，也不承诺 API 稳定）

Phase 14 的 `SceneSerializer` 只序列化 `RenderSettings + Light`，是为了解锁 WPF Lighting Tuner 这个**外部工具用例**。
它是 v0.1 的一部分。**但不等于"场景序列化"已实现**——AObject/MeshRenderer/Material/Mesh 还没有序列化。
完整场景序列化属于 v0.2+（见 Roadmap）。

---

## 6. 已知问题 & 技术债

1. ~~**Pipeline 每帧重建**~~: 已通过 Pipeline 缓存解决（Phase 7）
2. **ForwardRenderer 中 uniform 设置时序**: 在 cmdBuffer 的 Begin/End 之间调用 `shader->SetUniform*()` 是直接的 OpenGL 调用（不经过命令缓冲区录制），这在 OpenGL 后端可以工作但不够干净
3. **GLM include 依赖 junction**: 构建系统通过 `mklink /J` junction 解决 GLM 的包含路径问题，首次构建时需要管理员权限或开发者模式
4. **静态注册表线程安全**: Camera/Light/MeshRenderer 的静态 `vector<T*>` 不是线程安全的

---

## 7. 架构关键约定

### 主循环 14 步（EngineBase::MainLoop）

```
1. PollInput
2. Time::Update()
3. 窗口 resize → 更新 Camera 宽高比
4. DrainPendingObjects（PreInit/Init/PostInit 新对象，循环直到无新增）
5. Loop 持久对象（EngineBase::persistentList_）
6. Loop 场景对象（AScene::objectList_）
7. PostLoop 持久对象
8. PostLoop 场景对象
9. AScene::Tick(dt)（场景级逻辑）
10. UpdateTransforms（只遍历脏的根节点子树）
11. ForwardRenderer::RenderFrame()
12. 销毁标记对象（扫描 isDestroyed==true 的 unique_ptr）
13. 处理延迟场景切换
14. SwapBuffers
```

### 所有权规则

- `AScene` 拥有其创建的 `AObject`（unique_ptr）
- `AObject` 拥有其添加的 `AComponent`（unique_ptr）
- `GetComponent<T>()` 返回裸指针，不转移所有权
- `CreateObject<T>()` 返回裸指针，场景保留所有权
- `AddComponent<T>()` 返回裸指针，对象保留所有权
- Mesh/Material/Shader 使用 `shared_ptr`，允许多个 MeshRenderer 共享

### 组件生命周期

```
AddComponent<T>() → 构造 → owner_ 赋值 → OnAttach() → PreInit() → Init() → PostInit()
[每帧] → Loop(dt) / PostLoop(dt)（如果 enabled）
RemoveComponent<T>() → OnDetach() → 析构
AObject 析构 → 所有组件 OnDetach() → 所有组件析构
```

### 场景切换流程

```
LoadScene<T>()（延迟到帧末）
→ current.OnUnload()
→ 销毁旧场景所有对象
→ 删除旧场景
→ 创建新场景
→ new.OnLoad()
→ DrainPendingObjects()
→ 下一帧开始 Loop
```

---

## 8. 给 AI 的续开发指南

### 继续开发时的操作顺序

1. 阅读本文件了解当前进度
2. 阅读 `docs/API.md` 了解现有 API
3. 阅读 `Plan.md` 了解完整架构设计（包含详细的设计决策和边界情况处理）
4. 修改代码前确认构建环境：运行构建命令验证现有代码能编译通过
5. 新增文件后更新 `engine/CMakeLists.txt` 的 GLOB_RECURSE（按目录分组）
6. 完成后更新本文件的进度

### 编码规范

- 命名空间: `ark::`
- 头文件用 `#pragma once`
- include 路径: `#include "engine/core/AObject.h"`（从项目根目录开始）
- 不使用异常，用 `assert` + `ARK_LOG_FATAL`
- 不添加不必要的注释/文档/类型注解
- 组件注册模式: OnAttach 加入静态列表，OnDetach 移除
- 新增 cpp 文件所在目录需要在 `engine/CMakeLists.txt` 的 GLOB_RECURSE 中
