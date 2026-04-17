# StarArk 工程开发日志 (DevLog)

> **用途**: 供 AI 编码助手阅读，快速了解项目当前状态、已完成内容、待办事项和技术约束。  
> **最后更新**: 2026-04-17

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
│       ├── Material.h/cpp      # 材质（shader + 颜色/高光/纹理）
│       ├── MeshRenderer.h/cpp  # 网格渲染器组件
│       └── ForwardRenderer.h/cpp # 前向渲染管线（Blinn-Phong）
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
- **AObject**: 基类，自增 uint64_t id，内置 Transform，组件系统（Add/Get/RemoveComponent），生命周期（Init/PostInit/Tick/PostTick/OnDestroy），Destroy 级联到子节点，SetDontDestroy 转移到 EngineBase
- **AComponent**: 基类，OnAttach/OnDetach/Tick/PostTick，enabled 标记
- **Transform**: position/rotation(quat)/scale，父子层级，局部/世界矩阵 + 脏标记，析构时双向清理
- **IObjectOwner**: AScene 和 EngineBase 共同实现的接口
- **验证**: DemoScene 加载、TriangleObject 创建、组件系统工作、60fps

### Phase 4: 3D Rendering Pipeline ✅

- **Camera 组件**: 透视/正交投影，优先级排序，静态注册表（OnAttach 注册、OnDetach 注销）
- **Light 组件**: 方向光/点光/聚光灯类型，颜色/强度/范围/锥角，静态注册表
- **Mesh 资源**: 顶点+索引数据，GPU 上传，`CreateCube()` / `CreatePlane()` 原始体生成
- **Material**: shader 引用 + 颜色/高光/光泽度/漫反射纹理，Bind() 设置 per-material uniforms
- **MeshRenderer 组件**: 持有 Mesh + Material（shared_ptr），静态注册表
- **ForwardRenderer**: 遍历相机（按优先级排序）→ 收集可见 MeshRenderer → Blinn-Phong 光照 → 绘制
- EngineBase 主循环 step 11 调用 `ForwardRenderer::RenderFrame()`，step 3 窗口 resize 更新相机宽高比
- **验证**: 旋转立方体 + 透视相机 + 方向光 + Blinn-Phong 着色，稳定 60fps

---

## 5. 未完成 / 待开发

### 高优先级（核心功能缺失）

| 编号 | 功能 | 描述 | 依赖 |
|------|------|------|------|
| **F1** | 纹理加载 | stb_image 集成，支持 PNG/JPG 加载到 RHITexture | 需添加 stb_image 依赖 |
| **F2** | 模型加载 | Assimp 集成，FBX/OBJ/glTF 导入为 Mesh + Material | 需添加 Assimp 依赖 |
| **F3** | 点光/聚光灯渲染 | ForwardRenderer 当前仅支持方向光，需扩展 shader 支持多光源 | Phase 4 已有 Light 数据结构 |
| **F4** | 多光源支持 | 当前仅用第一个方向光，需支持多个光源的叠加 | F3 |
| **F5** | 渲染排序 | 按材质/深度排序 MeshRenderer，减少状态切换 | - |
| **F6** | Pipeline 缓存 | ForwardRenderer 每帧重建 Pipeline，需缓存复用 | - |

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

1. **Pipeline 每帧重建**: `ForwardRenderer::DrawMeshRenderer()` 中每次绘制都调用 `CreatePipeline()`，应缓存
2. **ForwardRenderer 中 uniform 设置时序**: 在 cmdBuffer 的 Begin/End 之间调用 `shader->SetUniform*()` 是直接的 OpenGL 调用（不经过命令缓冲区录制），这在 OpenGL 后端可以工作但不够干净
3. **GLM include 依赖 junction**: 构建系统通过 `mklink /J` junction 解决 GLM 的包含路径问题，首次构建时需要管理员权限或开发者模式
4. **静态注册表线程安全**: Camera/Light/MeshRenderer 的静态 `vector<T*>` 不是线程安全的
5. **stb_image / Assimp 未集成**: 纹理和模型加载功能缺失

---

## 7. 架构关键约定

### 主循环 14 步（EngineBase::MainLoop）

```
1. PollInput
2. Time::Update()
3. 窗口 resize → 更新 Camera 宽高比
4. DrainPendingObjects（Init/PostInit 新对象，循环直到无新增）
5. Tick 持久对象（EngineBase::persistentList_）
6. Tick 场景对象（AScene::objectList_）
7. PostTick 持久对象
8. PostTick 场景对象
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
AddComponent<T>() → 构造 → owner_ 赋值 → OnAttach()
[每帧] → Tick(dt) / PostTick(dt)（如果 enabled）
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
→ 下一帧开始 Tick
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
