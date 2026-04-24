# StarArk API 参考文档

> **命名空间**: `ark::`  
> **C++ 标准**: C++20  
> **头文件路径约定**: `#include "engine/模块/文件.h"`

---

## 目录

- [1. 引擎入口 — EngineBase](#1-引擎入口--enginebase)
- [2. 场景系统](#2-场景系统)
  - [2.1 SceneManager](#21-scenemanager)
  - [2.2 AScene](#22-ascene)
- [3. 对象系统](#3-对象系统)
  - [3.1 AObject](#31-aobject)
  - [3.2 AComponent](#32-acomponent)
  - [3.3 Transform](#33-transform)
- [4. 渲染组件](#4-渲染组件)
  - [4.1 Camera](#41-camera)
  - [4.2 Light](#42-light)
  - [4.3 Mesh](#43-mesh)
  - [4.4 Material](#44-material)
  - [4.5 MeshRenderer](#45-meshrenderer)
  - [4.6 ForwardRenderer](#46-forwardrenderer)
  - [4.7 OrbitCamera](#47-orbitcamera)
  - [4.8 TextureLoader](#48-textureloader)
  - [4.9 ModelLoader](#49-modelloader)
  - [4.10 ShaderSources](#410-shadersources)
  - [4.11 ShaderManager](#411-shadermanager)
  - [4.12 PostProcess](#412-postprocess)
  - [4.13 Skybox](#413-skybox)
  - [4.14 IBL](#414-ibl)
  - [4.15 RenderSettings](#415-rendersettings)
  - [4.16 ShadowMap](#416-shadowmap)
  - [4.17 SceneSerializer](#417-sceneserializer)
- [5. 平台层](#5-平台层)
  - [5.1 Window](#51-window)
  - [5.2 Input](#52-input)
  - [5.3 Time](#53-time)
- [6. 调试日志](#6-调试日志)
  - [6.1 DebugListenBus & 日志宏](#61-debuglistenbus--日志宏)
  - [6.2 IDebugListener](#62-idebuglistener)
  - [6.3 ConsoleDebugListener](#63-consoledebuglistener)
  - [6.4 FileDebugListener](#64-filedebuglistener)
- [7. RHI 渲染硬件接口](#7-rhi-渲染硬件接口)
  - [7.1 RHIDevice](#71-rhidevice)
  - [7.2 RHIBuffer](#72-rhibuffer)
  - [7.3 RHIShader](#73-rhishader)
  - [7.4 RHITexture](#74-rhitexture)
  - [7.5 RHIPipeline & PipelineDesc](#75-rhipipeline--pipelinedesc)
  - [7.6 RHICommandBuffer](#76-rhicommandbuffer)
  - [7.7 RHITypes](#77-rhitypes)
- [8. 用法示例](#8-用法示例)

---

## 1. 引擎入口 — EngineBase

**头文件**: `#include "engine/core/EngineBase.h"`

全局引擎单例，管理主循环、窗口、RHI 设备、场景管理器、渲染器和持久对象。

### 静态方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `EngineBase::Get()` | `EngineBase&` | 获取全局唯一实例 |

### 公开方法

| 方法 | 说明 |
|------|------|
| `Run<FirstScene>(int w, int h, const std::string& title)` | 启动引擎并进入主循环。`FirstScene` 必须继承 `AScene`。阻塞直到窗口关闭 |
| `GetWindow()` | 返回 `Window*` |
| `GetRHIDevice()` | 返回 `RHIDevice*` |
| `GetSceneManager()` | 返回 `SceneManager*` |
| `GetRenderer()` | 返回 `ForwardRenderer*` |
| `AcceptPersistentObject(unique_ptr<AObject>, bool needsInit)` | 内部方法，接受从场景转移来的持久对象 |

### 主循环（14 步）

引擎每帧按以下顺序执行：

1. `Window::PollEvents()` + `Input::Update()`
2. `Time::Update()`
3. 窗口 resize 检查 → 更新所有 Camera 宽高比
4. `DrainPendingObjects()` — 初始化本帧新建的对象（循环直到无新增）
5. Loop 持久对象（`persistentList_`）
6. Loop 场景对象（`AScene::objectList_`）
7. PostLoop 持久对象
8. PostLoop 场景对象
9. `AScene::Tick(dt)` — 场景级逻辑
10. 更新脏 Transform 的世界矩阵
11. `ForwardRenderer::RenderFrame()` — 渲染所有相机
12. 销毁标记为 `isDestroyed` 的对象
13. 处理延迟场景切换
14. `Window::SwapBuffers()`

---

## 2. 场景系统

### 2.1 SceneManager

**头文件**: `#include "engine/core/SceneManager.h"`

管理当前活动场景和场景切换。场景切换在帧末执行（延迟切换）。

| 方法 | 说明 |
|------|------|
| `LoadScene<T>()` | 延迟加载场景（在当前帧结束时执行切换）。`T` 必须继承 `AScene` |
| `LoadSceneImmediate<T>()` | 立即加载场景（仅用于首场景）。内部方法 |
| `GetActiveScene()` | 返回 `AScene*`，当前活动场景 |
| `HasPendingSwitch()` | 返回 `bool`，是否有待处理的场景切换 |
| `ProcessPendingSwitch()` | 执行延迟切换。内部方法，由 EngineBase 调用 |

### 2.2 AScene

**头文件**: `#include "engine/core/AScene.h"`

场景基类。继承此类来创建自定义场景。

#### 可重写方法

| 方法 | 调用时机 | 说明 |
|------|----------|------|
| `OnLoad()` | 场景加载后立即调用 | 在此创建初始对象 |
| `OnUnload()` | 场景卸载前调用 | 清理场景级资源 |
| `Tick(float dt)` | 每帧调用一次（步骤 9） | 场景级逻辑 |

#### 公开方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `CreateObject<T>(Args...)` | `T*` | 创建 AObject 派生对象。`T` 必须继承 `AObject`。返回裸指针，场景保留所有权 |
| `GetEngine()` | `EngineBase*` | 获取引擎实例 |

#### 使用示例

```cpp
class MyScene : public ark::AScene {
public:
    void OnLoad() override {
        CreateObject<PlayerObject>();
        CreateObject<EnemyObject>();
    }
    void Tick(float dt) override {
        // 场景级逻辑
    }
};
```

---

## 3. 对象系统

### 3.1 AObject

**头文件**: `#include "engine/core/AObject.h"`

游戏对象基类。拥有唯一 ID、名称、内置 Transform 和组件系统。

#### 可重写生命周期方法

| 方法 | 调用时机 | 说明 |
|------|----------|------|
| `PreInit()` | DrainPendingObjects 期间，Init 之前 | 最早的初始化阶段，适合设置基本属性 |
| `Init()` | DrainPendingObjects 期间 | 初始化（创建组件、设置 Transform） |
| `PostInit()` | Init 之后 | 可在此安全引用其他已初始化的对象 |
| `Loop(float dt)` | 每帧调用 | 逻辑更新 |
| `PostLoop(float dt)` | Loop 之后 | 延后逻辑（如相机跟随） |
| `OnDestroy()` | Destroy() 被调用时 | 清理自定义资源 |

#### 身份 & 状态

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetId()` | `uint64_t` | 全局唯一自增 ID |
| `GetName()` | `const std::string&` | 对象名称 |
| `SetName(const std::string&)` | `void` | 设置名称 |
| `SetActive(bool)` | `void` | 设置激活状态（影响 Tick 和渲染） |
| `IsSelfActive()` | `bool` | 自身是否激活 |
| `IsActiveInHierarchy()` | `bool` | 层级中是否激活（父节点也激活才为 true） |

#### Transform

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetTransform()` | `Transform&` | 获取内置变换组件（引用） |

#### 组件系统

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `AddComponent<T>(Args...)` | `T*` | 添加组件并返回裸指针。`T` 必须继承 `AComponent`。立即调用 `OnAttach()` |
| `GetComponent<T>()` | `T*` | 查找第一个 T 类型的组件。找不到返回 `nullptr` |
| `RemoveComponent<T>()` | `void` | 移除第一个 T 类型的组件。调用 `OnDetach()` 后销毁 |
| `GetComponents()` | `const vector<unique_ptr<AComponent>>&` | 获取所有组件 |

#### 销毁 & 持久化

| 方法 | 说明 |
|------|------|
| `Destroy()` | 标记销毁（帧末实际移除）。级联销毁所有子节点 |
| `IsDestroyed()` | 是否已标记销毁 |
| `SetDontDestroy(bool)` | 设为 true 将对象从场景转移到 EngineBase 持久列表，场景切换时不被销毁 |
| `IsDontDestroy()` | 是否为持久对象 |

### 3.2 AComponent

**头文件**: `#include "engine/core/AComponent.h"`

组件基类。所有自定义组件继承此类。

#### 可重写方法

| 方法 | 说明 |
|------|------|
| `OnAttach()` | 组件被添加到对象后调用 |
| `OnDetach()` | 组件被移除或对象销毁时调用 |
| `PreInit()` | OnAttach 之后、Init 之前调用 |
| `Init()` | PreInit 之后调用 |
| `PostInit()` | Init 之后调用 |
| `Loop(float dt)` | 每帧更新（仅当 enabled） |
| `PostLoop(float dt)` | Loop 后更新（仅当 enabled） |

#### 公开方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `SetEnabled(bool)` | `void` | 启用/禁用组件 |
| `IsEnabled()` | `bool` | 是否启用 |
| `GetOwner()` | `AObject*` | 获取所属对象 |

#### 使用示例

```cpp
class Rotator : public ark::AComponent {
public:
    void Loop(float dt) override {
        auto& t = GetOwner()->GetTransform();
        angle_ += speed_ * dt;
        t.SetLocalRotation(glm::angleAxis(angle_, glm::vec3(0, 1, 0)));
    }
private:
    float speed_ = glm::radians(90.0f);
    float angle_ = 0.0f;
};
```

### 3.3 Transform

**头文件**: `#include "engine/core/Transform.h"`

每个 AObject 内置一个 Transform。管理位置、旋转（四元数）、缩放和父子层级。使用脏标记系统延迟计算世界矩阵。

#### 局部变换

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetLocalPosition()` | `const glm::vec3&` | 局部位置 |
| `SetLocalPosition(const glm::vec3&)` | `void` | 设置局部位置 |
| `GetLocalRotation()` | `const glm::quat&` | 局部旋转（四元数） |
| `SetLocalRotation(const glm::quat&)` | `void` | 设置局部旋转 |
| `GetLocalScale()` | `const glm::vec3&` | 局部缩放 |
| `SetLocalScale(const glm::vec3&)` | `void` | 设置局部缩放 |

#### 世界变换

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetWorldPosition()` | `glm::vec3` | 世界空间位置 |
| `GetWorldMatrix()` | `const glm::mat4&` | 世界变换矩阵（脏标记自动重算） |

#### 层级

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetOwner()` | `AObject*` | 所属对象 |
| `GetParent()` | `Transform*` | 父 Transform（根节点为 nullptr） |
| `GetChildren()` | `const vector<Transform*>&` | 子 Transform 列表 |
| `SetParent(Transform*)` | `void` | 设置父节点（nullptr = 解除父子关系） |
| `AddChild(Transform*)` | `void` | 添加子节点（等同于 `child->SetParent(this)`） |
| `RemoveChild(Transform*)` | `void` | 移除子节点 |

#### 脏标记

| 方法 | 说明 |
|------|------|
| `IsDirty()` | 世界矩阵是否需要重算 |
| `MarkDirty()` | 手动标记脏（通常自动触发） |
| `UpdateWorldMatrix()` | 强制立即重算世界矩阵 |

> **注意**: 修改 `SetLocalPosition/Rotation/Scale` 会自动 `MarkDirty()`，EngineBase 在主循环步骤 10 统一更新。

---

## 4. 渲染组件

### 4.1 Camera

**头文件**: `#include "engine/rendering/Camera.h"`

相机组件。继承 `AComponent`，OnAttach 时自动注册到全局相机列表。

#### 投影设置

| 方法 | 说明 |
|------|------|
| `SetPerspective(float fovDeg, float nearClip, float farClip)` | 设置透视投影 |
| `SetOrthographic(float size, float nearClip, float farClip)` | 设置正交投影 |
| `SetAspectRatio(float aspect)` | 设置宽高比（引擎在窗口 resize 时自动调用） |

#### 矩阵获取

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetProjectionMatrix()` | `const glm::mat4&` | 投影矩阵（脏标记自动重算） |
| `GetViewMatrix()` | `glm::mat4` | 视图矩阵（= 世界矩阵的逆） |

#### 属性

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetFOV()` | `float` | 视场角（度） |
| `GetNearClip()` | `float` | 近裁剪面 |
| `GetFarClip()` | `float` | 远裁剪面 |
| `GetAspectRatio()` | `float` | 当前宽高比 |
| `GetPriority()` | `int` | 渲染优先级 |
| `SetPriority(int)` | `void` | 设置优先级（数值越大越后渲染） |
| `GetClearColor()` | `const glm::vec4&` | 清屏颜色 |
| `SetClearColor(const glm::vec4&)` | `void` | 设置清屏颜色 |

#### 静态方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Camera::GetAllCameras()` | `const vector<Camera*>&` | 获取所有已注册相机 |

### 4.2 Light

**头文件**: `#include "engine/rendering/Light.h"`

光源组件。继承 `AComponent`，OnAttach 自动注册。

#### 枚举

```cpp
enum class Light::Type { Directional, Point, Spot };
```

#### 属性方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `GetType()` / `SetType(Type)` | `Type` | 光源类型 |
| `GetColor()` / `SetColor(const glm::vec3&)` | `const glm::vec3&` | 光源颜色 |
| `GetIntensity()` / `SetIntensity(float)` | `float` | 光源强度 |
| `GetRange()` / `SetRange(float)` | `float` | 衰减范围（仅 Point/Spot） |
| `GetSpotInnerAngle()` | `float` | 聚光灯内锥角（度） |
| `GetSpotOuterAngle()` | `float` | 聚光灯外锥角（度） |
| `SetSpotAngles(float innerDeg, float outerDeg)` | `void` | 设置聚光灯锥角 |
| `GetAmbient()` / `SetAmbient(const glm::vec3&)` | `const glm::vec3&` | 环境光贡献 |

#### 静态方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Light::GetAllLights()` | `const vector<Light*>&` | 获取所有已注册光源 |

> **当前限制**: ~~ForwardRenderer 仅使用第一个方向光，尚未支持多光源和点光/聚光灯渲染~~ → 已支持多光源（4 方向光 + 8 点光 + 4 聚光灯）。

### 4.3 Mesh

**头文件**: `#include "engine/rendering/Mesh.h"`

网格资源，持有顶点和索引数据，可上传到 GPU。

#### 构建方法

| 方法 | 说明 |
|------|------|
| `SetVertices(const void* data, size_t sizeBytes, const VertexLayout& layout)` | 设置顶点数据 |
| `SetIndices(const uint32_t* data, size_t indexCount)` | 设置索引数据 |
| `Upload(RHIDevice* device)` | 上传顶点和索引缓冲区到 GPU |

#### 查询方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `IsUploaded()` | `bool` | 是否已上传到 GPU |
| `GetVertexBuffer()` | `RHIBuffer*` | 顶点缓冲区 |
| `GetIndexBuffer()` | `RHIBuffer*` | 索引缓冲区 |
| `GetVertexLayout()` | `const VertexLayout&` | 顶点布局描述 |
| `GetVertexCount()` | `uint32_t` | 顶点数 |
| `GetIndexCount()` | `uint32_t` | 索引数 |
| `HasIndices()` | `bool` | 是否有索引 |

#### 静态工厂方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Mesh::CreateCube()` | `unique_ptr<Mesh>` | 创建单位立方体（含法线、UV、tangent） |
| `Mesh::CreatePlane(float size = 10.0f)` | `unique_ptr<Mesh>` | 创建地面平面（含 tangent） |
| `Mesh::CreateSphere(int sectors = 36, int stacks = 18)` | `unique_ptr<Mesh>` | 创建 UV 球体（Y-up，含 tangent） |

> **注意**: `CreateCube()` / `CreatePlane()` / `CreateSphere()` 只创建 CPU 端数据，需调用 `Upload(device)` 上传到 GPU。
>
> **顶点布局（Phase 9 起统一）**: 所有内置 Primitive 使用 44 字节 `PrimVert { vec3 position; vec3 normal; vec2 uv; vec3 tangent; }`，对应着色器 attribute location 0/1/2/3。`ModelLoader` 也产出相同布局。

### 4.4 Material

**头文件**: `#include "engine/rendering/Material.h"`

材质，持有 shader 引用和着色参数。

#### 属性方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `SetShader(shared_ptr<RHIShader>)` | `void` | 设置着色器 |
| `GetShader()` | `RHIShader*` | 获取着色器 |
| `SetColor(const glm::vec4&)` | `void` | 设置漫反射颜色 |
| `GetColor()` | `const glm::vec4&` | 获取漫反射颜色 |
| `SetSpecular(const glm::vec3&)` | `void` | 设置高光颜色 |
| `GetSpecular()` | `const glm::vec3&` | 获取高光颜色 |
| `SetShininess(float)` | `void` | 设置高光指数 |
| `GetShininess()` | `float` | 获取高光指数 |
| `SetMetallic(float)` | `void` | 设置金属度 (PBR) |
| `GetMetallic()` | `float` | 获取金属度 |
| `SetRoughness(float)` | `void` | 设置粗糙度 (PBR) |
| `GetRoughness()` | `float` | 获取粗糙度 |
| `SetAO(float)` | `void` | 设置环境光遮蔽 (PBR) |
| `GetAO()` | `float` | 获取环境光遮蔽 |
| `SetPBR(bool)` | `void` | 启用/禁用 PBR 模式 |
| `IsPBR()` | `bool` | 是否启用 PBR |
| `SetDiffuseTexture(shared_ptr<RHITexture>)` | `void` | 设置漫反射/Base Color 纹理（unit 0） |
| `GetDiffuseTexture()` | `RHITexture*` | 获取漫反射纹理 |
| `SetNormalTexture(shared_ptr<RHITexture>)` | `void` | 设置切线空间法线贴图（unit 1） |
| `SetMetallicRoughnessTexture(shared_ptr<RHITexture>)` | `void` | 设置 glTF MR 贴图（unit 2，G=roughness / B=metallic） |
| `SetAOTexture(shared_ptr<RHITexture>)` | `void` | 设置 AO 贴图（unit 3，R 通道） |
| `SetEmissiveTexture(shared_ptr<RHITexture>)` | `void` | 设置自发光贴图（unit 4） |
| `SetEmissive(const glm::vec3&)` | `void` | 设置自发光颜色（与贴图相乘） |
| `GetEmissive()` | `const glm::vec3&` | 获取自发光颜色 |

#### 渲染方法

| 方法 | 说明 |
|------|------|
| `Bind()` | 将材质参数绑定到 shader uniform（`uMaterial.color` / `specular` / `shininess` / `metallic` / `roughness` / `ao` / `emissive` / `hasDiffuseTex` / `hasNormalTex` / `hasMetalRoughTex` / `hasAOTex` / `hasEmissiveTex`）；按需把 5 张贴图绑定到 unit 0–4，对应 sampler 为 `uDiffuseTex` / `uNormalTex` / `uMetalRoughTex` / `uAOTex` / `uEmissiveTex` |

> **sRGB 约定**: Base Color / Emissive 纹理应以 sRGB 采样；Normal / MR / AO 必须线性。由 `TextureLoader::Load(..., bool isSRGB)` 控制。

### 4.5 MeshRenderer

**头文件**: `#include "engine/rendering/MeshRenderer.h"`

网格渲染器组件。持有 Mesh 和 Material（shared_ptr），OnAttach 自动注册。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `SetMesh(shared_ptr<Mesh>)` | `void` | 设置网格 |
| `GetMesh()` | `Mesh*` | 获取网格 |
| `SetMaterial(shared_ptr<Material>)` | `void` | 设置材质 |
| `GetMaterial()` | `Material*` | 获取材质 |

#### 静态方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `MeshRenderer::GetAllRenderers()` | `const vector<MeshRenderer*>&` | 获取所有已注册渲染器 |

### 4.6 ForwardRenderer

**头文件**: `#include "engine/rendering/ForwardRenderer.h"`

前向渲染器。由 EngineBase 创建和拥有。

| 方法 | 说明 |
|------|------|
| `ForwardRenderer(RHIDevice*)` | 构造（传入 RHI 设备）；内部创建 `ShaderManager`、`PostProcess`、`Skybox`、`IBL` |
| `RenderFrame(Window*)` | 渲染一帧：`ShaderManager::CheckHotReload()` → 首帧 IBL 烘焙 → `PostProcess::BeginScene()` → 遍历相机渲染到 HDR FBO → `PostProcess::Apply()`（曝光 + ACES 合成）|
| `GetRenderSettings()` | `RenderSettings&` — 直接修改所有渲染参数（曝光/bloom/sky/ibl），Phase M10 将整体序列化 |
| `SetExposure(float)` / `GetExposure()` | 便捷访问 `settings_.exposure` |
| `GetShaderManager()` | `ShaderManager*` |
| `GetPostProcess()` | `PostProcess*` |
| `GetSkybox()` | `Skybox*` — 天空盒（Phase 11） |
| `GetIBL()` | `IBL*` — 图像光照探针（Phase 12） |
| `RebakeIBL()` | 手动重新烘焙 IBL（切换 Skybox 后调用） |
| `GetShadowMap()` | `ShadowMap*` — 方向光 shadow map（Phase 13） |
| `SetBloomEnabled/Threshold/Strength/Iterations(...)` | 便捷访问 `settings_.bloom.*`（与旧 API 兼容） |

#### 内部优化

- **Pipeline 缓存**: 使用 FNV-1a 哈希缓存 `(shader, vertexLayout, topology, depth, blend)` → `RHIPipeline*`，避免每帧重建 VAO
- **渲染排序**: 可见 MeshRenderer 先按 shader 指针、再按 material 指针排序，减少 GPU 状态切换

#### Shader Uniform 约定

ForwardRenderer 在绘制时设置以下 uniform：

| Uniform | 类型 | 说明 |
|---------|------|------|
| `uMVP` | `mat4` | Model-View-Projection 矩阵 |
| `uModel` | `mat4` | 模型矩阵 |
| `uNormalMatrix` | `mat4` | 法线变换矩阵（inverse transpose of model） |
| `uCameraPos` | `vec3` | 相机世界空间位置 |
| `uLight.direction` | `vec3` | 光源方向（从 Transform 的 forward 计算） |
| `uLight.color` | `vec3` | 光源颜色 × 强度 |
| `uLight.ambient` | `vec3` | 环境光 |

> **注意**: 以上单光源 uniform 已废弃，新版本使用多光源 uniform 数组（`uDirLights[i]`、`uPointLights[i]`、`uSpotLights[i]` + `uNumDirLights`、`uNumPointLights`、`uNumSpotLights`）。详见 `engine/rendering/ShaderSources.h`。
| `uMaterial.color` | `vec4` | 由 `Material::Bind()` 设置 |
| `uMaterial.specular` | `vec3` | 由 `Material::Bind()` 设置 |
| `uMaterial.shininess` | `float` | 由 `Material::Bind()` 设置 |
| `uMaterial.hasDiffuseTex` / `hasNormalTex` / `hasMetalRoughTex` / `hasAOTex` / `hasEmissiveTex` | `int` | 由 `Material::Bind()` 设置 |
| `uExposure` | `float` | 【历史遗留】在场景 shader 中作为 uniform 占位（仍会写入）；自 Phase 10 起真正的曝光系数由 `PostProcess` composite 着色器消费。 |

> **色彩流水线（Phase 10 更新）**: 场景 shader 输出线性 HDR 到 RGBA16F FBO → `PostProcess::Apply()` 做亮度提取 + 高斯 ping-pong + `composite = ACES((scene + bloom*strength) * exposure)` → 写入默认 FB（`GL_FRAMEBUFFER_SRGB` 自动完成最终 Gamma）。PBR/Phong 片段着色器内**不再**做 tonemap 也不再乘 `uExposure`。点光/聚光使用 `1/dist² * smoothstep` 的物理衰减。

### 4.7 OrbitCamera

**头文件**: `#include "engine/rendering/OrbitCamera.h"`

轨道相机控制器组件，围绕目标点旋转/缩放/平移。需配合 `Camera` 组件一起使用。

#### 配置方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `SetTarget(const glm::vec3&)` | `void` | 设置观察目标点 |
| `SetDistance(float d)` | `void` | 设置与目标的距离 |
| `SetYaw(float deg)` | `void` | 设置水平偏航角（度） |
| `SetPitch(float deg)` | `void` | 设置垂直俯仰角（度） |
| `SetSensitivity(float s)` | `void` | 设置旋转灵敏度 |
| `SetZoomSpeed(float s)` | `void` | 设置滚轮缩放速度 |
| `SetPanSpeed(float s)` | `void` | 设置平移速度 |
| `SetDistRange(float min, float max)` | `void` | 设置缩放距离范围 |
| `SetPitchRange(float min, float max)` | `void` | 设置俯仰角范围（度） |

#### 交互

- **右键拖拽**: 旋转（yaw/pitch）
- **滚轮**: 缩放（前进/后退）
- **中键拖拽**: 平移目标点

### 4.8 TextureLoader

**头文件**: `#include "engine/rendering/TextureLoader.h"`

从图片文件加载纹理到 GPU（基于 stb_image）。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `TextureLoader::Load(RHIDevice* device, const string& filepath, bool isSRGB = true)` | `shared_ptr<RHITexture>` | 加载图片（PNG/JPG/BMP/TGA），返回 GPU 纹理；失败返回 nullptr |

> **isSRGB 语义**: `true` → 使用 `TextureFormat::sRGB_Auto`，GPU 采样时自动线性化（适合 BaseColor/Emissive）；`false` → `TextureFormat::Linear`（适合 Normal/MR/AO/HeightMap 等数据贴图）。
>
> **注意**: 自动翻转 Y 轴以适配 OpenGL 坐标系；启用 16× 各向异性过滤（若 `GL_EXT_texture_filter_anisotropic` 可用）。

### 4.9 ModelLoader

**头文件**: `#include "engine/rendering/ModelLoader.h"`

从 3D 模型文件（OBJ/FBX/glTF）加载网格和材质（基于 Assimp）。

#### ModelNode 结构

```cpp
struct ModelNode {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
};
```

#### 静态方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `ModelLoader::Load(RHIDevice* device, shared_ptr<RHIShader> shader, const string& filepath)` | `vector<ModelNode>` | 加载模型文件，返回所有子网格+材质。失败返回空 vector |

#### 自动处理

- 三角化（aiProcess_Triangulate）
- 自动生成法线（aiProcess_GenNormals）
- 翻转 UV（aiProcess_FlipUVs）
- 计算切线空间（aiProcess_CalcTangentSpace）
- 漫反射颜色、高光颜色、光泽度自动提取
- 漫反射纹理路径相对模型文件目录自动解析

### 4.10 ShaderSources

**头文件**: `#include "engine/rendering/ShaderSources.h"`

内置 GLSL 450 着色器源码字符串，作为磁盘 `engine/shaders/*.vert|*.frag` 不存在时的 **fallback**。生产代码应通过 `ShaderManager::Get(name)` 获取，不直接引用这些常量。

| 常量 | 对应磁盘文件 |
|------|---------------|
| `ark::kPhongVS` / `kPhongFS` | `engine/shaders/phong.vert` / `.frag` |
| `ark::kPBR_VS` / `kPBR_FS` | `engine/shaders/pbr.vert` / `.frag` |

#### 共享 uniform 接口

- `uMVP`, `uModel`, `uNormalMatrix`, `uCameraPos`, `uExposure`
- `uMaterial.*`（color / specular / shininess / metallic / roughness / ao / emissive / hasDiffuseTex / hasNormalTex / hasMetalRoughTex / hasAOTex / hasEmissiveTex）
- `uDiffuseTex` (unit 0), `uNormalTex` (1), `uMetalRoughTex` (2), `uAOTex` (3), `uEmissiveTex` (4)
- `uDirLights[MAX_DIR_LIGHTS]`, `uPointLights[MAX_POINT_LIGHTS]`, `uSpotLights[MAX_SPOT_LIGHTS]` + 计数 uniform

### 4.11 ShaderManager

**头文件**: `#include "engine/rendering/ShaderManager.h"`

着色器资源管理器。每个 `ForwardRenderer` 持有一个实例，负责从磁盘加载 `engine/shaders/*.vert|*.frag`，缓存 `shared_ptr<RHIShader>`，并在 Debug 下做 mtime 轮询热重载。

#### 接口

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `ShaderManager(RHIDevice*)` | — | 构造（由 `ForwardRenderer` 调用） |
| `Get(const string& name)` | `shared_ptr<RHIShader>` | 按名字加载 / 取缓存。`name` 如 `"pbr"`、`"phong"`；解析路径为 `Paths::ResolveContent("shaders/" + name + ".vert")` 和 `.frag` |
| `CheckHotReload()` | `void` | 对所有 `fromDisk` 的条目对比 `last_write_time`；有变化时在**原 `RHIShader` 对象上**重新 `Compile()`（编译失败保留旧 program），因此外部持有的 `shared_ptr` 无需更换 |
| `SetHotReloadEnabled(bool)` / `IsHotReloadEnabled()` | `void` / `bool` | 运行时开关；默认 Debug=ON / Release=OFF（由 `ARK_SHADER_HOT_RELOAD` 宏控制） |

#### 加载策略

1. 优先从 `Paths::ResolveContent("shaders/<name>.vert|.frag")` 读取（构建期 `POST_BUILD` 把 `engine/shaders/` 复制到 `$<TARGET_FILE_DIR>/content/shaders/`）；
2. 若磁盘不存在，则回落到 `ShaderSources.h` 内嵌字符串，并把条目标记为非 `fromDisk`（不参与热重载）。

#### 构建选项

- CMake: `option(ARK_SHADER_HOT_RELOAD "..." ON)` → 注入 `target_compile_definitions(... ARK_SHADER_HOT_RELOAD=0|1)` 到 `StarArkEngine`。
- `engine/CMakeLists.txt` 同时导出内部变量 `ARK_ENGINE_SHADER_DIR`，供 `game` / `samples` 的 `POST_BUILD` 使用。

#### 用法示例

```cpp
auto shader = ark::EngineBase::Get().GetRenderer()
                 ->GetShaderManager()->Get("pbr");
material->SetShader(shader);
```

### 4.12 PostProcess

**头文件**: `#include "engine/rendering/PostProcess.h"`

HDR 离屏渲染 + Bloom + ACES tone mapping 后处理管线（Phase 10）。由 `ForwardRenderer` 拥有，一般通过 `ForwardRenderer::SetBloom*` 接口间接控制。

#### 管线

1. **HDR 场景 FBO**：RGBA16F 颜色 + D24S8 深度，全分辨率，场景所有相机写到这里
2. **亮度提取**（`bright` shader）：soft-knee 阈值把 HDR 高亮分量提到半分辨率 Bloom FBO[0]
3. **可分离高斯**（`blur` shader）：9-tap 水平/垂直交替 ping-pong `2 * iterations` 趟
4. **合成**（`composite` shader）：`result = ACES((scene + bloom * strength) * exposure)`，输出到默认 FB

#### 接口

| 方法 | 说明 |
|------|------|
| `Init(w, h)` | 分配 HDR FBO / Bloom ping-pong / 全屏三角形 / 编译 3 个内置着色器（首帧自动调用） |
| `BeginScene(w, h)` | 绑定 HDR FBO；若大小变化自动重分配 |
| `EndScene()` | 解绑，回默认 FB |
| `Apply(screenW, screenH, exposure, bloomThreshold, bloomStrength, blurIterations)` | 运行全部后处理 pass；strength=0 或 iterations=0 时直接走 composite-only |
| `SetBloomEnabled(bool)` / `IsBloomEnabled()` | 运行时开关 Bloom |

#### 默认参数（由 `ForwardRenderer` 传入）

| 参数 | 默认值 | 备注 |
|------|--------|------|
| `exposure` | 1.0 | 线性曝光系数，值越大画面越亮 |
| `bloomThreshold` | 1.0 | HDR 线性空间的亮度阈值；>1 的分量才会溢光 |
| `bloomStrength` | 0.6 | 合成时 Bloom 叠加系数 |
| `bloomIterations` | 5 | 高斯 ping-pong 次数，总 blur pass = `2 * iterations` |

#### 用法示例

```cpp
auto* renderer = ark::EngineBase::Get().GetRenderer();
renderer->SetExposure(1.2f);
renderer->SetBloomThreshold(1.5f);
renderer->SetBloomStrength(0.4f);
renderer->SetBloomIterations(6);
// 或直接关闭 Bloom：
renderer->SetBloomEnabled(false);
```

> **注意**：PostProcess 使用内置 GLSL 字符串编译自己的 3 个着色器，不走 `ShaderManager`（当前不支持后处理 shader 热重载）。规划阶段：待可扩展 post-FX 链时再战。

### 4.13 Skybox

**头文件**: `#include "engine/rendering/Skybox.h"`

天空盒（Phase 11）。由 `ForwardRenderer` 拥有一个，从 `GetSkybox()` 获取。采用内嵌 GLSL program 与单位立方体 VAO，不经由 RHI 抽象层（和 `PostProcess` 一致）。

#### 接口

| 方法 | 说明 |
|------|------|
| `Init()` | 懒初始化 GL 资源；若未通过 `SetFromFiles` 填充过数据则自动调 `GenerateProceduralGradient` 用默认颜色 |
| `SetFromFiles({+X,-X,+Y,-Y,+Z,-Z})` | 加载 6 面 LDR 图（PNG/JPG）到 sRGB 立方体贴图，自动生成 mipmap。返回 `bool` 表示全部面成功 |
| `GenerateProceduralGradient(zenith_rgb, horizon_rgb, ground_rgb, faceSize)` | 运行时生成 RGB16F 线性 HDR 渐变；值可 > 1（HDR） |
| `Render(view, projection)` | 由 `ForwardRenderer::RenderCamera` 在不透明绘制后、 HDR FBO 解绑前调用；depth func LEQUAL + depth mask OFF |
| `SetEnabled(bool)` / `IsEnabled()` | 运行时开关 |
| `SetIntensity(float)` / `GetIntensity()` | 采样后的乘数，默认 1.0 |
| `GetCubeMap()` | `uint32_t` — 底层 GL cubemap handle，供 Phase 12 IBL 卷积使用 |

#### 用法示例

```cpp
// 默认：程序化渐变，无需资产
auto* sky = ark::EngineBase::Get().GetRenderer()->GetSkybox();

// 自定义 6 面图片：
sky->SetFromFiles({
    ark::Paths::ResolveContent("skybox/right.png").string(),
    ark::Paths::ResolveContent("skybox/left.png").string(),
    ark::Paths::ResolveContent("skybox/top.png").string(),
    ark::Paths::ResolveContent("skybox/bottom.png").string(),
    ark::Paths::ResolveContent("skybox/front.png").string(),
    ark::Paths::ResolveContent("skybox/back.png").string(),
});

// 或自定义渐变：
sky->GenerateProceduralGradient(
    0.1f, 0.2f, 0.5f,   // zenith (深蓝)
    1.0f, 0.6f, 0.3f,   // horizon (暖橘)
    0.05f, 0.02f, 0.01f // ground
);
```

> **注意**：天空盒绘制到 HDR FBO 内，因此会被 `PostProcess` 的曝光和 bloom 处理。

### 4.14 IBL

**头文件**: `#include "engine/rendering/IBL.h"`

基于图像的照明（Phase 12，split-sum 近似）。由 `ForwardRenderer` 拥有一个 (`GetIBL()`)，在首帧 Skybox 就绪后自动烘焙；切换 Skybox 后调 `ForwardRenderer::RebakeIBL()`。不经由 RHI，直接用原生 GL。

#### 接口

| 方法 | 说明 |
|------|------|
| `Bake(envCubeMap, irradianceSize=32, prefilterSize=128, brdfLutSize=512)` | 从环境 cubemap 烘焙三件套；保存/恢复 FBO + viewport + 剔除 + 深度状态 |
| `GetIrradianceMap()` | `uint32_t` 辐照度 cubemap（RGB16F，32×32 × 6 面）— 漫反射环境项 |
| `GetPrefilterMap()` | `uint32_t` 按粗糙度预过滤的 cubemap（RGB16F，128×128，5 mip）— 镜面环境项 |
| `GetBrdfLUT()` | `uint32_t` 2D 查找表（RG16F，512×512，`(NdotV, roughness) → (A,B)`） |
| `GetPrefilterMipLevels()` | `int` 当前预过滤 mip 数（通常 5） |
| `IsValid()` | `bool` 是否已烘焙 |

**PBR shader 约定**：`uIrradianceMap` → unit 5，`uPrefilterMap` → unit 6，`uBrdfLUT` → unit 7；`uIBLEnabled` / `uPrefilterMaxLod` / `uIBLDiffuseIntensity` / `uIBLSpecularIntensity` 由 `ForwardRenderer::DrawMeshRenderer` 自动设置。

### 4.15 RenderSettings

**头文件**: `#include "engine/rendering/RenderSettings.h"`

渲染参数聚合结构体（Phase 12）。`ForwardRenderer::GetRenderSettings()` 返回可写引用。未来（Phase M10）场景序列化时将整体写入 JSON。

```cpp
struct RenderSettings {
    float exposure = 1.0f;
    struct { bool enabled=true; float threshold=1.0f, strength=0.6f; int iterations=5; } bloom;
    struct { bool enabled=true; float intensity=1.0f; } sky;
    struct { bool enabled=true; float diffuseIntensity=1.0f, specularIntensity=1.0f; } ibl;
};
```

`ForwardRenderer` 既提供兼容的单参 setter（`SetExposure/SetBloom*`），也允许直接修改 `GetRenderSettings()` 字段，例如：

```cpp
auto& rs = renderer->GetRenderSettings();
rs.ibl.specularIntensity = 0.7f;
rs.bloom.strength = 0.4f;
```

### 4.16 ShadowMap

**头文件**: `#include "engine/rendering/ShadowMap.h"`

方向光 shadow map（Phase 13）。由 `ForwardRenderer` 拥有 (`GetShadowMap()`)，每帧由 `RenderShadowPass()` 更新。**不经由 RHI**，直接原生 GL：`GL_DEPTH_COMPONENT24` + FBO（无颜色附件）+ `GL_CLAMP_TO_BORDER`（边界外视为完全受光）。

#### 接口

| 方法 | 说明 |
|------|------|
| `Init(int resolution)` | 懒创建 / 按分辨率重建 FBO + depth 纹理 |
| `Begin()` | 保存当前 viewport + FBO，绑定 shadow FBO，清空 depth |
| `End()` | 恢复先前 FBO + viewport |
| `UpdateMatrix(lightDir, focus, orthoHalfSize, near, far)` | 构造正交光源视投影矩阵，结果存入 `lightSpaceMatrix_` |
| `GetDepthTexture()` | `uint32_t` — 底层 GL depth 纹理 |
| `GetLightSpaceMatrix()` | `const glm::mat4&` |
| `GetResolution()` | `int` |
| `IsValid()` | `bool` |

#### PBR shader 约定

- `uShadowMap` → texture unit 8
- `uShadowEnabled`: `0` 禁用，`1` 启用
- `uLightSpaceMatrix`: 由 ForwardRenderer 填充
- `uShadowDepthBias` / `uShadowNormalBias` / `uShadowPcfKernel` / `uShadowTexelSize`: 见 `RenderSettings::shadow`

Shadow 只影响**第一个方向光**的直接光项；环境光 (`light.ambient`)、IBL 和其他点/聚光灯不受此 shadow map 影响（后续阶段可加各自阴影）。

#### 调参示例

```cpp
auto& s = renderer->GetRenderSettings().shadow;
s.enabled       = true;
s.resolution    = 2048;   // 4096 for higher quality, costs bandwidth
s.orthoHalfSize = 25.0f;  // 覆盖场景半径，过大→锯齿，过小→越出边界
s.depthBias     = 0.002f;
s.normalBias    = 0.010f;
s.pcfKernel     = 2;      // 5×5 = 25 taps
```

### 4.17 SceneSerializer

**头文件**: `#include "engine/rendering/SceneSerializer.h"`

渲染参数 + 光源的 JSON 序列化（Phase 14 / Mini-M10）。零依赖，为外部调光/材质工具提供的"数据面"。

#### 接口

| 方法 | 说明 |
|------|------|
| `Save(path, renderer)` | 把当前 `RenderSettings` + 所有活动 `Light` 组件（按 AObject 名）写入 JSON |
| `Load(path, renderer)` | 把 JSON 读回 `RenderSettings`；光源按名字匹配回 runtime |
| `EnableHotReload(path)` | 启用 mtime 轮询；路径为空则禁用 |
| `Tick(renderer)` | 每帧调用；首次 Tick 延迟执行初始 Save/Load（等待 `AObject::Init`），之后检测 mtime 变化时重新 `Load` |

`ForwardRenderer::RenderFrame` 每帧自动调用 `SceneSerializer::Tick(this)`，用户代码只需调 `EnableHotReload(path)`。

#### JSON Schema

```json
{
  "renderSettings": {
    "exposure": 1.0,
    "bloom":  { "enabled": true, "threshold": 1.0, "strength": 0.6, "iterations": 5 },
    "sky":    { "enabled": true, "intensity": 1.0 },
    "ibl":    { "enabled": true, "diffuseIntensity": 1.0, "specularIntensity": 1.0 },
    "shadow": { "enabled": true, "resolution": 2048, "orthoHalfSize": 25,
                "nearPlane": 0.1, "farPlane": 100,
                "depthBias": 0.002, "normalBias": 0.01, "pcfKernel": 2 }
  },
  "lights": [
    { "name": "Sun", "type": "Directional",
      "color": [1, 0.95, 0.9], "intensity": 1.0, "ambient": [0.15, 0.15, 0.15],
      "position": [0, 0, 0], "rotationEuler": [-45, 0, 0],
      "range": 10, "constant": 1, "linear": 0.09, "quadratic": 0.032,
      "innerAngle": 12.5, "outerAngle": 17.5 }
  ]
}
```

- 光源通过 `AObject::GetName()` 匹配。JSON 中缺失的字段保留当前运行时值；runtime 中存在而 JSON 中缺失的光源保持不动
- `rotationEuler` 为度；内部转为四元数写回 `Transform`
- 类型字符串：`Directional` / `Point` / `Spot`

#### 典型用法

```cpp
// 在 scene OnLoad 结尾：
ark::SceneSerializer::EnableHotReload(ark::Paths::ResolveContent("lighting.json"));
// 之后用任何工具（记事本/Python/Dear PyGui/VS Code）修改 lighting.json 保存即生效
```

---

## 5. 平台层

### 5.1 Window

**头文件**: `#include "engine/platform/Window.h"`

GLFW 窗口封装，创建 OpenGL 4.5 Core 上下文。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Window(int w, int h, const string& title)` | — | 构造并创建窗口 + GL 上下文 + GLEW 初始化 |
| `ShouldClose()` | `bool` | 窗口是否应关闭 |
| `PollEvents()` | `void` | 处理系统事件 |
| `SwapBuffers()` | `void` | 交换前后缓冲区 |
| `GetWidth()` | `int` | 窗口宽度 |
| `GetHeight()` | `int` | 窗口高度 |
| `WasResized()` | `bool` | 本帧是否发生 resize |
| `ResetResizeFlag()` | `void` | 清除 resize 标记 |
| `GetNativeHandle()` | `GLFWwindow*` | 获取原始 GLFW 窗口指针 |

### 5.2 Input

**头文件**: `#include "engine/platform/Input.h"`

静态输入查询类。每帧由 EngineBase 调用 `Update()`。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Input::Init(GLFWwindow*)` | `void` | 初始化（引擎内部调用） |
| `Input::Update()` | `void` | 更新输入状态（引擎内部调用） |
| `Input::GetKey(int keyCode)` | `bool` | 按键是否按住（使用 GLFW_KEY_* 常量） |
| `Input::GetKeyDown(int keyCode)` | `bool` | 按键是否刚按下（本帧） |
| `Input::GetKeyUp(int keyCode)` | `bool` | 按键是否刚释放（本帧） |
| `Input::GetMouseButton(int button)` | `bool` | 鼠标按钮是否按住 |
| `Input::GetMousePosition(double& x, double& y)` | `void` | 获取鼠标位置 |
| `Input::GetMouseButtonDown(int button)` | `bool` | 鼠标按钮是否刚按下（本帧边沿） |
| `Input::GetMouseButtonUp(int button)` | `bool` | 鼠标按钮是否刚释放（本帧边沿） |
| `Input::GetMouseDeltaX()` | `float` | 鼠标 X 轴移动增量 |
| `Input::GetMouseDeltaY()` | `float` | 鼠标 Y 轴移动增量 |
| `Input::GetScrollDelta()` | `float` | 滚轮增量（向上正，向下负） |

**按键常量**: 使用 GLFW 定义，如 `GLFW_KEY_W`、`GLFW_KEY_SPACE`、`GLFW_KEY_ESCAPE` 等。需 `#include <GLFW/glfw3.h>`。

### 5.3 Time

**头文件**: `#include "engine/platform/Time.h"`

静态时间类。每帧由 EngineBase 调用 `Update()`。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Time::Init()` | `void` | 初始化（引擎内部） |
| `Time::Update()` | `void` | 更新帧时间（引擎内部） |
| `Time::DeltaTime()` | `float` | 上一帧到当前帧的时间差（秒） |
| `Time::TotalTime()` | `float` | 引擎启动以来的总时间（秒） |
| `Time::FrameCount()` | `uint64_t` | 总帧数 |

---

## 6. 调试日志

### 6.1 DebugListenBus & 日志宏

**头文件**: `#include "engine/debug/DebugListenBus.h"`

全局日志广播总线。推荐使用宏而非直接调用。

#### 日志宏

| 宏 | 级别 | 说明 |
|----|------|------|
| `ARK_LOG_TRACE(category, message)` | Trace | 最详细的跟踪信息 |
| `ARK_LOG_DEBUG(category, message)` | Debug | 调试信息 |
| `ARK_LOG_INFO(category, message)` | Info | 一般信息 |
| `ARK_LOG_WARN(category, message)` | Warning | 警告 |
| `ARK_LOG_ERROR(category, message)` | Error | 错误 |
| `ARK_LOG_FATAL(category, message)` | Fatal | 致命错误，**调用后 `std::abort()`** |

- `category`: `std::string`，日志分类（如 `"Core"`, `"RHI"`, `"Rendering"`）
- `message`: `std::string`，日志消息
- 自动捕获 `std::source_location`（文件名、行号、函数名）

```cpp
ARK_LOG_INFO("Core", "Player spawned at position " + std::to_string(x));
ARK_LOG_FATAL("RHI", "Failed to compile shader");  // 会 abort
```

#### 日志级别枚举

```cpp
enum class LogLevel { Trace, Debug, Info, Warning, Error, Fatal };
```

#### LogMessage 结构

```cpp
struct LogMessage {
    LogLevel level;
    std::string category;
    std::string message;
    std::string timestamp;
    std::source_location location;
};
```

### 6.2 IDebugListener

**头文件**: `#include "engine/debug/IDebugListener.h"`

日志监听者抽象接口。构造时自动注册到 DebugListenBus，析构时自动注销（RAII）。

| 方法 | 说明 |
|------|------|
| `OnDebugMessage(const LogMessage& msg)` | 纯虚，收到日志时调用。子类实现此方法 |

### 6.3 ConsoleDebugListener

**头文件**: `#include "engine/debug/ConsoleDebugListener.h"`

控制台彩色日志输出。Warning 及以上输出到 stderr。

```cpp
ark::ConsoleDebugListener consoleListener;  // 构造即注册
```

### 6.4 FileDebugListener

**头文件**: `#include "engine/debug/FileDebugListener.h"`

文件轮转日志。使用 spdlog `rotating_file_sink_mt`。

```cpp
ark::FileDebugListener fileListener("logs/");  // 日志写入 logs/ 目录
```

---

## 7. RHI 渲染硬件接口

### 7.1 RHIDevice

**头文件**: `#include "engine/rhi/RHIDevice.h"`

抽象设备工厂，创建所有 RHI 资源。

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `CreateVertexBuffer(size_t size, BufferUsage usage)` | `unique_ptr<RHIBuffer>` | 创建顶点缓冲区 |
| `CreateIndexBuffer(size_t size, BufferUsage usage)` | `unique_ptr<RHIBuffer>` | 创建索引缓冲区 |
| `CreateShader()` | `unique_ptr<RHIShader>` | 创建着色器 |
| `CreateTexture()` | `unique_ptr<RHITexture>` | 创建纹理 |
| `CreatePipeline(const PipelineDesc& desc)` | `unique_ptr<RHIPipeline>` | 创建管线状态 |
| `CreateCommandBuffer()` | `unique_ptr<RHICommandBuffer>` | 创建命令缓冲区 |

#### 工厂函数

```cpp
std::unique_ptr<RHIDevice> CreateOpenGLDevice();
```

### 7.2 RHIBuffer

**头文件**: `#include "engine/rhi/RHIBuffer.h"`

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Upload(const void* data, size_t size)` | `void` | 上传数据到缓冲区 |
| `GetSize()` | `size_t` | 缓冲区大小（字节） |
| `GetType()` | `BufferType` | `Vertex` 或 `Index` |

### 7.3 RHIShader

**头文件**: `#include "engine/rhi/RHIShader.h"`

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Compile(const string& vs, const string& fs)` | `bool` | 编译着色器（顶点+片段源码） |
| `SetUniformMat4(const string& name, const float* value)` | `void` | 设置 mat4 uniform |
| `SetUniformVec3(const string& name, const float* value)` | `void` | 设置 vec3 uniform |
| `SetUniformVec4(const string& name, const float* value)` | `void` | 设置 vec4 uniform |
| `SetUniformFloat(const string& name, float value)` | `void` | 设置 float uniform |
| `SetUniformInt(const string& name, int value)` | `void` | 设置 int uniform |

> **注意**: GLShader 实现还有 `Bind()` 方法（绑定 shader program），但此方法不在抽象接口中。`GLPipeline::Bind()` 会内部调用 `GLShader::Bind()`。

### 7.4 RHITexture

**头文件**: `#include "engine/rhi/RHITexture.h"`

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Upload(int w, int h, int channels, const uint8_t* data)` | `void` | 上传像素数据 |
| `Bind(int unit = 0)` | `void` | 绑定纹理到指定纹理单元 |
| `GetWidth()` | `int` | 纹理宽度 |
| `GetHeight()` | `int` | 纹理高度 |

### 7.5 RHIPipeline & PipelineDesc

**头文件**: `#include "engine/rhi/RHIPipeline.h"`

#### PipelineDesc 结构

```cpp
struct PipelineDesc {
    RHIShader*       shader       = nullptr;
    VertexLayout     vertexLayout;
    PrimitiveTopology topology    = PrimitiveTopology::Triangles;
    bool             depthTest    = true;
    bool             depthWrite   = true;
    bool             blendEnabled = false;
};
```

#### RHIPipeline 方法

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Bind()` | `void` | 绑定管线状态（shader + VAO + depth/blend 设置） |
| `GetDesc()` | `const PipelineDesc&` | 获取管线描述 |

### 7.6 RHICommandBuffer

**头文件**: `#include "engine/rhi/RHICommandBuffer.h"`

命令缓冲区，录制渲染命令并批量提交。

| 方法 | 说明 |
|------|------|
| `Begin()` | 开始录制命令 |
| `End()` | 结束录制 |
| `Submit()` | 提交并执行所有录制的命令 |
| `SetViewport(int x, int y, int w, int h)` | 设置视口 |
| `Clear(float r, float g, float b, float a)` | 清屏 |
| `BindPipeline(RHIPipeline*)` | 绑定管线 |
| `BindVertexBuffer(RHIBuffer*)` | 绑定顶点缓冲区 |
| `BindIndexBuffer(RHIBuffer*)` | 绑定索引缓冲区 |
| `Draw(uint32_t vertexCount, uint32_t firstVertex = 0)` | 绘制（非索引） |
| `DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0)` | 绘制（索引） |

### 7.7 RHITypes

**头文件**: `#include "engine/rhi/RHITypes.h"`

#### 枚举

```cpp
enum class BufferType       { Vertex, Index };
enum class BufferUsage      { Static, Dynamic };
enum class ShaderStage      { Vertex, Fragment };
enum class PrimitiveTopology { Triangles, Lines, Points };
enum class IndexType        { UInt16, UInt32 };
enum class VertexAttribType { Float, Float2, Float3, Float4 };
```

#### 辅助函数

| 函数 | 返回值 | 说明 |
|------|--------|------|
| `VertexAttribComponentCount(VertexAttribType)` | `uint32_t` | 获取分量数（Float=1, Float3=3 等） |
| `VertexAttribSize(VertexAttribType)` | `uint32_t` | 获取字节大小 |

#### VertexLayout 结构

```cpp
struct VertexAttribute {
    std::string    name;
    VertexAttribType type;
    uint32_t       offset;
    bool           normalized = false;
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;
};
```

---

## 8. 用法示例

### 最小应用

```cpp
#include "engine/core/EngineBase.h"
#include "engine/core/AScene.h"
#include "engine/core/AObject.h"
#include "engine/debug/ConsoleDebugListener.h"

class MyObject : public ark::AObject {
public:
    void Init() override {
        SetName("MyObject");
        ARK_LOG_INFO("Game", "Hello from " + GetName());
    }
    void Loop(float dt) override {
        // 每帧逻辑
    }
};

class MyScene : public ark::AScene {
public:
    void OnLoad() override {
        CreateObject<MyObject>();
    }
};

int main() {
    ark::ConsoleDebugListener console;
    ark::EngineBase::Get().Run<MyScene>(1280, 720, "My Game");
    return 0;
}
```

### 带渲染的对象

```cpp
#include "engine/core/AObject.h"
#include "engine/core/EngineBase.h"
#include "engine/rendering/Camera.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshRenderer.h"

class CubeObject : public ark::AObject {
public:
    void Init() override {
        SetName("Cube");
        auto* device = ark::EngineBase::Get().GetRHIDevice();

        // 创建 shader（需要自定义 GLSL 源码）
        auto shader = std::shared_ptr<ark::RHIShader>(device->CreateShader().release());
        shader->Compile(vertexShaderSrc, fragmentShaderSrc);

        // 创建 mesh
        auto mesh = std::shared_ptr<ark::Mesh>(ark::Mesh::CreateCube().release());
        mesh->Upload(device);

        // 创建 material
        auto mat = std::make_shared<ark::Material>();
        mat->SetShader(shader);
        mat->SetColor(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));  // 红色

        // 添加 MeshRenderer 组件
        auto* mr = AddComponent<ark::MeshRenderer>();
        mr->SetMesh(mesh);
        mr->SetMaterial(mat);
    }
};
```

### 场景切换

```cpp
// 在任何地方（如组件的 Tick 中）：
ark::EngineBase::Get().GetSceneManager()->LoadScene<NextScene>();
// 切换在当前帧末执行
```

### 持久对象（跨场景）

```cpp
void MyObject::Init() override {
    SetDontDestroy(true);  // 该对象在场景切换时不会被销毁
}
```

### 父子层级

```cpp
void MyScene::OnLoad() override {
    auto* parent = CreateObject<ark::AObject>();
    auto* child = CreateObject<ark::AObject>();
    child->GetTransform().SetParent(&parent->GetTransform());
    // child 会跟随 parent 的变换
    // parent 销毁时 child 也会级联销毁
}
```
