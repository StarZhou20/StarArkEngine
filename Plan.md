# Plan: StarArk 3D Game Framework

## Overview
C++17/20 3D game framework, Unity-like architecture (AObject + Component composition), OpenGL rendering (GLEW) with RHI abstraction for future DX12. Blender as external scene editor, no built-in editor. Auto-maintained architecture docs for AI coding assistant readability.

## Key Decisions
- Language: C++17/20
- Build: CMake
- AI-Friendly: docs/DevLog.md (工程记录) + docs/API.md (API 文档) for AI coding assistants
- Component: GetComponent<T>() dynamic pattern (returns T* raw pointer, no ownership)
- Rendering: OpenGL (GLEW) first, RHI abstraction for DX12 future
- Transform: built-in on every AObject (mandatory, not a component)
- AObject identity: uint64_t id (auto-increment) + std::string name (user-settable), both with getters
- Memory: unique_ptr ownership (AScene owns AObject, AObject owns AComponent)
- Error handling: assert + ARK_LOG_FATAL (no exceptions)
- Scene switch: deferred to end of frame

## Architecture Layers

### Layer 0: Build & Dependencies
- CMake project with FetchContent/vcpkg
- Dependencies: GLFW (window), GLM (math), GLEW (OpenGL loader), Assimp (model loading), stb_image (textures), spdlog (logging)

### Layer 1: Platform Foundation
- Window management (GLFW)
- Input system (keyboard/mouse polling)
- Time management (delta time, frame timing)
- Logging (spdlog)
- File system utilities

### Layer 2: RHI (Rendering Hardware Interface)
- Abstract interfaces: RHIDevice, RHIBuffer, RHITexture, RHIShader, RHIPipeline, RHICommandBuffer
- RHICommandBuffer: abstract command recording, OpenGL backend executes immediately, DX12 backend defers to GPU queue
- OpenGL backend implementation (via GLEW)
- (Future: DX12 backend)

### Layer 3: Rendering
- Shader management & compilation
- Material system (shader + textures + uniform params)
- Mesh resource (vertex data, index data)
- Light system (Directional, Point, Spot as components)
- Forward rendering pipeline
- Render queue (sort by material/depth)

### Layer 4: Core Framework
- EngineBase: top-level owner, manages SceneManager + persistent objects (DontDestroy) + main loop. **Singleton**: `static EngineBase& Get()` for global access. Provides `GetSceneManager()`, `GetResourceManager()` etc. for AObject/AComponent to access engine services.
- SceneManager: owns current AScene, handles scene switching (hard switch MVP, extensible to additive)
- AScene: scene lifecycle (OnLoad/Tick/OnUnload), owns its AObjects, user inherits to create game scenes
- AObject: base class, lifecycle (Init/PostInit/Tick/PostTick/Destroy), component container, **built-in Transform** (mandatory), uint64_t id (auto-increment) + std::string name with getters, SetActive(bool)/IsActive() with hierarchy propagation (selfActive + activeInHierarchy), SetDontDestroy(bool) (immediately transfers to EngineBase, auto-detaches cross-boundary parent-child), isDestroyed flag, IObjectOwner* owner_ (points to AScene or EngineBase)
  - **Destroy cascade**: Destroy() is idempotent — if isDestroyed is already true, returns immediately (no-op). Otherwise, immediately recurses through Transform children, marking the entire subtree isDestroyed=true. Main loop step 12 only does memory reclamation (remove unique_ptrs). This prevents children from Ticking after parent is destroyed.
  - **activeInHierarchy propagation**: SetActive(bool) immediately recurses through Transform children, updating every descendant's activeInHierarchy = parentActiveInHierarchy && selfActive. This is a boolean propagation only, independent of Transform dirty flag / matrix calculation.
  - **SetDontDestroy(false) on persistent object**: no-op + ARK_LOG_WARN. Cannot transfer back to a scene.
  - **Object destruction cleanup**: Unified in AObject destructor — iterates all components and calls OnDetach() on each before they are destructed. This covers ALL destruction paths (step 12 reclamation, step 13 scene switch, PendingList cleanup) automatically. Step 12 only needs to remove unique_ptrs from ObjectList; the destructor handles OnDetach.
  - **RemoveComponent<T>()**: calls OnDetach() → removes unique_ptr from components_ → component destructs. Immediate, not deferred.
- AComponent: base class for all components (OnAttach/OnDetach/Tick/PostTick), SetEnabled(bool)/IsEnabled(), Tick order = insertion order
- Transform: position/rotation/scale, parent-child hierarchy, local/world matrix, dirty flag for deferred update (only dirty subtrees traversed) — NOT a component, built into AObject. Parent Destroy cascades to all children (immediate isDestroyed marking). SetDontDestroy auto-detaches cross-boundary parent-child.
  - **SetParent() rules**: (1) assert that child->owner_ == parent->owner_ — cross-boundary parent-child is forbidden. (2) After reparenting, immediately recalculate and propagate activeInHierarchy for the moved subtree based on new parent's activeInHierarchy.
  - **Destructor safety**: Transform destructor does bidirectional cleanup — removes self from parent's children_ list (`if (parent_) parent_->RemoveChild(this)`) and nullifies all children's parent_ pointer (`for (child : children_) child->parent_ = nullptr`). This makes step 12 destruction order irrelevant.
- IObjectOwner: interface implemented by AScene and EngineBase. Methods: TransferToPersistent(AObject*). SetDontDestroy(true) calls owner_->TransferToPersistent(this) — owner (AScene) internally searches both ObjectList AND PendingList for the unique_ptr, releases it, and moves it to EngineBase. **Target list is determined by source**: from ObjectList → EngineBase.PersistentList; from PendingList → EngineBase.PendingList (ensures Init runs before Tick). AScene holds an EngineBase* (injected via SceneManager at construction). EngineBase's TransferToPersistent is a no-op (already persistent). **QueueDestroy removed** — Destroy() only marks isDestroyed on the subtree, does not interact with owner. Step 12 scans by flag. (Implementation optimization: Destroy() sets owner's hasDestroyedObjects_=true dirty flag to skip scan when no objects were destroyed.)
- MeshRender: component, model loading (FBX/OBJ via Assimp), material assignment
- Camera: component, projection, view matrix, int priority (higher renders later/on top), registers to static list on OnAttach, unregisters on OnDetach. static GetAllCameras() returns sorted by priority. No camera → skip render
- Light: component, directional/point/spot light data, registers to static list on OnAttach, unregisters on OnDetach. static GetAllLights() for render pipeline.

#### Scene Lifecycle & Object Ownership
- AObject is owned by the AScene that created it (via AScene::CreateObject<T>())
- Each AScene has its own ObjectList, PendingList (DestroyedList removed — isDestroyed flag is source of truth, step 12 scans ObjectList)
- AScene lifecycle: OnLoad() → [per-frame: Tick(dt)] → OnUnload()
- OnLoad(): one-time setup, create initial objects
- Tick(dt): scene-level orchestration (win/lose check, trigger scene switch)
- OnUnload(): cleanup before switching away
- Object behavior logic goes in AObject::Tick() or AComponent::Tick(), NOT in AScene

#### Scene Switching
- SceneManager::LoadScene<T>() — request scene switch (deferred to end of frame)
- Switch sequence: current.OnUnload() → destroy all scene objects (ObjectList + PendingList; PendingList objects are directly destructed without calling Init/Destroy lifecycle, DontDestroy objects already in EngineBase) → delete old scene → create new scene → new scene.OnLoad() → **immediately** Init/PostInit new scene objects → next frame they can Tick
- DontDestroyOnSceneSwitch: calling AObject::SetDontDestroy(true) immediately transfers ownership from AScene to EngineBase's PersistentList
- Persistent objects owned by EngineBase directly, survive all scene switches

#### Main Loop (owned by EngineBase)
1. Poll Input
2. Time::Update() — calculate deltaTime, totalTime
3. Check window resize flag → update viewport + Camera aspect ratios
4. Init/PostInit new objects: **drain loop** — while active scene's PendingList or EngineBase's PendingList is non-empty, take current batch, move to respective ObjectLists, call Init/PostInit on each. **Filter**: skip objects with isDestroyed==true (directly destruct them without calling Init; AObject destructor handles OnDetach cleanup). Objects created during Init/PostInit enter PendingList and are processed in the next iteration of the drain loop.
5. Tick persistent objects (EngineBase's PersistentList) — skip isDestroyed and !activeInHierarchy
6. Active scene's Tick all objects (AObject::Tick + AComponent::Tick in insertion order) — skip isDestroyed and !activeInHierarchy, skip !isEnabled components
7. PostTick persistent objects — skip isDestroyed and !activeInHierarchy
8. Active scene's PostTick all objects (AObject::PostTick + AComponent::PostTick) — skip isDestroyed and !activeInHierarchy, skip !isEnabled components
9. Active scene's Tick(dt) — scene-level logic
10. UpdateTransforms — only traverse dirty root nodes and their subtrees, propagate WorldMatrix
11. Render (find cameras from static list sorted by priority, **filter out isDestroyed and !activeInHierarchy owners**; for each valid camera: collect MeshRenders from active scene + persistent objects (**filter out isDestroyed/!activeInHierarchy/!isEnabled**), collect lights from static list (**same filter**), sort, draw. No valid camera → skip render)
12. Destroy queued objects: scan ObjectList (scene + EngineBase persistent) and remove all entries where isDestroyed==true. Removing the unique_ptr triggers AObject destructor → OnDetach for all components. DestroyedList is no longer needed — isDestroyed flag on objects is the source of truth.
13. Process pending scene switch (if LoadScene was called during this frame): OnUnload → destroy scene objects → delete old scene → create new → OnLoad → **drain PendingList loop** (same as step 4: while PendingList non-empty, Init/PostInit each batch, filter isDestroyed)
14. Swap buffers

#### Entry Point
- `EngineBase::Run<FirstScene>()` — template parameter specifies the first scene. Internally calls `LoadScene<FirstScene>()` (executed immediately, not deferred), then enters the main loop. This is the only way to start the engine; there is no parameterless `Run()`.

#### Memory Management
- AScene holds `std::vector<std::unique_ptr<AObject>>` — scene owns objects
- AObject holds `std::vector<std::unique_ptr<AComponent>>` — object owns components
- `GetComponent<T>()` returns `T*` (raw pointer, no ownership transfer)
- `AScene::CreateObject<T>()` returns `T*` (raw pointer, scene retains ownership)
- `AObject::AddComponent<T>()` returns `T*` (raw pointer, object retains ownership)

#### Error Handling
- assert() for programming errors (null pointers, invalid state)
- ARK_LOG_FATAL + abort for unrecoverable runtime errors
- No exceptions used anywhere

## DebugListenBus System Design

### Overview
专用调试监听总线，基于 Observer 模式。全局单例广播日志消息，所有注册的 IDebugListener 全量接收。spdlog 作为文件写入后端。

### Core Types

#### LogLevel 枚举
`Trace, Debug, Info, Warning, Error, Fatal` — 6 级日志

#### LogMessage 结构体
- `LogLevel level` — 日志级别
- `std::string category` — 分类频道（"Core", "Rendering", "Resource" 等）
- `std::string message` — 日志内容
- `std::string timestamp` — 时间戳（ISO 8601）
- `std::source_location location` — C++20 源码位置（文件名、行号、函数名）

#### IDebugListener 接口
纯虚接口，继承后在构造/析构中自动向 DebugListenBus 注册/注销（RAII）。
- `virtual void OnDebugMessage(const LogMessage& msg) = 0;`
- 构造函数调用 `DebugListenBus::Get().RegisterListener(this)`
- 析构函数调用 `DebugListenBus::Get().UnregisterListener(this)`

#### DebugListenBus 单例
- `static DebugListenBus& Get()` — 单例访问
- `void RegisterListener(IDebugListener*)` — 注册监听者
- `void UnregisterListener(IDebugListener*)` — 注销监听者
- `void Broadcast(LogLevel, const std::string& category, const std::string& message, std::source_location)` — 广播消息
- 静态便捷宏: `ARK_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL(category, message)`
- 内部 `std::vector<IDebugListener*>` + `std::mutex` 保证线程安全

#### 内置监听者实现

**FileDebugListener**
- 使用 spdlog rotating_file_sink（10MB 单文件，保留 5 个历史文件）
- 日志格式: `[2026-04-17 10:30:45.123] [INFO] [Core] Engine initialized (Engine.cpp:42)`

**ConsoleDebugListener**
- 使用 spdlog stdout_color_sink（带颜色）
- Warning 及以上输出到 stderr

## Implementation Phases

### Phase 1: Foundation (blocks all others)
1. CMake project setup with dependency management (GLFW, GLM, GLEW via vcpkg)
2. Window creation with GLFW + OpenGL context (GLEW init)
3. Input system (basic keyboard/mouse polling)
4. Time management (delta time)
5. DebugListenBus + IDebugListener + FileDebugListener + ConsoleDebugListener (spdlog backend)
6. ARK_LOG_* macros integrated
7. **Verify**: Window opens, renders clear color, logs frame time to console + logs/ folder

### Phase 2: RHI + Basic Rendering (depends on Phase 1)
### Phase 3: Core Framework (parallel with Phase 2 for non-rendering parts)
### Phase 4: 3D Rendering Pipeline (depends on Phase 2 + 3)
### Phase 5: Documentation (贯穿所有阶段)
