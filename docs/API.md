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
5. Tick 持久对象（`persistentList_`）
6. Tick 场景对象（`AScene::objectList_`）
7. PostTick 持久对象
8. PostTick 场景对象
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
| `Init()` | DrainPendingObjects 期间 | 初始化（创建组件、设置 Transform） |
| `PostInit()` | Init 之后 | 可在此安全引用其他已初始化的对象 |
| `Tick(float dt)` | 每帧调用 | 逻辑更新 |
| `PostTick(float dt)` | Tick 之后 | 延后逻辑（如相机跟随） |
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
| `Tick(float dt)` | 每帧更新（仅当 enabled） |
| `PostTick(float dt)` | Tick 后更新（仅当 enabled） |

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
    void Tick(float dt) override {
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

> **当前限制**: ForwardRenderer 仅使用第一个方向光，尚未支持多光源和点光/聚光灯渲染。

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
| `Mesh::CreateCube()` | `unique_ptr<Mesh>` | 创建单位立方体（24 顶点、36 索引、含法线和 UV） |
| `Mesh::CreatePlane(float size = 10.0f)` | `unique_ptr<Mesh>` | 创建地面平面 |

> **注意**: `CreateCube()` / `CreatePlane()` 只创建 CPU 端数据，需调用 `Upload(device)` 上传到 GPU。

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
| `SetDiffuseTexture(shared_ptr<RHITexture>)` | `void` | 设置漫反射纹理 |
| `GetDiffuseTexture()` | `RHITexture*` | 获取漫反射纹理 |

#### 渲染方法

| 方法 | 说明 |
|------|------|
| `Bind()` | 将材质参数绑定到 shader uniform（`uMaterial.color` / `uMaterial.specular` / `uMaterial.shininess` / `uMaterial.hasDiffuseTex`） |

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
| `ForwardRenderer(RHIDevice*)` | 构造（传入 RHI 设备） |
| `RenderFrame(Window*)` | 渲染一帧：遍历所有相机（按优先级排序）→ 清屏 → 收集 MeshRenderer → 设置光照/MVP → 绘制 |

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
| `uMaterial.color` | `vec4` | 由 `Material::Bind()` 设置 |
| `uMaterial.specular` | `vec3` | 由 `Material::Bind()` 设置 |
| `uMaterial.shininess` | `float` | 由 `Material::Bind()` 设置 |
| `uMaterial.hasDiffuseTex` | `int` | 由 `Material::Bind()` 设置 |

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
    void Tick(float dt) override {
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
