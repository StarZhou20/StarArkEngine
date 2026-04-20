# StarArk 工程开发日志 (DevLog)

> **用途**: 供 AI 编码助手阅读，快速了解项目当前状态、已完成内容、待办事项和技术约束。  
> **最后更新**: 2026-04-21

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
| 构建目录 | `build2/` |
| 代理 | `http://127.0.0.1:9910`（下载依赖时需要） |

### 构建命令

```powershell
# 配置
cmd /c "call vcvarsall x64 >nul 2>&1 && cmake -S . -B build2 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# 编译
cmd /c "call vcvarsall x64 >nul 2>&1 && cmake --build build2"

# 运行
build2\game\StarArkGame.exe
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
│       ├── ShaderSources.h     # 内置 GLSL 着色器（Phong + PBR Cook-Torrance）
│       ├── OrbitCamera.h/cpp   # 轨道相机控制器组件
│       ├── TextureLoader.h/cpp # 纹理文件加载（stb_image）
│       └── ModelLoader.h/cpp  # 3D模型加载（Assimp）
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

---

## 5. 未完成 / 待开发

### 高优先级（核心功能缺失）

| 编号 | 功能 | 描述 | 状态 |
|------|------|------|------|
| **F1** | ~~纹理加载~~ | stb_image 集成，支持 PNG/JPG 加载到 RHITexture | ✅ 已完成 |
| **F2** | ~~模型加载~~ | Assimp 5.4.3 集成，OBJ/FBX/glTF 导入为 Mesh + Material | ✅ 已完成 |
| **F3** | ~~多光源渲染~~ | 支持方向光/点光/聚光灯多光源叠加 | ✅ 已完成 |
| **F4** | ~~PBR 渲染~~ | Cook-Torrance BRDF，metallic-roughness 工作流 | ✅ 已完成 |
| **F5** | ~~渲染排序~~ | 按 shader/material 指针排序 MeshRenderer，减少状态切换 | ✅ 已完成 |
| **F6** | ~~Pipeline 缓存~~ | FNV-1a 哈希缓存，避免每帧重建 Pipeline/VAO | ✅ 已完成 |

### 中优先级（完善功能）

| 编号 | 功能 | 描述 |
|------|------|------|
| **M1** | ResourceManager | 统一的资源管理器，缓存 Mesh/Material/Shader/Texture，避免重复加载 |
| **M2** | Skybox | 天空盒渲染（立方体贴图） |
| **M3** | Shadow Mapping | 方向光阴影贴图（至少基础版） |
| **M4** | 文件系统工具 | 路径解析、资源搜索、热重载基础设施 |
| **M5** | 场景序列化 | 从 JSON/二进制文件加载场景配置 |
| **M6** | 物理系统 | 碰撞检测 + 刚体（可集成 Bullet/Jolt） |
| **M7** | 音频系统 | 基础音效播放（可集成 OpenAL/miniaudio） |

### 低优先级（优化 & 工具）

| 编号 | 功能 | 描述 |
|------|------|------|
| **L1** | DX12 后端 | RHI 的 DirectX 12 实现 |
| **L2** | 多线程渲染 | 命令缓冲区在工作线程录制，主线程提交 |
| **L3** | GPU Instancing | 批量绘制相同 Mesh 的对象 |
| **L4** | 性能 Profiler | 帧时间分解、GPU 计时器、内存追踪 |
| **L5** | ImGui 集成 | 运行时调试 UI |
| **L6** | Blender 导出插件 | 自定义导出器，直接生成 StarArk 场景格式 |

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
